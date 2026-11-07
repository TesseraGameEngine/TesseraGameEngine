#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include "Mesh.h"
#include "Animation/Skeleton.h"

struct VertexSkinData {
    int boneIndices[4]{ -1, -1, -1, -1 };
    float boneWeights[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 origPos{0.0f};
    glm::vec3 origNormal{0.0f, 1.0f, 0.0f};
};

struct LoadedMorphTarget {
    std::string name;
    std::vector<glm::vec3> positionDeltas;
    std::vector<glm::vec3> normalDeltas;
};

struct LoadedMesh {
    MeshData meshData;
    glm::vec3 boundsMin{-1.0f};
    glm::vec3 boundsMax{1.0f};
    glm::vec3 center{0.0f};
    float maxExtent{2.0f};
    uint32_t vertexCount{0};
    uint32_t triangleCount{0};

    // Material & Texture properties from .mtl, .fbx, or .gltf/.glb
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
    std::vector<SubmeshSection> submeshes;

    // Skeletal / Armature & Animation info
    bool hasSkeleton{false};
    std::string skeletonName{""};
    std::vector<std::string> boneNames;
    std::vector<std::string> animClipNames;
    std::vector<VertexSkinData> skinData;
    std::shared_ptr<Animation::Skeleton> skeleton;
    std::vector<std::shared_ptr<Animation::SkeletalAnimationClip>> skeletalClips;
    std::vector<LoadedMorphTarget> morphTargets;
    float currentAnimTime{0.0f};
    bool isPlayingAnim{false};
    float animSpeed{1.0f};
    bool animLoop{true};
    float scaleFactor{1.0f};
};

class ObjLoader {
public:
    // Loads an OBJ file from disk, parses companion .mtl if present, triangulates faces, and generates normals if needed
    static bool Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize = true);

private:
    static bool LoadMtl(const std::string& mtlPath, LoadedMesh& outMesh);
};
