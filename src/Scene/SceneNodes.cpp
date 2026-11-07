#include <iostream>
#include <filesystem>
#include "Scene.h"
#include "Node.h"
#include "Nodes/AllNodes.h"

std::shared_ptr<Node> Scene::AddNode(ShapeType type, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<MeshNode>(type, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddCharacterNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<CharacterNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddCameraNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<CameraNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    if (m_activeCameraId == 0) {
        m_activeCameraId = node->id;
    }
    return node;
}

std::shared_ptr<Node> Scene::AddLightNode(LightType type, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<LightNode>(type, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddColliderNode(ColliderType colType, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<ColliderNode>(colType, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddEmptyNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<EmptyNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUICanvasNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UICanvasNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIPanelNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIPanelNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUITextNode(const std::string& text, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UITextNode>(text, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIButtonNode(const std::string& label, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIButtonNode>(label, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIProgressBarNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIProgressBarNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUISpinnerNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UISpinnerNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUITextFieldNode(const std::string& placeholder, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UITextFieldNode>(placeholder, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIPasswordFieldNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIPasswordFieldNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUISliderNode(float minVal, float maxVal, float curVal, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UISliderNode>(minVal, maxVal, curVal, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUICheckboxNode(const std::string& label, bool checked, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UICheckboxNode>(label, checked, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIRadioButtonNode(const std::string& label, const std::string& group, bool checked, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIRadioButtonNode>(label, group, checked, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIWindowNode(const std::string& title, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIWindowNode>(title, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIImageNode(const std::string& texturePath, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIImageNode>(texturePath, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIVideoNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIVideoNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIScrollPaneNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIScrollPaneNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIDropdownNode(const std::vector<std::string>& items, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIDropdownNode>(items, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIDatePickerNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIDatePickerNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUISelectableListNode(const std::vector<std::string>& items, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UISelectableListNode>(items, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIColorPickerNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIColorPickerNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUITableNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UITableNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUITabsNode(const std::vector<std::string>& tabs, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UITabsNode>(tabs, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIMenuBarNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIMenuBarNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIShapeNode(UIShapeType shape, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIShapeNode>(shape, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddCustomMeshNode(const std::string& meshName,
                                              const glm::vec3& bMin,
                                              const glm::vec3& bMax,
                                              uint32_t vertCount,
                                              uint32_t triCount,
                                              const std::string& nodeName,
                                              uint32_t parentId) {
    auto node = std::make_shared<MeshNode>(ShapeType::Custom, nodeName.empty() ? meshName : nodeName, parentId);
    node->id = m_nextId++;
    node->customMeshName = meshName;
    node->boundsMin = bMin;
    node->boundsMax = bMax;
    node->vertexCount = vertCount;
    node->triangleCount = triCount;
    node->position = glm::vec3(0.0f, 0.0f, 0.0f);
    node->rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    node->scale = glm::vec3(1.0f, 1.0f, 1.0f);
    node->color = glm::vec4(0.85f, 0.82f, 0.80f, 1.0f);

    m_nodes.push_back(node);

    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) {
            parent->childrenIds.push_back(node->id);
        }
    }

    m_selectedId = (int32_t)node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddSplineNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<SplineNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddDecalNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<DecalNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddParticleNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<ParticleNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddAudioNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<AudioNode>(name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddTerrainNode(const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<TerrainNode>(name, parentId);
    node->id = m_nextId++;
    node->customMeshName = node->GetTerrainMeshKey();
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

bool Scene::CopyNode(uint32_t id) {
    auto root = GetNodeById(id);
    if (!root) return false;

    m_clipboardNodes.clear();
    std::vector<uint32_t> subtreeIds;
    CollectSubtreeIds(id, subtreeIds);

    for (uint32_t sid : subtreeIds) {
        auto n = GetNodeById(sid);
        if (n) {
            m_clipboardNodes.push_back(n->Clone());
        }
    }
    return !m_clipboardNodes.empty();
}

bool Scene::CopySelected() {
    if (m_selectedId < 0) return false;
    return CopyNode(static_cast<uint32_t>(m_selectedId));
}

std::shared_ptr<Node> Scene::PasteNode(uint32_t targetParentId, const glm::vec3& positionOffset) {
    if (m_clipboardNodes.empty()) return nullptr;

    std::map<uint32_t, uint32_t> oldToNewId;
    std::vector<std::shared_ptr<Node>> pastedNodes;

    for (const auto& clipNode : m_clipboardNodes) {
        auto cloned = clipNode->Clone();
        uint32_t newId = m_nextId++;
        oldToNewId[clipNode->id] = newId;
        cloned->id = newId;
        cloned->childrenIds.clear();
        pastedNodes.push_back(cloned);
    }

    // Remap parent links and rebuild childrenIds
    std::shared_ptr<Node> pastedRoot = pastedNodes[0];
    for (size_t i = 0; i < pastedNodes.size(); ++i) {
        auto& n = pastedNodes[i];
        if (i == 0) {
            n->parentId = targetParentId;
            n->name = n->name + " (Copy)";
            if (n->IsUINode()) {
                n->uiPosition += glm::vec2(positionOffset.x * 20.0f, positionOffset.z * 20.0f);
            } else {
                n->position += positionOffset;
            }
        } else {
            auto it = oldToNewId.find(m_clipboardNodes[i]->parentId);
            n->parentId = (it != oldToNewId.end()) ? it->second : pastedRoot->id;
        }
        m_nodes.push_back(n);
    }

    for (auto& n : pastedNodes) {
        if (n->parentId != 0) {
            auto parent = GetNodeById(n->parentId);
            if (parent) {
                parent->childrenIds.push_back(n->id);
            }
        }
    }

    m_selectedId = static_cast<int32_t>(pastedRoot->id);
    return pastedRoot;
}

std::shared_ptr<Node> Scene::DuplicateNode(uint32_t id, const glm::vec3& positionOffset) {
    auto orig = GetNodeById(id);
    if (!orig) return nullptr;
    if (!CopyNode(id)) return nullptr;
    return PasteNode(orig->parentId, positionOffset);
}


std::shared_ptr<Node> Scene::AddUIBoxContainerNode(BoxOrientation orientation, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIBoxContainerNode>(orientation, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::AddUIGridContainerNode(int columns, const std::string& name, uint32_t parentId) {
    auto node = std::make_shared<UIGridContainerNode>(columns, name, parentId);
    node->id = m_nextId++;
    m_nodes.push_back(node);
    if (parentId != 0) {
        auto parent = GetNodeById(parentId);
        if (parent) parent->childrenIds.push_back(node->id);
    }
    m_selectedId = node->id;
    return node;
}

std::shared_ptr<Node> Scene::InstantiateScene(const std::string& sceneFilePath, uint32_t parentId, const glm::vec3& positionOffset) {
    Scene tempScene;
    if (!tempScene.LoadFromFile(sceneFilePath)) {
        if (!tempScene.LoadFromFile("scenes/" + sceneFilePath)) {
            std::cerr << "[Scene] Failed to load scene file for instancing: " << sceneFilePath << std::endl;
            return nullptr;
        }
    }

    const auto& srcNodes = tempScene.GetNodes();
    if (srcNodes.empty()) return nullptr;

    std::string prefabName = std::filesystem::path(sceneFilePath).stem().string();
    if (prefabName.empty()) prefabName = "SceneInstance";

    auto instanceRoot = AddEmptyNode(prefabName, parentId);
    instanceRoot->isSceneInstanceRoot = true;
    instanceRoot->instanceScenePath = sceneFilePath;
    instanceRoot->position += positionOffset;

    std::map<uint32_t, uint32_t> oldToNewId;
    std::vector<std::shared_ptr<Node>> clonedNodes;

    for (const auto& srcNode : srcNodes) {
        auto cloned = srcNode->Clone();
        uint32_t oldId = srcNode->id;
        uint32_t newId = m_nextId++;
        cloned->id = newId;
        cloned->childrenIds.clear();
        oldToNewId[oldId] = newId;
        clonedNodes.push_back(cloned);
    }

    for (size_t i = 0; i < clonedNodes.size(); ++i) {
        auto& cloned = clonedNodes[i];
        uint32_t srcParentId = srcNodes[i]->parentId;
        if (srcParentId == 0) {
            cloned->parentId = instanceRoot->id;
            instanceRoot->childrenIds.push_back(cloned->id);
        } else {
            auto it = oldToNewId.find(srcParentId);
            cloned->parentId = (it != oldToNewId.end()) ? it->second : instanceRoot->id;
        }
        m_nodes.push_back(cloned);
    }

    for (auto& cloned : clonedNodes) {
        if (cloned->parentId != 0 && cloned->parentId != instanceRoot->id) {
            auto parent = GetNodeById(cloned->parentId);
            if (parent) {
                parent->childrenIds.push_back(cloned->id);
            }
        }
    }

    m_selectedId = static_cast<int32_t>(instanceRoot->id);
    return instanceRoot;
}

bool Scene::ReloadSceneInstance(uint32_t instanceRootId) {
    auto instanceRoot = GetNodeById(instanceRootId);
    if (!instanceRoot || !instanceRoot->isSceneInstanceRoot || instanceRoot->instanceScenePath.empty()) {
        return false;
    }

    std::vector<uint32_t> childrenToRemove = instanceRoot->childrenIds;
    for (uint32_t cid : childrenToRemove) {
        DeleteNode(cid);
    }
    instanceRoot->childrenIds.clear();

    Scene tempScene;
    if (!tempScene.LoadFromFile(instanceRoot->instanceScenePath)) {
        if (!tempScene.LoadFromFile("scenes/" + instanceRoot->instanceScenePath)) {
            return false;
        }
    }

    const auto& srcNodes = tempScene.GetNodes();
    if (srcNodes.empty()) return true;

    std::map<uint32_t, uint32_t> oldToNewId;
    std::vector<std::shared_ptr<Node>> clonedNodes;

    for (const auto& srcNode : srcNodes) {
        auto cloned = srcNode->Clone();
        uint32_t oldId = srcNode->id;
        uint32_t newId = m_nextId++;
        cloned->id = newId;
        cloned->childrenIds.clear();
        oldToNewId[oldId] = newId;
        clonedNodes.push_back(cloned);
    }

    for (size_t i = 0; i < clonedNodes.size(); ++i) {
        auto& cloned = clonedNodes[i];
        uint32_t srcParentId = srcNodes[i]->parentId;
        if (srcParentId == 0) {
            cloned->parentId = instanceRoot->id;
            instanceRoot->childrenIds.push_back(cloned->id);
        } else {
            auto it = oldToNewId.find(srcParentId);
            cloned->parentId = (it != oldToNewId.end()) ? it->second : instanceRoot->id;
        }
        m_nodes.push_back(cloned);
    }

    for (auto& cloned : clonedNodes) {
        if (cloned->parentId != 0 && cloned->parentId != instanceRoot->id) {
            auto parent = GetNodeById(cloned->parentId);
            if (parent) {
                parent->childrenIds.push_back(cloned->id);
            }
        }
    }

    return true;
}
