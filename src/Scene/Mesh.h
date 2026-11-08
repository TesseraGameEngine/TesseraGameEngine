#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <string>

struct SubmeshSection {
    uint32_t firstIndex{0};
    uint32_t indexCount{0};
    std::string materialName{""};
    glm::vec4 diffuseColor{0.8f, 0.8f, 0.8f, 1.0f};
    std::string diffuseTexturePath{""};
    std::string normalMapPath{""};
    std::string metallicMapPath{""};
    std::string roughnessMapPath{""};
    std::string aoMapPath{""};
    std::string emissiveMapPath{""};
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity{0.0f};
    float roughness{0.5f};
    float metallic{0.0f};
    float alphaCutoff{0.0f}; // glTF alphaMode MASK: pixels with alpha below this are cut out (leaves)
};

struct Vertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{0.0f};
    glm::uvec4 boneIndices{0, 0, 0, 0};
    glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 color{1.0f}; // vertex colour (used when the node's material enables vertex colours)
    glm::vec2 lightmapUV{0.0f}; // position in the lightmap atlas (only lightmapped copies of a mesh set it)
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubmeshSection> submeshes;
};

class MeshGenerator {
public:
    static MeshData CreateCube(float size = 1.0f);
    static MeshData CreateSphere(float radius = 0.5f, uint32_t rings = 24, uint32_t sectors = 24);
    static MeshData CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    static MeshData CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
    static MeshData CreatePlane(float size = 2.0f);
    static MeshData CreateQuad(float size = 1.0f);
    static MeshData CreateGrid(float size = 40.0f, int divisions = 40);
    static MeshData CreateAxes(float size = 40.0f);
};
