#include "Nodes/UI/UIBoxContainerNode.h"
#include "Nodes/UI/UIGridContainerNode.h"
#include "Scene.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>

#include "Node.h"
#include "Nodes/NodeFactory.h"
#include "Nodes/AllNodes.h"
#include "Core/UndoRedoManager.h"

namespace {

// Terrain splat maps are saved as base64 (a quarter the size of a number array)
const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string TerrainBase64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out += kBase64[(v >> 18) & 63];
        out += kBase64[(v >> 12) & 63];
        out += i + 1 < data.size() ? kBase64[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? kBase64[v & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> TerrainBase64Decode(const std::string& text) {
    int lookup[256];
    for (int& v : lookup) v = -1;
    for (int i = 0; i < 64; ++i) lookup[static_cast<unsigned char>(kBase64[i])] = i;
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    uint32_t buffer = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (lookup[c] < 0) continue; // padding and anything else
        buffer = (buffer << 6) | static_cast<uint32_t>(lookup[c]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

Scene::Scene() {
    ResetToDefault();
}

void Scene::ResetToDefault() {
    Clear();
    m_name = "Main Scene";
    m_filePath = "";
    m_nextId = 1;

    // 1. Default 3D Cube
    auto cube = AddNode(ShapeType::Cube, "Cube");

    // 2. Default Point Light illuminating the scene
    auto light = AddLightNode(LightType::Point, "Point Light");
    light->position = glm::vec3(2.5f, 3.5f, 2.0f);
    light->lightColor = glm::vec3(1.0f, 0.95f, 0.85f);
    light->lightIntensity = 2.5f;

    // 3. Default Scene Camera
    auto camera = AddCameraNode("Main Camera");
    camera->position = glm::vec3(0.0f, 2.0f, 5.0f);
    camera->rotation = glm::vec3(-15.0f, 0.0f, 0.0f);
    m_activeCameraId = camera->id;

    m_selectedId = cube->id;
}

void Scene::Clear() {
    ClearNodes();
    m_environment = SceneEnvironment{};
}

void Scene::ClearNodes() {
    m_nodes.clear();
    m_selectedId = -1;
    m_activeCameraId = 0;
}

std::vector<std::shared_ptr<Node>> Scene::GetUINodes() const {
    std::vector<std::shared_ptr<Node>> uis;
    for (const auto& n : m_nodes) {
        if (n->IsUINode()) uis.push_back(n);
    }
    return uis;
}

bool Scene::IsDescendantOf(uint32_t potentialDescendantId, uint32_t ancestorId) const {
    if (potentialDescendantId == 0 || ancestorId == 0) return false;
    if (potentialDescendantId == ancestorId) return true;

    auto current = GetNodeById(potentialDescendantId);
    while (current && current->parentId != 0) {
        if (current->parentId == ancestorId) return true;
        current = GetNodeById(current->parentId);
    }
    return false;
}

bool Scene::SetParent(uint32_t childId, uint32_t newParentId) {
    if (childId == 0 || childId == newParentId) return false;

    auto child = GetNodeById(childId);
    if (!child) return false;

    // Prevent circular parenting
    if (newParentId != 0 && IsDescendantOf(newParentId, childId)) {
        std::cerr << "[Scene] Cannot parent a node to one of its descendants!" << std::endl;
        return false;
    }

    // Preserve world transform across reparenting
    glm::mat4 worldMat = child->GetWorldTransformMatrix(*this);

    // Remove from old parent's childrenIds list
    if (child->parentId != 0) {
        auto oldParent = GetNodeById(child->parentId);
        if (oldParent) {
            auto& cList = oldParent->childrenIds;
            cList.erase(std::remove(cList.begin(), cList.end(), childId), cList.end());
        }
    }

    // Assign new parent
    child->parentId = newParentId;

    if (newParentId != 0) {
        auto newParent = GetNodeById(newParentId);
        if (newParent) {
            // Check not already in children list
            if (std::find(newParent->childrenIds.begin(), newParent->childrenIds.end(), childId) == newParent->childrenIds.end()) {
                newParent->childrenIds.push_back(childId);
            }
        }
    }

    // Restore world transform relative to new parent
    child->SetWorldTransformMatrix(worldMat, *this);
    return true;
}

bool Scene::Unparent(uint32_t childId) {
    return SetParent(childId, 0);
}

std::vector<std::shared_ptr<Node>> Scene::GetRootNodes() const {
    std::vector<std::shared_ptr<Node>> roots;
    for (const auto& node : m_nodes) {
        if (node->parentId == 0) {
            roots.push_back(node);
        }
    }
    return roots;
}

std::vector<std::shared_ptr<Node>> Scene::GetChildren(uint32_t parentId) const {
    std::vector<std::shared_ptr<Node>> children;
    auto parent = GetNodeById(parentId);
    if (!parent) return children;

    for (uint32_t cid : parent->childrenIds) {
        auto c = GetNodeById(cid);
        if (c) children.push_back(c);
    }
    return children;
}

void Scene::CollectSubtreeIds(uint32_t nodeId, std::vector<uint32_t>& outIds) const {
    outIds.push_back(nodeId);
    auto node = GetNodeById(nodeId);
    if (node) {
        for (uint32_t cid : node->childrenIds) {
            CollectSubtreeIds(cid, outIds);
        }
    }
}

bool Scene::ReorderNodeBefore(uint32_t sourceId, uint32_t targetId) {
    if (sourceId == 0 || targetId == 0 || sourceId == targetId) return false;

    auto source = GetNodeById(sourceId);
    auto target = GetNodeById(targetId);
    if (!source || !target) return false;

    // Prevent circular parenting: target cannot be a descendant of source
    if (IsDescendantOf(targetId, sourceId)) {
        std::cerr << "[Scene] Cannot move a node before one of its descendants!" << std::endl;
        return false;
    }

    // Preserve world transform across potential reparenting
    glm::mat4 worldMat = source->GetWorldTransformMatrix(*this);

    // 1. If source had an old parent, remove from its childrenIds
    if (source->parentId != 0) {
        auto oldParent = GetNodeById(source->parentId);
        if (oldParent) {
            auto& cList = oldParent->childrenIds;
            cList.erase(std::remove(cList.begin(), cList.end(), sourceId), cList.end());
        }
    }

    // 2. Set new parent to match target's parent
    source->parentId = target->parentId;

    // 3. If target has a parent, insert sourceId into parent->childrenIds before targetId
    if (target->parentId != 0) {
        auto newParent = GetNodeById(target->parentId);
        if (newParent) {
            auto& cList = newParent->childrenIds;
            cList.erase(std::remove(cList.begin(), cList.end(), sourceId), cList.end());
            auto targetIt = std::find(cList.begin(), cList.end(), targetId);
            if (targetIt != cList.end()) {
                cList.insert(targetIt, sourceId);
            } else {
                cList.push_back(sourceId);
            }
        }
    }

    // 4. Reorder in m_nodes: move source's entire subtree before target
    std::vector<uint32_t> subtreeIds;
    CollectSubtreeIds(sourceId, subtreeIds);

    std::vector<std::shared_ptr<Node>> subtreeNodes;
    for (uint32_t sid : subtreeIds) {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [sid](const std::shared_ptr<Node>& n) {
            return n->id == sid;
        });
        if (it != m_nodes.end()) {
            subtreeNodes.push_back(*it);
        }
    }

    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&subtreeIds](const std::shared_ptr<Node>& n) {
        return std::find(subtreeIds.begin(), subtreeIds.end(), n->id) != subtreeIds.end();
    }), m_nodes.end());

    auto targetPos = std::find_if(m_nodes.begin(), m_nodes.end(), [targetId](const std::shared_ptr<Node>& n) {
        return n->id == targetId;
    });

    if (targetPos != m_nodes.end()) {
        m_nodes.insert(targetPos, subtreeNodes.begin(), subtreeNodes.end());
    } else {
        m_nodes.insert(m_nodes.end(), subtreeNodes.begin(), subtreeNodes.end());
    }

    // Restore world transform relative to new parent
    source->SetWorldTransformMatrix(worldMat, *this);
    return true;
}

bool Scene::ReorderNodeAfter(uint32_t sourceId, uint32_t targetId) {
    if (sourceId == 0 || targetId == 0 || sourceId == targetId) return false;

    auto source = GetNodeById(sourceId);
    auto target = GetNodeById(targetId);
    if (!source || !target) return false;

    // Prevent circular parenting: target cannot be a descendant of source
    if (IsDescendantOf(targetId, sourceId)) {
        std::cerr << "[Scene] Cannot move a node after one of its descendants!" << std::endl;
        return false;
    }

    // Preserve world transform across potential reparenting
    glm::mat4 worldMat = source->GetWorldTransformMatrix(*this);

    // 1. If source had an old parent, remove from its childrenIds
    if (source->parentId != 0) {
        auto oldParent = GetNodeById(source->parentId);
        if (oldParent) {
            auto& cList = oldParent->childrenIds;
            cList.erase(std::remove(cList.begin(), cList.end(), sourceId), cList.end());
        }
    }

    // 2. Set new parent to match target's parent
    source->parentId = target->parentId;

    // 3. If target has a parent, insert sourceId into parent->childrenIds after targetId
    if (target->parentId != 0) {
        auto newParent = GetNodeById(target->parentId);
        if (newParent) {
            auto& cList = newParent->childrenIds;
            cList.erase(std::remove(cList.begin(), cList.end(), sourceId), cList.end());
            auto targetIt = std::find(cList.begin(), cList.end(), targetId);
            if (targetIt != cList.end()) {
                cList.insert(targetIt + 1, sourceId);
            } else {
                cList.push_back(sourceId);
            }
        }
    }

    // 4. Reorder in m_nodes: move source's entire subtree after target's subtree
    std::vector<uint32_t> sourceSubtreeIds;
    CollectSubtreeIds(sourceId, sourceSubtreeIds);

    std::vector<std::shared_ptr<Node>> sourceSubtreeNodes;
    for (uint32_t sid : sourceSubtreeIds) {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [sid](const std::shared_ptr<Node>& n) {
            return n->id == sid;
        });
        if (it != m_nodes.end()) {
            sourceSubtreeNodes.push_back(*it);
        }
    }

    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&sourceSubtreeIds](const std::shared_ptr<Node>& n) {
        return std::find(sourceSubtreeIds.begin(), sourceSubtreeIds.end(), n->id) != sourceSubtreeIds.end();
    }), m_nodes.end());

    std::vector<uint32_t> targetSubtreeIds;
    CollectSubtreeIds(targetId, targetSubtreeIds);

    auto insertPos = m_nodes.end();
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (std::find(targetSubtreeIds.begin(), targetSubtreeIds.end(), (*it)->id) != targetSubtreeIds.end()) {
            insertPos = it + 1;
        }
    }

    if (insertPos != m_nodes.end()) {
        m_nodes.insert(insertPos, sourceSubtreeNodes.begin(), sourceSubtreeNodes.end());
    } else {
        m_nodes.insert(m_nodes.end(), sourceSubtreeNodes.begin(), sourceSubtreeNodes.end());
    }

    // Restore world transform relative to new parent
    source->SetWorldTransformMatrix(worldMat, *this);
    return true;
}

bool Scene::MoveNodeUp(uint32_t nodeId) {
    if (nodeId == 0) return false;
    auto node = GetNodeById(nodeId);
    if (!node) return false;

    if (node->parentId == 0) {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const std::shared_ptr<Node>& n) {
            return n->id == nodeId;
        });
        if (it == m_nodes.end()) return false;

        auto prevRootIt = m_nodes.end();
        for (auto searchIt = m_nodes.begin(); searchIt != it; ++searchIt) {
            if ((*searchIt)->parentId == 0) {
                prevRootIt = searchIt;
            }
        }
        if (prevRootIt == m_nodes.end()) {
            return false;
        }

        return ReorderNodeBefore(nodeId, (*prevRootIt)->id);
    } else {
        auto parent = GetNodeById(node->parentId);
        if (!parent) return false;

        auto it = std::find(parent->childrenIds.begin(), parent->childrenIds.end(), nodeId);
        if (it == parent->childrenIds.end() || it == parent->childrenIds.begin()) {
            return false;
        }

        uint32_t prevSiblingId = *(it - 1);
        return ReorderNodeBefore(nodeId, prevSiblingId);
    }
}

bool Scene::MoveNodeDown(uint32_t nodeId) {
    if (nodeId == 0) return false;
    auto node = GetNodeById(nodeId);
    if (!node) return false;

    if (node->parentId == 0) {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const std::shared_ptr<Node>& n) {
            return n->id == nodeId;
        });
        if (it == m_nodes.end()) return false;

        auto nextRootIt = m_nodes.end();
        for (auto searchIt = it + 1; searchIt != m_nodes.end(); ++searchIt) {
            if ((*searchIt)->parentId == 0) {
                nextRootIt = searchIt;
                break;
            }
        }
        if (nextRootIt == m_nodes.end()) {
            return false;
        }

        return ReorderNodeAfter(nodeId, (*nextRootIt)->id);
    } else {
        auto parent = GetNodeById(node->parentId);
        if (!parent) return false;

        auto it = std::find(parent->childrenIds.begin(), parent->childrenIds.end(), nodeId);
        if (it == parent->childrenIds.end() || it + 1 == parent->childrenIds.end()) {
            return false;
        }

        uint32_t nextSiblingId = *(it + 1);
        return ReorderNodeAfter(nodeId, nextSiblingId);
    }
}

bool Scene::MoveNodeToTop(uint32_t nodeId) {
    if (nodeId == 0) return false;
    auto node = GetNodeById(nodeId);
    if (!node) return false;

    if (node->parentId == 0) {
        auto roots = GetRootNodes();
        if (roots.empty() || roots[0]->id == nodeId) return false;
        return ReorderNodeBefore(nodeId, roots[0]->id);
    } else {
        auto parent = GetNodeById(node->parentId);
        if (!parent || parent->childrenIds.empty() || parent->childrenIds[0] == nodeId) return false;
        return ReorderNodeBefore(nodeId, parent->childrenIds[0]);
    }
}

bool Scene::MoveNodeToBottom(uint32_t nodeId) {
    if (nodeId == 0) return false;
    auto node = GetNodeById(nodeId);
    if (!node) return false;

    if (node->parentId == 0) {
        auto roots = GetRootNodes();
        if (roots.empty() || roots.back()->id == nodeId) return false;
        return ReorderNodeAfter(nodeId, roots.back()->id);
    } else {
        auto parent = GetNodeById(node->parentId);
        if (!parent || parent->childrenIds.empty() || parent->childrenIds.back() == nodeId) return false;
        return ReorderNodeAfter(nodeId, parent->childrenIds.back());
    }
}

bool Scene::DeleteNode(uint32_t id) {
    auto rootNode = GetNodeById(id);
    if (!rootNode) return false;

    JsonValue beforeSnapshot = Serialize();
    int32_t beforeSelectedId = m_selectedId;
    std::string deletedNodeName = rootNode->name;

    std::function<bool(uint32_t)> deleteRecursive = [&](uint32_t targetId) -> bool {
        auto node = GetNodeById(targetId);
        if (!node) return false;

        std::vector<uint32_t> childrenToDelete = node->childrenIds;
        for (uint32_t cid : childrenToDelete) {
            deleteRecursive(cid);
        }

        if (node->parentId != 0) {
            auto parent = GetNodeById(node->parentId);
            if (parent) {
                auto& cList = parent->childrenIds;
                cList.erase(std::remove(cList.begin(), cList.end(), targetId), cList.end());
            }
        }

        if (m_activeCameraId == targetId) {
            m_activeCameraId = 0;
        }

        if (m_selectedId == static_cast<int32_t>(targetId)) {
            m_selectedId = -1;
        }

        auto it = std::remove_if(m_nodes.begin(), m_nodes.end(), [targetId](const std::shared_ptr<Node>& n) {
            return n->id == targetId;
        });

        if (it != m_nodes.end()) {
            m_nodes.erase(it, m_nodes.end());
            if (m_activeCameraId == 0) {
                auto cameras = GetCameras();
                if (!cameras.empty()) m_activeCameraId = cameras[0]->id;
            }
            return true;
        }
        return false;
    };

    bool deleted = deleteRecursive(id);
    if (deleted) {
        Core::UndoRedoManager::Get().RecordSnapshot(
            beforeSnapshot,
            Serialize(),
            beforeSelectedId,
            m_selectedId,
            "Delete " + deletedNodeName
        );
    }
    return deleted;
}

bool Scene::DeleteSelected() {
    if (m_selectedId > 0) {
        return DeleteNode(static_cast<uint32_t>(m_selectedId));
    }
    return false;
}

std::shared_ptr<Node> Scene::GetNodeById(uint32_t id) const {
    for (const auto& n : m_nodes) {
        if (n->id == id) return n;
    }
    return nullptr;
}

std::shared_ptr<Node> Scene::GetNodeByName(const std::string& name) const {
    for (const auto& n : m_nodes) {
        if (n->name == name) return n;
    }
    return nullptr;
}

std::shared_ptr<Node> Scene::GetSelectedNode() const {
    if (m_selectedId <= 0) return nullptr;
    return GetNodeById(static_cast<uint32_t>(m_selectedId));
}

std::vector<std::shared_ptr<Node>> Scene::GetCameras() const {
    std::vector<std::shared_ptr<Node>> cameras;
    for (const auto& n : m_nodes) {
        if (n->nodeType == NodeType::Camera) cameras.push_back(n);
    }
    return cameras;
}

std::vector<std::shared_ptr<Node>> Scene::GetLights() const {
    std::vector<std::shared_ptr<Node>> lights;
    for (const auto& n : m_nodes) {
        if (n->nodeType == NodeType::Light) lights.push_back(n);
    }
    return lights;
}

std::vector<std::shared_ptr<Node>> Scene::GetColliders() const {
    std::vector<std::shared_ptr<Node>> colliders;
    for (const auto& n : m_nodes) {
        if (n->nodeType == NodeType::Collider) colliders.push_back(n);
    }
    return colliders;
}

std::shared_ptr<Node> Scene::GetActiveCamera() const {
    if (m_activeCameraId != 0) {
        auto cam = GetNodeById(m_activeCameraId);
        if (cam && cam->nodeType == NodeType::Camera) return cam;
    }
    auto cameras = GetCameras();
    if (!cameras.empty()) return cameras[0];
    return nullptr;
}

JsonValue Scene::Serialize() const {
    JsonValue sceneObj = JsonValue::Object();
    sceneObj["name"] = m_name;
    sceneObj["nextId"] = m_nextId;
    sceneObj["activeCameraId"] = m_activeCameraId;
    JsonValue worldOffsetArr = JsonValue::Array();
    worldOffsetArr.Push(m_worldOffset.x);
    worldOffsetArr.Push(m_worldOffset.y);
    worldOffsetArr.Push(m_worldOffset.z);
    sceneObj["worldOffset"] = worldOffsetArr;

    JsonValue envObj = JsonValue::Object();
    envObj["hdrExposure"] = m_environment.hdrExposure;
    envObj["enableACES"] = m_environment.enableACES;
    envObj["antiAliasing"] = m_environment.antiAliasing;
    envObj["enableShadows"] = m_environment.enableShadows;
    envObj["shadowDistance"] = m_environment.shadowDistance;
    envObj["enableSky"] = m_environment.enableSky;
    envObj["skyIntensity"] = m_environment.skyIntensity;
    envObj["physicalAtmosphere"] = m_environment.physicalAtmosphere;
    envObj["volumetricClouds"] = m_environment.volumetricClouds;
    envObj["cloudThickness"] = m_environment.cloudThickness;
    envObj["cloudDetail"] = m_environment.cloudDetail;
    envObj["timeOfDayEnabled"] = m_environment.timeOfDayEnabled;
    envObj["timeOfDay"] = m_environment.timeOfDay;
    envObj["dayLengthMinutes"] = m_environment.dayLengthMinutes;
    envObj["clockRunsInEditor"] = m_environment.clockRunsInEditor;
    envObj["latitude"] = m_environment.latitude;
    envObj["northAngle"] = m_environment.northAngle;
    envObj["dayOfYear"] = m_environment.dayOfYear;
    envObj["advanceCalendar"] = m_environment.advanceCalendar;
    envObj["moonEnabled"] = m_environment.moonEnabled;
    envObj["moonPhase"] = m_environment.moonPhase;
    envObj["moonBrightness"] = m_environment.moonBrightness;
    envObj["cloudsEnabled"] = m_environment.cloudsEnabled;
    envObj["cloudCoverage"] = m_environment.cloudCoverage;
    envObj["cloudOpacity"] = m_environment.cloudOpacity;
    envObj["cloudHeight"] = m_environment.cloudHeight;
    envObj["cloudScale"] = m_environment.cloudScale;
    envObj["windSpeed"] = m_environment.windSpeed;
    envObj["windDirection"] = m_environment.windDirection;
    envObj["cloudShadows"] = m_environment.cloudShadows;
    envObj["weatherEnabled"] = m_environment.weatherEnabled;
    envObj["weatherPreset"] = m_environment.weatherPreset;
    envObj["weatherTransition"] = m_environment.weatherTransition;
    envObj["rainAmount"] = m_environment.rainAmount;
    envObj["snowAmount"] = m_environment.snowAmount;
    envObj["wetness"] = m_environment.wetness;
    envObj["snowCover"] = m_environment.snowCover;
    envObj["lightningRate"] = m_environment.lightningRate;
    envObj["snowDepth"] = m_environment.snowDepth;
    envObj["thunderVolume"] = m_environment.thunderVolume;
    envObj["enableGI"] = m_environment.enableGI;
    JsonValue giSky = JsonValue::Array();
    giSky.Push(m_environment.giSkyColor.r); giSky.Push(m_environment.giSkyColor.g); giSky.Push(m_environment.giSkyColor.b);
    envObj["giSkyColor"] = giSky;
    envObj["giIntensity"] = m_environment.giIntensity;
    JsonValue fogCol = JsonValue::Array();
    fogCol.Push(m_environment.fogColor.r); fogCol.Push(m_environment.fogColor.g); fogCol.Push(m_environment.fogColor.b);
    envObj["fogColor"] = fogCol;
    envObj["fogDensity"] = m_environment.fogDensity;
    envObj["bloomIntensity"] = m_environment.bloomIntensity;
    envObj["ssaoIntensity"] = m_environment.ssaoIntensity;
    envObj["ssaoRadius"] = m_environment.ssaoRadius;
    envObj["ssrIntensity"] = m_environment.ssrIntensity;
    envObj["volumetricScattering"] = m_environment.volumetricScattering;
    envObj["useHDRI"] = m_environment.useHDRI;
    envObj["hdriPath"] = m_environment.hdriPath;
    envObj["hdriIntensity"] = m_environment.hdriIntensity;
    envObj["hdriRotation"] = m_environment.hdriRotation;
    envObj["autoExposure"] = m_environment.autoExposure;
    envObj["autoExposureMin"] = m_environment.autoExposureMin;
    envObj["autoExposureMax"] = m_environment.autoExposureMax;
    envObj["autoExposureSpeed"] = m_environment.autoExposureSpeed;
    envObj["contrast"] = m_environment.contrast;
    envObj["saturation"] = m_environment.saturation;
    envObj["temperature"] = m_environment.temperature;
    envObj["tint"] = m_environment.tint;
    envObj["vignette"] = m_environment.vignette;
    envObj["filmGrain"] = m_environment.filmGrain;
    envObj["sharpen"] = m_environment.sharpen;
    envObj["lensFlare"] = m_environment.lensFlare;
    envObj["depthOfField"] = m_environment.depthOfField;
    envObj["focusDistance"] = m_environment.focusDistance;
    envObj["aperture"] = m_environment.aperture;
    envObj["dofMaxBlur"] = m_environment.dofMaxBlur;
    envObj["motionBlur"] = m_environment.motionBlur;
    envObj["chromaticAberration"] = m_environment.chromaticAberration;
    envObj["colorLutPath"] = m_environment.colorLutPath;
    envObj["lutStrength"] = m_environment.lutStrength;
    envObj["ssrMaxDistance"] = m_environment.ssrMaxDistance;
    envObj["ssrThickness"] = m_environment.ssrThickness;
    envObj["volumetricFogDensity"] = m_environment.volumetricFogDensity;
    envObj["volumetricHeightFalloff"] = m_environment.volumetricHeightFalloff;
    envObj["volumetricBaseHeight"] = m_environment.volumetricBaseHeight;
    envObj["volumetricAnisotropy"] = m_environment.volumetricAnisotropy;
    envObj["occlusionCulling"] = m_environment.occlusionCulling;
    envObj["bakedGIEnabled"] = m_environment.bakedGIEnabled;
    envObj["bakedGIIntensity"] = m_environment.bakedGIIntensity;
    envObj["bakedGIBounces"] = m_environment.bakedGIBounces;
    envObj["bakedGISpacing"] = m_environment.bakedGISpacing;
    envObj["bakedGIPath"] = m_environment.bakedGIPath;
    envObj["bakeLightmaps"] = m_environment.bakeLightmaps;
    envObj["lightmapDensity"] = m_environment.lightmapDensity;
    envObj["lightmapSamples"] = m_environment.lightmapSamples;
    envObj["lightmapPath"] = m_environment.lightmapPath;
    envObj["vxgiEnabled"] = m_environment.vxgiEnabled;
    envObj["vxgiIntensity"] = m_environment.vxgiIntensity;
    envObj["vxgiRange"] = m_environment.vxgiRange;
    envObj["vxgiBounce"] = m_environment.vxgiBounce;
    sceneObj["environment"] = envObj;

    JsonValue nodesArr = JsonValue::Array();
    for (const auto& node : m_nodes) {
        // Nodes streamed in from world sectors belong to their sector files, not this scene
        if (!node->streamSector.empty()) continue;
        JsonValue n = JsonValue::Object();
        n["id"] = node->id;
        n["parentId"] = node->parentId;
        n["name"] = node->name;
        n["nodeType"] = NodeTypeToString(node->nodeType);
        n["shapeType"] = ShapeTypeToString(node->type);
        n["customMeshName"] = node->customMeshName;

        // Children IDs
        JsonValue childrenArr = JsonValue::Array();
        for (uint32_t cid : node->childrenIds) {
            childrenArr.Push(cid);
        }
        n["children"] = childrenArr;

        // Transform
        JsonValue tf = JsonValue::Object();
        JsonValue pos = JsonValue::Array();
        pos.Push(node->position.x); pos.Push(node->position.y); pos.Push(node->position.z);
        tf["position"] = pos;

        JsonValue rot = JsonValue::Array();
        rot.Push(node->rotation.x); rot.Push(node->rotation.y); rot.Push(node->rotation.z);
        tf["rotation"] = rot;

        JsonValue sc = JsonValue::Array();
        sc.Push(node->scale.x); sc.Push(node->scale.y); sc.Push(node->scale.z);
        tf["scale"] = sc;
        n["transform"] = tf;

        // Color & Material
        JsonValue col = JsonValue::Array();
        col.Push(node->color.r); col.Push(node->color.g); col.Push(node->color.b); col.Push(node->color.a);
        n["color"] = col;
        n["texturePath"] = node->texturePath;
        n["normalMapPath"] = node->normalMapPath;
        n["metallicMapPath"] = node->metallicMapPath;
        n["roughnessMapPath"] = node->roughnessMapPath;
        n["aoMapPath"] = node->aoMapPath;
        n["emissiveMapPath"] = node->emissiveMapPath;
        n["heightMapPath"] = node->heightMapPath;
        n["shaderGraphPath"] = node->shaderGraphPath;
        n["materialName"] = node->materialName;
        n["roughness"] = node->roughness;
        n["metallic"] = node->metallic;
        n["aoStrength"] = node->aoStrength;
        JsonValue emCol = JsonValue::Array();
        emCol.Push(node->emissiveColor.r); emCol.Push(node->emissiveColor.g); emCol.Push(node->emissiveColor.b);
        n["emissiveColor"] = emCol;
        n["emissiveIntensity"] = node->emissiveIntensity;
        n["alphaCutoff"] = node->alphaCutoff;
        n["doubleSided"] = node->doubleSided;
        n["castShadows"] = node->castShadows;
        n["refractionIOR"] = node->refractionIOR;
        n["isMirror"] = node->isMirror;
        if (node->isVideoScreen || node->nodeType == NodeType::UI_Video) {
            JsonValue vs = JsonValue::Object();
            vs["enabled"] = node->isVideoScreen;
            vs["channelPreset"] = node->videoChannelPreset;
            vs["sourcePath"] = node->videoSourcePath;
            vs["playing"] = node->videoPlaying;
            vs["loop"] = node->videoLoop;
            vs["speed"] = node->videoSpeed;
            vs["currentTime"] = node->videoCurrentTime;
            vs["duration"] = node->videoDuration;
            vs["fps"] = node->videoFps;
            vs["atlasCols"] = node->videoAtlasCols;
            vs["atlasRows"] = node->videoAtlasRows;
            vs["brightness"] = node->videoScreenBrightness;
            vs["scanlines"] = node->videoScanlineStrength;
            vs["audioPath"] = node->videoAudioPath;
            vs["audioVolume"] = node->videoAudioVolume;
            vs["spatialAudio"] = node->videoSpatialAudio;
            n["videoScreen"] = vs;
        }
        n["materialModel"] = node->materialModel;
        auto vec3Json = [](const glm::vec3& v) { JsonValue a = JsonValue::Array(); a.Push(v.x); a.Push(v.y); a.Push(v.z); return a; };
        n["subsurfaceColor"] = vec3Json(node->subsurfaceColor);
        n["subsurfaceStrength"] = node->subsurfaceStrength;
        n["subsurfaceTranslucency"] = node->subsurfaceTranslucency;
        n["hairHighlightTint"] = vec3Json(node->hairHighlightTint);
        n["hairHighlightShift"] = node->hairHighlightShift;
        n["irisRadius"] = node->irisRadius;
        n["irisDepth"] = node->irisDepth;
        n["corneaIOR"] = node->corneaIOR;
        n["clearCoat"] = node->clearCoat;
        n["clearCoatRoughness"] = node->clearCoatRoughness;
        n["triplanarMapping"] = node->triplanarMapping;
        n["triplanarTileSize"] = node->triplanarTileSize;
        n["triplanarSharpness"] = node->triplanarSharpness;
        n["detailAlbedoPath"] = node->detailAlbedoPath;
        n["detailNormalPath"] = node->detailNormalPath;
        n["detailTiling"] = node->detailTiling;
        n["detailStrength"] = node->detailStrength;
        n["useVertexColors"] = node->useVertexColors;
        n["lightmapStatic"] = node->lightmapStatic;
        n["isReflectionProbe"] = node->isReflectionProbe;
        if (node->isCloth) {
            JsonValue cloth = JsonValue::Object();
            cloth["resX"] = static_cast<int>(node->clothResX);
            cloth["resY"] = static_cast<int>(node->clothResY);
            cloth["width"] = node->clothWidth;
            cloth["height"] = node->clothHeight;
            cloth["mass"] = node->clothMass;
            cloth["compliance"] = node->clothCompliance;
            cloth["pinTop"] = node->clothPinTop;
            JsonValue wind = JsonValue::Array();
            wind.Push(node->clothWind.x); wind.Push(node->clothWind.y); wind.Push(node->clothWind.z);
            cloth["wind"] = wind;
            n["cloth"] = cloth;
        }
        if (node->isLiquid) {
            JsonValue liquid = JsonValue::Object();
            liquid["fill"] = node->liquidFill;
            JsonValue top = JsonValue::Array();
            top.Push(node->liquidTopColor.r); top.Push(node->liquidTopColor.g); top.Push(node->liquidTopColor.b); top.Push(node->liquidTopColor.a);
            liquid["topColor"] = top;
            liquid["slosh"] = node->liquidSlosh;
            liquid["colorDepth"] = node->liquidColorDepth;
            n["liquid"] = liquid;
        }
        if (node->isFluidVolume) {
            JsonValue fluid = JsonValue::Object();
            JsonValue size = JsonValue::Array();
            size.Push(node->fluidSize.x); size.Push(node->fluidSize.y); size.Push(node->fluidSize.z);
            fluid["size"] = size;
            fluid["fill"] = node->fluidFill;
            fluid["start"] = node->fluidStart;
            fluid["spacing"] = node->fluidSpacing;
            fluid["viscosity"] = node->fluidViscosity;
            JsonValue color = JsonValue::Array();
            color.Push(node->fluidColor.r); color.Push(node->fluidColor.g); color.Push(node->fluidColor.b); color.Push(node->fluidColor.a);
            fluid["color"] = color;
            fluid["colorDepth"] = node->fluidColorDepth;
            n["fluidVolume"] = fluid;
        }
        if (node->isPourSpout) {
            JsonValue pour = JsonValue::Object();
            pour["flow"] = node->pourFlow;
            pour["startAngle"] = node->pourStartAngle;
            pour["streamRadius"] = node->pourStreamRadius;
            pour["speed"] = node->pourSpeed;
            JsonValue color = JsonValue::Array();
            color.Push(node->pourColor.r); color.Push(node->pourColor.g); color.Push(node->pourColor.b); color.Push(node->pourColor.a);
            pour["color"] = color;
            pour["sourceId"] = static_cast<double>(node->pourSourceId);
            n["pourSpout"] = pour;
        }
        if (node->isOcean) {
            JsonValue ocean = JsonValue::Object();
            JsonValue size = JsonValue::Array();
            size.Push(node->oceanSize.x); size.Push(node->oceanSize.y);
            ocean["size"] = size;
            ocean["waveHeight"] = node->waveHeight;
            ocean["waveLength"] = node->waveLength;
            ocean["waveDirection"] = node->waveDirection;
            ocean["choppiness"] = node->waveChoppiness;
            ocean["waveSpeed"] = node->waveSpeed;
            JsonValue shallow = JsonValue::Array();
            shallow.Push(node->waterShallowColor.r); shallow.Push(node->waterShallowColor.g); shallow.Push(node->waterShallowColor.b);
            ocean["shallowColor"] = shallow;
            JsonValue deep = JsonValue::Array();
            deep.Push(node->waterDeepColor.r); deep.Push(node->waterDeepColor.g); deep.Push(node->waterDeepColor.b);
            ocean["deepColor"] = deep;
            ocean["clarity"] = node->waterClarity;
            ocean["roughness"] = node->waterRoughness;
            ocean["whitecaps"] = node->whitecaps;
            ocean["shoreFoamWidth"] = node->shoreFoamWidth;
            ocean["soundVolume"] = node->oceanSoundVolume;
            n["ocean"] = ocean;
        }
        n["probeRadius"] = node->probeRadius;
        n["probeBoxProjection"] = node->probeBoxProjection;
        n["isGIVolume"] = node->isGIVolume;
        {
            JsonValue giSize = JsonValue::Array();
            giSize.Push(node->giVolumeSize.x); giSize.Push(node->giVolumeSize.y); giSize.Push(node->giVolumeSize.z);
            n["giVolumeSize"] = giSize;
        }
        JsonValue probeBox = JsonValue::Array();
        probeBox.Push(node->probeBoxSize.x); probeBox.Push(node->probeBoxSize.y); probeBox.Push(node->probeBoxSize.z);
        n["probeBoxSize"] = probeBox;
        n["heightScale"] = node->heightScale;
        n["scriptPath"] = node->scriptPath;
        n["hasSkeleton"] = node->hasSkeleton;
        n["skeletonName"] = node->skeletonName;
        n["currentAnimState"] = node->currentAnimState;
        n["playAnimation"] = node->playAnimation;
        n["animationSpeed"] = node->animationSpeed;
        n["animationStartOffset"] = node->animationStartOffset;
        n["boneCount"] = (int)node->boneNames.size();
        JsonValue bList = JsonValue::Array();
        for (const auto& b : node->boneNames) bList.Push(b);
        n["boneNames"] = bList;
        JsonValue cList = JsonValue::Array();
        for (const auto& c : node->animClipNames) cList.Push(c);
        n["animClipNames"] = cList;

        // IK & Morph Targets
        n["enableIK"] = node->enableIK;
        n["ikTargetBone"] = node->ikTargetBone;
        JsonValue ikTargetPos = JsonValue::Array();
        ikTargetPos.Push(node->ikTargetPosition.x); ikTargetPos.Push(node->ikTargetPosition.y); ikTargetPos.Push(node->ikTargetPosition.z);
        n["ikTargetPosition"] = ikTargetPos;
        JsonValue ikPole = JsonValue::Array();
        ikPole.Push(node->ikPoleVector.x); ikPole.Push(node->ikPoleVector.y); ikPole.Push(node->ikPoleVector.z);
        n["ikPoleVector"] = ikPole;
        n["ikWeight"] = node->ikWeight;
        if (!node->morphWeights.empty()) {
            JsonValue morphObj = JsonValue::Object();
            for (const auto& kv : node->morphWeights) {
                morphObj[kv.first] = kv.second;
            }
            n["morphWeights"] = morphObj;
        }

        // RigidBody Physics
        if (true) {
            JsonValue rb = JsonValue::Object();
            rb["type"] = RigidBodyTypeToString(node->rigidBodyType);
            rb["mass"] = node->mass;
            rb["useGravity"] = node->useGravity;
            rb["floatsOnWater"] = node->floatsOnWater;
            rb["buoyancy"] = node->buoyancy;
            rb["linearDamping"] = node->linearDamping;
            rb["angularDamping"] = node->angularDamping;
            rb["friction"] = node->friction;
            rb["bounciness"] = node->bounciness;
            rb["constraintType"] = node->constraintType;
            rb["constraintTargetNodeId"] = node->constraintTargetNodeId;
            rb["enableRagdoll"] = node->enableRagdoll;
            rb["collisionLayer"] = static_cast<double>(node->collisionLayer);
            rb["collisionMask"] = static_cast<double>(node->collisionMask);
            JsonValue lv = JsonValue::Array();
            lv.Push(node->linearVelocity.x); lv.Push(node->linearVelocity.y); lv.Push(node->linearVelocity.z);
            rb["linearVelocity"] = lv;
            JsonValue av = JsonValue::Array();
            av.Push(node->angularVelocity.x); av.Push(node->angularVelocity.y); av.Push(node->angularVelocity.z);
            rb["angularVelocity"] = av;
            n["rigidBody"] = rb;
        }

        // Nested Scene Instancing
        n["instanceScenePath"] = node->instanceScenePath;
        n["isSceneInstanceRoot"] = node->isSceneInstanceRoot;

        // Collision filtering bitfields
        n["collisionLayer"] = static_cast<double>(node->collisionLayer);
        n["collisionMask"] = static_cast<double>(node->collisionMask);

        // Networking
        n["networkId"] = node->networkId;
        n["multiplayerAuthority"] = node->multiplayerAuthority;
        n["replicateTransform"] = node->replicateTransform;

        // Declarative Multiplayer Property Sync
        if (node->replicationConfig.enabled || !node->replicationConfig.rules.empty()) {
            JsonValue repObj = JsonValue::Object();
            repObj["enabled"] = node->replicationConfig.enabled;
            JsonValue rulesArr = JsonValue::Array();
            for (const auto& r : node->replicationConfig.rules) {
                JsonValue rObj = JsonValue::Object();
                rObj["property"] = r.propertyName;
                rObj["mode"] = ReplicationModeToString(r.mode);
                rObj["hz"] = r.syncRateHz;
                rulesArr.Push(rObj);
            }
            repObj["rules"] = rulesArr;
            n["replicationConfig"] = repObj;
        }

        // LOD Configuration
        n["enableLOD"] = node->enableLOD;
        n["maxDrawDistance"] = node->maxDrawDistance;
        n["forcedLOD"] = node->forcedLOD;
        JsonValue lodArr = JsonValue::Array();
        for (const auto& lod : node->lodLevels) {
            JsonValue lObj = JsonValue::Object();
            lObj["meshKey"] = lod.meshKey;
            lObj["distance"] = lod.distance;
            lObj["reductionRatio"] = lod.reductionRatio;
            lObj["triangleCount"] = static_cast<double>(lod.triangleCount);
            lodArr.Push(lObj);
        }
        n["lodLevels"] = lodArr;

        // Components
        if (node->nodeType == NodeType::Light) {
            JsonValue l = JsonValue::Object();
            l["type"] = LightTypeToString(node->lightType);
            JsonValue lc = JsonValue::Array();
            lc.Push(node->lightColor.r); lc.Push(node->lightColor.g); lc.Push(node->lightColor.b);
            l["color"] = lc;
            l["intensity"] = node->lightIntensity;
            l["range"] = node->lightRange;
            l["innerCone"] = node->spotInnerConeAngle;
            l["outerCone"] = node->spotOuterConeAngle;
            l["castShadows"] = node->castShadows;
            l["shadowBias"] = node->shadowBias;
            JsonValue areaSz = JsonValue::Array();
            areaSz.Push(node->areaLightSize.x); areaSz.Push(node->areaLightSize.y);
            l["areaSize"] = areaSz;
            l["twoSided"] = node->areaLightTwoSided;
            n["light"] = l;
        } else if (node->nodeType == NodeType::Camera) {
            JsonValue c = JsonValue::Object();
            c["fov"] = node->cameraFov;
            c["near"] = node->cameraNear;
            c["far"] = node->cameraFar;
            n["camera"] = c;
        } else if (node->nodeType == NodeType::Collider) {
            JsonValue colObj = JsonValue::Object();
            colObj["type"] = ColliderTypeToString(node->colliderType);
            JsonValue sz = JsonValue::Array();
            sz.Push(node->colliderSize.x); sz.Push(node->colliderSize.y); sz.Push(node->colliderSize.z);
            colObj["size"] = sz;
            colObj["radius"] = node->colliderRadius;
            colObj["height"] = node->colliderHeight;
            JsonValue cnt = JsonValue::Array();
            cnt.Push(node->colliderCenter.x); cnt.Push(node->colliderCenter.y); cnt.Push(node->colliderCenter.z);
            colObj["center"] = cnt;
            colObj["isTrigger"] = node->isTrigger;
            colObj["bounciness"] = node->bounciness;
            colObj["friction"] = node->friction;
            n["collider"] = colObj;
        } else if (node->nodeType == NodeType::Character) {
            auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
            if (charNode) {
                JsonValue charObj = JsonValue::Object();
                charObj["name"] = charNode->characterName;
                charObj["dialogue"] = charNode->dialogueText;
                charObj["radius"] = charNode->interactionRadius;
                charObj["moveSpeed"] = charNode->moveSpeed;
                charObj["turnSpeed"] = charNode->turnSpeed;
                charObj["tolerance"] = charNode->waypointTolerance;
                charObj["isPatrolling"] = charNode->isPatrolling;
                charObj["loopWaypoints"] = charNode->loopWaypoints;
                charObj["idleAnim"] = charNode->idleAnimClip;
                charObj["walkAnim"] = charNode->walkAnimClip;
                charObj["groundClamping"] = charNode->applyGroundClamping;
                charObj["groundOffset"] = charNode->groundOffset;
                charObj["clampToWorldFloor"] = charNode->clampToWorldFloor;

                JsonValue wpArr = JsonValue::Array();
                for (const auto& wp : charNode->waypoints) {
                    JsonValue p = JsonValue::Array();
                    p.Push(wp.x); p.Push(wp.y); p.Push(wp.z);
                    wpArr.Push(p);
                }
                charObj["waypoints"] = wpArr;
                n["character"] = charObj;
            }
        } else if (node->nodeType == NodeType::Spline) {
            auto splineNode = std::dynamic_pointer_cast<SplineNode>(node);
            if (splineNode) {
                JsonValue spObj = JsonValue::Object();
                spObj["mode"] = SplineModeToString(splineNode->splineMode);
                spObj["closedLoop"] = splineNode->closedLoop;
                spObj["tension"] = splineNode->tension;
                spObj["subdivisions"] = splineNode->subdivisions;
                JsonValue ptsArr = JsonValue::Array();
                for (const auto& pt : splineNode->controlPoints) {
                    JsonValue p = JsonValue::Array();
                    p.Push(pt.x); p.Push(pt.y); p.Push(pt.z);
                    ptsArr.Push(p);
                }
                spObj["controlPoints"] = ptsArr;
                n["spline"] = spObj;
            }
        } else if (node->nodeType == NodeType::Decal) {
            auto decalNode = std::dynamic_pointer_cast<DecalNode>(node);
            if (decalNode) {
                JsonValue decObj = JsonValue::Object();
                JsonValue sz = JsonValue::Array();
                sz.Push(decalNode->decalSize.x); sz.Push(decalNode->decalSize.y); sz.Push(decalNode->decalSize.z);
                decObj["size"] = sz;
                decObj["opacity"] = decalNode->opacity;
                decObj["fadeDistance"] = decalNode->fadeDistance;
                decObj["normalAngleThreshold"] = decalNode->normalAngleThreshold;
                decObj["blendMode"] = static_cast<int>(decalNode->blendMode);
                decObj["sortOrder"] = decalNode->sortOrder;
                n["decal"] = decObj;
            }
        } else if (node->nodeType == NodeType::ParticleEmitter) {
            auto partNode = std::dynamic_pointer_cast<ParticleNode>(node);
            if (partNode) {
                JsonValue pObj = JsonValue::Object();
                pObj["simulationMode"] = static_cast<int>(partNode->simulationMode);
                pObj["emitterShape"] = static_cast<int>(partNode->emitterShape);
                pObj["maxParticles"] = partNode->maxParticles;
                pObj["spawnRate"] = partNode->spawnRate;
                pObj["lifetimeMin"] = partNode->lifetimeMin;
                pObj["lifetimeMax"] = partNode->lifetimeMax;
                pObj["startSpeed"] = partNode->startSpeed;
                pObj["startSize"] = partNode->startSize;
                pObj["endSize"] = partNode->endSize;
                JsonValue scArr = JsonValue::Array();
                scArr.Push(partNode->startColor.r); scArr.Push(partNode->startColor.g); scArr.Push(partNode->startColor.b); scArr.Push(partNode->startColor.a);
                pObj["startColor"] = scArr;
                JsonValue ecArr = JsonValue::Array();
                ecArr.Push(partNode->endColor.r); ecArr.Push(partNode->endColor.g); ecArr.Push(partNode->endColor.b); ecArr.Push(partNode->endColor.a);
                pObj["endColor"] = ecArr;
                pObj["gravityMultiplier"] = partNode->gravityMultiplier;
                pObj["emitterRadius"] = partNode->emitterRadius;
                pObj["coneAngle"] = partNode->coneAngle;
                pObj["isEmitting"] = partNode->isEmitting;
                pObj["looping"] = partNode->looping;
                pObj["worldSpace"] = partNode->worldSpace;
                pObj["enableVortex"] = partNode->enableVortexModule;
                pObj["vortexStrength"] = partNode->vortexStrength;
                pObj["enableAttractor"] = partNode->enableAttractorModule;
                pObj["attractorStrength"] = partNode->attractorStrength;
                pObj["enableCurlNoise"] = partNode->enableCurlNoiseModule;
                pObj["curlNoiseStrength"] = partNode->curlNoiseStrength;
                pObj["curlNoiseFrequency"] = partNode->curlNoiseFrequency;
                pObj["renderMode"] = static_cast<int>(partNode->renderMode);
                JsonValue boxExtArr = JsonValue::Array();
                boxExtArr.Push(partNode->emitterBoxExtents.x); boxExtArr.Push(partNode->emitterBoxExtents.y); boxExtArr.Push(partNode->emitterBoxExtents.z);
                pObj["emitterBoxExtents"] = boxExtArr;
                JsonValue vortexArr = JsonValue::Array();
                vortexArr.Push(partNode->vortexAxis.x); vortexArr.Push(partNode->vortexAxis.y); vortexArr.Push(partNode->vortexAxis.z);
                pObj["vortexAxis"] = vortexArr;
                JsonValue attractArr = JsonValue::Array();
                attractArr.Push(partNode->attractorOffset.x); attractArr.Push(partNode->attractorOffset.y); attractArr.Push(partNode->attractorOffset.z);
                pObj["attractorOffset"] = attractArr;
                pObj["drag"] = partNode->drag;
                pObj["startRotationMin"] = partNode->startRotationMin;
                pObj["startRotationMax"] = partNode->startRotationMax;
                pObj["angularVelocityMin"] = partNode->angularVelocityMin;
                pObj["angularVelocityMax"] = partNode->angularVelocityMax;
                pObj["builtinSheet"] = static_cast<int>(partNode->builtinSheet);
                pObj["flipbookColumns"] = partNode->flipbookColumns;
                pObj["flipbookRows"] = partNode->flipbookRows;
                pObj["flipbookFrames"] = partNode->flipbookFrames;
                pObj["flipbookMode"] = static_cast<int>(partNode->flipbookMode);
                pObj["flipbookFps"] = partNode->flipbookFps;
                pObj["flipbookCycles"] = partNode->flipbookCycles;
                pObj["flipbookRandomStart"] = partNode->flipbookRandomStart;
                pObj["flipbookBlend"] = partNode->flipbookBlend;
                pObj["softness"] = partNode->softness;
                pObj["cameraFade"] = partNode->cameraFade;
                pObj["lit"] = partNode->lit;
                pObj["glowIntensity"] = partNode->glowIntensity;
                pObj["selfGlow"] = partNode->selfGlow;
                pObj["velocityStretch"] = partNode->velocityStretch;
                pObj["distortionStrength"] = partNode->distortionStrength;
                pObj["trailMode"] = static_cast<int>(partNode->trailMode);
                pObj["trailLength"] = partNode->trailLength;
                pObj["trailInterval"] = partNode->trailInterval;
                pObj["trailWidth"] = partNode->trailWidth;
                pObj["trailTaper"] = partNode->trailTaper;
                pObj["trailDrawParticles"] = partNode->trailDrawParticles;
                pObj["trailTextureTiling"] = partNode->trailTextureTiling;
                pObj["collisionMode"] = static_cast<int>(partNode->collisionMode);
                pObj["collisionPlaneHeight"] = partNode->collisionPlaneHeight;
                pObj["collideWithWater"] = partNode->collideWithWater;
                pObj["collisionRadiusScale"] = partNode->collisionRadiusScale;
                pObj["bounce"] = partNode->bounce;
                pObj["friction"] = partNode->friction;
                pObj["killOnCollision"] = partNode->killOnCollision;
                pObj["lifetimeLossOnHit"] = partNode->lifetimeLossOnHit;
                pObj["hitSpawnCount"] = partNode->hitSpawnCount;
                pObj["hitSpawnSpeed"] = partNode->hitSpawnSpeed;
                pObj["hitSpawnSize"] = partNode->hitSpawnSize;
                n["particle"] = pObj;
            }
        } else if (node->nodeType == NodeType::AudioSource) {
            auto audioNode = std::dynamic_pointer_cast<AudioNode>(node);
            if (audioNode) {
                JsonValue aObj = JsonValue::Object();
                aObj["clipPath"] = audioNode->audioClipPath;
                aObj["busName"] = audioNode->busName;
                aObj["is3DSpatial"] = audioNode->is3DSpatial;
                aObj["volume"] = audioNode->volume;
                aObj["pitch"] = audioNode->pitch;
                aObj["loop"] = audioNode->loop;
                aObj["playOnAwake"] = audioNode->playOnAwake;
                aObj["attenuationModel"] = static_cast<int>(audioNode->attenuationModel);
                aObj["minDistance"] = audioNode->minDistance;
                aObj["maxDistance"] = audioNode->maxDistance;
                aObj["rolloffFactor"] = audioNode->rolloffFactor;
                aObj["isReverbZone"] = audioNode->isReverbZone;
                aObj["reverbInnerRadius"] = audioNode->reverbInnerRadius;
                aObj["reverbOuterRadius"] = audioNode->reverbOuterRadius;
                aObj["reverbRoomSize"] = audioNode->reverbRoomSize;
                aObj["reverbDecayTime"] = audioNode->reverbDecayTime;
                aObj["reverbDamping"] = audioNode->reverbDamping;
                aObj["reverbWetMix"] = audioNode->reverbWetMix;
                n["audio"] = aObj;
            }
        } else if (node->nodeType == NodeType::Terrain) {
            auto tNode = std::dynamic_pointer_cast<TerrainNode>(node);
            if (tNode) {
                JsonValue tObj = JsonValue::Object();
                tObj["gridResolution"] = tNode->gridResolution;
                tObj["sizeX"] = tNode->terrainSize.x;
                tObj["sizeZ"] = tNode->terrainSize.y;
                tObj["heightScale"] = tNode->heightScale;
                tObj["uvTiling"] = tNode->uvTiling;
                tObj["heightmapPath"] = tNode->heightmapPath;
                JsonValue hArr = JsonValue::Array();
                for (float h : tNode->heightData) {
                    hArr.Push(h);
                }
                tObj["heightData"] = hArr;
                // Paint layers and the splat map (RGBA weights, base64)
                JsonValue layersArr = JsonValue::Array();
                for (const TerrainLayer& layer : tNode->layers) {
                    JsonValue l = JsonValue::Object();
                    l["name"] = layer.name;
                    l["texture"] = layer.texturePath;
                    JsonValue tint = JsonValue::Array();
                    tint.Push(layer.tint.r); tint.Push(layer.tint.g); tint.Push(layer.tint.b);
                    l["tint"] = tint;
                    l["tiling"] = layer.tiling;
                    l["roughness"] = layer.roughness;
                    layersArr.Push(l);
                }
                tObj["layers"] = layersArr;
                tObj["splatResolution"] = tNode->splatResolution;
                tObj["splat"] = TerrainBase64Encode(tNode->splatData);
                // Foliage types and instances (x, z, rotation, scale, type for each)
                JsonValue typesArr = JsonValue::Array();
                for (const FoliageType& type : tNode->foliageTypes) {
                    JsonValue t = JsonValue::Object();
                    t["name"] = type.name;
                    t["builtIn"] = type.builtIn;
                    t["model"] = type.modelMesh;
                    JsonValue c = JsonValue::Array();
                    c.Push(type.color.r); c.Push(type.color.g); c.Push(type.color.b); c.Push(type.color.a);
                    t["color"] = c;
                    t["minScale"] = type.minScale;
                    t["maxScale"] = type.maxScale;
                    t["density"] = type.density;
                    t["maxSlope"] = type.maxSlope;
                    t["alignToSlope"] = type.alignToSlope;
                    t["castShadows"] = type.castShadows;
                    t["wind"] = type.wind;
                    t["collision"] = type.collision;
                    t["drawDistance"] = type.drawDistance;
                    typesArr.Push(t);
                }
                tObj["foliageTypes"] = typesArr;
                JsonValue foliageArr = JsonValue::Array();
                for (const FoliageInstance& f : tNode->foliage) {
                    foliageArr.Push(f.local.x); foliageArr.Push(f.local.y); foliageArr.Push(f.rotation);
                    foliageArr.Push(f.scale); foliageArr.Push(static_cast<double>(f.type));
                }
                tObj["foliage"] = foliageArr;
                n["terrain"] = tObj;
            }
        } else if (node->IsUINode()) {
            JsonValue uiObj = JsonValue::Object();
            JsonValue uipos = JsonValue::Array();
            uipos.Push(node->uiPosition.x); uipos.Push(node->uiPosition.y);
            uiObj["position"] = uipos;
            JsonValue uisz = JsonValue::Array();
            uisz.Push(node->uiSize.x); uisz.Push(node->uiSize.y);
            uiObj["size"] = uisz;
            uiObj["anchor"] = UIAnchorToString(node->uiAnchor);
            uiObj["text"] = node->uiText;
            if (!node->uiLocKey.empty()) uiObj["locKey"] = node->uiLocKey;
            uiObj["placeholder"] = node->uiPlaceholder;
            uiObj["fontSize"] = node->uiFontSize;
            uiObj["fontPath"] = node->uiFontPath;
            JsonValue uic = JsonValue::Array();
            uic.Push(node->uiColor.r); uic.Push(node->uiColor.g); uic.Push(node->uiColor.b); uic.Push(node->uiColor.a);
            uiObj["color"] = uic;
            JsonValue uitc = JsonValue::Array();
            uitc.Push(node->uiTextColor.r); uitc.Push(node->uiTextColor.g); uitc.Push(node->uiTextColor.b); uitc.Push(node->uiTextColor.a);
            uiObj["textColor"] = uitc;
            JsonValue uihc = JsonValue::Array();
            uihc.Push(node->uiHoverColor.r); uihc.Push(node->uiHoverColor.g); uihc.Push(node->uiHoverColor.b); uihc.Push(node->uiHoverColor.a);
            uiObj["hoverColor"] = uihc;
            JsonValue uipc = JsonValue::Array();
            uipc.Push(node->uiPressColor.r); uipc.Push(node->uiPressColor.g); uipc.Push(node->uiPressColor.b); uipc.Push(node->uiPressColor.a);
            uiObj["pressColor"] = uipc;
            JsonValue uibc = JsonValue::Array();
            uibc.Push(node->uiBorderColor.r); uibc.Push(node->uiBorderColor.g); uibc.Push(node->uiBorderColor.b); uibc.Push(node->uiBorderColor.a);
            uiObj["borderColor"] = uibc;
            uiObj["borderThickness"] = node->uiBorderThickness;
            uiObj["rounding"] = node->uiRounding;
            uiObj["progress"] = node->uiProgress;
            uiObj["value"] = node->uiValue;
            uiObj["minVal"] = node->uiMinVal;
            uiObj["maxVal"] = node->uiMaxVal;
            uiObj["checked"] = node->uiChecked;
            uiObj["radioGroup"] = node->uiRadioGroup;
            uiObj["selectedIndex"] = node->uiSelectedIndex;
            JsonValue itemsArr = JsonValue::Array();
            for (const auto& item : node->uiItems) itemsArr.Push(item);
            uiObj["items"] = itemsArr;
            uiObj["windowMovable"] = node->uiWindowMovable;
            uiObj["windowClosable"] = node->uiWindowClosable;
            uiObj["windowIsOpen"] = node->uiWindowIsOpen;
            uiObj["shapeType"] = UIShapeTypeToString(node->uiShapeType);
            uiObj["visible"] = node->uiVisible;
            uiObj["enabled"] = node->uiEnabled;
            uiObj["action"] = node->uiOnClickAction;

            if (node->nodeType == NodeType::UI_BoxContainer) {
                auto boxNode = std::dynamic_pointer_cast<UIBoxContainerNode>(node);
                if (boxNode) {
                    uiObj["boxOrientation"] = BoxOrientationToString(boxNode->orientation);
                    uiObj["spacing"] = boxNode->spacing;
                    uiObj["padding"] = boxNode->padding;
                    uiObj["autoResize"] = boxNode->autoResize;
                }
            } else if (node->nodeType == NodeType::UI_GridContainer) {
                auto gridNode = std::dynamic_pointer_cast<UIGridContainerNode>(node);
                if (gridNode) {
                    uiObj["gridColumns"] = gridNode->columns;
                    uiObj["cellWidth"] = gridNode->cellWidth;
                    uiObj["cellHeight"] = gridNode->cellHeight;
                    uiObj["horizontalSpacing"] = gridNode->horizontalSpacing;
                    uiObj["verticalSpacing"] = gridNode->verticalSpacing;
                    uiObj["padding"] = gridNode->padding;
                    uiObj["autoResize"] = gridNode->autoResize;
                }
            }

            n["ui"] = uiObj;
        }

        if (node->type == ShapeType::Custom) {
            JsonValue bMin = JsonValue::Array();
            bMin.Push(node->boundsMin.x); bMin.Push(node->boundsMin.y); bMin.Push(node->boundsMin.z);
            n["boundsMin"] = bMin;

            JsonValue bMax = JsonValue::Array();
            bMax.Push(node->boundsMax.x); bMax.Push(node->boundsMax.y); bMax.Push(node->boundsMax.z);
            n["boundsMax"] = bMax;

            n["vertexCount"] = node->vertexCount;
            n["triangleCount"] = node->triangleCount;
        }

        nodesArr.Push(n);
    }
    sceneObj["nodes"] = nodesArr;
    if (!m_cutscenes.empty()) {
        JsonValue cutscenesArr = JsonValue::Array();
        for (const auto& cs : m_cutscenes) {
            cutscenesArr.Push(cs.Serialize());
        }
        sceneObj["cutscenes"] = cutscenesArr;
    }
    return sceneObj;
}

bool Scene::Deserialize(const JsonValue& json, std::function<bool(const std::string&, const std::string&)> loadMeshCallback) {
    if (!json.IsObject() || !json.HasKey("nodes")) return false;

    Clear();
    m_name = json.HasKey("name") ? json["name"].AsString("Scene") : "Scene";
    m_nextId = json.HasKey("nextId") ? json["nextId"].AsUInt(1) : 1;
    m_activeCameraId = json.HasKey("activeCameraId") ? json["activeCameraId"].AsUInt(0) : 0;
    if (json.HasKey("worldOffset") && json["worldOffset"].arrVal.size() >= 3) {
        m_worldOffset = glm::dvec3(json["worldOffset"][0].AsDouble(), json["worldOffset"][1].AsDouble(), json["worldOffset"][2].AsDouble());
    } else {
        m_worldOffset = glm::dvec3(0.0);
    }

    if (json.HasKey("environment") && json["environment"].IsObject()) {
        const auto& env = json["environment"];
        m_environment.hdrExposure = env["hdrExposure"].AsFloat(1.0f);
        m_environment.enableACES = env["enableACES"].AsBool(true);
        if (env.HasKey("antiAliasing")) {
            m_environment.antiAliasing = std::clamp(env["antiAliasing"].AsInt(2), 0, 2);
        } else if (env.HasKey("enableFXAA")) {
            // Older scenes: FXAA on becomes TAA (the new default), FXAA off stays off
            m_environment.antiAliasing = env["enableFXAA"].AsBool(true) ? 2 : 0;
        }
        m_environment.enableShadows = env["enableShadows"].AsBool(true);
        if (env.HasKey("shadowDistance")) m_environment.shadowDistance = env["shadowDistance"].AsFloat(40.0f);
        if (env.HasKey("enableSky")) m_environment.enableSky = env["enableSky"].AsBool(true);
        if (env.HasKey("skyIntensity")) m_environment.skyIntensity = env["skyIntensity"].AsFloat(1.0f);
        {
            SceneEnvironment& e = m_environment;
            const SceneEnvironment d;
            auto f = [&](const char* key, float& value, float fallback) { if (env.HasKey(key)) value = env[key].AsFloat(fallback); };
            auto b = [&](const char* key, bool& value, bool fallback) { if (env.HasKey(key)) value = env[key].AsBool(fallback); };
            b("timeOfDayEnabled", e.timeOfDayEnabled, d.timeOfDayEnabled);
            f("timeOfDay", e.timeOfDay, d.timeOfDay);
            f("dayLengthMinutes", e.dayLengthMinutes, d.dayLengthMinutes);
            b("clockRunsInEditor", e.clockRunsInEditor, d.clockRunsInEditor);
            f("latitude", e.latitude, d.latitude);
            f("northAngle", e.northAngle, d.northAngle);
            f("dayOfYear", e.dayOfYear, d.dayOfYear);
            b("advanceCalendar", e.advanceCalendar, d.advanceCalendar);
            b("moonEnabled", e.moonEnabled, d.moonEnabled);
            f("moonPhase", e.moonPhase, d.moonPhase);
            f("moonBrightness", e.moonBrightness, d.moonBrightness);
            b("cloudsEnabled", e.cloudsEnabled, d.cloudsEnabled);
            f("cloudCoverage", e.cloudCoverage, d.cloudCoverage);
            f("cloudOpacity", e.cloudOpacity, d.cloudOpacity);
            f("cloudHeight", e.cloudHeight, d.cloudHeight);
            f("cloudScale", e.cloudScale, d.cloudScale);
            f("windSpeed", e.windSpeed, d.windSpeed);
            f("windDirection", e.windDirection, d.windDirection);
            f("cloudShadows", e.cloudShadows, d.cloudShadows);
            b("physicalAtmosphere", e.physicalAtmosphere, d.physicalAtmosphere);
            b("volumetricClouds", e.volumetricClouds, d.volumetricClouds);
            f("cloudThickness", e.cloudThickness, d.cloudThickness);
            f("cloudDetail", e.cloudDetail, d.cloudDetail);
            b("weatherEnabled", e.weatherEnabled, d.weatherEnabled);
            if (env.HasKey("weatherPreset")) e.weatherPreset = env["weatherPreset"].AsInt(0);
            f("weatherTransition", e.weatherTransition, d.weatherTransition);
            f("rainAmount", e.rainAmount, d.rainAmount);
            f("snowAmount", e.snowAmount, d.snowAmount);
            f("wetness", e.wetness, d.wetness);
            f("snowCover", e.snowCover, d.snowCover);
            f("lightningRate", e.lightningRate, d.lightningRate);
            f("snowDepth", e.snowDepth, d.snowDepth);
            f("thunderVolume", e.thunderVolume, d.thunderVolume);
        }
        m_environment.enableGI = env["enableGI"].AsBool(true);
        if (env.HasKey("giSkyColor") && env["giSkyColor"].arrVal.size() >= 3) {
            m_environment.giSkyColor = glm::vec3(env["giSkyColor"][0].AsFloat(), env["giSkyColor"][1].AsFloat(), env["giSkyColor"][2].AsFloat());
        }
        m_environment.giIntensity = env["giIntensity"].AsFloat(1.0f);
        if (env.HasKey("fogColor") && env["fogColor"].arrVal.size() >= 3) {
            m_environment.fogColor = glm::vec3(env["fogColor"][0].AsFloat(), env["fogColor"][1].AsFloat(), env["fogColor"][2].AsFloat());
        }
        m_environment.fogDensity = env["fogDensity"].AsFloat(0.0f);
        m_environment.bloomIntensity = env["bloomIntensity"].AsFloat(0.35f);
        m_environment.ssaoIntensity = env["ssaoIntensity"].AsFloat(1.0f);
        if (env.HasKey("ssaoRadius")) m_environment.ssaoRadius = env["ssaoRadius"].AsFloat(0.5f);
        m_environment.ssrIntensity = env["ssrIntensity"].AsFloat(0.35f);
        m_environment.volumetricScattering = env["volumetricScattering"].AsFloat(0.0f);
        const SceneEnvironment defaults;
        auto readFloat = [&](const char* key, float& out, float fallback) {
            if (env.HasKey(key)) out = env[key].AsFloat(fallback);
        };
        auto readBool = [&](const char* key, bool& out, bool fallback) {
            if (env.HasKey(key)) out = env[key].AsBool(fallback);
        };
        readBool("useHDRI", m_environment.useHDRI, defaults.useHDRI);
        if (env.HasKey("hdriPath")) m_environment.hdriPath = env["hdriPath"].AsString("");
        readFloat("hdriIntensity", m_environment.hdriIntensity, defaults.hdriIntensity);
        readFloat("hdriRotation", m_environment.hdriRotation, defaults.hdriRotation);
        readBool("autoExposure", m_environment.autoExposure, defaults.autoExposure);
        readFloat("autoExposureMin", m_environment.autoExposureMin, defaults.autoExposureMin);
        readFloat("autoExposureMax", m_environment.autoExposureMax, defaults.autoExposureMax);
        readFloat("autoExposureSpeed", m_environment.autoExposureSpeed, defaults.autoExposureSpeed);
        readFloat("contrast", m_environment.contrast, defaults.contrast);
        readFloat("saturation", m_environment.saturation, defaults.saturation);
        readFloat("temperature", m_environment.temperature, defaults.temperature);
        readFloat("tint", m_environment.tint, defaults.tint);
        readFloat("vignette", m_environment.vignette, defaults.vignette);
        readFloat("filmGrain", m_environment.filmGrain, defaults.filmGrain);
        readFloat("sharpen", m_environment.sharpen, defaults.sharpen);
        readFloat("lensFlare", m_environment.lensFlare, defaults.lensFlare);
        readBool("depthOfField", m_environment.depthOfField, defaults.depthOfField);
        readFloat("focusDistance", m_environment.focusDistance, defaults.focusDistance);
        readFloat("aperture", m_environment.aperture, defaults.aperture);
        readFloat("dofMaxBlur", m_environment.dofMaxBlur, defaults.dofMaxBlur);
        readFloat("motionBlur", m_environment.motionBlur, defaults.motionBlur);
        readFloat("chromaticAberration", m_environment.chromaticAberration, defaults.chromaticAberration);
        if (env.HasKey("colorLutPath")) m_environment.colorLutPath = env["colorLutPath"].AsString("");
        readFloat("lutStrength", m_environment.lutStrength, defaults.lutStrength);
        readFloat("ssrMaxDistance", m_environment.ssrMaxDistance, defaults.ssrMaxDistance);
        readFloat("ssrThickness", m_environment.ssrThickness, defaults.ssrThickness);
        readFloat("volumetricFogDensity", m_environment.volumetricFogDensity, defaults.volumetricFogDensity);
        readFloat("volumetricHeightFalloff", m_environment.volumetricHeightFalloff, defaults.volumetricHeightFalloff);
        readFloat("volumetricBaseHeight", m_environment.volumetricBaseHeight, defaults.volumetricBaseHeight);
        readFloat("volumetricAnisotropy", m_environment.volumetricAnisotropy, defaults.volumetricAnisotropy);
        readBool("occlusionCulling", m_environment.occlusionCulling, defaults.occlusionCulling);
        readBool("bakedGIEnabled", m_environment.bakedGIEnabled, defaults.bakedGIEnabled);
        readFloat("bakedGIIntensity", m_environment.bakedGIIntensity, defaults.bakedGIIntensity);
        if (env.HasKey("bakedGIBounces")) m_environment.bakedGIBounces = std::clamp(env["bakedGIBounces"].AsInt(2), 1, 4);
        readFloat("bakedGISpacing", m_environment.bakedGISpacing, defaults.bakedGISpacing);
        if (env.HasKey("bakedGIPath")) m_environment.bakedGIPath = env["bakedGIPath"].AsString("");
        if (env.HasKey("bakeLightmaps")) m_environment.bakeLightmaps = env["bakeLightmaps"].AsBool(true);
        readFloat("lightmapDensity", m_environment.lightmapDensity, defaults.lightmapDensity);
        if (env.HasKey("lightmapSamples")) m_environment.lightmapSamples = std::clamp(env["lightmapSamples"].AsInt(128), 16, 1024);
        if (env.HasKey("lightmapPath")) m_environment.lightmapPath = env["lightmapPath"].AsString("");
        readBool("vxgiEnabled", m_environment.vxgiEnabled, defaults.vxgiEnabled);
        readFloat("vxgiIntensity", m_environment.vxgiIntensity, defaults.vxgiIntensity);
        readFloat("vxgiRange", m_environment.vxgiRange, defaults.vxgiRange);
        readFloat("vxgiBounce", m_environment.vxgiBounce, defaults.vxgiBounce);
    }

    const auto& nodes = json["nodes"];
    for (size_t i = 0; i < nodes.arrVal.size(); ++i) {
        const auto& n = nodes[i];
        NodeType loadedType = StringToNodeType(n.HasKey("nodeType") ? n["nodeType"].AsString("Mesh") : "Mesh");
        auto node = NodeFactory::CreateNode(loadedType);

        node->id = n["id"].AsUInt(m_nextId++);
        node->parentId = n.HasKey("parentId") ? n["parentId"].AsUInt(0) : 0;
        node->name = n["name"].AsString("Node");
        node->nodeType = StringToNodeType(n["nodeType"].AsString("Mesh"));
        node->type = StringToShapeType(n["shapeType"].AsString("Cube"));
        node->customMeshName = n.HasKey("customMeshName") ? n["customMeshName"].AsString() : "";

        // Nested Scene Instancing
        node->instanceScenePath = n.HasKey("instanceScenePath") ? n["instanceScenePath"].AsString("") : "";
        node->isSceneInstanceRoot = n.HasKey("isSceneInstanceRoot") ? n["isSceneInstanceRoot"].AsBool(false) : false;

        // Collision filtering
        if (n.HasKey("collisionLayer")) node->collisionLayer = static_cast<uint32_t>(n["collisionLayer"].AsUInt(1));
        if (n.HasKey("collisionMask")) node->collisionMask = static_cast<uint32_t>(n["collisionMask"].AsUInt(1));

        // Declarative Multiplayer Property Sync
        if (n.HasKey("replicationConfig") && n["replicationConfig"].IsObject()) {
            const auto& rep = n["replicationConfig"];
            node->replicationConfig.enabled = rep["enabled"].AsBool(false);
            if (rep.HasKey("rules") && rep["rules"].IsArray()) {
                node->replicationConfig.rules.clear();
                for (size_t rIdx = 0; rIdx < rep["rules"].arrVal.size(); ++rIdx) {
                    const auto& rJson = rep["rules"][rIdx];
                    PropertyReplicationRule rule;
                    rule.propertyName = rJson["property"].AsString("position");
                    rule.mode = StringToReplicationMode(rJson["mode"].AsString("OnChange"));
                    rule.syncRateHz = rJson["hz"].AsFloat(20.0f);
                    node->replicationConfig.rules.push_back(rule);
                }
            }
        }

        // Transform
        if (n.HasKey("transform")) {
            const auto& tf = n["transform"];
            if (tf.HasKey("position") && tf["position"].arrVal.size() >= 3) {
                node->position = glm::vec3(tf["position"][0].AsFloat(), tf["position"][1].AsFloat(), tf["position"][2].AsFloat());
            }
            if (tf.HasKey("rotation") && tf["rotation"].arrVal.size() >= 3) {
                node->rotation = glm::vec3(tf["rotation"][0].AsFloat(), tf["rotation"][1].AsFloat(), tf["rotation"][2].AsFloat());
            }
            if (tf.HasKey("scale") && tf["scale"].arrVal.size() >= 3) {
                node->scale = glm::vec3(tf["scale"][0].AsFloat(1.0f), tf["scale"][1].AsFloat(1.0f), tf["scale"][2].AsFloat(1.0f));
            }
        }

        // Color & Material
        if (n.HasKey("color") && n["color"].arrVal.size() >= 4) {
            node->color = glm::vec4(n["color"][0].AsFloat(), n["color"][1].AsFloat(), n["color"][2].AsFloat(), n["color"][3].AsFloat(1.0f));
        }
        if (n.HasKey("texturePath")) node->texturePath = n["texturePath"].AsString();
        if (n.HasKey("normalMapPath")) node->normalMapPath = n["normalMapPath"].AsString();
        if (n.HasKey("metallicMapPath")) node->metallicMapPath = n["metallicMapPath"].AsString();
        if (n.HasKey("roughnessMapPath")) node->roughnessMapPath = n["roughnessMapPath"].AsString();
        if (n.HasKey("aoMapPath")) node->aoMapPath = n["aoMapPath"].AsString();
        if (n.HasKey("emissiveMapPath")) node->emissiveMapPath = n["emissiveMapPath"].AsString();
        if (n.HasKey("heightMapPath")) node->heightMapPath = n["heightMapPath"].AsString();
        if (n.HasKey("shaderGraphPath")) node->shaderGraphPath = n["shaderGraphPath"].AsString();
        if (n.HasKey("materialName")) node->materialName = n["materialName"].AsString();
        if (n.HasKey("roughness")) node->roughness = n["roughness"].AsFloat(0.5f);
        if (n.HasKey("metallic")) node->metallic = n["metallic"].AsFloat(0.0f);
        if (n.HasKey("aoStrength")) node->aoStrength = n["aoStrength"].AsFloat(1.0f);
        if (n.HasKey("emissiveColor") && n["emissiveColor"].arrVal.size() >= 3) {
            node->emissiveColor = glm::vec3(n["emissiveColor"][0].AsFloat(), n["emissiveColor"][1].AsFloat(), n["emissiveColor"][2].AsFloat());
        }
        if (n.HasKey("emissiveIntensity")) node->emissiveIntensity = n["emissiveIntensity"].AsFloat(0.0f);
        if (n.HasKey("alphaCutoff")) node->alphaCutoff = n["alphaCutoff"].AsFloat(0.0f);
        if (n.HasKey("doubleSided")) node->doubleSided = n["doubleSided"].AsBool(true);
        if (n.HasKey("castShadows")) node->castShadows = n["castShadows"].AsBool(true);
        if (n.HasKey("refractionIOR")) node->refractionIOR = n["refractionIOR"].AsFloat(1.5f);
        if (n.HasKey("isMirror")) node->isMirror = n["isMirror"].AsBool(false);
        if (n.HasKey("videoScreen")) {
            const JsonValue& vs = n["videoScreen"];
            node->isVideoScreen = vs["enabled"].AsBool(node->nodeType != NodeType::UI_Video);
            node->videoChannelPreset = vs["channelPreset"].AsInt(1);
            node->videoSourcePath = vs["sourcePath"].AsString("");
            node->videoPlaying = vs["playing"].AsBool(true);
            node->videoLoop = vs["loop"].AsBool(true);
            node->videoSpeed = vs["speed"].AsFloat(1.0f);
            node->videoCurrentTime = vs["currentTime"].AsFloat(0.0f);
            node->videoDuration = vs["duration"].AsFloat(10.0f);
            node->videoFps = vs["fps"].AsFloat(24.0f);
            node->videoAtlasCols = vs["atlasCols"].AsInt(4);
            node->videoAtlasRows = vs["atlasRows"].AsInt(4);
            node->videoScreenBrightness = vs["brightness"].AsFloat(1.6f);
            node->videoScanlineStrength = vs["scanlines"].AsFloat(0.15f);
            node->videoAudioPath = vs["audioPath"].AsString("");
            node->videoAudioVolume = vs["audioVolume"].AsFloat(0.8f);
            node->videoSpatialAudio = vs["spatialAudio"].AsBool(true);
        }
        {
            auto readVec3 = [&](const char* key, glm::vec3& out) {
                if (n.HasKey(key) && n[key].arrVal.size() >= 3) out = glm::vec3(n[key][0].AsFloat(), n[key][1].AsFloat(), n[key][2].AsFloat());
            };
            auto readF = [&](const char* key, float& out) { if (n.HasKey(key)) out = n[key].AsFloat(out); };
            if (n.HasKey("materialModel")) node->materialModel = std::clamp(n["materialModel"].AsInt(0), 0, 3);
            readVec3("subsurfaceColor", node->subsurfaceColor);
            readF("subsurfaceStrength", node->subsurfaceStrength);
            readF("subsurfaceTranslucency", node->subsurfaceTranslucency);
            readVec3("hairHighlightTint", node->hairHighlightTint);
            readF("hairHighlightShift", node->hairHighlightShift);
            readF("irisRadius", node->irisRadius);
            readF("irisDepth", node->irisDepth);
            readF("corneaIOR", node->corneaIOR);
            readF("clearCoat", node->clearCoat);
            readF("clearCoatRoughness", node->clearCoatRoughness);
            if (n.HasKey("triplanarMapping")) node->triplanarMapping = n["triplanarMapping"].AsBool(false);
            readF("triplanarTileSize", node->triplanarTileSize);
            readF("triplanarSharpness", node->triplanarSharpness);
            if (n.HasKey("detailAlbedoPath")) node->detailAlbedoPath = n["detailAlbedoPath"].AsString("");
            if (n.HasKey("detailNormalPath")) node->detailNormalPath = n["detailNormalPath"].AsString("");
            readF("detailTiling", node->detailTiling);
            readF("detailStrength", node->detailStrength);
            if (n.HasKey("useVertexColors")) node->useVertexColors = n["useVertexColors"].AsBool(false);
            if (n.HasKey("lightmapStatic")) node->lightmapStatic = n["lightmapStatic"].AsBool(true);
        }
        if (n.HasKey("isReflectionProbe")) node->isReflectionProbe = n["isReflectionProbe"].AsBool(false);
        if (n.HasKey("cloth")) {
            const JsonValue& cloth = n["cloth"];
            node->isCloth = true;
            if (cloth.HasKey("resX")) node->clothResX = static_cast<uint32_t>(cloth["resX"].AsInt(10));
            if (cloth.HasKey("resY")) node->clothResY = static_cast<uint32_t>(cloth["resY"].AsInt(10));
            if (cloth.HasKey("width")) node->clothWidth = cloth["width"].AsFloat(1.5f);
            if (cloth.HasKey("height")) node->clothHeight = cloth["height"].AsFloat(1.5f);
            if (cloth.HasKey("mass")) node->clothMass = cloth["mass"].AsFloat(1.0f);
            if (cloth.HasKey("compliance")) node->clothCompliance = cloth["compliance"].AsFloat(0.00001f);
            if (cloth.HasKey("pinTop")) node->clothPinTop = cloth["pinTop"].AsBool(true);
            if (cloth.HasKey("wind") && cloth["wind"].arrVal.size() >= 3) {
                node->clothWind = glm::vec3(cloth["wind"][0].AsFloat(0.0f), cloth["wind"][1].AsFloat(0.0f), cloth["wind"][2].AsFloat(0.0f));
            }
        }
        if (n.HasKey("liquid")) {
            const JsonValue& liquid = n["liquid"];
            node->isLiquid = true;
            if (liquid.HasKey("fill")) node->liquidFill = liquid["fill"].AsFloat(0.6f);
            if (liquid.HasKey("topColor") && liquid["topColor"].arrVal.size() >= 4) {
                node->liquidTopColor = glm::vec4(liquid["topColor"][0].AsFloat(0.85f), liquid["topColor"][1].AsFloat(0.95f),
                                                 liquid["topColor"][2].AsFloat(1.0f), liquid["topColor"][3].AsFloat(0.55f));
            }
            if (liquid.HasKey("slosh")) node->liquidSlosh = liquid["slosh"].AsFloat(1.0f);
            if (liquid.HasKey("colorDepth")) node->liquidColorDepth = liquid["colorDepth"].AsFloat(0.05f);
        }
        if (n.HasKey("fluidVolume")) {
            const JsonValue& fluid = n["fluidVolume"];
            node->isFluidVolume = true;
            if (fluid.HasKey("size") && fluid["size"].arrVal.size() >= 3) {
                node->fluidSize = glm::vec3(fluid["size"][0].AsFloat(1.0f), fluid["size"][1].AsFloat(0.6f), fluid["size"][2].AsFloat(0.6f));
            }
            if (fluid.HasKey("fill")) node->fluidFill = fluid["fill"].AsFloat(0.4f);
            if (fluid.HasKey("start")) node->fluidStart = fluid["start"].AsInt(1);
            if (fluid.HasKey("spacing")) node->fluidSpacing = fluid["spacing"].AsFloat(0.04f);
            if (fluid.HasKey("viscosity")) node->fluidViscosity = fluid["viscosity"].AsFloat(0.05f);
            if (fluid.HasKey("color") && fluid["color"].arrVal.size() >= 4) {
                node->fluidColor = glm::vec4(fluid["color"][0].AsFloat(0.55f), fluid["color"][1].AsFloat(0.8f),
                                             fluid["color"][2].AsFloat(0.95f), fluid["color"][3].AsFloat(1.0f));
            }
            if (fluid.HasKey("colorDepth")) node->fluidColorDepth = fluid["colorDepth"].AsFloat(0.5f);
        }
        if (n.HasKey("pourSpout")) {
            const JsonValue& pour = n["pourSpout"];
            node->isPourSpout = true;
            if (pour.HasKey("flow")) node->pourFlow = pour["flow"].AsFloat(-1.0f);
            if (pour.HasKey("startAngle")) node->pourStartAngle = pour["startAngle"].AsFloat(60.0f);
            if (pour.HasKey("streamRadius")) node->pourStreamRadius = pour["streamRadius"].AsFloat(0.012f);
            if (pour.HasKey("speed")) node->pourSpeed = pour["speed"].AsFloat(1.0f);
            if (pour.HasKey("color") && pour["color"].arrVal.size() >= 4) {
                node->pourColor = glm::vec4(pour["color"][0].AsFloat(0.8f), pour["color"][1].AsFloat(0.92f),
                                            pour["color"][2].AsFloat(1.0f), pour["color"][3].AsFloat(0.35f));
            }
            if (pour.HasKey("sourceId")) node->pourSourceId = static_cast<uint32_t>(pour["sourceId"].AsFloat(0.0f));
        }
        if (n.HasKey("ocean")) {
            const JsonValue& ocean = n["ocean"];
            node->isOcean = true;
            if (ocean.HasKey("size") && ocean["size"].arrVal.size() >= 2) {
                node->oceanSize = glm::vec2(ocean["size"][0].AsFloat(2000.0f), ocean["size"][1].AsFloat(2000.0f));
            }
            auto readColor = [&](const char* key, glm::vec3& out) {
                if (ocean.HasKey(key) && ocean[key].arrVal.size() >= 3) {
                    out = glm::vec3(ocean[key][0].AsFloat(out.r), ocean[key][1].AsFloat(out.g), ocean[key][2].AsFloat(out.b));
                }
            };
            readColor("shallowColor", node->waterShallowColor);
            readColor("deepColor", node->waterDeepColor);
            if (ocean.HasKey("waveHeight")) node->waveHeight = ocean["waveHeight"].AsFloat(0.8f);
            if (ocean.HasKey("waveLength")) node->waveLength = ocean["waveLength"].AsFloat(24.0f);
            if (ocean.HasKey("waveDirection")) node->waveDirection = ocean["waveDirection"].AsFloat(30.0f);
            if (ocean.HasKey("choppiness")) node->waveChoppiness = ocean["choppiness"].AsFloat(0.6f);
            if (ocean.HasKey("waveSpeed")) node->waveSpeed = ocean["waveSpeed"].AsFloat(1.0f);
            if (ocean.HasKey("clarity")) node->waterClarity = ocean["clarity"].AsFloat(4.0f);
            if (ocean.HasKey("roughness")) node->waterRoughness = ocean["roughness"].AsFloat(0.06f);
            if (ocean.HasKey("whitecaps")) node->whitecaps = ocean["whitecaps"].AsFloat(0.4f);
            if (ocean.HasKey("shoreFoamWidth")) node->shoreFoamWidth = ocean["shoreFoamWidth"].AsFloat(1.0f);
            if (ocean.HasKey("soundVolume")) node->oceanSoundVolume = ocean["soundVolume"].AsFloat(0.8f);
        }
        if (n.HasKey("probeRadius")) node->probeRadius = n["probeRadius"].AsFloat(10.0f);
        if (n.HasKey("probeBoxProjection")) node->probeBoxProjection = n["probeBoxProjection"].AsBool(false);
        if (n.HasKey("isGIVolume")) node->isGIVolume = n["isGIVolume"].AsBool(false);
        if (n.HasKey("giVolumeSize") && n["giVolumeSize"].arrVal.size() >= 3) {
            node->giVolumeSize = glm::vec3(n["giVolumeSize"][0].AsFloat(20.0f), n["giVolumeSize"][1].AsFloat(6.0f), n["giVolumeSize"][2].AsFloat(20.0f));
        }
        if (n.HasKey("probeBoxSize") && n["probeBoxSize"].arrVal.size() >= 3) {
            node->probeBoxSize = glm::vec3(n["probeBoxSize"][0].AsFloat(10.0f), n["probeBoxSize"][1].AsFloat(4.0f), n["probeBoxSize"][2].AsFloat(10.0f));
        }
        if (n.HasKey("heightScale")) node->heightScale = n["heightScale"].AsFloat(0.04f);
        if (n.HasKey("scriptPath")) node->scriptPath = n["scriptPath"].AsString();
        if (n.HasKey("hasSkeleton")) node->hasSkeleton = n["hasSkeleton"].AsBool(false);
        if (n.HasKey("skeletonName")) node->skeletonName = n["skeletonName"].AsString("");
        if (n.HasKey("currentAnimState")) node->currentAnimState = n["currentAnimState"].AsString("Idle");
        if (n.HasKey("playAnimation")) node->playAnimation = n["playAnimation"].AsBool(false);
        if (n.HasKey("animationSpeed")) node->animationSpeed = n["animationSpeed"].AsFloat(1.0f);
        if (n.HasKey("animationStartOffset")) node->animationStartOffset = n["animationStartOffset"].AsFloat(0.0f);
        if (n.HasKey("boneCount")) node->boneCount = (uint32_t)n["boneCount"].AsInt(0);
        if (n.HasKey("boneNames") && n["boneNames"].IsArray()) {
            node->boneNames.clear();
            for (const auto& b : n["boneNames"].arrVal) node->boneNames.push_back(b.AsString(""));
            if (node->boneCount == 0) node->boneCount = (uint32_t)node->boneNames.size();
        }
        if (n.HasKey("animClipNames") && n["animClipNames"].IsArray()) {
            node->animClipNames.clear();
            for (const auto& c : n["animClipNames"].arrVal) node->animClipNames.push_back(c.AsString(""));
        }

        // IK & Morph Targets
        if (n.HasKey("enableIK")) node->enableIK = n["enableIK"].AsBool(false);
        if (n.HasKey("ikTargetBone")) node->ikTargetBone = n["ikTargetBone"].AsString();
        if (n.HasKey("ikTargetPosition") && n["ikTargetPosition"].arrVal.size() >= 3) {
            node->ikTargetPosition = glm::vec3(n["ikTargetPosition"][0].AsFloat(), n["ikTargetPosition"][1].AsFloat(), n["ikTargetPosition"][2].AsFloat());
        }
        if (n.HasKey("ikPoleVector") && n["ikPoleVector"].arrVal.size() >= 3) {
            node->ikPoleVector = glm::vec3(n["ikPoleVector"][0].AsFloat(), n["ikPoleVector"][1].AsFloat(), n["ikPoleVector"][2].AsFloat());
        }
        if (n.HasKey("ikWeight")) node->ikWeight = n["ikWeight"].AsFloat(1.0f);
        if (n.HasKey("morphWeights") && n["morphWeights"].IsObject()) {
            node->morphWeights.clear();
            for (const auto& kv : n["morphWeights"].objVal) {
                node->morphWeights[kv.first] = kv.second.AsFloat(0.0f);
            }
        }

        // RigidBody Physics
        if (n.HasKey("rigidBody") && n["rigidBody"].IsObject()) {
            const auto& rb = n["rigidBody"];
            node->rigidBodyType = StringToRigidBodyType(rb["type"].AsString("None"));
            node->mass = rb["mass"].AsFloat(1.0f);
            node->useGravity = rb["useGravity"].AsBool(true);
            if (rb.HasKey("floatsOnWater")) node->floatsOnWater = rb["floatsOnWater"].AsBool(false);
            if (rb.HasKey("buoyancy")) node->buoyancy = rb["buoyancy"].AsFloat(1.4f);
            node->linearDamping = rb["linearDamping"].AsFloat(0.05f);
            node->angularDamping = rb["angularDamping"].AsFloat(0.05f);
            if (rb.HasKey("friction")) node->friction = rb["friction"].AsFloat(0.5f);
            if (rb.HasKey("bounciness")) node->bounciness = rb["bounciness"].AsFloat(0.0f);
            if (rb.HasKey("constraintType")) node->constraintType = rb["constraintType"].AsInt(0);
            if (rb.HasKey("constraintTargetNodeId")) node->constraintTargetNodeId = rb["constraintTargetNodeId"].AsUInt(0);
            if (rb.HasKey("enableRagdoll")) node->enableRagdoll = rb["enableRagdoll"].AsBool(false);
            if (rb.HasKey("collisionLayer")) node->collisionLayer = static_cast<uint32_t>(rb["collisionLayer"].AsUInt(1));
            if (rb.HasKey("collisionMask")) node->collisionMask = static_cast<uint32_t>(rb["collisionMask"].AsUInt(1));
            if (rb.HasKey("linearVelocity") && rb["linearVelocity"].arrVal.size() >= 3) {
                node->linearVelocity = glm::vec3(rb["linearVelocity"][0].AsFloat(), rb["linearVelocity"][1].AsFloat(), rb["linearVelocity"][2].AsFloat());
            }
            if (rb.HasKey("angularVelocity") && rb["angularVelocity"].arrVal.size() >= 3) {
                node->angularVelocity = glm::vec3(rb["angularVelocity"][0].AsFloat(), rb["angularVelocity"][1].AsFloat(), rb["angularVelocity"][2].AsFloat());
            }
        }

        // Networking
        if (n.HasKey("networkId")) node->networkId = n["networkId"].AsUInt(0);
        if (n.HasKey("multiplayerAuthority")) node->multiplayerAuthority = n["multiplayerAuthority"].AsUInt(1);
        if (n.HasKey("replicateTransform")) node->replicateTransform = n["replicateTransform"].AsBool(false);

        // LOD Configuration
        if (n.HasKey("enableLOD")) node->enableLOD = n["enableLOD"].AsBool(false);
        if (n.HasKey("maxDrawDistance")) node->maxDrawDistance = n["maxDrawDistance"].AsFloat(0.0f);
        if (n.HasKey("forcedLOD")) node->forcedLOD = static_cast<int>(n["forcedLOD"].AsInt(-1));
        if (n.HasKey("lodLevels") && n["lodLevels"].IsArray()) {
            node->lodLevels.clear();
            for (size_t lIdx = 0; lIdx < n["lodLevels"].arrVal.size(); ++lIdx) {
                const auto& lObj = n["lodLevels"].arrVal[lIdx];
                LODLevel lvl;
                lvl.meshKey = lObj["meshKey"].AsString("");
                lvl.distance = lObj["distance"].AsFloat(25.0f);
                lvl.reductionRatio = lObj["reductionRatio"].AsFloat(0.5f);
                lvl.triangleCount = static_cast<uint32_t>(lObj["triangleCount"].AsUInt(0));
                node->lodLevels.push_back(lvl);
            }
        }

        // Children IDs
        if (n.HasKey("children") && n["children"].IsArray()) {
            for (size_t c = 0; c < n["children"].arrVal.size(); ++c) {
                node->childrenIds.push_back(n["children"][c].AsUInt());
            }
        }

        // Node-specific components
        if (node->nodeType == NodeType::Light && n.HasKey("light")) {
            const auto& l = n["light"];
            node->lightType = StringToLightType(l["type"].AsString("Point"));
            if (l.HasKey("color") && l["color"].arrVal.size() >= 3) {
                node->lightColor = glm::vec3(l["color"][0].AsFloat(), l["color"][1].AsFloat(), l["color"][2].AsFloat());
            }
            node->lightIntensity = l["intensity"].AsFloat(2.0f);
            node->lightRange = l["range"].AsFloat(25.0f);
            if (l.HasKey("innerCone")) node->spotInnerConeAngle = l["innerCone"].AsFloat(15.0f);
            if (l.HasKey("outerCone")) node->spotOuterConeAngle = l["outerCone"].AsFloat(30.0f);
            if (l.HasKey("castShadows")) node->castShadows = l["castShadows"].AsBool(true);
            if (l.HasKey("areaSize") && l["areaSize"].arrVal.size() >= 2) {
                node->areaLightSize = glm::vec2(l["areaSize"][0].AsFloat(2.0f), l["areaSize"][1].AsFloat(1.0f));
            }
            if (l.HasKey("twoSided")) node->areaLightTwoSided = l["twoSided"].AsBool(false);
            if (l.HasKey("shadowBias")) node->shadowBias = l["shadowBias"].AsFloat(0.005f);
        } else if (node->nodeType == NodeType::Camera && n.HasKey("camera")) {
            const auto& c = n["camera"];
            node->cameraFov = c["fov"].AsFloat(60.0f);
            node->cameraNear = c["near"].AsFloat(0.1f);
            node->cameraFar = c["far"].AsFloat(100.0f);
        } else if (node->nodeType == NodeType::Collider && n.HasKey("collider")) {
            const auto& col = n["collider"];
            node->colliderType = StringToColliderType(col["type"].AsString("Box"));
            if (col.HasKey("size") && col["size"].arrVal.size() >= 3) {
                node->colliderSize = glm::vec3(col["size"][0].AsFloat(1.0f), col["size"][1].AsFloat(1.0f), col["size"][2].AsFloat(1.0f));
            }
            node->colliderRadius = col["radius"].AsFloat(0.5f);
            node->colliderHeight = col["height"].AsFloat(1.0f);
            if (col.HasKey("center") && col["center"].arrVal.size() >= 3) {
                node->colliderCenter = glm::vec3(col["center"][0].AsFloat(), col["center"][1].AsFloat(), col["center"][2].AsFloat());
            }
            node->isTrigger = col["isTrigger"].AsBool(false);
            node->bounciness = col["bounciness"].AsFloat(0.0f);
            node->friction = col["friction"].AsFloat(0.5f);
        } else if (node->nodeType == NodeType::Character && n.HasKey("character")) {
            auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
            if (charNode) {
                const auto& ch = n["character"];
                if (ch.HasKey("name")) charNode->characterName = ch["name"].AsString("NPC");
                if (ch.HasKey("dialogue")) charNode->dialogueText = ch["dialogue"].AsString("Greetings, adventurer!");
                if (ch.HasKey("radius")) charNode->interactionRadius = ch["radius"].AsFloat(3.5f);
                if (ch.HasKey("moveSpeed")) charNode->moveSpeed = ch["moveSpeed"].AsFloat(2.5f);
                if (ch.HasKey("turnSpeed")) charNode->turnSpeed = ch["turnSpeed"].AsFloat(6.0f);
                if (ch.HasKey("tolerance")) charNode->waypointTolerance = ch["tolerance"].AsFloat(0.5f);
                if (ch.HasKey("isPatrolling")) charNode->isPatrolling = ch["isPatrolling"].AsBool(true);
                if (ch.HasKey("loopWaypoints")) charNode->loopWaypoints = ch["loopWaypoints"].AsBool(true);
                if (ch.HasKey("idleAnim")) charNode->idleAnimClip = ch["idleAnim"].AsString("Idle");
                if (ch.HasKey("walkAnim")) charNode->walkAnimClip = ch["walkAnim"].AsString("Walk");
                if (ch.HasKey("groundClamping")) charNode->applyGroundClamping = ch["groundClamping"].AsBool(true);
                if (ch.HasKey("groundOffset")) charNode->groundOffset = ch["groundOffset"].AsFloat(0.0f);
                if (ch.HasKey("clampToWorldFloor")) charNode->clampToWorldFloor = ch["clampToWorldFloor"].AsBool(true);

                if (ch.HasKey("waypoints") && ch["waypoints"].IsArray()) {
                    charNode->waypoints.clear();
                    for (size_t w = 0; w < ch["waypoints"].arrVal.size(); ++w) {
                        const auto& wp = ch["waypoints"][w];
                        if (wp.IsArray() && wp.arrVal.size() >= 3) {
                            charNode->waypoints.push_back(glm::vec3(wp[0].AsFloat(), wp[1].AsFloat(), wp[2].AsFloat()));
                        }
                    }
                }
            }
        } else if (node->nodeType == NodeType::Spline && n.HasKey("spline")) {
            auto splineNode = std::dynamic_pointer_cast<SplineNode>(node);
            if (splineNode) {
                const auto& sp = n["spline"];
                if (sp.HasKey("mode")) splineNode->splineMode = StringToSplineMode(sp["mode"].AsString("CatmullRom"));
                if (sp.HasKey("closedLoop")) splineNode->closedLoop = sp["closedLoop"].AsBool(false);
                if (sp.HasKey("tension")) splineNode->tension = sp["tension"].AsFloat(0.5f);
                if (sp.HasKey("subdivisions")) splineNode->subdivisions = sp["subdivisions"].AsInt(32);
                if (sp.HasKey("controlPoints") && sp["controlPoints"].IsArray()) {
                    splineNode->controlPoints.clear();
                    for (const auto& pt : sp["controlPoints"].arrVal) {
                        if (pt.IsArray() && pt.arrVal.size() >= 3) {
                            splineNode->controlPoints.push_back(glm::vec3(pt[0].AsFloat(), pt[1].AsFloat(), pt[2].AsFloat()));
                        }
                    }
                }
            }
        } else if (node->nodeType == NodeType::Decal && n.HasKey("decal")) {
            auto decalNode = std::dynamic_pointer_cast<DecalNode>(node);
            if (decalNode) {
                const auto& dec = n["decal"];
                if (dec.HasKey("size") && dec["size"].arrVal.size() >= 3) {
                    decalNode->decalSize = glm::vec3(dec["size"][0].AsFloat(2.0f), dec["size"][1].AsFloat(2.0f), dec["size"][2].AsFloat(2.0f));
                }
                if (dec.HasKey("opacity")) decalNode->opacity = dec["opacity"].AsFloat(1.0f);
                if (dec.HasKey("fadeDistance")) decalNode->fadeDistance = dec["fadeDistance"].AsFloat(25.0f);
                if (dec.HasKey("normalAngleThreshold")) decalNode->normalAngleThreshold = dec["normalAngleThreshold"].AsFloat(75.0f);
                if (dec.HasKey("blendMode")) decalNode->blendMode = static_cast<DecalBlendMode>(dec["blendMode"].AsInt(0));
                if (dec.HasKey("sortOrder")) decalNode->sortOrder = dec["sortOrder"].AsInt(0);
            }
        } else if (node->nodeType == NodeType::ParticleEmitter && n.HasKey("particle")) {
            auto partNode = std::dynamic_pointer_cast<ParticleNode>(node);
            if (partNode) {
                const auto& pObj = n["particle"];
                if (pObj.HasKey("simulationMode")) partNode->simulationMode = static_cast<ParticleSimulationMode>(pObj["simulationMode"].AsInt(0));
                if (pObj.HasKey("emitterShape")) partNode->emitterShape = static_cast<ParticleEmitterShape>(pObj["emitterShape"].AsInt(3));
                if (pObj.HasKey("maxParticles")) partNode->maxParticles = pObj["maxParticles"].AsUInt(1000);
                if (pObj.HasKey("spawnRate")) partNode->spawnRate = pObj["spawnRate"].AsFloat(40.0f);
                if (pObj.HasKey("lifetimeMin")) partNode->lifetimeMin = pObj["lifetimeMin"].AsFloat(1.0f);
                if (pObj.HasKey("lifetimeMax")) partNode->lifetimeMax = pObj["lifetimeMax"].AsFloat(2.5f);
                if (pObj.HasKey("startSpeed")) partNode->startSpeed = pObj["startSpeed"].AsFloat(3.5f);
                if (pObj.HasKey("startSize")) partNode->startSize = pObj["startSize"].AsFloat(0.25f);
                if (pObj.HasKey("endSize")) partNode->endSize = pObj["endSize"].AsFloat(0.05f);
                if (pObj.HasKey("startColor") && pObj["startColor"].arrVal.size() >= 4) {
                    partNode->startColor = glm::vec4(pObj["startColor"][0].AsFloat(), pObj["startColor"][1].AsFloat(), pObj["startColor"][2].AsFloat(), pObj["startColor"][3].AsFloat());
                }
                if (pObj.HasKey("endColor") && pObj["endColor"].arrVal.size() >= 4) {
                    partNode->endColor = glm::vec4(pObj["endColor"][0].AsFloat(), pObj["endColor"][1].AsFloat(), pObj["endColor"][2].AsFloat(), pObj["endColor"][3].AsFloat());
                }
                if (pObj.HasKey("gravityMultiplier")) partNode->gravityMultiplier = pObj["gravityMultiplier"].AsFloat(-0.5f);
                if (pObj.HasKey("emitterRadius")) partNode->emitterRadius = pObj["emitterRadius"].AsFloat(0.5f);
                if (pObj.HasKey("coneAngle")) partNode->coneAngle = pObj["coneAngle"].AsFloat(25.0f);
                if (pObj.HasKey("isEmitting")) partNode->isEmitting = pObj["isEmitting"].AsBool(true);
                if (pObj.HasKey("looping")) partNode->looping = pObj["looping"].AsBool(true);
                if (pObj.HasKey("worldSpace")) partNode->worldSpace = pObj["worldSpace"].AsBool(true);
                if (pObj.HasKey("enableVortex")) partNode->enableVortexModule = pObj["enableVortex"].AsBool(false);
                if (pObj.HasKey("vortexStrength")) partNode->vortexStrength = pObj["vortexStrength"].AsFloat(3.0f);
                if (pObj.HasKey("enableAttractor")) partNode->enableAttractorModule = pObj["enableAttractor"].AsBool(false);
                if (pObj.HasKey("attractorStrength")) partNode->attractorStrength = pObj["attractorStrength"].AsFloat(5.0f);
                if (pObj.HasKey("enableCurlNoise")) partNode->enableCurlNoiseModule = pObj["enableCurlNoise"].AsBool(false);
                if (pObj.HasKey("curlNoiseStrength")) partNode->curlNoiseStrength = pObj["curlNoiseStrength"].AsFloat(1.5f);
                if (pObj.HasKey("curlNoiseFrequency")) partNode->curlNoiseFrequency = pObj["curlNoiseFrequency"].AsFloat(1.0f);
                if (pObj.HasKey("renderMode")) partNode->renderMode = static_cast<ParticleRenderMode>(pObj["renderMode"].AsInt(0));
                if (pObj.HasKey("emitterBoxExtents") && pObj["emitterBoxExtents"].arrVal.size() >= 3) {
                    partNode->emitterBoxExtents = glm::vec3(pObj["emitterBoxExtents"][0].AsFloat(1.0f), pObj["emitterBoxExtents"][1].AsFloat(1.0f), pObj["emitterBoxExtents"][2].AsFloat(1.0f));
                }
                if (pObj.HasKey("vortexAxis") && pObj["vortexAxis"].arrVal.size() >= 3) {
                    partNode->vortexAxis = glm::vec3(pObj["vortexAxis"][0].AsFloat(0.0f), pObj["vortexAxis"][1].AsFloat(1.0f), pObj["vortexAxis"][2].AsFloat(0.0f));
                }
                if (pObj.HasKey("attractorOffset") && pObj["attractorOffset"].arrVal.size() >= 3) {
                    partNode->attractorOffset = glm::vec3(pObj["attractorOffset"][0].AsFloat(0.0f), pObj["attractorOffset"][1].AsFloat(3.0f), pObj["attractorOffset"][2].AsFloat(0.0f));
                }
                if (pObj.HasKey("drag")) partNode->drag = pObj["drag"].AsFloat(0.0f);
                if (pObj.HasKey("startRotationMin")) partNode->startRotationMin = pObj["startRotationMin"].AsFloat(0.0f);
                if (pObj.HasKey("startRotationMax")) partNode->startRotationMax = pObj["startRotationMax"].AsFloat(0.0f);
                if (pObj.HasKey("angularVelocityMin")) partNode->angularVelocityMin = pObj["angularVelocityMin"].AsFloat(0.0f);
                if (pObj.HasKey("angularVelocityMax")) partNode->angularVelocityMax = pObj["angularVelocityMax"].AsFloat(0.0f);
                if (pObj.HasKey("builtinSheet")) partNode->builtinSheet = static_cast<ParticleSheet>(pObj["builtinSheet"].AsInt(0));
                if (pObj.HasKey("flipbookColumns")) partNode->flipbookColumns = pObj["flipbookColumns"].AsInt(1);
                if (pObj.HasKey("flipbookRows")) partNode->flipbookRows = pObj["flipbookRows"].AsInt(1);
                if (pObj.HasKey("flipbookFrames")) partNode->flipbookFrames = pObj["flipbookFrames"].AsInt(0);
                if (pObj.HasKey("flipbookMode")) partNode->flipbookMode = static_cast<ParticleFlipbookMode>(pObj["flipbookMode"].AsInt(0));
                if (pObj.HasKey("flipbookFps")) partNode->flipbookFps = pObj["flipbookFps"].AsFloat(24.0f);
                if (pObj.HasKey("flipbookCycles")) partNode->flipbookCycles = pObj["flipbookCycles"].AsFloat(1.0f);
                if (pObj.HasKey("flipbookRandomStart")) partNode->flipbookRandomStart = pObj["flipbookRandomStart"].AsBool(false);
                if (pObj.HasKey("flipbookBlend")) partNode->flipbookBlend = pObj["flipbookBlend"].AsBool(true);
                if (pObj.HasKey("softness")) partNode->softness = pObj["softness"].AsFloat(0.4f);
                if (pObj.HasKey("cameraFade")) partNode->cameraFade = pObj["cameraFade"].AsFloat(0.3f);
                if (pObj.HasKey("lit")) partNode->lit = pObj["lit"].AsBool(true);
                if (pObj.HasKey("glowIntensity")) partNode->glowIntensity = pObj["glowIntensity"].AsFloat(2.5f);
                if (pObj.HasKey("selfGlow")) partNode->selfGlow = pObj["selfGlow"].AsFloat(0.0f);
                if (pObj.HasKey("velocityStretch")) partNode->velocityStretch = pObj["velocityStretch"].AsFloat(0.0f);
                if (pObj.HasKey("distortionStrength")) partNode->distortionStrength = pObj["distortionStrength"].AsFloat(0.03f);
                if (pObj.HasKey("trailMode")) partNode->trailMode = static_cast<ParticleTrailMode>(pObj["trailMode"].AsInt(0));
                if (pObj.HasKey("trailLength")) partNode->trailLength = pObj["trailLength"].AsInt(16);
                if (pObj.HasKey("trailInterval")) partNode->trailInterval = pObj["trailInterval"].AsFloat(0.02f);
                if (pObj.HasKey("trailWidth")) partNode->trailWidth = pObj["trailWidth"].AsFloat(1.0f);
                if (pObj.HasKey("trailTaper")) partNode->trailTaper = pObj["trailTaper"].AsBool(true);
                if (pObj.HasKey("trailDrawParticles")) partNode->trailDrawParticles = pObj["trailDrawParticles"].AsBool(true);
                if (pObj.HasKey("trailTextureTiling")) partNode->trailTextureTiling = pObj["trailTextureTiling"].AsFloat(1.0f);
                if (pObj.HasKey("collisionMode")) partNode->collisionMode = static_cast<ParticleCollisionMode>(pObj["collisionMode"].AsInt(0));
                if (pObj.HasKey("collisionPlaneHeight")) partNode->collisionPlaneHeight = pObj["collisionPlaneHeight"].AsFloat(0.0f);
                if (pObj.HasKey("collideWithWater")) partNode->collideWithWater = pObj["collideWithWater"].AsBool(true);
                if (pObj.HasKey("collisionRadiusScale")) partNode->collisionRadiusScale = pObj["collisionRadiusScale"].AsFloat(0.5f);
                if (pObj.HasKey("bounce")) partNode->bounce = pObj["bounce"].AsFloat(0.35f);
                if (pObj.HasKey("friction")) partNode->friction = pObj["friction"].AsFloat(0.25f);
                if (pObj.HasKey("killOnCollision")) partNode->killOnCollision = pObj["killOnCollision"].AsBool(false);
                if (pObj.HasKey("lifetimeLossOnHit")) partNode->lifetimeLossOnHit = pObj["lifetimeLossOnHit"].AsFloat(0.0f);
                if (pObj.HasKey("hitSpawnCount")) partNode->hitSpawnCount = pObj["hitSpawnCount"].AsInt(0);
                if (pObj.HasKey("hitSpawnSpeed")) partNode->hitSpawnSpeed = pObj["hitSpawnSpeed"].AsFloat(2.0f);
                if (pObj.HasKey("hitSpawnSize")) partNode->hitSpawnSize = pObj["hitSpawnSize"].AsFloat(0.4f);
            }
        } else if (node->nodeType == NodeType::AudioSource && n.HasKey("audio")) {
            auto audioNode = std::dynamic_pointer_cast<AudioNode>(node);
            if (audioNode) {
                const auto& aObj = n["audio"];
                if (aObj.HasKey("clipPath")) audioNode->audioClipPath = aObj["clipPath"].AsString();
                if (aObj.HasKey("busName")) audioNode->busName = aObj["busName"].AsString("SFX");
                if (aObj.HasKey("is3DSpatial")) audioNode->is3DSpatial = aObj["is3DSpatial"].AsBool(true);
                if (aObj.HasKey("volume")) audioNode->volume = aObj["volume"].AsFloat(1.0f);
                if (aObj.HasKey("pitch")) audioNode->pitch = aObj["pitch"].AsFloat(1.0f);
                if (aObj.HasKey("loop")) audioNode->loop = aObj["loop"].AsBool(false);
                if (aObj.HasKey("playOnAwake")) audioNode->playOnAwake = aObj["playOnAwake"].AsBool(true);
                if (aObj.HasKey("attenuationModel")) audioNode->attenuationModel = static_cast<AudioAttenuationModel>(aObj["attenuationModel"].AsInt(0));
                if (aObj.HasKey("minDistance")) audioNode->minDistance = aObj["minDistance"].AsFloat(1.0f);
                if (aObj.HasKey("maxDistance")) audioNode->maxDistance = aObj["maxDistance"].AsFloat(30.0f);
                if (aObj.HasKey("rolloffFactor")) audioNode->rolloffFactor = aObj["rolloffFactor"].AsFloat(1.0f);
                if (aObj.HasKey("isReverbZone")) audioNode->isReverbZone = aObj["isReverbZone"].AsBool(false);
                if (aObj.HasKey("reverbInnerRadius")) audioNode->reverbInnerRadius = aObj["reverbInnerRadius"].AsFloat(3.0f);
                if (aObj.HasKey("reverbOuterRadius")) audioNode->reverbOuterRadius = aObj["reverbOuterRadius"].AsFloat(12.0f);
                if (aObj.HasKey("reverbRoomSize")) audioNode->reverbRoomSize = aObj["reverbRoomSize"].AsFloat(0.65f);
                if (aObj.HasKey("reverbDecayTime")) audioNode->reverbDecayTime = aObj["reverbDecayTime"].AsFloat(1.8f);
                if (aObj.HasKey("reverbDamping")) audioNode->reverbDamping = aObj["reverbDamping"].AsFloat(0.4f);
                if (aObj.HasKey("reverbWetMix")) audioNode->reverbWetMix = aObj["reverbWetMix"].AsFloat(0.35f);
            }
        } else if (node->nodeType == NodeType::Terrain && n.HasKey("terrain")) {
            auto tNode = std::dynamic_pointer_cast<TerrainNode>(node);
            if (tNode) {
                const auto& tObj = n["terrain"];
                if (tObj.HasKey("gridResolution")) tNode->gridResolution = tObj["gridResolution"].AsUInt(64);
                if (tObj.HasKey("sizeX") && tObj.HasKey("sizeZ")) {
                    tNode->terrainSize = glm::vec2(tObj["sizeX"].AsFloat(100.0f), tObj["sizeZ"].AsFloat(100.0f));
                }
                if (tObj.HasKey("heightScale")) tNode->heightScale = tObj["heightScale"].AsFloat(20.0f);
                if (tObj.HasKey("uvTiling")) tNode->uvTiling = tObj["uvTiling"].AsFloat(16.0f);
                if (tObj.HasKey("heightmapPath")) tNode->heightmapPath = tObj["heightmapPath"].AsString();
                if (tObj.HasKey("heightData") && tObj["heightData"].arrVal.size() == tNode->gridResolution * tNode->gridResolution) {
                    tNode->heightData.resize(tObj["heightData"].arrVal.size());
                    for (size_t i = 0; i < tObj["heightData"].arrVal.size(); ++i) {
                        tNode->heightData[i] = tObj["heightData"][i].AsFloat(0.0f);
                    }
                }
                if (tObj.HasKey("layers")) {
                    tNode->layers.clear();
                    for (const auto& l : tObj["layers"].arrVal) {
                        TerrainLayer layer;
                        layer.name = l["name"].AsString();
                        layer.texturePath = l["texture"].AsString();
                        if (l.HasKey("tint") && l["tint"].arrVal.size() >= 3) {
                            layer.tint = glm::vec3(l["tint"][0].AsFloat(0.5f), l["tint"][1].AsFloat(0.5f), l["tint"][2].AsFloat(0.5f));
                        }
                        layer.tiling = l["tiling"].AsFloat(4.0f);
                        layer.roughness = l["roughness"].AsFloat(0.85f);
                        if (tNode->layers.size() < static_cast<size_t>(TerrainNode::kMaxLayers)) tNode->layers.push_back(layer);
                    }
                }
                if (tObj.HasKey("splatResolution")) tNode->splatResolution = tObj["splatResolution"].AsUInt(256);
                {
                    std::vector<uint8_t> splat = tObj.HasKey("splat") ? TerrainBase64Decode(tObj["splat"].AsString()) : std::vector<uint8_t>{};
                    if (splat.size() == static_cast<size_t>(tNode->splatResolution) * tNode->splatResolution * 4) {
                        tNode->splatData = std::move(splat);
                        ++tNode->splatVersion;
                    } else {
                        tNode->InitSplat();
                    }
                }
                if (tObj.HasKey("foliageTypes")) {
                    tNode->foliageTypes.clear();
                    for (const auto& t : tObj["foliageTypes"].arrVal) {
                        FoliageType type;
                        type.name = t["name"].AsString();
                        type.builtIn = t["builtIn"].AsInt(0);
                        type.modelMesh = t["model"].AsString();
                        if (t.HasKey("color") && t["color"].arrVal.size() >= 4) {
                            type.color = glm::vec4(t["color"][0].AsFloat(1.0f), t["color"][1].AsFloat(1.0f), t["color"][2].AsFloat(1.0f), t["color"][3].AsFloat(1.0f));
                        }
                        type.minScale = t["minScale"].AsFloat(0.8f);
                        type.maxScale = t["maxScale"].AsFloat(1.25f);
                        type.density = t["density"].AsFloat(2.0f);
                        type.maxSlope = t["maxSlope"].AsFloat(35.0f);
                        type.alignToSlope = t["alignToSlope"].AsBool(true);
                        type.castShadows = t["castShadows"].AsBool(true);
                        type.wind = t["wind"].AsBool(true);
                        type.collision = t["collision"].AsBool(false);
                        type.drawDistance = t["drawDistance"].AsFloat(150.0f);
                        tNode->foliageTypes.push_back(type);
                    }
                }
                if (tObj.HasKey("foliage")) {
                    const auto& arr = tObj["foliage"].arrVal;
                    tNode->foliage.clear();
                    for (size_t i = 0; i + 4 < arr.size(); i += 5) {
                        FoliageInstance f;
                        f.local = glm::vec2(arr[i].AsFloat(0.0f), arr[i + 1].AsFloat(0.0f));
                        f.rotation = arr[i + 2].AsFloat(0.0f);
                        f.scale = arr[i + 3].AsFloat(1.0f);
                        f.type = static_cast<uint32_t>(std::max(arr[i + 4].AsInt(0), 0));
                        tNode->foliage.push_back(f);
                    }
                    tNode->NotifyFoliageChanged();
                }
                tNode->isMeshDirty = true;
            }
        }

        if (node->IsUINode() && n.HasKey("ui")) {
            const auto& ui = n["ui"];
            if (ui.HasKey("position") && ui["position"].arrVal.size() >= 2) {
                node->uiPosition = glm::vec2(ui["position"][0].AsFloat(), ui["position"][1].AsFloat());
            }
            if (ui.HasKey("size") && ui["size"].arrVal.size() >= 2) {
                node->uiSize = glm::vec2(ui["size"][0].AsFloat(180.0f), ui["size"][1].AsFloat(44.0f));
            }
            node->uiAnchor = StringToUIAnchor(ui["anchor"].AsString("Center"));
            node->uiText = ui["text"].AsString("Button");
            if (ui.HasKey("locKey")) node->uiLocKey = ui["locKey"].AsString();
            node->uiPlaceholder = ui["placeholder"].AsString("Enter text...");
            node->uiFontSize = ui["fontSize"].AsFloat(16.0f);
            if (ui.HasKey("fontPath")) node->uiFontPath = ui["fontPath"].AsString();
            if (ui.HasKey("color") && ui["color"].arrVal.size() >= 4) {
                node->uiColor = glm::vec4(ui["color"][0].AsFloat(), ui["color"][1].AsFloat(), ui["color"][2].AsFloat(), ui["color"][3].AsFloat());
            }
            if (ui.HasKey("textColor") && ui["textColor"].arrVal.size() >= 4) {
                node->uiTextColor = glm::vec4(ui["textColor"][0].AsFloat(), ui["textColor"][1].AsFloat(), ui["textColor"][2].AsFloat(), ui["textColor"][3].AsFloat());
            }
            if (ui.HasKey("hoverColor") && ui["hoverColor"].arrVal.size() >= 4) {
                node->uiHoverColor = glm::vec4(ui["hoverColor"][0].AsFloat(), ui["hoverColor"][1].AsFloat(), ui["hoverColor"][2].AsFloat(), ui["hoverColor"][3].AsFloat());
            }
            if (ui.HasKey("pressColor") && ui["pressColor"].arrVal.size() >= 4) {
                node->uiPressColor = glm::vec4(ui["pressColor"][0].AsFloat(), ui["pressColor"][1].AsFloat(), ui["pressColor"][2].AsFloat(), ui["pressColor"][3].AsFloat());
            }
            if (ui.HasKey("borderColor") && ui["borderColor"].arrVal.size() >= 4) {
                node->uiBorderColor = glm::vec4(ui["borderColor"][0].AsFloat(), ui["borderColor"][1].AsFloat(), ui["borderColor"][2].AsFloat(), ui["borderColor"][3].AsFloat());
            }
            node->uiBorderThickness = ui["borderThickness"].AsFloat(1.0f);
            node->uiRounding = ui["rounding"].AsFloat(4.0f);
            node->uiProgress = ui["progress"].AsFloat(0.5f);
            node->uiValue = ui["value"].AsFloat(50.0f);
            node->uiMinVal = ui["minVal"].AsFloat(0.0f);
            node->uiMaxVal = ui["maxVal"].AsFloat(100.0f);
            node->uiChecked = ui["checked"].AsBool(false);
            node->uiRadioGroup = ui["radioGroup"].AsString("DefaultGroup");
            node->uiSelectedIndex = ui["selectedIndex"].AsInt(0);
            if (ui.HasKey("items") && ui["items"].IsArray()) {
                node->uiItems.clear();
                for (const auto& itemVal : ui["items"].arrVal) {
                    node->uiItems.push_back(itemVal.AsString());
                }
            }
            node->uiWindowMovable = ui["windowMovable"].AsBool(true);
            node->uiWindowClosable = ui["windowClosable"].AsBool(true);
            node->uiWindowIsOpen = ui["windowIsOpen"].AsBool(true);
            node->uiShapeType = StringToUIShapeType(ui["shapeType"].AsString("Rectangle"));
            node->uiVisible = ui["visible"].AsBool(true);
            node->uiEnabled = ui["enabled"].AsBool(true);
            node->uiOnClickAction = ui["action"].AsString();

            if (node->nodeType == NodeType::UI_BoxContainer) {
                auto boxNode = std::dynamic_pointer_cast<UIBoxContainerNode>(node);
                if (boxNode && ui.HasKey("boxOrientation")) {
                    boxNode->orientation = StringToBoxOrientation(ui["boxOrientation"].AsString("Vertical"));
                    boxNode->spacing = ui["spacing"].AsFloat(8.0f);
                    boxNode->padding = ui["padding"].AsFloat(8.0f);
                    boxNode->autoResize = ui["autoResize"].AsBool(true);
                }
            } else if (node->nodeType == NodeType::UI_GridContainer) {
                auto gridNode = std::dynamic_pointer_cast<UIGridContainerNode>(node);
                if (gridNode && ui.HasKey("gridColumns")) {
                    gridNode->columns = ui["gridColumns"].AsInt(4);
                    gridNode->cellWidth = ui["cellWidth"].AsFloat(60.0f);
                    gridNode->cellHeight = ui["cellHeight"].AsFloat(60.0f);
                    gridNode->horizontalSpacing = ui["horizontalSpacing"].AsFloat(8.0f);
                    gridNode->verticalSpacing = ui["verticalSpacing"].AsFloat(8.0f);
                    gridNode->padding = ui["padding"].AsFloat(8.0f);
                    gridNode->autoResize = ui["autoResize"].AsBool(true);
                }
            }
        }

        if (node->type == ShapeType::Custom) {
            if (n.HasKey("boundsMin") && n["boundsMin"].arrVal.size() >= 3) {
                node->boundsMin = glm::vec3(n["boundsMin"][0].AsFloat(-1.0f), n["boundsMin"][1].AsFloat(-1.0f), n["boundsMin"][2].AsFloat(-1.0f));
            }
            if (n.HasKey("boundsMax") && n["boundsMax"].arrVal.size() >= 3) {
                node->boundsMax = glm::vec3(n["boundsMax"][0].AsFloat(1.0f), n["boundsMax"][1].AsFloat(1.0f), n["boundsMax"][2].AsFloat(1.0f));
            }
            node->vertexCount = n["vertexCount"].AsUInt(0);
            node->triangleCount = n["triangleCount"].AsUInt(0);

            // (terrain meshes are generated from the heights, not loaded)
            if (loadMeshCallback && !node->customMeshName.empty() && node->nodeType != NodeType::Terrain) {
                loadMeshCallback(node->customMeshName, node->customMeshName);
            }
        }

        m_nodes.push_back(node);
        if (node->id >= m_nextId) m_nextId = node->id + 1;
    }

    // Reconstruct children IDs if they were not explicitly serialized
    for (const auto& node : m_nodes) {
        if (node->parentId != 0) {
            auto parent = GetNodeById(node->parentId);
            if (parent) {
                if (std::find(parent->childrenIds.begin(), parent->childrenIds.end(), node->id) == parent->childrenIds.end()) {
                    parent->childrenIds.push_back(node->id);
                }
            }
        }
    }

    if (!m_nodes.empty()) {
        m_selectedId = m_nodes[0]->id;
    }

    m_cutscenes.clear();
    if (json.HasKey("cutscenes") && json["cutscenes"].IsArray()) {
        for (const auto& csJson : json["cutscenes"].arrVal) {
            Animation::CutsceneSequence seq;
            seq.Deserialize(csJson);
            m_cutscenes.push_back(seq);
        }
    }
    if (false) {
    }
    return true;
}

bool Scene::SaveToFile(const std::string& filePath) const {
    JsonValue json = Serialize();
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Scene] Failed to open file for saving: " << filePath << std::endl;
        return false;
    }
    file << json.Serialize(2);
    file.close();
    std::cout << "[Scene] Successfully saved scene to " << filePath << std::endl;
    return true;
}

bool Scene::LoadFromFile(const std::string& filePath, std::function<bool(const std::string&, const std::string&)> loadMeshCallback) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[Scene] Failed to open scene file: " << filePath << std::endl;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    JsonValue root = JsonValue::Parse(content);
    if (!root.IsObject()) {
        std::cerr << "[Scene] Invalid JSON in scene file: " << filePath << std::endl;
        return false;
    }

    bool success = Deserialize(root, loadMeshCallback);
    if (success) {
        m_filePath = filePath;
        // Derive name from filename if not set
        std::filesystem::path p(filePath);
        m_name = p.stem().string();
    }
    return success;
}

std::shared_ptr<Scene> Scene::Clone() const {
    auto cloneScene = std::make_shared<Scene>();
    cloneScene->m_name = m_name;
    cloneScene->m_filePath = m_filePath;
    cloneScene->m_nextId = m_nextId;
    cloneScene->m_selectedId = m_selectedId;
    cloneScene->m_activeCameraId = m_activeCameraId;
    cloneScene->m_environment = m_environment;
    cloneScene->m_cutscenes = m_cutscenes;

    cloneScene->m_nodes.clear();
    for (const auto& node : m_nodes) {
        auto clonedNode = node->Clone();
        clonedNode->clothSim.reset(); // simulated afresh in the clone (never shared between scenes)
        cloneScene->m_nodes.push_back(clonedNode);
    }
    return cloneScene;
}

void Scene::CopyFrom(const Scene& other) {
    m_name = other.m_name;
    m_filePath = other.m_filePath;
    m_nextId = other.m_nextId;
    m_selectedId = other.m_selectedId;
    m_activeCameraId = other.m_activeCameraId;
    m_environment = other.m_environment;
    m_cutscenes = other.m_cutscenes;

    m_nodes.clear();
    for (const auto& node : other.m_nodes) {
        m_nodes.push_back(node->Clone());
    }
}

Animation::CutsceneSequence* Scene::FindCutscene(const std::string& name) {
    for (auto& cs : m_cutscenes) {
        if (cs.name == name) return &cs;
    }
    return nullptr;
}

const Animation::CutsceneSequence* Scene::FindCutscene(const std::string& name) const {
    for (const auto& cs : m_cutscenes) {
        if (cs.name == name) return &cs;
    }
    return nullptr;
}

Animation::CutsceneSequence& Scene::AddCutscene(const Animation::CutsceneSequence& seq) {
    for (auto& cs : m_cutscenes) {
        if (cs.name == seq.name) {
            cs = seq;
            return cs;
        }
    }
    m_cutscenes.push_back(seq);
    return m_cutscenes.back();
}

bool Scene::RemoveCutscene(const std::string& name) {
    auto it = std::remove_if(m_cutscenes.begin(), m_cutscenes.end(), [&](const Animation::CutsceneSequence& cs) {
        return cs.name == name;
    });
    if (it == m_cutscenes.end()) return false;
    if (true) {
        m_cutscenes.erase(it, m_cutscenes.end());
        return true;
    }
}
