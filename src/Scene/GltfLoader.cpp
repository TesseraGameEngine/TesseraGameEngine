#include "GltfLoader.h"
#include "Utils/SimpleJson.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {

std::vector<uint8_t> DecodeBase64(const std::string& input) {
    static const int8_t kDecTable[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> out;
    out.reserve((input.size() * 3) / 4);
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        int8_t d = kDecTable[c];
        if (d == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

struct GltfAccessorInfo {
    int bufferView{-1};
    size_t byteOffset{0};
    int componentType{5126}; // 5126 = FLOAT, 5123 = UNSIGNED_SHORT, 5125 = UNSIGNED_INT, 5121 = UNSIGNED_BYTE
    size_t count{0};
    std::string type{"SCALAR"};
};

struct GltfBufferViewInfo {
    int buffer{0};
    size_t byteOffset{0};
    size_t byteLength{0};
    size_t byteStride{0};
};

size_t ComponentSize(int componentType) {
    switch (componentType) {
        case 5120:
        case 5121: return 1;
        case 5122:
        case 5123: return 2;
        case 5125:
        case 5126: return 4;
        default: return 4;
    }
}

size_t TypeComponentCount(const std::string& type) {
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 1;
}

bool ParseGltfJsonAndBuffers(const JsonValue& root,
                             const std::vector<std::vector<uint8_t>>& buffers,
                             const std::string& baseDir,
                             LoadedMesh& outMesh,
                             bool autoCenterAndNormalize) {
    if (!root.IsObject() || !root.HasKey("asset")) return false;

    std::vector<GltfBufferViewInfo> bufferViews;
    if (root.HasKey("bufferViews") && root["bufferViews"].IsArray()) {
        for (const auto& bv : root["bufferViews"].arrVal) {
            GltfBufferViewInfo info;
            info.buffer = bv.HasKey("buffer") ? bv["buffer"].AsInt(0) : 0;
            info.byteOffset = bv.HasKey("byteOffset") ? static_cast<size_t>(bv["byteOffset"].AsInt(0)) : 0;
            info.byteLength = bv.HasKey("byteLength") ? static_cast<size_t>(bv["byteLength"].AsInt(0)) : 0;
            info.byteStride = bv.HasKey("byteStride") ? static_cast<size_t>(bv["byteStride"].AsInt(0)) : 0;
            bufferViews.push_back(info);
        }
    }

    std::vector<GltfAccessorInfo> accessors;
    if (root.HasKey("accessors") && root["accessors"].IsArray()) {
        for (const auto& acc : root["accessors"].arrVal) {
            GltfAccessorInfo info;
            info.bufferView = acc.HasKey("bufferView") ? acc["bufferView"].AsInt(-1) : -1;
            info.byteOffset = acc.HasKey("byteOffset") ? static_cast<size_t>(acc["byteOffset"].AsInt(0)) : 0;
            info.componentType = acc.HasKey("componentType") ? acc["componentType"].AsInt(5126) : 5126;
            info.count = acc.HasKey("count") ? static_cast<size_t>(acc["count"].AsInt(0)) : 0;
            info.type = acc.HasKey("type") ? acc["type"].AsString("SCALAR") : "SCALAR";
            accessors.push_back(info);
        }
    }

    // Parse images/textures for PBR materials
    std::vector<std::string> imageUris;
    if (root.HasKey("images") && root["images"].IsArray()) {
        for (const auto& img : root["images"].arrVal) {
            std::string uri = img.HasKey("uri") ? img["uri"].AsString("") : "";
            if (!uri.empty() && uri.rfind("data:", 0) != 0 && !baseDir.empty()) {
                uri = (std::filesystem::path(baseDir) / uri).string();
            }
            imageUris.push_back(uri);
        }
    }

    std::vector<int> textureToImage;
    if (root.HasKey("textures") && root["textures"].IsArray()) {
        for (const auto& tex : root["textures"].arrVal) {
            textureToImage.push_back(tex.HasKey("source") ? tex["source"].AsInt(-1) : -1);
        }
    }

    auto resolveTextureUri = [&](int texIndex) -> std::string {
        if (texIndex >= 0 && texIndex < static_cast<int>(textureToImage.size())) {
            int imgIdx = textureToImage[texIndex];
            if (imgIdx >= 0 && imgIdx < static_cast<int>(imageUris.size())) {
                return imageUris[imgIdx];
            }
        }
        return "";
    };

    // Parse first PBR material if present
    // Parse all PBR materials if present
    std::vector<SubmeshSection> parsedMaterials;
    if (root.HasKey("materials") && root["materials"].IsArray()) {
        for (size_t mIdx = 0; mIdx < root["materials"].arrVal.size(); ++mIdx) {
            const auto& mat = root["materials"][mIdx];
            SubmeshSection sm;
            sm.materialName = mat.HasKey("name") ? mat["name"].AsString("glTF_Material_" + std::to_string(mIdx)) : ("glTF_Material_" + std::to_string(mIdx));
            if (mat.HasKey("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].IsObject()) {
                const auto& pbr = mat["pbrMetallicRoughness"];
                if (pbr.HasKey("baseColorFactor") && pbr["baseColorFactor"].arrVal.size() >= 4) {
                    sm.diffuseColor = glm::vec4(pbr["baseColorFactor"][0].AsFloat(1.0f), pbr["baseColorFactor"][1].AsFloat(1.0f), pbr["baseColorFactor"][2].AsFloat(1.0f), pbr["baseColorFactor"][3].AsFloat(1.0f));
                }
                sm.metallic = pbr.HasKey("metallicFactor") ? pbr["metallicFactor"].AsFloat(0.0f) : 0.0f;
                sm.roughness = pbr.HasKey("roughnessFactor") ? pbr["roughnessFactor"].AsFloat(0.5f) : 0.5f;
                if (pbr.HasKey("baseColorTexture") && pbr["baseColorTexture"].HasKey("index")) {
                    sm.diffuseTexturePath = resolveTextureUri(pbr["baseColorTexture"]["index"].AsInt(-1));
                }
                if (pbr.HasKey("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].HasKey("index")) {
                    std::string mrUri = resolveTextureUri(pbr["metallicRoughnessTexture"]["index"].AsInt(-1));
                    sm.metallicMapPath = mrUri;
                    sm.roughnessMapPath = mrUri;
                }
            }
            if (mat.HasKey("normalTexture") && mat["normalTexture"].HasKey("index")) sm.normalMapPath = resolveTextureUri(mat["normalTexture"]["index"].AsInt(-1));
            if (mat.HasKey("occlusionTexture") && mat["occlusionTexture"].HasKey("index")) sm.aoMapPath = resolveTextureUri(mat["occlusionTexture"]["index"].AsInt(-1));
            if (mat.HasKey("emissiveTexture") && mat["emissiveTexture"].HasKey("index")) sm.emissiveMapPath = resolveTextureUri(mat["emissiveTexture"]["index"].AsInt(-1));
            if (mat.HasKey("emissiveFactor") && mat["emissiveFactor"].arrVal.size() >= 3) {
                sm.emissiveColor = glm::vec3(mat["emissiveFactor"][0].AsFloat(0.0f), mat["emissiveFactor"][1].AsFloat(0.0f), mat["emissiveFactor"][2].AsFloat(0.0f));
                if (glm::length(sm.emissiveColor) > 0.001f) sm.emissiveIntensity = 1.0f;
            }
            parsedMaterials.push_back(sm);
        }
    }
    if (root.HasKey("materials") && root["materials"].IsArray() && !root["materials"].arrVal.empty()) {
        const auto& mat = root["materials"][0];
        outMesh.materialName = mat.HasKey("name") ? mat["name"].AsString("glTF_Material") : "glTF_Material";
        if (mat.HasKey("pbrMetallicRoughness") && mat["pbrMetallicRoughness"].IsObject()) {
            const auto& pbr = mat["pbrMetallicRoughness"];
            if (pbr.HasKey("baseColorFactor") && pbr["baseColorFactor"].arrVal.size() >= 4) {
                outMesh.diffuseColor = glm::vec4(
                    pbr["baseColorFactor"][0].AsFloat(1.0f),
                    pbr["baseColorFactor"][1].AsFloat(1.0f),
                    pbr["baseColorFactor"][2].AsFloat(1.0f),
                    pbr["baseColorFactor"][3].AsFloat(1.0f)
                );
            }
            outMesh.metallic = pbr.HasKey("metallicFactor") ? pbr["metallicFactor"].AsFloat(0.0f) : 0.0f;
            outMesh.roughness = pbr.HasKey("roughnessFactor") ? pbr["roughnessFactor"].AsFloat(0.5f) : 0.5f;
            if (pbr.HasKey("baseColorTexture") && pbr["baseColorTexture"].HasKey("index")) {
                outMesh.diffuseTexturePath = resolveTextureUri(pbr["baseColorTexture"]["index"].AsInt(-1));
            }
            if (pbr.HasKey("metallicRoughnessTexture") && pbr["metallicRoughnessTexture"].HasKey("index")) {
                std::string mrUri = resolveTextureUri(pbr["metallicRoughnessTexture"]["index"].AsInt(-1));
                outMesh.metallicMapPath = mrUri;
                outMesh.roughnessMapPath = mrUri;
            }
        }
        if (mat.HasKey("normalTexture") && mat["normalTexture"].HasKey("index")) {
            outMesh.normalMapPath = resolveTextureUri(mat["normalTexture"]["index"].AsInt(-1));
        }
        if (mat.HasKey("occlusionTexture") && mat["occlusionTexture"].HasKey("index")) {
            outMesh.aoMapPath = resolveTextureUri(mat["occlusionTexture"]["index"].AsInt(-1));
        }
        if (mat.HasKey("emissiveTexture") && mat["emissiveTexture"].HasKey("index")) {
            outMesh.emissiveMapPath = resolveTextureUri(mat["emissiveTexture"]["index"].AsInt(-1));
        }
        if (mat.HasKey("emissiveFactor") && mat["emissiveFactor"].arrVal.size() >= 3) {
            outMesh.emissiveColor = glm::vec3(
                mat["emissiveFactor"][0].AsFloat(0.0f),
                mat["emissiveFactor"][1].AsFloat(0.0f),
                mat["emissiveFactor"][2].AsFloat(0.0f)
            );
            if (glm::length(outMesh.emissiveColor) > 0.001f) {
                outMesh.emissiveIntensity = 1.0f;
            }
        }
    }

    // Parse skins/skeleton if present
    if (root.HasKey("skins") && root["skins"].IsArray() && !root["skins"].arrVal.empty()) {
        const auto& skin = root["skins"][0];
        outMesh.hasSkeleton = true;
        outMesh.skeletonName = skin.HasKey("name") ? skin["name"].AsString("glTF_Armature") : "glTF_Armature";
        outMesh.skeleton = std::make_shared<Animation::Skeleton>();
        if (skin.HasKey("joints") && skin["joints"].IsArray()) {
            for (size_t j = 0; j < skin["joints"].arrVal.size(); ++j) {
                int nodeIdx = skin["joints"][j].AsInt(-1);
                std::string boneName = "Joint_" + std::to_string(j);
                if (nodeIdx >= 0 && root.HasKey("nodes") && nodeIdx < static_cast<int>(root["nodes"].arrVal.size())) {
                    const auto& jNode = root["nodes"][nodeIdx];
                    if (jNode.HasKey("name")) boneName = jNode["name"].AsString(boneName);
                }
                outMesh.boneNames.push_back(boneName);
                outMesh.skeleton->AddBone(boneName, j == 0 ? -1 : 0);
            }
            outMesh.skeleton->CalculateInverseBindMatrices();
        }
    }

    auto getAccessorDataPtr = [&](int accIdx, size_t& outStride, size_t& outCount, int& outCompType) -> const uint8_t* {
        if (accIdx < 0 || accIdx >= static_cast<int>(accessors.size())) return nullptr;
        const auto& acc = accessors[accIdx];
        if (acc.bufferView < 0 || acc.bufferView >= static_cast<int>(bufferViews.size())) return nullptr;
        const auto& bv = bufferViews[acc.bufferView];
        if (bv.buffer < 0 || bv.buffer >= static_cast<int>(buffers.size())) return nullptr;
        const auto& buf = buffers[bv.buffer];

        size_t elemSize = ComponentSize(acc.componentType) * TypeComponentCount(acc.type);
        outStride = (bv.byteStride > 0) ? bv.byteStride : elemSize;
        outCount = acc.count;
        outCompType = acc.componentType;

        size_t startOffset = bv.byteOffset + acc.byteOffset;
        if (startOffset + (acc.count > 0 ? (acc.count - 1) * outStride + elemSize : 0) > buf.size()) {
            return nullptr;
        }
        return buf.data() + startOffset;
    };

    outMesh.meshData.vertices.clear();
    outMesh.meshData.vertices.clear();
    outMesh.meshData.indices.clear();
    outMesh.meshData.submeshes.clear();
    outMesh.submeshes.clear();
    outMesh.morphTargets.clear();

    if (!root.HasKey("meshes") || !root["meshes"].IsArray() || root["meshes"].arrVal.empty()) {
        return false;
    }

    for (const auto& meshJson : root["meshes"].arrVal) {
        if (!meshJson.HasKey("primitives") || !meshJson["primitives"].IsArray()) continue;
        for (const auto& prim : meshJson["primitives"].arrVal) {
            if (!prim.HasKey("attributes") || !prim["attributes"].IsObject()) continue;
            const auto& attrs = prim["attributes"];
            if (!attrs.HasKey("POSITION")) continue;

            size_t posStride = 0, posCount = 0;
            int posCompType = 0;
            const uint8_t* posPtr = getAccessorDataPtr(attrs["POSITION"].AsInt(-1), posStride, posCount, posCompType);
            if (!posPtr || posCount == 0 || posCompType != 5126) continue;

            size_t normStride = 0, normCount = 0; int normComp = 0;
            const uint8_t* normPtr = attrs.HasKey("NORMAL")
                ? getAccessorDataPtr(attrs["NORMAL"].AsInt(-1), normStride, normCount, normComp) : nullptr;

            size_t uvStride = 0, uvCount = 0; int uvComp = 0;
            const uint8_t* uvPtr = attrs.HasKey("TEXCOORD_0")
                ? getAccessorDataPtr(attrs["TEXCOORD_0"].AsInt(-1), uvStride, uvCount, uvComp) : nullptr;

            size_t colorStride = 0, colorCount = 0; int colorComp = 0;
            const uint8_t* colorPtr = attrs.HasKey("COLOR_0")
                ? getAccessorDataPtr(attrs["COLOR_0"].AsInt(-1), colorStride, colorCount, colorComp) : nullptr;
            // COLOR_0 is VEC3 or VEC4: float, or normalized unsigned byte / short
            const int colorComponents = colorPtr ? std::clamp(static_cast<int>(colorStride / (colorComp == 5126 ? 4 : colorComp == 5123 ? 2 : 1)), 3, 4) : 0;

            size_t jointsStride = 0, jointsCount = 0; int jointsComp = 0;
            const uint8_t* jointsPtr = attrs.HasKey("JOINTS_0")
                ? getAccessorDataPtr(attrs["JOINTS_0"].AsInt(-1), jointsStride, jointsCount, jointsComp) : nullptr;

            size_t weightsStride = 0, weightsCount = 0; int weightsComp = 0;
            const uint8_t* weightsPtr = attrs.HasKey("WEIGHTS_0")
                ? getAccessorDataPtr(attrs["WEIGHTS_0"].AsInt(-1), weightsStride, weightsCount, weightsComp) : nullptr;

            uint32_t firstIndex = static_cast<uint32_t>(outMesh.meshData.indices.size());
            uint32_t baseVertex = static_cast<uint32_t>(outMesh.meshData.vertices.size());
            for (size_t v = 0; v < posCount; ++v) {
                Vertex vert{};
                const float* p = reinterpret_cast<const float*>(posPtr + v * posStride);
                vert.pos = glm::vec3(p[0], p[1], p[2]);

                if (normPtr && v < normCount && normComp == 5126) {
                    const float* n = reinterpret_cast<const float*>(normPtr + v * normStride);
                    vert.normal = glm::vec3(n[0], n[1], n[2]);
                } else {
                    vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                if (uvPtr && v < uvCount && uvComp == 5126) {
                    const float* uv = reinterpret_cast<const float*>(uvPtr + v * uvStride);
                    vert.texCoord = glm::vec2(uv[0], uv[1]);
                } else {
                    vert.texCoord = glm::vec2(0.0f, 0.0f);
                }

                if (colorPtr && v < colorCount) {
                    const uint8_t* c = colorPtr + v * colorStride;
                    for (int k = 0; k < colorComponents; ++k) {
                        if (colorComp == 5126) vert.color[k] = reinterpret_cast<const float*>(c)[k];
                        else if (colorComp == 5123) vert.color[k] = reinterpret_cast<const uint16_t*>(c)[k] / 65535.0f;
                        else if (colorComp == 5121) vert.color[k] = c[k] / 255.0f;
                    }
                }

                if (jointsPtr && v < jointsCount) {
                    if (jointsComp == 5121) { // UNSIGNED_BYTE
                        const uint8_t* j = jointsPtr + v * jointsStride;
                        vert.boneIndices = glm::uvec4(j[0], j[1], j[2], j[3]);
                    } else if (jointsComp == 5123) { // UNSIGNED_SHORT
                        const uint16_t* j = reinterpret_cast<const uint16_t*>(jointsPtr + v * jointsStride);
                        vert.boneIndices = glm::uvec4(j[0], j[1], j[2], j[3]);
                    }
                }
                if (weightsPtr && v < weightsCount && weightsComp == 5126) {
                    const float* w = reinterpret_cast<const float*>(weightsPtr + v * weightsStride);
                    vert.boneWeights = glm::vec4(w[0], w[1], w[2], w[3]);
                }

                outMesh.meshData.vertices.push_back(vert);
            }

            // Indices
            uint32_t subFirstIndex = static_cast<uint32_t>(outMesh.meshData.indices.size());
            if (prim.HasKey("indices")) {
                size_t idxStride = 0, idxCount = 0;
                int idxComp = 0;
                const uint8_t* idxPtr = getAccessorDataPtr(prim["indices"].AsInt(-1), idxStride, idxCount, idxComp);
                if (idxPtr && idxCount > 0) {
                    for (size_t i = 0; i < idxCount; ++i) {
                        uint32_t indexVal = 0;
                        const uint8_t* elem = idxPtr + i * idxStride;
                        if (idxComp == 5121) indexVal = *elem;
                        else if (idxComp == 5123) indexVal = *reinterpret_cast<const uint16_t*>(elem);
                        else if (idxComp == 5125) indexVal = *reinterpret_cast<const uint32_t*>(elem);
                        outMesh.meshData.indices.push_back(baseVertex + indexVal);
                    }
                }
            } else {
                for (size_t i = 0; i < posCount; ++i) {
                    outMesh.meshData.indices.push_back(baseVertex + static_cast<uint32_t>(i));
                }
            }

            // Morph Targets
            uint32_t primIndexCount = static_cast<uint32_t>(outMesh.meshData.indices.size()) - subFirstIndex;
            if (primIndexCount > 0) {
                int matIdx = prim.HasKey("material") ? prim["material"].AsInt(0) : 0;
                SubmeshSection sec = (matIdx >= 0 && matIdx < static_cast<int>(parsedMaterials.size())) ? parsedMaterials[matIdx] : SubmeshSection{};
                sec.firstIndex = subFirstIndex;
                sec.indexCount = primIndexCount;
                outMesh.submeshes.push_back(sec);
                outMesh.meshData.submeshes.push_back(sec);
            }
            if (prim.HasKey("targets") && prim["targets"].IsArray()) {
                for (size_t tIdx = 0; tIdx < prim["targets"].arrVal.size(); ++tIdx) {
                    const auto& targetObj = prim["targets"][tIdx];
                    LoadedMorphTarget mt;
                    mt.name = "Target_" + std::to_string(tIdx);
                    if (meshJson.HasKey("extras") && meshJson["extras"].HasKey("targetNames") &&
                        tIdx < meshJson["extras"]["targetNames"].arrVal.size()) {
                        mt.name = meshJson["extras"]["targetNames"][tIdx].AsString(mt.name);
                    }
                    if (targetObj.HasKey("POSITION")) {
                        size_t dStride = 0, dCount = 0; int dComp = 0;
                        const uint8_t* dPtr = getAccessorDataPtr(targetObj["POSITION"].AsInt(-1), dStride, dCount, dComp);
                        if (dPtr && dComp == 5126) {
                            mt.positionDeltas.resize(dCount);
                            for (size_t k = 0; k < dCount; ++k) {
                                const float* dp = reinterpret_cast<const float*>(dPtr + k * dStride);
                                mt.positionDeltas[k] = glm::vec3(dp[0], dp[1], dp[2]);
                            }
                        }
                    }
                    if (targetObj.HasKey("NORMAL")) {
                        size_t dStride = 0, dCount = 0; int dComp = 0;
                        const uint8_t* dPtr = getAccessorDataPtr(targetObj["NORMAL"].AsInt(-1), dStride, dCount, dComp);
                        if (dPtr && dComp == 5126) {
                            mt.normalDeltas.resize(dCount);
                            for (size_t k = 0; k < dCount; ++k) {
                                const float* dn = reinterpret_cast<const float*>(dPtr + k * dStride);
                                mt.normalDeltas[k] = glm::vec3(dn[0], dn[1], dn[2]);
                            }
                        }
                    }
                    outMesh.morphTargets.push_back(mt);
                }
            }
        }
    }

    if (outMesh.meshData.vertices.empty()) return false;

    // Compute bounds and optional centering
    glm::vec3 bMin(1e9f), bMax(-1e9f);
    for (const auto& v : outMesh.meshData.vertices) {
        bMin = glm::min(bMin, v.pos);
        bMax = glm::max(bMax, v.pos);
    }
    glm::vec3 ext = bMax - bMin;
    float maxExt = std::max({ext.x, ext.y, ext.z, 0.0001f});
    glm::vec3 center = (bMin + bMax) * 0.5f;

    if (autoCenterAndNormalize) {
        float scale = 2.0f / maxExt;
        outMesh.scaleFactor = scale;
        for (auto& v : outMesh.meshData.vertices) {
            v.pos = (v.pos - center) * scale;
        }
        bMin = (bMin - center) * scale;
        bMax = (bMax - center) * scale;
    }

    outMesh.boundsMin = bMin;
    outMesh.boundsMax = bMax;
    outMesh.center = (bMin + bMax) * 0.5f;
    outMesh.maxExtent = maxExt;
    outMesh.vertexCount = static_cast<uint32_t>(outMesh.meshData.vertices.size());
    outMesh.triangleCount = static_cast<uint32_t>(outMesh.meshData.indices.size() / 3);

    return true;
}

} // namespace

bool GltfLoader::LoadFromMemory(const uint8_t* data, size_t size, LoadedMesh& outMesh,
                                const std::string& baseDir, bool autoCenterAndNormalize) {
    if (!data || size < 4) return false;

    // Check for Binary glTF (.glb) magic 'glTF' (0x46546C67)
    uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(uint32_t));

    if (magic == 0x46546C67u) {
        if (size < 12) return false;
        uint32_t version = 0, totalLen = 0;
        std::memcpy(&version, data + 4, 4);
        std::memcpy(&totalLen, data + 8, 4);
        if (version != 2 || totalLen > size) return false;

        std::string jsonStr;
        std::vector<std::vector<uint8_t>> buffers;

        size_t offset = 12;
        while (offset + 8 <= totalLen) {
            uint32_t chunkLen = 0, chunkType = 0;
            std::memcpy(&chunkLen, data + offset, 4);
            std::memcpy(&chunkType, data + offset + 4, 4);
            offset += 8;
            if (offset + chunkLen > totalLen) break;

            if (chunkType == 0x4E4F534Au) { // 'JSON'
                jsonStr.assign(reinterpret_cast<const char*>(data + offset), chunkLen);
            } else if (chunkType == 0x004E4942u) { // 'BIN\0'
                std::vector<uint8_t> binChunk(data + offset, data + offset + chunkLen);
                buffers.push_back(std::move(binChunk));
            }
            offset += chunkLen;
        }

        if (jsonStr.empty()) return false;
        JsonValue root = JsonValue::Parse(jsonStr);
        return ParseGltfJsonAndBuffers(root, buffers, baseDir, outMesh, autoCenterAndNormalize);
    }

    // Otherwise parse as ASCII/UTF-8 .gltf JSON
    std::string jsonStr(reinterpret_cast<const char*>(data), size);
    JsonValue root = JsonValue::Parse(jsonStr);
    if (!root.IsObject()) return false;

    std::vector<std::vector<uint8_t>> buffers;
    if (root.HasKey("buffers") && root["buffers"].IsArray()) {
        for (const auto& bufObj : root["buffers"].arrVal) {
            std::string uri = bufObj.HasKey("uri") ? bufObj["uri"].AsString("") : "";
            const std::string b64Prefix = "base64,";
            size_t b64Pos = uri.find(b64Prefix);
            if (b64Pos != std::string::npos) {
                buffers.push_back(DecodeBase64(uri.substr(b64Pos + b64Prefix.size())));
            } else if (!uri.empty()) {
                std::filesystem::path binPath = baseDir.empty()
                    ? std::filesystem::path(uri)
                    : (std::filesystem::path(baseDir) / uri);
                std::ifstream binFile(binPath, std::ios::binary | std::ios::ate);
                if (binFile.is_open()) {
                    size_t fsz = static_cast<size_t>(binFile.tellg());
                    binFile.seekg(0, std::ios::beg);
                    std::vector<uint8_t> bytes(fsz);
                    binFile.read(reinterpret_cast<char*>(bytes.data()), fsz);
                    buffers.push_back(std::move(bytes));
                } else {
                    buffers.emplace_back();
                }
            } else {
                buffers.emplace_back();
            }
        }
    }

    return ParseGltfJsonAndBuffers(root, buffers, baseDir, outMesh, autoCenterAndNormalize);
}

bool GltfLoader::Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[GltfLoader] Failed to open file: " << filepath << std::endl;
        return false;
    }
    std::streamsize sz = file.tellg();
    if (sz <= 0) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), sz)) return false;

    std::string baseDir = std::filesystem::path(filepath).parent_path().string();
    return LoadFromMemory(bytes.data(), bytes.size(), outMesh, baseDir, autoCenterAndNormalize);
}

std::vector<uint8_t> GltfLoader::GenerateMinimalGLB() {
    // 3 vertices (positions + normals + uvs + morph target deltas) and 3 uint16 indices (padded to 4-byte boundary)
    struct BinPayload {
        float positions[9] = {
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             0.0f,  1.0f, 0.0f
        };
        float normals[9] = {
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f
        };
        float morphDeltas[9] = {
            0.0f, 0.2f, 0.0f,
            0.0f, 0.2f, 0.0f,
            0.0f, 0.5f, 0.0f
        };
        uint16_t indices[4] = { 0, 1, 2, 0 }; // 8 bytes
    } payload;

    std::string json = R"({
  "asset": { "version": "2.0" },
  "buffers": [{ "byteLength": 116 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 108, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "materials": [
    {
      "name": "TestPBR",
      "pbrMetallicRoughness": {
        "baseColorFactor": [0.9, 0.4, 0.2, 1.0],
        "metallicFactor": 0.8,
        "roughnessFactor": 0.25
      },
      "emissiveFactor": [0.5, 0.2, 0.0]
    }
  ],
  "meshes": [
    {
      "name": "TriangleMesh",
      "extras": { "targetNames": ["Smile"] },
      "primitives": [
        {
          "attributes": { "POSITION": 0, "NORMAL": 1 },
          "indices": 3,
          "material": 0,
          "targets": [{ "POSITION": 2 }]
        }
      ]
    }
  ]
})";

    while (json.size() % 4 != 0) json.push_back(' ');

    uint32_t jsonChunkLen = static_cast<uint32_t>(json.size());
    uint32_t binChunkLen = static_cast<uint32_t>(sizeof(BinPayload));
    uint32_t totalLen = 12 + 8 + jsonChunkLen + 8 + binChunkLen;

    std::vector<uint8_t> glb(totalLen);
    uint32_t magic = 0x46546C67u;
    uint32_t version = 2;
    uint32_t jsonType = 0x4E4F534Au;
    uint32_t binType = 0x004E4942u;

    size_t offset = 0;
    std::memcpy(glb.data() + offset, &magic, 4); offset += 4;
    std::memcpy(glb.data() + offset, &version, 4); offset += 4;
    std::memcpy(glb.data() + offset, &totalLen, 4); offset += 4;

    std::memcpy(glb.data() + offset, &jsonChunkLen, 4); offset += 4;
    std::memcpy(glb.data() + offset, &jsonType, 4); offset += 4;
    std::memcpy(glb.data() + offset, json.data(), jsonChunkLen); offset += jsonChunkLen;

    std::memcpy(glb.data() + offset, &binChunkLen, 4); offset += 4;
    std::memcpy(glb.data() + offset, &binType, 4); offset += 4;
    std::memcpy(glb.data() + offset, &payload, binChunkLen);

    return glb;
}
