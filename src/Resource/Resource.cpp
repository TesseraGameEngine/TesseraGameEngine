#include "Resource.h"

namespace Resource {

MeshResource::MeshResource() {
    type = ResourceType::Mesh;
}

uint64_t MeshResource::CalculateMemoryUsage() const {
    uint64_t bytes = sizeof(MeshResource);
    bytes += positions.size() * sizeof(glm::vec3);
    bytes += normals.size() * sizeof(glm::vec3);
    bytes += texCoords.size() * sizeof(glm::vec2);
    bytes += indices.size() * sizeof(uint32_t);
    return bytes;
}

TextureResource::TextureResource() {
    type = ResourceType::Texture;
}

uint64_t TextureResource::CalculateMemoryUsage() const {
    uint64_t bytes = sizeof(TextureResource);
    bytes += pixelData.size() * sizeof(uint8_t);
    return bytes;
}

MaterialResource::MaterialResource() {
    type = ResourceType::Material;
}

uint64_t MaterialResource::CalculateMemoryUsage() const {
    uint64_t bytes = sizeof(MaterialResource);
    bytes += diffuseTexturePath.size();
    bytes += normalMapPath.size();
    bytes += metallicRoughnessMapPath.size();
    bytes += shaderGraphPath.size();
    return bytes;
}

AudioResource::AudioResource() {
    type = ResourceType::Audio;
}

uint64_t AudioResource::CalculateMemoryUsage() const {
    uint64_t bytes = sizeof(AudioResource);
    bytes += sampleData.size() * sizeof(float);
    return bytes;
}

} // namespace Resource

