#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace Resource {

enum class ResourceType {
    Generic,
    Mesh,
    Texture,
    Material,
    Audio
};

inline const char* ResourceTypeToString(ResourceType type) {
    switch (type) {
        case ResourceType::Mesh: return "Mesh";
        case ResourceType::Texture: return "Texture";
        case ResourceType::Material: return "Material";
        case ResourceType::Audio: return "Audio";
        default: return "Generic";
    }
}

class Resource : public std::enable_shared_from_this<Resource> {
public:
    virtual ~Resource() = default;

    std::string path{""};
    std::string name{""};
    ResourceType type{ResourceType::Generic};
    bool isLoaded{false};
    uint64_t memoryUsage{0};

    virtual uint64_t CalculateMemoryUsage() const { return memoryUsage; }
};

class MeshResource : public Resource {
public:
    MeshResource();
    ~MeshResource() override = default;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;
    std::vector<uint32_t> indices;

    glm::vec3 boundsMin{-1.0f, -1.0f, -1.0f};
    glm::vec3 boundsMax{1.0f, 1.0f, 1.0f};
    uint32_t vertexCount{0};
    uint32_t triangleCount{0};

    uint64_t CalculateMemoryUsage() const override;
};

class TextureResource : public Resource {
public:
    TextureResource();
    ~TextureResource() override = default;

    int width{0};
    int height{0};
    int channels{4};
    std::vector<uint8_t> pixelData;

    uint64_t CalculateMemoryUsage() const override;
};

class MaterialResource : public Resource {
public:
    MaterialResource();
    ~MaterialResource() override = default;

    glm::vec4 baseColor{0.8f, 0.45f, 0.2f, 1.0f};
    float roughness{0.5f};
    float metallic{0.0f};
    float aoStrength{1.0f};
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity{0.0f};
    float heightScale{0.04f};

    std::string diffuseTexturePath{""};
    std::string normalMapPath{""};
    std::string metallicRoughnessMapPath{""};
    std::string shaderGraphPath{""};

    uint64_t CalculateMemoryUsage() const override;
};

class AudioResource : public Resource {
public:
    AudioResource();
    ~AudioResource() override = default;

    float durationSeconds{0.0f};
    int sampleRate{44100};
    int channels{2};
    std::vector<float> sampleData;

    uint64_t CalculateMemoryUsage() const override;
};

} // namespace Resource

