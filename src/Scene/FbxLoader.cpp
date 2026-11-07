#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "FbxLoader.h"
#include <ofbx.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>
#include <filesystem>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>

static void DecomposeOfbxTransform(const ofbx::DMatrix& dmat, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale) {
    glm::mat4 m(1.0f);
    for (int i = 0; i < 16; ++i) {
        glm::value_ptr(m)[i] = static_cast<float>(dmat.m[i]);
    }
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, outScale, outRot, outPos, skew, perspective);
}

bool FbxLoader::Load(const std::string& filepath, LoadedMesh& outMesh, bool autoCenterAndNormalize, const std::string& sourceFallbackPath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::string fallback = "../" + filepath;
        file.open(fallback, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[FbxLoader] Failed to open file: " << filepath << std::endl;
            return false;
        }
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<ofbx::u8> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        std::cerr << "[FbxLoader] Failed to read file data: " << filepath << std::endl;
        return false;
    }
    file.close();

    ofbx::IScene* scene = ofbx::load(buffer.data(), buffer.size(), (ofbx::u16)ofbx::LoadFlags::NONE);
    if (!scene) {
        std::cerr << "[FbxLoader] Error parsing FBX file: " << ofbx::getError() << std::endl;
        return false;
    }

    int meshCount = scene->getMeshCount();
    auto skeleton = std::make_shared<Animation::Skeleton>();
    std::filesystem::path modelPath(filepath);
    skeleton->name = modelPath.stem().string() + "_Armature";

    std::map<const ofbx::Object*, int> ofbxBoneToId;
    std::vector<const ofbx::Object*> idToOfbxBone;

    // 1. Gather bones from skin clusters
    for (int m = 0; m < meshCount; ++m) {
        const ofbx::Mesh* mesh = scene->getMesh(m);
        if (!mesh) continue;
        const ofbx::Skin* skin = mesh->getSkin();
        if (skin) {
            int clusterCount = skin->getClusterCount();
            for (int c = 0; c < clusterCount; ++c) {
                const ofbx::Cluster* cluster = skin->getCluster(c);
                if (!cluster) continue;
                const ofbx::Object* link = cluster->getLink();
                if (link && ofbxBoneToId.find(link) == ofbxBoneToId.end()) {
                    std::string bName = link->name ? link->name : ("Bone_" + std::to_string(ofbxBoneToId.size()));
                    int bId = skeleton->AddBone(bName, -1);
                    ofbxBoneToId[link] = bId;
                    idToOfbxBone.push_back(link);
                }
            }
        }
    }

    // 2. Gather bones from LIMB_NODEs
    int allObjCount = scene->getAllObjectCount();
    const ofbx::Object* const* allObjs = scene->getAllObjects();
    if (allObjs) {
        for (int i = 0; i < allObjCount; ++i) {
            const ofbx::Object* obj = allObjs[i];
            if (!obj) continue;
            if (obj->getType() == ofbx::Object::Type::LIMB_NODE) {
                if (ofbxBoneToId.find(obj) == ofbxBoneToId.end()) {
                    std::string bName = obj->name ? obj->name : ("Bone_" + std::to_string(ofbxBoneToId.size()));
                    int bId = skeleton->AddBone(bName, -1);
                    ofbxBoneToId[obj] = bId;
                    idToOfbxBone.push_back(obj);
                }
            }
        }
    }

    // Set hierarchy & rest poses
    for (size_t b = 0; b < idToOfbxBone.size(); ++b) {
        const ofbx::Object* boneObj = idToOfbxBone[b];
        const ofbx::Object* parentObj = boneObj->getParent();
        if (parentObj && ofbxBoneToId.find(parentObj) != ofbxBoneToId.end()) {
            int pId = ofbxBoneToId[parentObj];
            skeleton->bones[b].parentId = pId;
            skeleton->bones[pId].childrenIds.push_back(static_cast<int>(b));
        }

        ofbx::DMatrix dmat = boneObj->getLocalTransform();
        glm::vec3 pos, scl;
        glm::quat rot;
        DecomposeOfbxTransform(dmat, pos, rot, scl);

        skeleton->bones[b].bindPosition = pos;
        skeleton->bones[b].bindRotation = rot;
        skeleton->bones[b].bindScale = scl;
        skeleton->bones[b].currentPosition = pos;
        skeleton->bones[b].currentRotation = rot;
        skeleton->bones[b].currentScale = scl;
    }
    skeleton->CalculateInverseBindMatrices();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    outMesh.skinData.clear();
    outMesh.submeshes.clear();
    outMesh.meshData.submeshes.clear();

    for (int m = 0; m < meshCount; ++m) {
        const ofbx::Mesh* mesh = scene->getMesh(m);
        if (!mesh) continue;

        // Try extracting material diffuse color & PBR textures from the material
        std::filesystem::path fbxPath(filepath);
        std::filesystem::path modelDir = fbxPath.parent_path();
        std::string modelStem = fbxPath.stem().string();

        auto ResolveTexturePath = [&](const std::string& raw) -> std::string {
            if (raw.empty()) return "";
            std::string s = raw;
            std::replace(s.begin(), s.end(), '\\', '/');

            // 1. Check if path exists as-is
            if (std::filesystem::exists(s)) {
                return std::filesystem::absolute(s).lexically_normal().string();
            }

            // 2. Windows drive-letter missing (e.g. /Users/... -> C:/Users/...)
            if (s.rfind("/Users/", 0) == 0 || s.rfind("/users/", 0) == 0) {
                std::string withDrive = "C:" + s;
                if (std::filesystem::exists(withDrive)) {
                    return std::filesystem::absolute(withDrive).lexically_normal().string();
                }
            }

            // 3. Relative to model file directory
            std::filesystem::path p1 = modelDir / s;
            if (std::filesystem::exists(p1)) {
                return std::filesystem::absolute(p1).lexically_normal().string();
            }

            // 4. Filename only in model directory
            std::string fname = std::filesystem::path(s).filename().string();
            std::filesystem::path p2 = modelDir / fname;
            if (std::filesystem::exists(p2)) {
                return std::filesystem::absolute(p2).lexically_normal().string();
            }

            // 5. In <stem>.fbm folder (standard FBX embedded folder)
            std::filesystem::path p3 = modelDir / (modelStem + ".fbm") / fname;
            if (std::filesystem::exists(p3)) {
                return std::filesystem::absolute(p3).lexically_normal().string();
            }

            // 6. In textures/ subfolder
            std::filesystem::path p4 = modelDir / "textures" / fname;
            if (std::filesystem::exists(p4)) {
                return std::filesystem::absolute(p4).lexically_normal().string();
            }

            // 7. In parent textures/ folder (e.g. assets/textures)
            std::filesystem::path p5 = modelDir.parent_path() / "textures" / fname;
            if (std::filesystem::exists(p5)) {
                return std::filesystem::absolute(p5).lexically_normal().string();
            }

            // 8. In sourceFallbackPath directory or its .fbm folder
            if (!sourceFallbackPath.empty()) {
                std::filesystem::path srcDir = std::filesystem::path(sourceFallbackPath).parent_path();
                std::string srcStem = std::filesystem::path(sourceFallbackPath).stem().string();

                std::filesystem::path sp1 = srcDir / s;
                if (std::filesystem::exists(sp1)) return std::filesystem::absolute(sp1).lexically_normal().string();

                std::filesystem::path sp2 = srcDir / (srcStem + ".fbm") / fname;
                if (std::filesystem::exists(sp2)) return std::filesystem::absolute(sp2).lexically_normal().string();

                std::filesystem::path sp3 = srcDir / fname;
                if (std::filesystem::exists(sp3)) return std::filesystem::absolute(sp3).lexically_normal().string();

                std::filesystem::path sp4 = srcDir / "textures" / fname;
                if (std::filesystem::exists(sp4)) return std::filesystem::absolute(sp4).lexically_normal().string();
            }

            return s;
        };

        auto CheckTexture = [&](const ofbx::Texture* tex, const std::string& fallbackSlot) {
            if (!tex) return;
            ofbx::DataView relFn = tex->getRelativeFileName();
            std::string rawPath;
            if (relFn.begin && relFn.end > relFn.begin) {
                rawPath = std::string(reinterpret_cast<const char*>(relFn.begin), relFn.end - relFn.begin);
            } else {
                ofbx::DataView absFn = tex->getFileName();
                if (absFn.begin && absFn.end > absFn.begin) {
                    rawPath = std::string(reinterpret_cast<const char*>(absFn.begin), absFn.end - absFn.begin);
                }
            }
            if (rawPath.empty()) return;
            std::string resolved = ResolveTexturePath(rawPath);

            std::string lower = rawPath;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (lower.find("norm") != std::string::npos || fallbackSlot == "normal") {
                if (outMesh.normalMapPath.empty()) outMesh.normalMapPath = resolved;
            } else if (lower.find("metal") != std::string::npos || fallbackSlot == "metallic") {
                if (outMesh.metallicMapPath.empty()) outMesh.metallicMapPath = resolved;
            } else if (lower.find("rough") != std::string::npos || lower.find("gloss") != std::string::npos || fallbackSlot == "roughness") {
                if (outMesh.roughnessMapPath.empty()) outMesh.roughnessMapPath = resolved;
            } else if (lower.find("diff") != std::string::npos || lower.find("albedo") != std::string::npos || fallbackSlot == "diffuse") {
                if (outMesh.diffuseTexturePath.empty()) outMesh.diffuseTexturePath = resolved;
            } else {
                if (outMesh.diffuseTexturePath.empty()) outMesh.diffuseTexturePath = resolved;
            }
        };

        if (mesh->getMaterialCount() > 0 && outMesh.materialName.empty()) {
            const ofbx::Material* mat = mesh->getMaterial(0);
            if (mat) {
                outMesh.materialName = mat->name;
                ofbx::Color col = mat->getDiffuseColor();
                outMesh.diffuseColor = glm::vec4(col.r, col.g, col.b, 1.0f);

                CheckTexture(mat->getTexture(ofbx::Texture::DIFFUSE), "diffuse");
                CheckTexture(mat->getTexture(ofbx::Texture::NORMAL), "normal");
                CheckTexture(mat->getTexture(ofbx::Texture::SPECULAR), "metallic");
                CheckTexture(mat->getTexture(ofbx::Texture::REFLECTION), "metallic");
                CheckTexture(mat->getTexture(ofbx::Texture::SHININESS), "roughness");
            }
        }

        // Also scan all Texture objects in FBX scene to detect any named textures
        for (int i = 0; i < allObjCount; ++i) {
            const ofbx::Object* obj = allObjs[i];
            if (obj && obj->getType() == ofbx::Object::Type::TEXTURE) {
                CheckTexture(static_cast<const ofbx::Texture*>(obj), "");
            }
        }

        const ofbx::GeometryData& geom = mesh->getGeometryData();
        ofbx::Vec3Attributes positions = geom.getPositions();
        ofbx::Vec3Attributes normals = geom.getNormals();
        ofbx::Vec2Attributes uvs = geom.getUVs();
        ofbx::Vec4Attributes colors = geom.getColors();

        // Build per-vertex skin weights lookup for this mesh
        int maxCp = (std::max)(positions.count, positions.values_count);
        struct TempWeight { int boneId; float w; };
        std::vector<std::vector<TempWeight>> vertexWeights(maxCp);

        const ofbx::Skin* skin = mesh->getSkin();
        if (skin) {
            int clusterCount = skin->getClusterCount();
            for (int c = 0; c < clusterCount; ++c) {
                const ofbx::Cluster* cluster = skin->getCluster(c);
                if (!cluster) continue;
                const ofbx::Object* link = cluster->getLink();
                if (!link || ofbxBoneToId.find(link) == ofbxBoneToId.end()) continue;
                int boneId = ofbxBoneToId[link];

                int numWeights = cluster->getWeightsCount();
                int numIndices = cluster->getIndicesCount();
                const double* weights = (numWeights > 0) ? cluster->getWeights() : nullptr;
                const int* cIndices = (numIndices > 0) ? cluster->getIndices() : nullptr;
                int count = (std::min)(numWeights, numIndices);
                if (!weights || !cIndices) continue;

                for (int k = 0; k < count; ++k) {
                    int origV = cIndices[k];
                    if (origV >= 0 && origV < (int)vertexWeights.size()) {
                        float w = static_cast<float>(weights[k]);
                        if (w > 0.0001f) {
                            vertexWeights[origV].push_back({ boneId, w });
                        }
                    }
                }
            }
        }

        int partitionCount = geom.getPartitionCount();
        for (int p = 0; p < partitionCount; ++p) {
            ofbx::GeometryPartition partition = geom.getPartition(p);
            SubmeshSection sec;
            sec.firstIndex = static_cast<uint32_t>(indices.size());
            sec.indexCount = 0;
            sec.diffuseColor = outMesh.diffuseColor;
            sec.diffuseTexturePath = outMesh.diffuseTexturePath;
            sec.normalMapPath = outMesh.normalMapPath;
            sec.metallicMapPath = outMesh.metallicMapPath;
            sec.roughnessMapPath = outMesh.roughnessMapPath;
            const ofbx::Material* partMat = (p < mesh->getMaterialCount()) ? mesh->getMaterial(p) : (mesh->getMaterialCount() > 0 ? mesh->getMaterial(0) : nullptr);
            if (partMat) {
                sec.materialName = partMat->name ? partMat->name : outMesh.materialName;
                ofbx::Color col = partMat->getDiffuseColor();
                sec.diffuseColor = glm::vec4(col.r, col.g, col.b, 1.0f);
            }
            outMesh.submeshes.push_back(sec);
            outMesh.meshData.submeshes.push_back(sec);
            for (int polyIdx = 0; polyIdx < partition.polygon_count; ++polyIdx) {
                const ofbx::GeometryPartition::Polygon& polygon = partition.polygons[polyIdx];
                int stackIndices[64];
                std::vector<int> heapIndices;
                int* triIndices = stackIndices;
                int neededIndices = (polygon.vertex_count > 2) ? 3 * (polygon.vertex_count - 2) : 0;
                if (neededIndices > 64) {
                    heapIndices.resize(neededIndices);
                    triIndices = heapIndices.data();
                }
                ofbx::u32 triCount = ofbx::triangulate(geom, polygon, triIndices);

                for (ofbx::u32 t = 0; t < triCount; ++t) {
                    int vIdx = triIndices[t];
                    Vertex v{};

                    if (vIdx < positions.count) {
                        ofbx::Vec3 pos = positions.get(vIdx);
                        v.pos = glm::vec3(pos.x, pos.y, pos.z);
                    }

                    if (vIdx < normals.count) {
                        ofbx::Vec3 norm = normals.get(vIdx);
                        v.normal = glm::vec3(norm.x, norm.y, norm.z);
                    } else {
                        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    }

                    if (vIdx < uvs.count) {
                        ofbx::Vec2 uv = uvs.get(vIdx);
                        // Flip V coordinate for Vulkan UV convention
                        v.texCoord = glm::vec2(uv.x, 1.0f - uv.y);
                    } else {
                        v.texCoord = glm::vec2(0.0f);
                    }
                    if (colors.values && vIdx < colors.count) {
                        ofbx::Vec4 c = colors.get(vIdx);
                        v.color = glm::vec4(c.x, c.y, c.z, c.w);
                    }

                    VertexSkinData skinEntry{};
                    skinEntry.origPos = v.pos;
                    skinEntry.origNormal = v.normal;

                    int cpIdx = (positions.indices && vIdx < positions.count) ? positions.indices[vIdx] : vIdx;
                    if (cpIdx >= 0 && cpIdx < (int)vertexWeights.size()) {
                        auto& wList = vertexWeights[cpIdx];
                        std::sort(wList.begin(), wList.end(), [](const auto& a, const auto& b) {
                            return a.w > b.w;
                        });
                        float sum = 0.0f;
                        int maxB = (std::min)((int)wList.size(), 4);
                        for (int i = 0; i < maxB; ++i) {
                            skinEntry.boneIndices[i] = wList[i].boneId;
                            skinEntry.boneWeights[i] = wList[i].w;
                            sum += wList[i].w;
                        }
                        if (sum > 0.0001f) {
                            for (int i = 0; i < maxB; ++i) {
                                skinEntry.boneWeights[i] /= sum;
                            }
                        }
                    }
                    outMesh.skinData.push_back(skinEntry);

                    v.boneIndices = glm::uvec4(
                        skinEntry.boneIndices[0] >= 0 ? skinEntry.boneIndices[0] : 0,
                        skinEntry.boneIndices[1] >= 0 ? skinEntry.boneIndices[1] : 0,
                        skinEntry.boneIndices[2] >= 0 ? skinEntry.boneIndices[2] : 0,
                        skinEntry.boneIndices[3] >= 0 ? skinEntry.boneIndices[3] : 0
                    );
                    v.boneWeights = glm::vec4(
                        skinEntry.boneWeights[0],
                        skinEntry.boneWeights[1],
                        skinEntry.boneWeights[2],
                        skinEntry.boneWeights[3]
                    );

                    indices.push_back(static_cast<uint32_t>(vertices.size()));
                    vertices.push_back(v);
                    outMesh.submeshes.back().indexCount = static_cast<uint32_t>(indices.size()) - outMesh.submeshes.back().firstIndex;
                    outMesh.meshData.submeshes.back().indexCount = outMesh.submeshes.back().indexCount;
                }
            }
        }
    }

    // 3. Extract animation clips / animation stacks
    int animStackCount = scene->getAnimationStackCount();
    for (int a = 0; a < animStackCount; ++a) {
        const ofbx::AnimationStack* stack = scene->getAnimationStack(a);
        if (!stack) continue;
        const ofbx::AnimationLayer* layer = stack->getLayer(0);
        if (!layer) continue;

        auto clip = std::make_shared<Animation::SkeletalAnimationClip>();
        clip->name = stack->name ? stack->name : ("Clip_" + std::to_string(a));
        size_t pipePos = clip->name.find('|');
        if (pipePos != std::string::npos && pipePos + 1 < clip->name.length()) {
            clip->name = clip->name.substr(pipePos + 1);
        }

        const ofbx::TakeInfo* takeInfo = scene->getTakeInfo(stack->name);
        double startTime = takeInfo ? takeInfo->local_time_from : 0.0;
        double endTime = takeInfo ? takeInfo->local_time_to : 0.0;
        if (endTime <= startTime) {
            endTime = startTime + 1.0;
        }

        float duration = static_cast<float>(endTime - startTime);
        if (duration <= 0.05f) duration = 1.0f;
        clip->duration = duration;

        const float sampleRate = 30.0f;
        int frameCount = static_cast<int>(duration * sampleRate) + 1;

        // Detect if Frame 0 is an artist reference/bind pose frame rather than the active animation loop.
        // Some FBX exporters (Maya, 3ds Max, Renderpeople) insert an un-animated rest pose at Frame 0
        // with raw rotation = 0, causing major limbs (arms/legs) to snap into a T-pose before the actual
        // animated cycle begins on Frame 1. If Frame 0 jumps > 25 degrees to Frame 1 while Frame 1 and Frame End
        // form a continuous seamless loop (< 45 degrees difference), offset the clip to start at Frame 1.
        bool skipFrame0 = false;
                if (startTime == 0.0 && frameCount >= 3) {
            for (size_t b = 0; b < idToOfbxBone.size(); ++b) {
                const ofbx::Object* boneObj = idToOfbxBone[b];
                std::string bName = boneObj->name ? boneObj->name : "";
                std::transform(bName.begin(), bName.end(), bName.begin(), ::tolower);
                if (bName.find("upperarm") != std::string::npos || bName.find("arm") != std::string::npos ||
                    bName.find("thigh") != std::string::npos || bName.find("upperleg") != std::string::npos) {
                    const ofbx::AnimationCurveNode* rNode = layer->getCurveNode(*boneObj, "Lcl Rotation");
                    if (rNode) {
                        ofbx::DVec3 r0 = rNode->getNodeLocalTransform(0.0);
                        ofbx::DVec3 r1 = rNode->getNodeLocalTransform(1.0 / sampleRate);
                        ofbx::DVec3 rEnd = rNode->getNodeLocalTransform(endTime);
                        double diff01 = std::abs(r1.x - r0.x) + std::abs(r1.y - r0.y) + std::abs(r1.z - r0.z);
                        double diff1End = std::abs(rEnd.x - r1.x) + std::abs(rEnd.y - r1.y) + std::abs(rEnd.z - r1.z);
                        if (diff01 > 25.0 && diff1End < 45.0) {
                                                        skipFrame0 = true;
                            break;
                        }
                    }
                }
            }
        }

        if (skipFrame0) {
            startTime += (1.0 / sampleRate);
            duration = static_cast<float>(endTime - startTime);
            if (duration <= 0.05f) duration = 1.0f;
            clip->duration = duration;
            frameCount = static_cast<int>(duration * sampleRate) + 1;
        }

        for (size_t b = 0; b < idToOfbxBone.size(); ++b) {
            const ofbx::Object* boneObj = idToOfbxBone[b];
            const ofbx::AnimationCurveNode* tNode = layer->getCurveNode(*boneObj, "Lcl Translation");
            const ofbx::AnimationCurveNode* rNode = layer->getCurveNode(*boneObj, "Lcl Rotation");
            const ofbx::AnimationCurveNode* sNode = layer->getCurveNode(*boneObj, "Lcl Scaling");

            if (tNode || rNode || sNode) {
                auto track = clip->GetOrCreateTrack(static_cast<int>(b), skeleton->bones[b].name);
                for (int f = 0; f < frameCount; ++f) {
                    float localT = f / sampleRate;
                    double evalTime = startTime + localT;

                    ofbx::DVec3 trans = tNode ? tNode->getNodeLocalTransform(evalTime) : boneObj->getLocalTranslation();
                    ofbx::DVec3 rotEuler = rNode ? rNode->getNodeLocalTransform(evalTime) : boneObj->getLocalRotation();
                    ofbx::DVec3 scale = sNode ? sNode->getNodeLocalTransform(evalTime) : boneObj->getLocalScaling();

                    ofbx::DMatrix dmat = boneObj->evalLocal(trans, rotEuler, scale);
                    glm::vec3 pos, scl;
                    glm::quat rot;
                    DecomposeOfbxTransform(dmat, pos, rot, scl);

                    track->AddKeyframe(localT, pos, rot, scl);
                }
            }
        }

        outMesh.skeletalClips.push_back(clip);
        if (std::find(outMesh.animClipNames.begin(), outMesh.animClipNames.end(), clip->name) == outMesh.animClipNames.end()) {
            outMesh.animClipNames.push_back(clip->name);
        }
    }

    if (!skeleton->bones.empty()) {
        outMesh.hasSkeleton = true;
        outMesh.skeleton = skeleton;
        outMesh.skeletonName = skeleton->name;
        for (const auto& b : skeleton->bones) {
            if (std::find(outMesh.boneNames.begin(), outMesh.boneNames.end(), b.name) == outMesh.boneNames.end()) {
                outMesh.boneNames.push_back(b.name);
            }
        }
    }

    scene->destroy();

    if (vertices.empty()) {
        std::cerr << "[FbxLoader] No geometry vertices found in FBX: " << filepath << std::endl;
        return false;
    }

    // Compute bounding box
    glm::vec3 bMin(std::numeric_limits<float>::max());
    glm::vec3 bMax(std::numeric_limits<float>::lowest());

    for (const auto& v : vertices) {
        bMin = glm::min(bMin, v.pos);
        bMax = glm::max(bMax, v.pos);
    }

    glm::vec3 origCenter = (bMin + bMax) * 0.5f;
    glm::vec3 boxSize = bMax - bMin;
    float maxExtent = (std::max)((std::max)(boxSize.x, boxSize.y), boxSize.z);
    if (maxExtent <= 0.0f) maxExtent = 1.0f;

    float scaleFactor = 1.0f;
    // Normalize size and center pivot if requested
    if (autoCenterAndNormalize) {
        scaleFactor = 2.0f / maxExtent;
        for (auto& v : vertices) {
            v.pos = (v.pos - origCenter) * scaleFactor;
        }
        bMin = (bMin - origCenter) * scaleFactor;
        bMax = (bMax - origCenter) * scaleFactor;
    }

    outMesh.meshData.vertices = std::move(vertices);
    outMesh.meshData.indices = std::move(indices);
    outMesh.boundsMin = bMin;
    outMesh.boundsMax = bMax;
    outMesh.center = origCenter;
    outMesh.maxExtent = maxExtent;
    outMesh.scaleFactor = scaleFactor;
    outMesh.vertexCount = static_cast<uint32_t>(outMesh.meshData.vertices.size());
    outMesh.triangleCount = static_cast<uint32_t>(outMesh.meshData.indices.size() / 3);

    std::cout << "[FbxLoader] Successfully loaded " << filepath << " ("
              << outMesh.vertexCount << " verts, " << outMesh.triangleCount << " tris)" << std::endl;

    return true;
}
