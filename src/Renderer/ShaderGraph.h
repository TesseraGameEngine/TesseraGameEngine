#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <glm/glm.hpp>
#include "Utils/SimpleJson.h"

enum class ShaderNodeType {
    PBRMaster = 0,
    Texture2D,
    ConstantFloat,
    ConstantVec3,
    ConstantColor,
    Add,
    Subtract,
    Multiply,
    Lerp,
    Fresnel,
    Time,
    UV,
    ParallaxOffset,
    Sine,
    Clamp
};

inline const char* ShaderNodeTypeToString(ShaderNodeType type) {
    switch (type) {
        case ShaderNodeType::PBRMaster: return "PBRMaster";
        case ShaderNodeType::Texture2D: return "Texture2D";
        case ShaderNodeType::ConstantFloat: return "ConstantFloat";
        case ShaderNodeType::ConstantVec3: return "ConstantVec3";
        case ShaderNodeType::ConstantColor: return "ConstantColor";
        case ShaderNodeType::Add: return "Add";
        case ShaderNodeType::Subtract: return "Subtract";
        case ShaderNodeType::Multiply: return "Multiply";
        case ShaderNodeType::Lerp: return "Lerp";
        case ShaderNodeType::Fresnel: return "Fresnel";
        case ShaderNodeType::Time: return "Time";
        case ShaderNodeType::UV: return "UV";
        case ShaderNodeType::ParallaxOffset: return "ParallaxOffset";
        case ShaderNodeType::Sine: return "Sine";
        case ShaderNodeType::Clamp: return "Clamp";
        default: return "ConstantFloat";
    }
}

inline ShaderNodeType StringToShaderNodeType(const std::string& str) {
    if (str == "PBRMaster") return ShaderNodeType::PBRMaster;
    if (str == "Texture2D") return ShaderNodeType::Texture2D;
    if (str == "ConstantVec3") return ShaderNodeType::ConstantVec3;
    if (str == "ConstantColor") return ShaderNodeType::ConstantColor;
    if (str == "Add") return ShaderNodeType::Add;
    if (str == "Subtract") return ShaderNodeType::Subtract;
    if (str == "Multiply") return ShaderNodeType::Multiply;
    if (str == "Lerp") return ShaderNodeType::Lerp;
    if (str == "Fresnel") return ShaderNodeType::Fresnel;
    if (str == "Time") return ShaderNodeType::Time;
    if (str == "UV") return ShaderNodeType::UV;
    if (str == "ParallaxOffset") return ShaderNodeType::ParallaxOffset;
    if (str == "Sine") return ShaderNodeType::Sine;
    if (str == "Clamp") return ShaderNodeType::Clamp;
    return ShaderNodeType::ConstantFloat;
}

struct ShaderGraphNode {
    uint32_t id{0};
    ShaderNodeType type{ShaderNodeType::ConstantFloat};
    std::string name{"Node"};
    glm::vec2 editorPos{100.0f, 100.0f};

    // Node parameters
    float floatVal{1.0f};
    glm::vec3 vec3Val{1.0f, 1.0f, 1.0f};
    glm::vec4 colorVal{0.8f, 0.8f, 0.8f, 1.0f};
    std::string texturePath{""};
};

struct ShaderGraphLink {
    uint32_t id{0};
    uint32_t fromNodeId{0};
    std::string fromSlot{"Out"};
    uint32_t toNodeId{0};
    std::string toSlot{"In"};
};

struct ShaderGraphOutput {
    glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float metallic{0.0f};
    float roughness{0.5f};
    float ao{1.0f};
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};
    glm::vec2 parallaxUVOffset{0.0f, 0.0f};
};

class ShaderGraph {
public:
    ShaderGraph(const std::string& name = "NewShaderGraph");

    uint32_t AddNode(ShaderNodeType type, const std::string& name = "", const glm::vec2& pos = glm::vec2(100.0f));
    bool RemoveNode(uint32_t nodeId);
    ShaderGraphNode* GetNode(uint32_t nodeId);
    const ShaderGraphNode* GetNode(uint32_t nodeId) const;

    uint32_t AddLink(uint32_t fromNodeId, const std::string& fromSlot, uint32_t toNodeId, const std::string& toSlot);
    bool RemoveLink(uint32_t linkId);

    // Evaluates the node graph for a given surface sample
    ShaderGraphOutput Evaluate(float time = 0.0f,
                               const glm::vec2& uv = glm::vec2(0.5f),
                               const glm::vec3& viewDir = glm::vec3(0.0f, 0.0f, 1.0f),
                               const glm::vec3& surfaceNormal = glm::vec3(0.0f, 0.0f, 1.0f)) const;

    // Generates GLSL fragment shader source code from the connected node graph
    std::string CompileToGLSL() const;
    // The graph as an EvaluateShaderGraph() function for shader.frag (compiled with SHADER_GRAPH
    // defined; see VulkanPipeline::GetShaderGraphPipeline): built-in names map onto the engine's
    std::string CompileToMaterialFunction() const;

    // Serialization
    JsonValue Serialize() const;
    bool Deserialize(const JsonValue& json);
    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    uint32_t GetMasterNodeId() const { return m_masterNodeId; }
    const std::vector<ShaderGraphNode>& GetNodes() const { return m_nodes; }
    std::vector<ShaderGraphNode>& GetNodesMutable() { return m_nodes; }
    const std::vector<ShaderGraphLink>& GetLinks() const { return m_links; }

private:
    glm::vec4 EvaluateNode(uint32_t nodeId,
                           float time,
                           const glm::vec2& uv,
                           const glm::vec3& viewDir,
                           const glm::vec3& surfaceNormal,
                           int depth) const;

    glm::vec4 EvaluateInputSlot(uint32_t toNodeId,
                                const std::string& toSlot,
                                const glm::vec4& defaultVal,
                                float time,
                                const glm::vec2& uv,
                                const glm::vec3& viewDir,
                                const glm::vec3& surfaceNormal,
                                int depth) const;

    std::string EmitNodeGLSL(uint32_t nodeId, std::map<uint32_t, std::string>& emittedVars, std::string& bodyCode, int depth) const;
    std::string EmitInputSlotGLSL(uint32_t toNodeId, const std::string& toSlot, const std::string& defaultExpr,
                                  std::map<uint32_t, std::string>& emittedVars, std::string& bodyCode, int depth) const;

    std::string m_name{"NewShaderGraph"};
    std::vector<ShaderGraphNode> m_nodes;
    std::vector<ShaderGraphLink> m_links;
    uint32_t m_masterNodeId{1};
    uint32_t m_nextNodeId{1};
    uint32_t m_nextLinkId{1};
};
