#include "ShaderGraph.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

ShaderGraph::ShaderGraph(const std::string& name) : m_name(name) {
    // Always create the PBR Master output node (ID = 1)
    ShaderGraphNode master;
    master.id = m_nextNodeId++;
    master.type = ShaderNodeType::PBRMaster;
    master.name = "PBR Master";
    master.editorPos = glm::vec2(500.0f, 200.0f);
    master.colorVal = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    master.floatVal = 0.5f; // default roughness
    m_masterNodeId = master.id;
    m_nodes.push_back(master);
}

uint32_t ShaderGraph::AddNode(ShaderNodeType type, const std::string& name, const glm::vec2& pos) {
    ShaderGraphNode node;
    node.id = m_nextNodeId++;
    node.type = type;
    node.name = name.empty() ? ShaderNodeTypeToString(type) : name;
    node.editorPos = pos;
    m_nodes.push_back(node);
    return node.id;
}

bool ShaderGraph::RemoveNode(uint32_t nodeId) {
    if (nodeId == m_masterNodeId) return false; // Cannot remove PBRMaster node
    auto it = std::remove_if(m_nodes.begin(), m_nodes.end(), [nodeId](const ShaderGraphNode& n) {
        return n.id == nodeId;
    });
    if (it == m_nodes.end()) return false;
    m_nodes.erase(it, m_nodes.end());

    // Remove connected links
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [nodeId](const ShaderGraphLink& l) {
        return l.fromNodeId == nodeId || l.toNodeId == nodeId;
    }), m_links.end());
    return true;
}

ShaderGraphNode* ShaderGraph::GetNode(uint32_t nodeId) {
    for (auto& n : m_nodes) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

const ShaderGraphNode* ShaderGraph::GetNode(uint32_t nodeId) const {
    for (const auto& n : m_nodes) {
        if (n.id == nodeId) return &n;
    }
    return nullptr;
}

uint32_t ShaderGraph::AddLink(uint32_t fromNodeId, const std::string& fromSlot, uint32_t toNodeId, const std::string& toSlot) {
    if (fromNodeId == toNodeId) return 0;
    if (!GetNode(fromNodeId) || !GetNode(toNodeId)) return 0;

    // Replace existing link connected to the same target input slot
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [&](const ShaderGraphLink& l) {
        return l.toNodeId == toNodeId && l.toSlot == toSlot;
    }), m_links.end());

    ShaderGraphLink link;
    link.id = m_nextLinkId++;
    link.fromNodeId = fromNodeId;
    link.fromSlot = fromSlot;
    link.toNodeId = toNodeId;
    link.toSlot = toSlot;
    m_links.push_back(link);
    return link.id;
}

bool ShaderGraph::RemoveLink(uint32_t linkId) {
    auto it = std::remove_if(m_links.begin(), m_links.end(), [linkId](const ShaderGraphLink& l) {
        return l.id == linkId;
    });
    if (it == m_links.end()) return false;
    m_links.erase(it, m_links.end());
    return true;
}

glm::vec4 ShaderGraph::EvaluateInputSlot(uint32_t toNodeId,
                                         const std::string& toSlot,
                                         const glm::vec4& defaultVal,
                                         float time,
                                         const glm::vec2& uv,
                                         const glm::vec3& viewDir,
                                         const glm::vec3& surfaceNormal,
                                         int depth) const {
    if (depth > 16) return defaultVal;
    for (const auto& link : m_links) {
        if (link.toNodeId == toNodeId && link.toSlot == toSlot) {
            return EvaluateNode(link.fromNodeId, time, uv, viewDir, surfaceNormal, depth + 1);
        }
    }
    return defaultVal;
}

glm::vec4 ShaderGraph::EvaluateNode(uint32_t nodeId,
                                    float time,
                                    const glm::vec2& uv,
                                    const glm::vec3& viewDir,
                                    const glm::vec3& surfaceNormal,
                                    int depth) const {
    if (depth > 16) return glm::vec4(0.0f);
    const ShaderGraphNode* node = GetNode(nodeId);
    if (!node) return glm::vec4(0.0f);

    switch (node->type) {
        case ShaderNodeType::ConstantFloat:
            return glm::vec4(node->floatVal, node->floatVal, node->floatVal, 1.0f);
        case ShaderNodeType::ConstantVec3:
            return glm::vec4(node->vec3Val, 1.0f);
        case ShaderNodeType::ConstantColor:
            return node->colorVal;
        case ShaderNodeType::Time:
            return glm::vec4(time, std::sin(time), std::cos(time), time * 0.5f);
        case ShaderNodeType::UV:
            return glm::vec4(uv.x, uv.y, 0.0f, 1.0f);
        case ShaderNodeType::Texture2D: {
            glm::vec4 sampleUV = EvaluateInputSlot(nodeId, "UV", glm::vec4(uv.x, uv.y, 0.0f, 1.0f), time, uv, viewDir, surfaceNormal, depth);
            // Procedural fallback tint modulated by UV & node colorVal
            float checker = (std::fmod( std::floor(sampleUV.x * 4.0f) + std::floor(sampleUV.y * 4.0f), 2.0f ) == 0.0f) ? 1.0f : 0.85f;
            return node->colorVal * glm::vec4(checker, checker, checker, 1.0f);
        }
        case ShaderNodeType::Add: {
            glm::vec4 a = EvaluateInputSlot(nodeId, "A", glm::vec4(node->floatVal), time, uv, viewDir, surfaceNormal, depth);
            glm::vec4 b = EvaluateInputSlot(nodeId, "B", glm::vec4(node->vec3Val, 1.0f), time, uv, viewDir, surfaceNormal, depth);
            return a + b;
        }
        case ShaderNodeType::Subtract: {
            glm::vec4 a = EvaluateInputSlot(nodeId, "A", glm::vec4(node->floatVal), time, uv, viewDir, surfaceNormal, depth);
            glm::vec4 b = EvaluateInputSlot(nodeId, "B", glm::vec4(node->vec3Val, 1.0f), time, uv, viewDir, surfaceNormal, depth);
            return a - b;
        }
        case ShaderNodeType::Multiply: {
            glm::vec4 a = EvaluateInputSlot(nodeId, "A", glm::vec4(node->floatVal), time, uv, viewDir, surfaceNormal, depth);
            glm::vec4 b = EvaluateInputSlot(nodeId, "B", glm::vec4(node->vec3Val, 1.0f), time, uv, viewDir, surfaceNormal, depth);
            return a * b;
        }
        case ShaderNodeType::Lerp: {
            glm::vec4 a = EvaluateInputSlot(nodeId, "A", glm::vec4(0.0f), time, uv, viewDir, surfaceNormal, depth);
            glm::vec4 b = EvaluateInputSlot(nodeId, "B", glm::vec4(1.0f), time, uv, viewDir, surfaceNormal, depth);
            glm::vec4 t = EvaluateInputSlot(nodeId, "T", glm::vec4(node->floatVal), time, uv, viewDir, surfaceNormal, depth);
            return glm::mix(a, b, glm::clamp(t.x, 0.0f, 1.0f));
        }
        case ShaderNodeType::Fresnel: {
            glm::vec3 N = glm::normalize(surfaceNormal);
            glm::vec3 V = glm::normalize(viewDir);
            float ndotv = glm::clamp(glm::dot(N, V), 0.0f, 1.0f);
            float power = std::max(node->floatVal, 0.1f);
            float f = std::pow(1.0f - ndotv, power);
            return glm::vec4(f, f, f, 1.0f);
        }
        case ShaderNodeType::ParallaxOffset: {
            glm::vec4 heightSample = EvaluateInputSlot(nodeId, "Height", glm::vec4(0.5f), time, uv, viewDir, surfaceNormal, depth);
            float scale = node->floatVal; // heightScale
            glm::vec3 V = glm::normalize(viewDir);
            float h = heightSample.x * scale - (scale * 0.5f);
            glm::vec2 offset = glm::vec2(V.x, V.y) * (h / std::max(V.z, 0.15f));
            return glm::vec4(uv.x - offset.x, uv.y - offset.y, offset.x, offset.y);
        }
        case ShaderNodeType::Sine: {
            glm::vec4 inVal = EvaluateInputSlot(nodeId, "In", glm::vec4(time), time, uv, viewDir, surfaceNormal, depth);
            return glm::vec4(std::sin(inVal.x), std::sin(inVal.y), std::sin(inVal.z), 1.0f);
        }
        case ShaderNodeType::Clamp: {
            glm::vec4 inVal = EvaluateInputSlot(nodeId, "In", glm::vec4(node->floatVal), time, uv, viewDir, surfaceNormal, depth);
            return glm::clamp(inVal, glm::vec4(0.0f), glm::vec4(1.0f));
        }
        default:
            return node->colorVal;
    }
}

ShaderGraphOutput ShaderGraph::Evaluate(float time,
                                        const glm::vec2& uv,
                                        const glm::vec3& viewDir,
                                        const glm::vec3& surfaceNormal) const {
    ShaderGraphOutput out;
    const ShaderGraphNode* master = GetNode(m_masterNodeId);
    glm::vec4 defaultAlbedo = master ? master->colorVal : glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float defaultRoughness = master ? master->floatVal : 0.5f;

    out.albedo = EvaluateInputSlot(m_masterNodeId, "Albedo", defaultAlbedo, time, uv, viewDir, surfaceNormal, 0);
    glm::vec4 normVal = EvaluateInputSlot(m_masterNodeId, "Normal", glm::vec4(surfaceNormal, 1.0f), time, uv, viewDir, surfaceNormal, 0);
    out.normal = glm::normalize(glm::vec3(normVal));
    out.metallic = glm::clamp(EvaluateInputSlot(m_masterNodeId, "Metallic", glm::vec4(0.0f), time, uv, viewDir, surfaceNormal, 0).x, 0.0f, 1.0f);
    out.roughness = glm::clamp(EvaluateInputSlot(m_masterNodeId, "Roughness", glm::vec4(defaultRoughness), time, uv, viewDir, surfaceNormal, 0).x, 0.04f, 1.0f);
    out.ao = glm::clamp(EvaluateInputSlot(m_masterNodeId, "AO", glm::vec4(1.0f), time, uv, viewDir, surfaceNormal, 0).x, 0.0f, 1.0f);
    out.emissive = glm::vec3(EvaluateInputSlot(m_masterNodeId, "Emissive", glm::vec4(0.0f), time, uv, viewDir, surfaceNormal, 0));
    glm::vec4 parallax = EvaluateInputSlot(m_masterNodeId, "Parallax", glm::vec4(0.0f), time, uv, viewDir, surfaceNormal, 0);
    out.parallaxUVOffset = glm::vec2(parallax.z, parallax.w);
    return out;
}

std::string ShaderGraph::EmitInputSlotGLSL(uint32_t toNodeId, const std::string& toSlot, const std::string& defaultExpr,
                                           std::map<uint32_t, std::string>& emittedVars, std::string& bodyCode, int depth) const {
    if (depth > 16) return defaultExpr;
    for (const auto& link : m_links) {
        if (link.toNodeId == toNodeId && link.toSlot == toSlot) {
            return EmitNodeGLSL(link.fromNodeId, emittedVars, bodyCode, depth + 1);
        }
    }
    return defaultExpr;
}

std::string ShaderGraph::EmitNodeGLSL(uint32_t nodeId, std::map<uint32_t, std::string>& emittedVars, std::string& bodyCode, int depth) const {
    auto it = emittedVars.find(nodeId);
    if (it != emittedVars.end()) return it->second;

    const ShaderGraphNode* node = GetNode(nodeId);
    if (!node || depth > 16) return "vec4(0.0)";

    std::string varName = "node_" + std::to_string(nodeId);
    std::ostringstream line;

    switch (node->type) {
        case ShaderNodeType::ConstantFloat:
            line << "    vec4 " << varName << " = vec4(" << node->floatVal << ");\n";
            break;
        case ShaderNodeType::ConstantVec3:
            line << "    vec4 " << varName << " = vec4(" << node->vec3Val.x << ", " << node->vec3Val.y << ", " << node->vec3Val.z << ", 1.0);\n";
            break;
        case ShaderNodeType::ConstantColor:
            line << "    vec4 " << varName << " = vec4(" << node->colorVal.r << ", " << node->colorVal.g << ", " << node->colorVal.b << ", " << node->colorVal.a << ");\n";
            break;
        case ShaderNodeType::Time:
            line << "    vec4 " << varName << " = vec4(u_Time, sin(u_Time), cos(u_Time), u_Time * 0.5);\n";
            break;
        case ShaderNodeType::UV:
            line << "    vec4 " << varName << " = vec4(fragTexCoord, 0.0, 1.0);\n";
            break;
        case ShaderNodeType::Texture2D: {
            std::string uvExpr = EmitInputSlotGLSL(nodeId, "UV", "vec4(fragTexCoord, 0.0, 1.0)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = texture(albedoMap, " << uvExpr << ".xy);\n";
            break;
        }
        case ShaderNodeType::Add: {
            std::string a = EmitInputSlotGLSL(nodeId, "A", "vec4(0.0)", emittedVars, bodyCode, depth);
            std::string b = EmitInputSlotGLSL(nodeId, "B", "vec4(0.0)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = (" << a << " + " << b << ");\n";
            break;
        }
        case ShaderNodeType::Subtract: {
            std::string a = EmitInputSlotGLSL(nodeId, "A", "vec4(0.0)", emittedVars, bodyCode, depth);
            std::string b = EmitInputSlotGLSL(nodeId, "B", "vec4(0.0)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = (" << a << " - " << b << ");\n";
            break;
        }
        case ShaderNodeType::Multiply: {
            std::string a = EmitInputSlotGLSL(nodeId, "A", "vec4(1.0)", emittedVars, bodyCode, depth);
            std::string b = EmitInputSlotGLSL(nodeId, "B", "vec4(1.0)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = (" << a << " * " << b << ");\n";
            break;
        }
        case ShaderNodeType::Lerp: {
            std::string a = EmitInputSlotGLSL(nodeId, "A", "vec4(0.0)", emittedVars, bodyCode, depth);
            std::string b = EmitInputSlotGLSL(nodeId, "B", "vec4(1.0)", emittedVars, bodyCode, depth);
            std::string t = EmitInputSlotGLSL(nodeId, "T", "vec4(0.5)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = mix(" << a << ", " << b << ", clamp(" << t << ".x, 0.0, 1.0));\n";
            break;
        }
        case ShaderNodeType::Fresnel:
            line << "    vec4 " << varName << " = vec4(pow(1.0 - max(dot(normalize(fragNormal), normalize(viewDir)), 0.0), " << std::max(node->floatVal, 0.1f) << "));\n";
            break;
        case ShaderNodeType::ParallaxOffset: {
            std::string h = EmitInputSlotGLSL(nodeId, "Height", "vec4(0.5)", emittedVars, bodyCode, depth);
            line << "    vec2 " << varName << "_off = viewDir.xy * ((" << h << ".x * " << node->floatVal << ") / max(viewDir.z, 0.15));\n";
            line << "    vec4 " << varName << " = vec4(fragTexCoord - " << varName << "_off, " << varName << "_off);\n";
            break;
        }
        case ShaderNodeType::Sine: {
            std::string inE = EmitInputSlotGLSL(nodeId, "In", "vec4(u_Time)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = sin(" << inE << ");\n";
            break;
        }
        case ShaderNodeType::Clamp: {
            std::string inE = EmitInputSlotGLSL(nodeId, "In", "vec4(0.5)", emittedVars, bodyCode, depth);
            line << "    vec4 " << varName << " = clamp(" << inE << ", vec4(0.0), vec4(1.0));\n";
            break;
        }
        default:
            line << "    vec4 " << varName << " = vec4(1.0);\n";
            break;
    }

    bodyCode += line.str();
    emittedVars[nodeId] = varName;
    return varName;
}

std::string ShaderGraph::CompileToGLSL() const {
    std::map<uint32_t, std::string> emittedVars;
    std::string bodyCode;

    std::string albedoExpr = EmitInputSlotGLSL(m_masterNodeId, "Albedo", "vec4(0.8, 0.8, 0.8, 1.0)", emittedVars, bodyCode, 0);
    std::string normalExpr = EmitInputSlotGLSL(m_masterNodeId, "Normal", "vec4(fragNormal, 1.0)", emittedVars, bodyCode, 0);
    std::string metallicExpr = EmitInputSlotGLSL(m_masterNodeId, "Metallic", "vec4(0.0)", emittedVars, bodyCode, 0);
    std::string roughnessExpr = EmitInputSlotGLSL(m_masterNodeId, "Roughness", "vec4(0.5)", emittedVars, bodyCode, 0);
    std::string aoExpr = EmitInputSlotGLSL(m_masterNodeId, "AO", "vec4(1.0)", emittedVars, bodyCode, 0);
    std::string emissiveExpr = EmitInputSlotGLSL(m_masterNodeId, "Emissive", "vec4(0.0)", emittedVars, bodyCode, 0);

    std::ostringstream glsl;
    glsl << "// Auto-generated by Engine Visual ShaderGraph: " << m_name << "\n";
    glsl << "#version 450\n\n";
    glsl << "layout(location = 0) in vec3 fragColor;\n";
    glsl << "layout(location = 1) in vec3 fragNormal;\n";
    glsl << "layout(location = 2) in vec3 fragWorldPos;\n";
    glsl << "layout(location = 3) in vec2 fragTexCoord;\n";
    glsl << "layout(location = 0) out vec4 outColor;\n\n";
    glsl << "uniform float u_Time;\n";
    glsl << "layout(binding = 1) uniform sampler2D albedoMap;\n\n";
    glsl << "void EvaluateShaderGraph(vec3 viewDir, out vec4 sgAlbedo, out vec3 sgNormal, out float sgMetallic, out float sgRoughness, out float sgAO, out vec3 sgEmissive) {\n";
    glsl << bodyCode;
    glsl << "    sgAlbedo = " << albedoExpr << ";\n";
    glsl << "    sgNormal = normalize(" << normalExpr << ".xyz);\n";
    glsl << "    sgMetallic = clamp(" << metallicExpr << ".x, 0.0, 1.0);\n";
    glsl << "    sgRoughness = clamp(" << roughnessExpr << ".x, 0.04, 1.0);\n";
    glsl << "    sgAO = clamp(" << aoExpr << ".x, 0.0, 1.0);\n";
    glsl << "    sgEmissive = " << emissiveExpr << ".rgb;\n";
    glsl << "}\n";
    return glsl.str();
}

std::string ShaderGraph::CompileToMaterialFunction() const {
    std::map<uint32_t, std::string> emittedVars;
    std::string bodyCode;

    std::string albedoExpr = EmitInputSlotGLSL(m_masterNodeId, "Albedo", "vec4(0.8, 0.8, 0.8, 1.0)", emittedVars, bodyCode, 0);
    std::string normalExpr = EmitInputSlotGLSL(m_masterNodeId, "Normal", "vec4(fragNormal, 1.0)", emittedVars, bodyCode, 0);
    std::string metallicExpr = EmitInputSlotGLSL(m_masterNodeId, "Metallic", "vec4(0.0)", emittedVars, bodyCode, 0);
    std::string roughnessExpr = EmitInputSlotGLSL(m_masterNodeId, "Roughness", "vec4(0.5)", emittedVars, bodyCode, 0);
    std::string aoExpr = EmitInputSlotGLSL(m_masterNodeId, "AO", "vec4(1.0)", emittedVars, bodyCode, 0);
    std::string emissiveExpr = EmitInputSlotGLSL(m_masterNodeId, "Emissive", "vec4(0.0)", emittedVars, bodyCode, 0);

    std::ostringstream glsl;
    glsl << "// Auto-generated from shader graph: " << m_name << "\n";
    glsl << "#define u_Time (ubo.timeParams.x)\n";
    glsl << "#define fragWorldPos fragPosition\n";
    glsl << "void EvaluateShaderGraph(vec3 viewDir, out vec4 sgAlbedo, out vec3 sgNormal, out float sgMetallic, out float sgRoughness, out float sgAO, out vec3 sgEmissive) {\n";
    glsl << bodyCode;
    glsl << "    sgAlbedo = " << albedoExpr << ";\n";
    glsl << "    sgNormal = normalize(" << normalExpr << ".xyz);\n";
    glsl << "    sgMetallic = clamp(" << metallicExpr << ".x, 0.0, 1.0);\n";
    glsl << "    sgRoughness = clamp(" << roughnessExpr << ".x, 0.04, 1.0);\n";
    glsl << "    sgAO = clamp(" << aoExpr << ".x, 0.0, 1.0);\n";
    glsl << "    sgEmissive = " << emissiveExpr << ".rgb;\n";
    glsl << "}\n";
    glsl << "#undef u_Time\n";
    glsl << "#undef fragWorldPos\n";
    return glsl.str();
}

JsonValue ShaderGraph::Serialize() const {
    JsonValue root = JsonValue::Object();
    root["name"] = m_name;
    root["masterNodeId"] = m_masterNodeId;
    root["nextNodeId"] = m_nextNodeId;
    root["nextLinkId"] = m_nextLinkId;

    JsonValue nodesArr = JsonValue::Array();
    for (const auto& node : m_nodes) {
        JsonValue n = JsonValue::Object();
        n["id"] = node.id;
        n["type"] = ShaderNodeTypeToString(node.type);
        n["name"] = node.name;
        JsonValue pos = JsonValue::Array();
        pos.Push(node.editorPos.x); pos.Push(node.editorPos.y);
        n["pos"] = pos;
        n["floatVal"] = node.floatVal;
        JsonValue v3 = JsonValue::Array();
        v3.Push(node.vec3Val.x); v3.Push(node.vec3Val.y); v3.Push(node.vec3Val.z);
        n["vec3Val"] = v3;
        JsonValue col = JsonValue::Array();
        col.Push(node.colorVal.r); col.Push(node.colorVal.g); col.Push(node.colorVal.b); col.Push(node.colorVal.a);
        n["colorVal"] = col;
        n["texturePath"] = node.texturePath;
        nodesArr.Push(n);
    }
    root["nodes"] = nodesArr;

    JsonValue linksArr = JsonValue::Array();
    for (const auto& link : m_links) {
        JsonValue l = JsonValue::Object();
        l["id"] = link.id;
        l["fromNodeId"] = link.fromNodeId;
        l["fromSlot"] = link.fromSlot;
        l["toNodeId"] = link.toNodeId;
        l["toSlot"] = link.toSlot;
        linksArr.Push(l);
    }
    root["links"] = linksArr;
    return root;
}

bool ShaderGraph::Deserialize(const JsonValue& json) {
    if (!json.IsObject() || !json.HasKey("nodes")) return false;
    m_name = json.HasKey("name") ? json["name"].AsString("ShaderGraph") : "ShaderGraph";
    m_masterNodeId = json.HasKey("masterNodeId") ? json["masterNodeId"].AsUInt(1) : 1;
    m_nextNodeId = json.HasKey("nextNodeId") ? json["nextNodeId"].AsUInt(2) : 2;
    m_nextLinkId = json.HasKey("nextLinkId") ? json["nextLinkId"].AsUInt(1) : 1;

    m_nodes.clear();
    for (const auto& n : json["nodes"].arrVal) {
        ShaderGraphNode node;
        node.id = n["id"].AsUInt(m_nextNodeId++);
        node.type = StringToShaderNodeType(n["type"].AsString("ConstantFloat"));
        node.name = n["name"].AsString("Node");
        if (n.HasKey("pos") && n["pos"].arrVal.size() >= 2) {
            node.editorPos = glm::vec2(n["pos"][0].AsFloat(), n["pos"][1].AsFloat());
        }
        node.floatVal = n.HasKey("floatVal") ? n["floatVal"].AsFloat(1.0f) : 1.0f;
        if (n.HasKey("vec3Val") && n["vec3Val"].arrVal.size() >= 3) {
            node.vec3Val = glm::vec3(n["vec3Val"][0].AsFloat(), n["vec3Val"][1].AsFloat(), n["vec3Val"][2].AsFloat());
        }
        if (n.HasKey("colorVal") && n["colorVal"].arrVal.size() >= 4) {
            node.colorVal = glm::vec4(n["colorVal"][0].AsFloat(), n["colorVal"][1].AsFloat(), n["colorVal"][2].AsFloat(), n["colorVal"][3].AsFloat());
        }
        node.texturePath = n.HasKey("texturePath") ? n["texturePath"].AsString("") : "";
        m_nodes.push_back(node);
    }

    m_links.clear();
    if (json.HasKey("links") && json["links"].IsArray()) {
        for (const auto& l : json["links"].arrVal) {
            ShaderGraphLink link;
            link.id = l["id"].AsUInt(m_nextLinkId++);
            link.fromNodeId = l["fromNodeId"].AsUInt(0);
            link.fromSlot = l["fromSlot"].AsString("Out");
            link.toNodeId = l["toNodeId"].AsUInt(0);
            link.toSlot = l["toSlot"].AsString("In");
            m_links.push_back(link);
        }
    }
    return true;
}

bool ShaderGraph::SaveToFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << Serialize().Serialize(2);
    return true;
}

bool ShaderGraph::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Deserialize(JsonValue::Parse(content));
}

