#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <filesystem>

struct VertexKey {
    int v{0};
    int vt{0};
    int vn{0};

    bool operator==(const VertexKey& other) const {
        return v == other.v && vt == other.vt && vn == other.vn;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const {
        std::size_t h1 = std::hash<int>()(k.v);
        std::size_t h2 = std::hash<int>()(k.vt);
        std::size_t h3 = std::hash<int>()(k.vn);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

static bool ParseVertexIndices(const std::string& token, int& v, int& vt, int& vn) {
    v = 0;
    vt = 0;
    vn = 0;
    if (token.empty()) return false;

    size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos) {
        v = std::stoi(token);
        return true;
    }

    v = std::stoi(token.substr(0, firstSlash));
    size_t secondSlash = token.find('/', firstSlash + 1);
    if (secondSlash == std::string::npos) {
        // Format: v/vt
        if (firstSlash + 1 < token.length()) {
            vt = std::stoi(token.substr(firstSlash + 1));
        }
        return true;
    }

    // Format: v/vt/vn or v//vn
    if (secondSlash > firstSlash + 1) {
        vt = std::stoi(token.substr(firstSlash + 1, secondSlash - (firstSlash + 1)));
    }
    if (secondSlash + 1 < token.length()) {
        vn = std::stoi(token.substr(secondSlash + 1));
    }
    return true;
}

bool ObjLoader::LoadMtl(const std::string& mtlPath, LoadedMesh& outMesh) {
    std::ifstream file(mtlPath);
    if (!file.is_open()) return false;

    std::filesystem::path mtlDir = std::filesystem::path(mtlPath).parent_path();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "newmtl") {
            ss >> outMesh.materialName;
        } else if (prefix == "Kd") {
            float r, g, b;
            ss >> r >> g >> b;
            outMesh.diffuseColor = glm::vec4(r, g, b, 1.0f);
        } else if (prefix == "map_Kd") {
            std::string texName;
            ss >> texName;
            if (!texName.empty()) {
                std::filesystem::path texPath = mtlDir / texName;
                outMesh.diffuseTexturePath = texPath.string();
            }
        }
    }
    std::cout << "[ObjLoader] Loaded MTL material: " << mtlPath << " (diffuse=" << outMesh.diffuseColor.r << ","
              << outMesh.diffuseColor.g << "," << outMesh.diffuseColor.b
              << ", texture=" << outMesh.diffuseTexturePath << ")" << std::endl;
    return true;
}

bool ObjLoader::Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Try fallback relative paths if needed
        std::string fallback = "../" + filepath;
        file.open(fallback);
        if (!file.is_open()) {
            std::cerr << "[ObjLoader] Failed to open file: " << filepath << std::endl;
            return false;
        }
    }

    std::vector<glm::vec3> rawPositions;
    std::vector<glm::vec3> rawNormals;
    std::vector<glm::vec2> rawTexCoords;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

    bool fileHasNormals = false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "mtllib") {
            std::string mtlFile;
            ss >> mtlFile;
            if (!mtlFile.empty()) {
                std::filesystem::path objDir = std::filesystem::path(filepath).parent_path();
                std::filesystem::path fullMtlPath = objDir / mtlFile;
                LoadMtl(fullMtlPath.string(), outMesh);
            }
        } else if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            rawPositions.push_back(pos);
        } else if (prefix == "vn") {
            glm::vec3 normal;
            ss >> normal.x >> normal.y >> normal.z;
            rawNormals.push_back(normal);
            fileHasNormals = true;
        } else if (prefix == "vt") {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            // Flip V coordinate for Vulkan UV convention
            uv.y = 1.0f - uv.y;
            rawTexCoords.push_back(uv);
        } else if (prefix == "f") {
            std::vector<std::string> faceTokens;
            std::string token;
            while (ss >> token) {
                faceTokens.push_back(token);
            }

            if (faceTokens.size() < 3) continue;

            // Triangulate n-gon face using triangle fan
            for (size_t i = 1; i + 1 < faceTokens.size(); ++i) {
                std::string triTokens[3] = { faceTokens[0], faceTokens[i], faceTokens[i + 1] };

                for (int j = 0; j < 3; ++j) {
                    int vi = 0, vti = 0, vni = 0;
                    ParseVertexIndices(triTokens[j], vi, vti, vni);

                    // Support 1-based indexing and negative relative indexing
                    int posIdx = (vi < 0) ? (static_cast<int>(rawPositions.size()) + vi) : (vi - 1);
                    int uvIdx = (vti < 0) ? (static_cast<int>(rawTexCoords.size()) + vti) : (vti - 1);
                    int normIdx = (vni < 0) ? (static_cast<int>(rawNormals.size()) + vni) : (vni - 1);

                    VertexKey key{ posIdx, uvIdx, normIdx };

                    auto it = uniqueVertices.find(key);
                    if (it != uniqueVertices.end()) {
                        indices.push_back(it->second);
                    } else {
                        Vertex vert{};
                        if (posIdx >= 0 && posIdx < static_cast<int>(rawPositions.size())) {
                            vert.pos = rawPositions[posIdx];
                        }
                        if (normIdx >= 0 && normIdx < static_cast<int>(rawNormals.size())) {
                            vert.normal = rawNormals[normIdx];
                        }
                        if (uvIdx >= 0 && uvIdx < static_cast<int>(rawTexCoords.size())) {
                            vert.texCoord = rawTexCoords[uvIdx];
                        } else {
                            vert.texCoord = glm::vec2(0.0f);
                        }

                        uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                        uniqueVertices[key] = newIndex;
                        vertices.push_back(vert);
                        indices.push_back(newIndex);
                    }
                }
            }
        }
    }

    if (vertices.empty()) {
        std::cerr << "[ObjLoader] No vertices found in file: " << filepath << std::endl;
        return false;
    }

    // Procedural normal generation if the OBJ model lacked normals
    if (!fileHasNormals) {
        for (auto& v : vertices) {
            v.normal = glm::vec3(0.0f);
        }
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            glm::vec3 v0 = vertices[i0].pos;
            glm::vec3 v1 = vertices[i1].pos;
            glm::vec3 v2 = vertices[i2].pos;

            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 faceNormal = glm::cross(edge1, edge2);

            float len = glm::length(faceNormal);
            if (len > 1e-6f) {
                faceNormal /= len;
                vertices[i0].normal += faceNormal;
                vertices[i1].normal += faceNormal;
                vertices[i2].normal += faceNormal;
            }
        }
        for (auto& v : vertices) {
            float len = glm::length(v.normal);
            if (len > 1e-6f) {
                v.normal /= len;
            } else {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    }

    // Compute bounding box
    glm::vec3 bMin(std::numeric_limits<float>::max());
    glm::vec3 bMax(std::numeric_limits<float>::lowest());

    for (const auto& v : vertices) {
        bMin = glm::min(bMin, v.pos);
        bMax = glm::max(bMax, v.pos);
    }

    glm::vec3 center = (bMin + bMax) * 0.5f;
    glm::vec3 size = bMax - bMin;
    float maxExtent = std::max(std::max(size.x, size.y), size.z);
    if (maxExtent <= 0.0f) maxExtent = 1.0f;

    // Normalize size and center pivot if requested
    if (autoCenterAndNormalize) {
        float scaleFactor = 2.0f / maxExtent;
        for (auto& v : vertices) {
            v.pos = (v.pos - center) * scaleFactor;
        }
        bMin = (bMin - center) * scaleFactor;
        bMax = (bMax - center) * scaleFactor;
        center = glm::vec3(0.0f);
        maxExtent = 2.0f;
    }

    outMesh.meshData.vertices = std::move(vertices);
    outMesh.meshData.indices = std::move(indices);
    outMesh.boundsMin = bMin;
    outMesh.boundsMax = bMax;
    outMesh.center = center;
    outMesh.maxExtent = maxExtent;
    outMesh.vertexCount = static_cast<uint32_t>(outMesh.meshData.vertices.size());
    outMesh.triangleCount = static_cast<uint32_t>(outMesh.meshData.indices.size() / 3);

    std::cout << "[ObjLoader] Successfully loaded " << filepath << " ("
              << outMesh.vertexCount << " verts, " << outMesh.triangleCount << " tris)" << std::endl;

    return true;
}
