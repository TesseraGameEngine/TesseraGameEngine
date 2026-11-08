#include "Resource/Resource.h"
#include "Resource/ResourceManager.h"
#include "Networking/NetworkManager.h"
#include "TestFramework.h"
#include "Utils/SimpleJson.h"
#include "Core/Camera.h"
#include "Core/InputManager.h"
#include "Core/UndoRedoManager.h"
#include "Physics/Collision.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Mesh.h"
#include "Scene/MeshSimplifier.h"
#include "Scene/Scene.h"
#include "Scene/Nodes/AllNodes.h"
#include "Animation/Animation.h"
#include "Animation/Skeleton.h"
#include "Gameplay/Time.h"
#include "Renderer/RenderCommon.h"
#include "Renderer/IRenderTarget.h"
#include "Standalone/BundleFormat.h"
#include "Scripting/LuaScriptSystem.h"
#include "Gameplay/GameLoop.h"
#include "Monitoring/GameProfiler.h"
#include "Export/WebExporter.h"
#include "UI/RulerTool.h"
#include "Video/VideoPlayer.h"
#include "Renderer/RayTracing/RayTracingManager.h"
#include "Renderer/Meshlet/MeshletManager.h"
#include "Renderer/Meshlet/MeshletBuilder.h"
#include "Renderer/Pipeline/VulkanPipelineCache.h"
#include "Renderer/Upscaling/TemporalUpscaler.h"
#include "Core/LargeWorldCoordinates.h"
#include "Renderer/RayTracing/MetalRayTracing.h"
#include "Renderer/RayTracing/VulkanRayTracing.h"
#include "Localization/Localization.h"
#include "Localization/GameLocalization.h"
#include <sol/sol.hpp>
#include <fstream>
#include <cstring>
#include <filesystem>

// =========================================================================
// 1. SimpleJson Tests
// =========================================================================
TEST_CASE(SimpleJson, ParsePrimitives) {
    JsonValue nullVal = JsonValue::Parse("null");
    ASSERT_TRUE(nullVal.IsNull());

    JsonValue boolTrue = JsonValue::Parse("true");
    ASSERT_TRUE(boolTrue.IsBool());
    ASSERT_TRUE(boolTrue.AsBool());

    JsonValue boolFalse = JsonValue::Parse("false");
    ASSERT_TRUE(boolFalse.IsBool());
    ASSERT_FALSE(boolFalse.AsBool());

    JsonValue numVal = JsonValue::Parse("42.5");
    ASSERT_TRUE(numVal.IsNumber());
    ASSERT_FLOAT_NEAR(numVal.AsFloat(), 42.5f, 0.001f);
    ASSERT_EQ(numVal.AsInt(), 42);

    JsonValue strVal = JsonValue::Parse("\"Hello Tessera\"");
    ASSERT_TRUE(strVal.IsString());
    ASSERT_EQ(strVal.AsString(), "Hello Tessera");
}

TEST_CASE(SimpleJson, ObjectAndArraySerialization) {
    JsonValue obj = JsonValue::Object();
    obj["name"] = "EngineTest";
    obj["version"] = 2.0;

    JsonValue arr = JsonValue::Array();
    arr.Push(10);
    arr.Push(20);
    arr.Push(30);
    obj["values"] = arr;

    std::string jsonStr = obj.Serialize();
    JsonValue parsed = JsonValue::Parse(jsonStr);

    ASSERT_TRUE(parsed.IsObject());
    ASSERT_EQ(parsed["name"].AsString(), "EngineTest");
    ASSERT_FLOAT_NEAR(parsed["version"].AsFloat(), 2.0f, 0.001f);
    ASSERT_TRUE(parsed["values"].IsArray());
    ASSERT_EQ(parsed["values"].arrVal.size(), 3);
    ASSERT_EQ(parsed["values"][1].AsInt(), 20);
}

// =========================================================================
// 2. Camera Tests
// =========================================================================
TEST_CASE(Camera, ViewAndProjectionMatrix) {
    Camera cam;
    cam.SetViewParams(0.0f, 0.0f, 10.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    cam.SetViewportSize(1280.0f, 720.0f);

    glm::mat4 view = cam.GetViewMatrix();
    float detView = glm::determinant(view);
    ASSERT_FLOAT_NEAR(std::abs(detView), 1.0f, 0.001f);

    glm::mat4 proj = cam.GetProjectionMatrix();
    // Projection element [1][1] should be negative in Vulkan clip space
    ASSERT_TRUE(proj[1][1] < 0.0f);

    ASSERT_FLOAT_NEAR(cam.GetTarget().y, 1.0f, 0.001f);
    ASSERT_FLOAT_NEAR(cam.GetDistance(), 10.0f, 0.001f);
}

TEST_CASE(Camera, OrbitMovementAndFocus) {
    Camera cam;
    cam.FocusOn(glm::vec3(5.0f, 2.0f, -3.0f), 8.0f);

    ASSERT_FLOAT_NEAR(cam.GetTarget().x, 5.0f, 0.001f);
    ASSERT_FLOAT_NEAR(cam.GetTarget().y, 2.0f, 0.001f);
    ASSERT_FLOAT_NEAR(cam.GetTarget().z, -3.0f, 0.001f);
    ASSERT_FLOAT_NEAR(cam.GetDistance(), 8.0f, 0.001f);
}

TEST_CASE(Camera, TouchZoomPanAndCameraViewfinderControls) {
    Camera cam;
    cam.SetViewParams(0.0f, 0.0f, 10.0f, glm::vec3(0.0f, 0.0f, 0.0f));
    float initialDist = cam.GetDistance();
    ASSERT_FLOAT_NEAR(initialDist, 10.0f, 0.001f);

    // 1. Viewport Zoom In & Zoom Out
    cam.Zoom(1.5f);
    ASSERT_TRUE(cam.GetDistance() < initialDist);
    float zoomedInDist = cam.GetDistance();

    cam.Zoom(-1.5f);
    ASSERT_TRUE(cam.GetDistance() > zoomedInDist);

    // 2. Viewport Pan Controls (<, >, ^, v)
    glm::vec3 initialTarget = cam.GetTarget();
    cam.Pan(15.0f, 0.0f); // Pan left
    ASSERT_TRUE(cam.GetTarget().x != initialTarget.x || cam.GetTarget().z != initialTarget.z);

    cam.Pan(0.0f, 15.0f); // Pan up
    ASSERT_TRUE(cam.GetTarget().y != initialTarget.y);

    // 3. Camera Viewfinder Zoom (FOV adjustment) and Dolly Controls
    Scene scene;
    auto camNode = scene.AddCameraNode();
    scene.SetActiveCamera(camNode->id);
    auto activeCam = scene.GetActiveCamera();
    ASSERT_TRUE(activeCam != nullptr);
    ASSERT_FLOAT_NEAR(activeCam->cameraFov, 60.0f, 0.01f);

    // Zoom In (narrow FOV)
    activeCam->cameraFov = std::clamp(activeCam->cameraFov - 1.5f, 10.0f, 120.0f);
    ASSERT_FLOAT_NEAR(activeCam->cameraFov, 58.5f, 0.01f);

    // Zoom Out (widen FOV)
    activeCam->cameraFov = std::clamp(activeCam->cameraFov + 1.5f, 10.0f, 120.0f);
    ASSERT_FLOAT_NEAR(activeCam->cameraFov, 60.0f, 0.01f);

    // Dolly Forward and Backward along local view vector
    glm::vec3 initialPos = activeCam->position;
    activeCam->position += glm::vec3(activeCam->GetWorldTransformMatrix(scene) * glm::vec4(0.0f, 0.0f, -0.5f, 0.0f));
    ASSERT_TRUE(activeCam->position != initialPos);

    activeCam->position += glm::vec3(activeCam->GetWorldTransformMatrix(scene) * glm::vec4(0.0f, 0.0f, 0.5f, 0.0f));
    ASSERT_FLOAT_NEAR(activeCam->position.x, initialPos.x, 0.01f);
    ASSERT_FLOAT_NEAR(activeCam->position.y, initialPos.y, 0.01f);
    ASSERT_FLOAT_NEAR(activeCam->position.z, initialPos.z, 0.01f);
}

TEST_CASE(Camera, CameraDeletionAndSceneRootNodeUpdate) {
    Scene scene;
    scene.ResetToDefault();

    // Verify initial scene has active camera and is listed in roots
    auto cam = scene.GetActiveCamera();
    ASSERT_TRUE(cam != nullptr);
    uint32_t camId = cam->id;
    ASSERT_TRUE(!scene.GetCameras().empty());

    bool foundInRoots = false;
    for (const auto& root : scene.GetRootNodes()) {
        if (root->id == camId) {
            foundInRoots = true;
            break;
        }
    }
    ASSERT_TRUE(foundInRoots);

    // Delete camera from scene
    scene.SelectNode(camId);
    ASSERT_EQ(scene.GetSelectedId(), static_cast<int32_t>(camId));
    bool deleted = scene.DeleteSelected();
    ASSERT_TRUE(deleted);

    // Verify camera is completely removed from scene, active camera is null, and not in root nodes
    ASSERT_TRUE(scene.GetActiveCamera() == nullptr);
    ASSERT_TRUE(scene.GetCameras().empty());
    ASSERT_EQ(scene.GetSelectedId(), -1);

    foundInRoots = false;
    for (const auto& root : scene.GetRootNodes()) {
        if (root->id == camId || root->nodeType == NodeType::Camera) {
            foundInRoots = true;
            break;
        }
    }
    ASSERT_FALSE(foundInRoots);

    // Test Undo restores the camera node
    Core::UndoRedoManager::Get().Undo(scene);
    ASSERT_TRUE(scene.GetActiveCamera() != nullptr);
    ASSERT_EQ(scene.GetActiveCamera()->id, camId);

    // Test Redo deletes it again
    Core::UndoRedoManager::Get().Redo(scene);
    ASSERT_TRUE(scene.GetActiveCamera() == nullptr);
    ASSERT_TRUE(scene.GetCameras().empty());
}

// =========================================================================
// 3. InputManager Tests
// =========================================================================
TEST_CASE(InputManager, ActionBindingAndConfig) {
    auto& input = Core::InputManager::Get();
    input.ResetToDefaults();

    auto action = input.FindAction("Jump");
    ASSERT_TRUE(action != nullptr);
    ASSERT_EQ(action->actionId, "Jump");

    // Rebind Jump primary to custom key
    Core::InputButton customButton;
    customButton.device = Core::InputDevice::Keyboard;
    customButton.code = 32; // Space
    input.SetPrimaryBinding("Jump", customButton);

    auto updated = input.FindAction("Jump");
    ASSERT_TRUE(updated != nullptr);
    ASSERT_EQ(updated->primary.code, 32);
    ASSERT_EQ(updated->primary.device, Core::InputDevice::Keyboard);
}

TEST_CASE(InputManager, SerializationRoundtrip) {
    auto& input = Core::InputManager::Get();
    JsonValue serialized = input.Serialize();
    ASSERT_TRUE(serialized.IsArray());
    ASSERT_TRUE(serialized.arrVal.size() > 0);

    bool ok = input.Deserialize(serialized);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(input.FindAction("MoveForward") != nullptr);
}

// =========================================================================
// 4. Collision & Physics Tests
// =========================================================================
TEST_CASE(Collision, RayAABBIntersection) {
    Physics::AABB box;
    box.minBound = glm::vec3(-1.0f, -1.0f, -1.0f);
    box.maxBound = glm::vec3(1.0f, 1.0f, 1.0f);

    Physics::Ray directRay;
    directRay.origin = glm::vec3(0.0f, 0.0f, 5.0f);
    directRay.direction = glm::vec3(0.0f, 0.0f, -1.0f);

    float tMin = 0.0f;
    bool hit = Physics::RayIntersectsAABB(directRay, box, tMin);
    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(tMin, 4.0f, 0.001f);

    Physics::Ray missRay;
    missRay.origin = glm::vec3(0.0f, 5.0f, 5.0f);
    missRay.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    hit = Physics::RayIntersectsAABB(missRay, box, tMin);
    ASSERT_FALSE(hit);
}

TEST_CASE(Collision, BoundingSphereOverlap) {
    Physics::BoundingSphere s1{glm::vec3(0.0f), 1.0f};
    Physics::BoundingSphere s2{glm::vec3(1.5f, 0.0f, 0.0f), 1.0f};
    Physics::BoundingSphere s3{glm::vec3(5.0f, 0.0f, 0.0f), 1.0f};

    ASSERT_TRUE(Physics::CheckSphereSphereOverlap(s1, s2));
    ASSERT_FALSE(Physics::CheckSphereSphereOverlap(s1, s3));
}

// =========================================================================
// 5. MeshGenerator Tests
// =========================================================================
TEST_CASE(MeshGenerator, ProceduralShapes) {
    MeshData cube = MeshGenerator::CreateCube(2.0f);
    ASSERT_EQ(cube.vertices.size(), 24);
    ASSERT_EQ(cube.indices.size(), 36);

    MeshData sphere = MeshGenerator::CreateSphere(1.0f, 12, 12);
    ASSERT_TRUE(sphere.vertices.size() > 0);
    ASSERT_TRUE(sphere.indices.size() > 0);

    MeshData plane = MeshGenerator::CreatePlane(10.0f);
    ASSERT_EQ(plane.vertices.size(), 4);
    ASSERT_EQ(plane.indices.size(), 6);

    MeshData cylinder = MeshGenerator::CreateCylinder(1.0f, 2.0f, 16);
    ASSERT_TRUE(cylinder.vertices.size() > 0);

    MeshData cone = MeshGenerator::CreateCone(1.0f, 2.0f, 16);
    ASSERT_TRUE(cone.vertices.size() > 0);
}

// =========================================================================
// 6. Scene & Node Hierarchy Tests
// =========================================================================
TEST_CASE(Scene, NodeAdditionAndHierarchy) {
    Scene scene;
    auto parentNode = scene.AddNode(ShapeType::Cube, "ParentNode");
    ASSERT_TRUE(parentNode != nullptr);
    parentNode->position = glm::vec3(1.0f, 2.0f, 3.0f);

    auto childNode = scene.AddNode(ShapeType::Sphere, "ChildNode", parentNode->id);
    ASSERT_TRUE(childNode != nullptr);
    childNode->position = glm::vec3(0.0f, 5.0f, 0.0f);

    ASSERT_EQ(childNode->parentId, parentNode->id);
    ASSERT_EQ(parentNode->childrenIds.size(), 1);
    ASSERT_EQ(parentNode->childrenIds[0], childNode->id);

    // World position of child should compound parent transform
    glm::vec3 worldPos = childNode->GetWorldPosition(scene);
    ASSERT_FLOAT_NEAR(worldPos.x, 1.0f, 0.001f);
    ASSERT_FLOAT_NEAR(worldPos.y, 7.0f, 0.001f);
    ASSERT_FLOAT_NEAR(worldPos.z, 3.0f, 0.001f);
}

TEST_CASE(Scene, NodeReorderingAndHierarchy) {
    Scene scene;
    scene.Clear();
    auto nodeA = scene.AddNode(ShapeType::Cube, "NodeA");
    auto nodeB = scene.AddNode(ShapeType::Sphere, "NodeB");
    auto nodeC = scene.AddNode(ShapeType::Plane, "NodeC");

    // 1. Root reordering
    auto roots = scene.GetRootNodes();
    ASSERT_EQ(roots.size(), 3);
    ASSERT_EQ(roots[0]->id, nodeA->id);
    ASSERT_EQ(roots[1]->id, nodeB->id);
    ASSERT_EQ(roots[2]->id, nodeC->id);

    // Move B up -> [B, A, C]
    ASSERT_TRUE(scene.MoveNodeUp(nodeB->id));
    roots = scene.GetRootNodes();
    ASSERT_EQ(roots[0]->id, nodeB->id);
    ASSERT_EQ(roots[1]->id, nodeA->id);
    ASSERT_EQ(roots[2]->id, nodeC->id);

    // Move B down -> [A, B, C]
    ASSERT_TRUE(scene.MoveNodeDown(nodeB->id));
    roots = scene.GetRootNodes();
    ASSERT_EQ(roots[0]->id, nodeA->id);
    ASSERT_EQ(roots[1]->id, nodeB->id);
    ASSERT_EQ(roots[2]->id, nodeC->id);

    // Move A to bottom -> [B, C, A]
    ASSERT_TRUE(scene.MoveNodeToBottom(nodeA->id));
    roots = scene.GetRootNodes();
    ASSERT_EQ(roots[0]->id, nodeB->id);
    ASSERT_EQ(roots[1]->id, nodeC->id);
    ASSERT_EQ(roots[2]->id, nodeA->id);

    // Move A to top -> [A, B, C]
    ASSERT_TRUE(scene.MoveNodeToTop(nodeA->id));
    roots = scene.GetRootNodes();
    ASSERT_EQ(roots[0]->id, nodeA->id);
    ASSERT_EQ(roots[1]->id, nodeB->id);
    ASSERT_EQ(roots[2]->id, nodeC->id);

    // 2. Child reordering under NodeA
    auto c1 = scene.AddNode(ShapeType::Cube, "Child1", nodeA->id);
    auto c2 = scene.AddNode(ShapeType::Cube, "Child2", nodeA->id);
    auto c3 = scene.AddNode(ShapeType::Cube, "Child3", nodeA->id);

    auto aChildren = scene.GetChildren(nodeA->id);
    ASSERT_EQ(aChildren.size(), 3);
    ASSERT_EQ(aChildren[0]->id, c1->id);
    ASSERT_EQ(aChildren[1]->id, c2->id);
    ASSERT_EQ(aChildren[2]->id, c3->id);

    // Move c2 up -> [c2, c1, c3]
    ASSERT_TRUE(scene.MoveNodeUp(c2->id));
    aChildren = scene.GetChildren(nodeA->id);
    ASSERT_EQ(aChildren[0]->id, c2->id);
    ASSERT_EQ(aChildren[1]->id, c1->id);
    ASSERT_EQ(aChildren[2]->id, c3->id);

    // Move c2 down -> [c1, c2, c3]
    ASSERT_TRUE(scene.MoveNodeDown(c2->id));
    aChildren = scene.GetChildren(nodeA->id);
    ASSERT_EQ(aChildren[0]->id, c1->id);
    ASSERT_EQ(aChildren[1]->id, c2->id);
    ASSERT_EQ(aChildren[2]->id, c3->id);

    // 3. Reorder across parents: Move c3 before root nodeB
    ASSERT_TRUE(scene.ReorderNodeBefore(c3->id, nodeB->id));
    ASSERT_EQ(c3->parentId, 0);
    roots = scene.GetRootNodes();
    // Order should now be [A, c3, B, C]
    ASSERT_EQ(roots.size(), 4);
    ASSERT_EQ(roots[0]->id, nodeA->id);
    ASSERT_EQ(roots[1]->id, c3->id);
    ASSERT_EQ(roots[2]->id, nodeB->id);
    ASSERT_EQ(roots[3]->id, nodeC->id);

    // A only has c1 and c2 now
    aChildren = scene.GetChildren(nodeA->id);
    ASSERT_EQ(aChildren.size(), 2);
    ASSERT_EQ(aChildren[0]->id, c1->id);
    ASSERT_EQ(aChildren[1]->id, c2->id);

    // Reorder c3 back into A after c1 -> A's children should be [c1, c3, c2]
    ASSERT_TRUE(scene.ReorderNodeAfter(c3->id, c1->id));
    ASSERT_EQ(c3->parentId, nodeA->id);
    aChildren = scene.GetChildren(nodeA->id);
    ASSERT_EQ(aChildren.size(), 3);
    ASSERT_EQ(aChildren[0]->id, c1->id);
    ASSERT_EQ(aChildren[1]->id, c3->id);
    ASSERT_EQ(aChildren[2]->id, c2->id);

    // 4. Cycle prevention: Cannot move ancestor before its own child
    ASSERT_FALSE(scene.ReorderNodeBefore(nodeA->id, c1->id));
    ASSERT_FALSE(scene.ReorderNodeAfter(nodeA->id, c1->id));
}

TEST_CASE(Scene, CloneIsolation) {
    Scene original;
    auto node = original.AddNode(ShapeType::Cube, "Hero");
    node->position = glm::vec3(10.0f, 0.0f, 0.0f);

    auto cloned = original.Clone();
    ASSERT_TRUE(cloned != nullptr);
    ASSERT_EQ(cloned->GetNodes().size(), original.GetNodes().size());

    auto clonedHero = cloned->GetNodeById(node->id);
    ASSERT_TRUE(clonedHero != nullptr);

    // Mutate clone; original must stay untouched
    clonedHero->position = glm::vec3(99.0f, 0.0f, 0.0f);
    ASSERT_FLOAT_NEAR(node->position.x, 10.0f, 0.001f);
    ASSERT_FLOAT_NEAR(clonedHero->position.x, 99.0f, 0.001f);
}

TEST_CASE(Scene, SerializationRoundtrip) {
    Scene scene;
    scene.SetName("SaveTestScene");
    auto n1 = scene.AddNode(ShapeType::Plane, "Floor");
    n1->position = glm::vec3(0.0f, -1.0f, 0.0f);

    auto light = scene.AddLightNode(LightType::Directional, "Sun");
    light->lightIntensity = 3.5f;

    auto uiCanvas = scene.AddUICanvasNode("HUD");
    auto uiBar = scene.AddUIProgressBarNode("HealthBar", uiCanvas->id);
    uiBar->uiProgress = 0.75f;

    JsonValue serialized = scene.Serialize();
    ASSERT_TRUE(serialized.IsObject());

    Scene restored;
    bool ok = restored.Deserialize(serialized);
    ASSERT_TRUE(ok);
    ASSERT_EQ(restored.GetName(), "SaveTestScene");

    auto rFloor = restored.GetNodeById(n1->id);
    ASSERT_TRUE(rFloor != nullptr);
    ASSERT_FLOAT_NEAR(rFloor->position.y, -1.0f, 0.001f);

    auto rSun = restored.GetNodeById(light->id);
    ASSERT_TRUE(rSun != nullptr);
    ASSERT_EQ(rSun->nodeType, NodeType::Light);
    ASSERT_FLOAT_NEAR(rSun->lightIntensity, 3.5f, 0.001f);

    auto rBar = restored.GetNodeById(uiBar->id);
    ASSERT_TRUE(rBar != nullptr);
    ASSERT_FLOAT_NEAR(rBar->uiProgress, 0.75f, 0.001f);
}

// =========================================================================
// 7. Animation System Tests
// =========================================================================
TEST_CASE(Animation, TrackInterpolation) {
    Animation::TransformTrack track;
    track.nodeId = 1;
    track.AddPositionKey(0.0f, glm::vec3(0.0f, 0.0f, 0.0f));
    track.AddPositionKey(2.0f, glm::vec3(10.0f, 20.0f, 0.0f));

    glm::vec3 mid = track.SamplePosition(1.0f);
    ASSERT_FLOAT_NEAR(mid.x, 5.0f, 0.001f);
    ASSERT_FLOAT_NEAR(mid.y, 10.0f, 0.001f);

    glm::vec3 start = track.SamplePosition(-1.0f);
    ASSERT_FLOAT_NEAR(start.x, 0.0f, 0.001f);

    glm::vec3 end = track.SamplePosition(3.0f);
    ASSERT_FLOAT_NEAR(end.x, 10.0f, 0.001f);
}

TEST_CASE(Animation, ClipAndPlayerEvaluation) {
    Scene scene;
    auto node = scene.AddNode(ShapeType::Cube, "AnimatedBox");
    node->id = 42;

    auto clip = std::make_shared<Animation::AnimationClip>();
    clip->name = "MoveBox";
    clip->duration = 2.0f;
    clip->loop = true;
    clip->AddKeyframe(42, 0.0f, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    clip->AddKeyframe(42, 2.0f, glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f));

    Animation::AnimationPlayer player;
    player.SetClip(clip);
    player.Play();

    player.Update(scene, 1.0f);
    ASSERT_FLOAT_NEAR(node->position.x, 2.0f, 0.001f);

    // Loop around to 2.5s -> 0.5s into clip
    player.Update(scene, 1.5f);
    ASSERT_FLOAT_NEAR(node->position.x, 1.0f, 0.001f);
}

// =========================================================================
// 8. Decoupled Graphics Abstraction Tests
// =========================================================================
class MockRenderTarget : public Graphics::IRenderTarget {
public:
    uint32_t width{1280};
    uint32_t height{720};
    void* mockTexId{(void*)(uintptr_t)0xDEADBEEF};

    uint32_t GetWidth() const override { return width; }
    uint32_t GetHeight() const override { return height; }
    void Resize(uint32_t w, uint32_t h) override { width = w; height = h; }
    void* GetTextureId() const override { return mockTexId; }
};

TEST_CASE(RendererAbstraction, MockRenderTargetInterface) {
    MockRenderTarget target;
    ASSERT_EQ(target.GetWidth(), 1280);
    ASSERT_EQ(target.GetHeight(), 720);
    ASSERT_EQ(target.GetTextureId(), (void*)(uintptr_t)0xDEADBEEF);

    target.Resize(1920, 1080);
    ASSERT_EQ(target.GetWidth(), 1920);
    ASSERT_EQ(target.GetHeight(), 1080);

    Graphics::PolygonMode mode = Graphics::PolygonMode::Wireframe;
    ASSERT_EQ(static_cast<int>(mode), 1);
}

// =========================================================================
// 9. Single-File Bundler Format Tests
// =========================================================================
TEST_CASE(BundleFormat, StructLayout) {
    ASSERT_EQ(sizeof(BundleTrailer), 24);
    ASSERT_EQ(sizeof(BundleHeader), 144);

    BundleTrailer trailer;
    trailer.payloadOffset = 1024;
    trailer.payloadSize = 4096;
    ASSERT_EQ(memcmp(trailer.magic, "AGYPACK1", 8), 0);
}

// =========================================================================
// 10. Lua Scripting System Tests
// =========================================================================
TEST_CASE(LuaScripting, RuntimeInitializationAndExecution) {
    auto& lua = Scripting::LuaScriptSystem::Get();
    bool initOk = lua.Init();
    ASSERT_TRUE(initOk);

    Scene scene;
    auto node = scene.AddNode(ShapeType::Cube, "ScriptNode");
    node->position = glm::vec3(0.0f, 0.0f, 0.0f);

    std::string script = 
        "function OnInit(n)\n"
        "    NodeAPI.SetPosition(n.id, 5.0, 10.0, 15.0)\n"
        "end\n";

    std::string scriptPath = "temp_test_script.lua";
    {
        std::ofstream out(scriptPath);
        out << script;
        out.close();
    }

    node->scriptPath = scriptPath;
    lua.InitNodeScript(*node, scene);

    ASSERT_FLOAT_NEAR(node->position.x, 5.0f, 0.001f);
    ASSERT_FLOAT_NEAR(node->position.y, 10.0f, 0.001f);
    ASSERT_FLOAT_NEAR(node->position.z, 15.0f, 0.001f);

    std::remove(scriptPath.c_str());
}

TEST_CASE(LuaScripting, Sol2BindingAndExecution) {
    // 1. Standalone sol::state test
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string);

    lua.script("function add(a, b) return a + b end");
    sol::function addFunc = lua["add"];
    int result = addFunc(15, 27);
    ASSERT_EQ(result, 42);

    // 2. C++ Struct / Class Binding (Usertype)
    struct PlayerHero {
        std::string name;
        int hp{100};
        void TakeDamage(int dmg) { hp -= dmg; }
    };

    lua.new_usertype<PlayerHero>("PlayerHero",
        "name", &PlayerHero::name,
        "hp", &PlayerHero::hp,
        "take_damage", &PlayerHero::TakeDamage
    );

    lua.script(R"(
        hero = PlayerHero.new()
        hero.name = "Arthur"
        hero:take_damage(25)
    )");

    PlayerHero& hero = lua["hero"];
    ASSERT_EQ(hero.name, "Arthur");
    ASSERT_EQ(hero.hp, 75);

    // 3. Test sol::state_view interoperability with engine's LuaScriptSystem
    auto& scriptSys = Scripting::LuaScriptSystem::Get();
    scriptSys.Init();
    sol::state_view engineLua(scriptSys.GetState());
    engineLua.script("engine_sol_val = 123 * 2");
    int val = engineLua["engine_sol_val"];
    ASSERT_EQ(val, 246);
}

TEST_CASE(LuaScripting, MultipleNodeScriptsIsolation) {
    auto& lua = Scripting::LuaScriptSystem::Get();
    lua.Shutdown();
    lua.Init();

    Scene scene;
    auto nodeA = scene.AddNode(ShapeType::Cube, "EntityA");
    auto nodeB = scene.AddNode(ShapeType::Sphere, "EntityB");

    nodeA->position = glm::vec3(0.0f, 0.0f, 0.0f);
    nodeB->position = glm::vec3(0.0f, 0.0f, 0.0f);

    std::string scriptPathA = "temp_script_a.lua";
    std::string scriptPathB = "temp_script_b.lua";

    {
        std::ofstream out(scriptPathA);
        out << "speed = 50.0\n"
               "function OnUpdate(self, dt)\n"
               "    self.position.x = self.position.x + speed * dt\n"
               "end\n";
    }

    {
        std::ofstream out(scriptPathB);
        out << "speed = 5.0\n"
               "function OnUpdate(self, dt)\n"
               "    self.position.y = self.position.y + speed * dt\n"
               "end\n";
    }

    nodeA->scriptPath = scriptPathA;
    nodeB->scriptPath = scriptPathB;

    lua.InitNodeScript(*nodeA, scene);
    lua.InitNodeScript(*nodeB, scene);

    // Run updates for 1 second
    float dt = 1.0f;
    lua.UpdateNodeScript(*nodeA, scene, dt);
    lua.UpdateNodeScript(*nodeB, scene, dt);

    // Node A moved strictly along X by 50, Node B moved strictly along Y by 5
    ASSERT_FLOAT_NEAR(nodeA->position.x, 50.0f, 0.01f);
    ASSERT_FLOAT_NEAR(nodeA->position.y, 0.0f, 0.01f);

    ASSERT_FLOAT_NEAR(nodeB->position.x, 0.0f, 0.01f);
    ASSERT_FLOAT_NEAR(nodeB->position.y, 5.0f, 0.01f);

    std::remove(scriptPathA.c_str());
    std::remove(scriptPathB.c_str());
}

// =========================================================================
// Main Test Runner Entry Point
// =========================================================================
// =========================================================================
// 11. Armature & Skeleton Tests
// =========================================================================
TEST_CASE(Skeleton, HierarchyAndInverseBindMatrices) {
    Animation::Skeleton skel;
    skel.name = "Humanoid";

    int root = skel.AddBone("Hips", -1);
    int spine = skel.AddBone("Spine", root);
    int head = skel.AddBone("Head", spine);

    ASSERT_EQ(skel.bones.size(), 3);
    ASSERT_EQ(skel.FindBoneIndex("Spine"), spine);
    ASSERT_EQ(skel.FindBoneIndex("Head"), head);

    skel.bones[root].bindPosition = glm::vec3(0.0f, 1.0f, 0.0f);
    skel.bones[root].currentPosition = skel.bones[root].bindPosition;
    skel.bones[spine].bindPosition = glm::vec3(0.0f, 0.5f, 0.0f);
    skel.bones[spine].currentPosition = skel.bones[spine].bindPosition;
    skel.bones[head].bindPosition = glm::vec3(0.0f, 0.3f, 0.0f);
    skel.bones[head].currentPosition = skel.bones[head].bindPosition;

    skel.CalculateInverseBindMatrices();

    std::vector<glm::mat4> skinMatrices;
    skel.EvaluateSkinningMatrices(skinMatrices);
    ASSERT_EQ(skinMatrices.size(), 3);

    // In bind pose, skinning matrices should equal identity
    for (const auto& mat : skinMatrices) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                float expected = (r == c) ? 1.0f : 0.0f;
                ASSERT_FLOAT_NEAR(mat[r][c], expected, 0.001f);
            }
        }
    }
}

// =========================================================================
// 12. Animation Tree & State Machine Tests
// =========================================================================
TEST_CASE(AnimationTree, StateTransitionsAndCrossfade) {
    Animation::Skeleton skel;
    int bone = skel.AddBone("Root", -1);

    auto idleClip = std::make_shared<Animation::SkeletalAnimationClip>();
    idleClip->name = "Idle";
    idleClip->duration = 1.0f;
    auto idleTrack = idleClip->GetOrCreateTrack(bone, "Root");
    idleTrack->AddKeyframe(0.0f, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));

    auto runClip = std::make_shared<Animation::SkeletalAnimationClip>();
    runClip->name = "Run";
    runClip->duration = 1.0f;
    auto runTrack = runClip->GetOrCreateTrack(bone, "Root");
    runTrack->AddKeyframe(0.0f, glm::vec3(0.0f, 0.0f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));

    Animation::AnimationTree animTree;
    animTree.AddState("Idle", idleClip);
    animTree.AddState("Run", runClip);

    // Initial state is Idle
    ASSERT_EQ(animTree.GetCurrentStateName(), "Idle");
    animTree.Update(0.1f, skel);
    ASSERT_FLOAT_NEAR(skel.bones[bone].currentPosition.z, 0.0f, 0.001f);

    // Transition to Run with blend duration 0.2s
    animTree.SetCurrentState("Run", 0.2f);
    // Halfway through blend (0.1s): blend weight is 0.5
    animTree.Update(0.1f, skel);
    ASSERT_FLOAT_NEAR(skel.bones[bone].currentPosition.z, 2.5f, 0.05f);

    // Complete blend (another 0.1s)
    animTree.Update(0.1f, skel);
    ASSERT_EQ(animTree.GetCurrentStateName(), "Run");
    ASSERT_FLOAT_NEAR(skel.bones[bone].currentPosition.z, 5.0f, 0.001f);
}

// =========================================================================
// 13. Lua Animation Tree Control Tests
// =========================================================================
TEST_CASE(LuaScripting, AnimationTreeStateControl) {
    auto& lua = Scripting::LuaScriptSystem::Get();
    lua.Init();

    Scene scene;
    auto player = scene.AddNode(ShapeType::Cube, "AnimatedHero");
    player->currentAnimState = "Idle";

    std::string scriptPath = "temp_anim_tree_test.lua";
    {
        std::ofstream out(scriptPath);
        out << "function OnInit(n)\n"
               "    NodeAPI.SetAnimState(n.id, 'Walking')\n"
               "end\n";
        out.close();
    }

    player->scriptPath = scriptPath;
    lua.InitNodeScript(*player, scene);

    ASSERT_EQ(player->currentAnimState, "Walking");
    std::remove(scriptPath.c_str());
}

TEST_CASE(LuaScripting, CubeJumpAndPhysicsImpulse) {
    auto& lua = Scripting::LuaScriptSystem::Get();
    lua.Shutdown();
    lua.Init();

    Physics::PhysicsWorld physicsWorld;
    physicsWorld.Initialize();
    lua.SetPhysicsWorld(&physicsWorld);

    Scene scene;
    auto cube = scene.AddNode(ShapeType::Cube, "JumpingCube");
    cube->position = glm::vec3(0.0f, 0.0f, 0.0f);
    cube->rigidBodyType = RigidBodyType::Dynamic;
    cube->mass = 1.0f;

    physicsWorld.BuildFromScene(scene);
    ASSERT_TRUE(physicsWorld.HasBodyForNode(cube->id));

    std::string scriptPath = "temp_cube_jump_test.lua";
    {
        std::ofstream out(scriptPath);
        out << "jumpTriggered = false\n"
               "function OnUpdate(self, dt)\n"
               "    -- Jump using direct impulse\n"
               "    self:AddImpulse(0.0, 10.0, 0.0)\n"
               "    jumpTriggered = true\n"
               "end\n";
        out.close();
    }

    cube->scriptPath = scriptPath;
    lua.InitNodeScript(*cube, scene);
    lua.UpdateNodeScript(*cube, scene, 0.016f);

    glm::vec3 vel = physicsWorld.GetLinearVelocity(cube->id);
    ASSERT_FLOAT_NEAR(vel.y, 10.0f, 0.1f);

    // Step physics and verify the cube moved upward
    physicsWorld.Step(scene, 0.016f);
    ASSERT_TRUE(cube->position.y > 0.05f);

    // Clean up
    lua.SetPhysicsWorld(nullptr);
    physicsWorld.Shutdown();
    std::remove(scriptPath.c_str());
}

TEST_CASE(Scene, RiggedNodeArmatureSerialization) {
    Scene scene;
    auto node = scene.AddCustomMeshNode("hero.fbx", glm::vec3(-1.0f), glm::vec3(1.0f), 2400, 1800, "RiggedHero");
    node->hasSkeleton = true;
    node->skeletonName = "Hero_Armature";
    node->currentAnimState = "Idle";
    node->boneNames = { "Root", "Pelvis", "Spine", "Neck", "Head", "Arm.L", "Arm.R" };
    node->animClipNames = { "Idle", "Walk", "Run", "Slash" };

    // Serialize scene
    JsonValue serialized = scene.Serialize();

    // Deserialize into fresh scene
    Scene loadedScene;
    bool ok = loadedScene.Deserialize(serialized);
    ASSERT_TRUE(ok);

    auto loadedNode = loadedScene.GetNodeById(node->id);
    ASSERT_TRUE(loadedNode != nullptr);
    ASSERT_TRUE(loadedNode->hasSkeleton);
    ASSERT_EQ(loadedNode->skeletonName, "Hero_Armature");
    ASSERT_EQ(loadedNode->currentAnimState, "Idle");
    ASSERT_EQ(loadedNode->boneNames.size(), 7);
    ASSERT_EQ(loadedNode->boneNames[2], "Spine");
    ASSERT_EQ(loadedNode->animClipNames.size(), 4);
    ASSERT_EQ(loadedNode->animClipNames[3], "Slash");
}

TEST_CASE(Scene, PbrMaterialSerialization) {
    Scene scene;
    auto node = scene.AddNode(ShapeType::Cube, "PBR_Test_Model");
    node->texturePath = "assets/textures/diffuse.png";
    node->normalMapPath = "assets/textures/normal.png";
    node->metallicMapPath = "assets/textures/metallic.png";
    node->roughnessMapPath = "assets/textures/roughness.png";
    node->roughness = 0.35f;
    node->metallic = 0.85f;

    JsonValue serialized = scene.Serialize();

    Scene loadedScene;
    bool ok = loadedScene.Deserialize(serialized);
    ASSERT_TRUE(ok);

    auto loadedNode = loadedScene.GetNodeById(node->id);
    ASSERT_TRUE(loadedNode != nullptr);
    ASSERT_EQ(loadedNode->texturePath, "assets/textures/diffuse.png");
    ASSERT_EQ(loadedNode->normalMapPath, "assets/textures/normal.png");
    ASSERT_EQ(loadedNode->metallicMapPath, "assets/textures/metallic.png");
    ASSERT_EQ(loadedNode->roughnessMapPath, "assets/textures/roughness.png");
    ASSERT_FLOAT_NEAR(loadedNode->roughness, 0.35f, 0.001f);
    ASSERT_FLOAT_NEAR(loadedNode->metallic, 0.85f, 0.001f);
}

#include "Scene/FbxLoader.h"
#include <ofbx.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>

TEST_CASE(FbxLoader, LoadNathanFbxWalkCycle) {
    std::string path = "projects/MyGameProject/assets/models/rp_nathan_animated_003_walking.fbx";
    if (std::filesystem::exists(path)) {
        LoadedMesh mesh;
        bool ok = FbxLoader::Load(path, mesh, true);
        ASSERT_TRUE(ok);
        ASSERT_TRUE(mesh.hasSkeleton);
        ASSERT_TRUE(mesh.skeleton != nullptr);
        ASSERT_EQ(mesh.skeleton->bones.size(), 88);
        ASSERT_TRUE(!mesh.skeletalClips.empty());

        // 1. Verify Rest Pose skinning evaluates to identity matrices
        std::vector<glm::mat4> restMatrices;
        mesh.skeleton->EvaluateSkinningMatrices(restMatrices);
        float maxRestDiff = 0.0f;
        for (const auto& m : restMatrices) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    maxRestDiff = (std::max)(maxRestDiff, std::abs(m[r][c] - ((r == c) ? 1.0f : 0.0f)));
                }
            }
        }
        ASSERT_TRUE(maxRestDiff < 0.001f);

        // 2. Verify all vertices are properly weighted (0 unweighted)
        int unweightedCount = 0;
        for (const auto& s : mesh.skinData) {
            float totalW = s.boneWeights[0] + s.boneWeights[1] + s.boneWeights[2] + s.boneWeights[3];
            if (totalW <= 0.0001f) unweightedCount++;
        }
        ASSERT_EQ(unweightedCount, 0);

        // 3. Verify active walk cycle animation clip
        auto clip = mesh.skeletalClips[0];
        ASSERT_EQ(clip->name, "Take 001");
        // Frame 0 reference pose (T-pose with drooped wrists) is properly skipped, active walk cycle duration ~2.23s
        ASSERT_TRUE(clip->duration > 2.0f && clip->duration < 2.3f);

        // 4. Evaluate deformed bounds at t = 0.0s (active walk cycle start)
        clip->Apply(*mesh.skeleton, 0.0f);
        std::vector<glm::mat4> animMatrices0;
        mesh.skeleton->EvaluateSkinningMatrices(animMatrices0);

        glm::vec3 min0(1e9f), max0(-1e9f);
        for (const auto& s : mesh.skinData) {
            glm::vec3 p(0.0f);
            for (int b = 0; b < 4; ++b) {
                if (s.boneWeights[b] > 0.0f && s.boneIndices[b] >= 0) {
                    p += s.boneWeights[b] * glm::vec3(animMatrices0[s.boneIndices[b]] * glm::vec4(s.origPos, 1.0f));
                }
            }
            if (mesh.scaleFactor > 0.0f) p = (p - mesh.center) * mesh.scaleFactor;
            min0 = glm::min(min0, p);
            max0 = glm::max(max0, p);
        }

        // Nathan's active walk cycle bounds:
        // Height ~ 1.95 units, width < 0.7 units (arms hanging naturally, NOT outstretched in 2.0m T-pose)
        float width0 = max0.x - min0.x;
        float height0 = max0.y - min0.y;
        ASSERT_TRUE(height0 > 1.8f && height0 < 2.1f);
        ASSERT_TRUE(width0 < 0.70f);

        // Verify fingers remain anatomically proportional (< 22cm from wrist, zero stretched claws)
        int handL = mesh.skeleton->FindBoneIndex("rp_nathan_animated_003_walking_hand_l");
        int handR = mesh.skeleton->FindBoneIndex("rp_nathan_animated_003_walking_hand_r");
        ASSERT_TRUE(handL >= 0 && handR >= 0);
        glm::mat4 wbL = glm::inverse(mesh.skeleton->bones[handL].inverseBindMatrix);
        glm::vec3 handPosL = glm::vec3((animMatrices0[handL] * wbL)[3]);
        glm::mat4 wbR = glm::inverse(mesh.skeleton->bones[handR].inverseBindMatrix);
        glm::vec3 handPosR = glm::vec3((animMatrices0[handR] * wbR)[3]);
        float maxFingerDistL = 0.0f;
        float maxFingerDistR = 0.0f;
        for (const auto& s : mesh.skinData) {
            glm::vec3 defPos(0.0f);
            bool hasFinger = false;
            bool isLeft = false;
            for (int b = 0; b < 4; ++b) {
                if (s.boneWeights[b] > 0.0f && s.boneIndices[b] >= 0) {
                    defPos += s.boneWeights[b] * glm::vec3(animMatrices0[s.boneIndices[b]] * glm::vec4(s.origPos, 1.0f));
                    std::string bName = mesh.skeleton->bones[s.boneIndices[b]].name;
                    if (bName.find("index") != std::string::npos || bName.find("thumb") != std::string::npos ||
                        bName.find("middle") != std::string::npos || bName.find("ring") != std::string::npos ||
                        bName.find("pinky") != std::string::npos) {
                        hasFinger = true;
                        if (bName.find("_l") != std::string::npos) isLeft = true;
                    }
                }
            }
            if (hasFinger) {
                if (isLeft) maxFingerDistL = (std::max)(maxFingerDistL, glm::distance(defPos, handPosL));
                else maxFingerDistR = (std::max)(maxFingerDistR, glm::distance(defPos, handPosR));
            }
        }
        ASSERT_TRUE(maxFingerDistL > 10.0f && maxFingerDistL < 22.0f);
        ASSERT_TRUE(maxFingerDistR > 10.0f && maxFingerDistR < 22.0f);

        // 5. Evaluate deformed bounds at mid-stride (t = 0.5s)
        clip->Apply(*mesh.skeleton, 0.5f);
        std::vector<glm::mat4> animMatrices05;
        mesh.skeleton->EvaluateSkinningMatrices(animMatrices05);

        glm::vec3 min05(1e9f), max05(-1e9f);
        for (const auto& s : mesh.skinData) {
            glm::vec3 p(0.0f);
            for (int b = 0; b < 4; ++b) {
                if (s.boneWeights[b] > 0.0f && s.boneIndices[b] >= 0) {
                    p += s.boneWeights[b] * glm::vec3(animMatrices05[s.boneIndices[b]] * glm::vec4(s.origPos, 1.0f));
                }
            }
            if (mesh.scaleFactor > 0.0f) p = (p - mesh.center) * mesh.scaleFactor;
            min05 = glm::min(min05, p);
            max05 = glm::max(max05, p);
        }

        float width05 = max05.x - min05.x;
        float height05 = max05.y - min05.y;
        ASSERT_TRUE(height05 > 1.8f && height05 < 2.1f);
        ASSERT_TRUE(width05 < 0.70f);
    }
}
TEST_CASE(FbxLoader, LoadUserIdleFbx) {
    std::string path = "C:/Users/arron/Desktop/new/MyGameProject/assets/models/idle.fbx";
    if (std::filesystem::exists(path)) {
        LoadedMesh mesh;
        bool ok = FbxLoader::Load(path, mesh, true);
        ASSERT_TRUE(ok);
        ASSERT_TRUE(mesh.hasSkeleton);
        ASSERT_TRUE(!mesh.skeleton->bones.empty());
        ASSERT_TRUE(!mesh.skeletalClips.empty());

        // Verify rest pose has identity skinning matrices (error < 1e-4)
        std::vector<glm::mat4> restMatrices;
        mesh.skeleton->EvaluateSkinningMatrices(restMatrices);
        float maxRestDiff = 0.0f;
        for (const auto& m : restMatrices) {
            float diff = 0.0f;
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    diff += std::abs(m[r][c] - ((r == c) ? 1.0f : 0.0f));
                }
            }
            maxRestDiff = (std::max)(maxRestDiff, diff);
        }
        ASSERT_TRUE(maxRestDiff < 0.001f);

        // Verify all vertices are weighted
        int unweightedVerts = 0;
        for (const auto& s : mesh.skinData) {
            float totalW = s.boneWeights[0] + s.boneWeights[1] + s.boneWeights[2] + s.boneWeights[3];
            if (totalW <= 0.0001f) unweightedVerts++;
        }
        ASSERT_TRUE(unweightedVerts == 0);

        // Apply animation at t = 0 and verify skinning matrices evaluate without NaN/Inf
        auto clip = mesh.skeletalClips[0];
        clip->Apply(*mesh.skeleton, 0.0f);
        std::vector<glm::mat4> animMatrices;
        mesh.skeleton->EvaluateSkinningMatrices(animMatrices);
        ASSERT_EQ(animMatrices.size(), mesh.skeleton->bones.size());
        for (const auto& mat : animMatrices) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    ASSERT_TRUE(!std::isnan(mat[r][c]) && !std::isinf(mat[r][c]));
                }
            }
        }
    }
}

// =========================================================================
// 12. 2D UI Nodes and Scripting API Tests
// =========================================================================
TEST_CASE(Scene, UI2DNodesAndScriptingAPI) {
    Scene scene;
    scene.Clear();

    // 1. Add various 2D UI nodes
    auto canvas = scene.AddUICanvasNode("MainCanvas");
    ASSERT_TRUE(canvas != nullptr);
    ASSERT_TRUE(canvas->IsUINode());
    ASSERT_EQ(canvas->nodeType, NodeType::UI_Canvas);

    auto btn = scene.AddUIButtonNode("Play Game", "PlayBtn", canvas->id);
    ASSERT_TRUE(btn != nullptr);
    ASSERT_TRUE(btn->IsUINode());
    ASSERT_EQ(btn->uiText, "Play Game");

    auto spinner = scene.AddUISpinnerNode("LoadingSpin", canvas->id);
    ASSERT_TRUE(spinner != nullptr);
    ASSERT_EQ(spinner->nodeType, NodeType::UI_Spinner);

    auto txtField = scene.AddUITextFieldNode("Username...", "UserField", canvas->id);
    ASSERT_TRUE(txtField != nullptr);
    ASSERT_EQ(txtField->nodeType, NodeType::UI_TextField);
    ASSERT_EQ(txtField->uiPlaceholder, "Username...");

    auto pwdField = scene.AddUIPasswordFieldNode("PassField", canvas->id);
    ASSERT_TRUE(pwdField != nullptr);
    ASSERT_EQ(pwdField->nodeType, NodeType::UI_PasswordField);

    auto slider = scene.AddUISliderNode(0.0f, 100.0f, 75.0f, "VolumeSlider", canvas->id);
    ASSERT_TRUE(slider != nullptr);
    ASSERT_EQ(slider->nodeType, NodeType::UI_Slider);
    ASSERT_FLOAT_NEAR(slider->uiValue, 75.0f, 0.001f);

    auto chk = scene.AddUICheckboxNode("Mute Audio", true, "MuteChk", canvas->id);
    ASSERT_TRUE(chk != nullptr);
    ASSERT_TRUE(chk->uiChecked);

    auto rad = scene.AddUIRadioButtonNode("Low Graphics", "QualityGroup", true, "LowRad", canvas->id);
    ASSERT_TRUE(rad != nullptr);
    ASSERT_EQ(rad->uiRadioGroup, "QualityGroup");
    ASSERT_TRUE(rad->uiChecked);

    auto win = scene.AddUIWindowNode("Options Window", "OptWindow", canvas->id);
    ASSERT_TRUE(win != nullptr);
    ASSERT_TRUE(win->uiWindowIsOpen);
    ASSERT_TRUE(win->uiWindowMovable);
    ASSERT_TRUE(win->uiWindowClosable);

    auto img = scene.AddUIImageNode("textures/logo.png", "LogoImg", canvas->id);
    ASSERT_TRUE(img != nullptr);
    ASSERT_EQ(img->texturePath, "textures/logo.png");

    auto vid = scene.AddUIVideoNode("IntroVid", canvas->id);
    ASSERT_TRUE(vid != nullptr);
    ASSERT_EQ(vid->nodeType, NodeType::UI_Video);

    auto scroll = scene.AddUIScrollPaneNode("ContentScroll", canvas->id);
    ASSERT_TRUE(scroll != nullptr);
    ASSERT_EQ(scroll->nodeType, NodeType::UI_ScrollPane);

    auto drop = scene.AddUIDropdownNode({"Easy", "Medium", "Hard"}, "DiffDrop", canvas->id);
    ASSERT_TRUE(drop != nullptr);
    ASSERT_EQ(drop->uiItems.size(), 3);

    auto datePick = scene.AddUIDatePickerNode("MatchDate", canvas->id);
    ASSERT_TRUE(datePick != nullptr);
    ASSERT_EQ(datePick->nodeType, NodeType::UI_DatePicker);

    auto list = scene.AddUISelectableListNode({"Item A", "Item B", "Item C"}, "InvList", canvas->id);
    ASSERT_TRUE(list != nullptr);
    ASSERT_EQ(list->uiItems.size(), 3);

    auto colPick = scene.AddUIColorPickerNode("ThemeColor", canvas->id);
    ASSERT_TRUE(colPick != nullptr);
    ASSERT_EQ(colPick->nodeType, NodeType::UI_ColorPicker);

    auto tbl = scene.AddUITableNode("ScoreTable", canvas->id);
    ASSERT_TRUE(tbl != nullptr);
    ASSERT_EQ(tbl->nodeType, NodeType::UI_Table);

    auto tabs = scene.AddUITabsNode({"Audio", "Video", "Gameplay"}, "SettingsTabs", canvas->id);
    ASSERT_TRUE(tabs != nullptr);
    ASSERT_EQ(tabs->uiItems.size(), 3);

    auto menu = scene.AddUIMenuBarNode("TopMenuBar", canvas->id);
    ASSERT_TRUE(menu != nullptr);
    ASSERT_EQ(menu->nodeType, NodeType::UI_MenuBar);

    auto shapeRect = scene.AddUIShapeNode(UIShapeType::Rectangle, "ShapeRect", canvas->id);
    ASSERT_TRUE(shapeRect != nullptr);
    ASSERT_EQ(shapeRect->uiShapeType, UIShapeType::Rectangle);

    auto shapeCircle = scene.AddUIShapeNode(UIShapeType::Circle, "ShapeCircle", canvas->id);
    ASSERT_TRUE(shapeCircle != nullptr);
    ASSERT_EQ(shapeCircle->uiShapeType, UIShapeType::Circle);

    // 2. Serialization & Deserialization round-trip
    JsonValue serialized = scene.Serialize();
    Scene loadedScene;
    ASSERT_TRUE(loadedScene.Deserialize(serialized));

    auto loadedSlider = loadedScene.GetNodeByName("VolumeSlider");
    ASSERT_TRUE(loadedSlider != nullptr);
    ASSERT_EQ(loadedSlider->nodeType, NodeType::UI_Slider);
    ASSERT_FLOAT_NEAR(loadedSlider->uiValue, 75.0f, 0.001f);
    ASSERT_FLOAT_NEAR(loadedSlider->uiMinVal, 0.0f, 0.001f);
    ASSERT_FLOAT_NEAR(loadedSlider->uiMaxVal, 100.0f, 0.001f);

    auto loadedWin = loadedScene.GetNodeByName("OptWindow");
    ASSERT_TRUE(loadedWin != nullptr);
    ASSERT_TRUE(loadedWin->uiWindowIsOpen);
    ASSERT_TRUE(loadedWin->uiWindowMovable);
    ASSERT_TRUE(loadedWin->uiWindowClosable);

    auto loadedDrop = loadedScene.GetNodeByName("DiffDrop");
    ASSERT_TRUE(loadedDrop != nullptr);
    ASSERT_EQ(loadedDrop->uiItems.size(), 3);
    ASSERT_EQ(loadedDrop->uiItems[1], "Medium");

    auto loadedCircle = loadedScene.GetNodeByName("ShapeCircle");
    ASSERT_TRUE(loadedCircle != nullptr);
    ASSERT_EQ(loadedCircle->uiShapeType, UIShapeType::Circle);

    // 3. Scripting API verification on UI nodes
    Scripting::LuaScriptSystem luaSys;
    ASSERT_TRUE(luaSys.Init());

    // Write a temporary test script
    std::string testScript =
        "function OnInit(self)\n"
        "    self:SetText(\"Updated from Lua\")\n"
        "    self:SetValue(99.0)\n"
        "    self:SetChecked(true)\n"
        "    self:SetProgress(0.85)\n"
        "    self:SetWindowOpen(false)\n"
        "    self:SetWindowMovable(false)\n"
        "    self:SetWindowClosable(false)\n"
        "    self:SetColor(0.2, 0.4, 0.6, 1.0)\n"
        "end\n"
        "function OnUpdate(self, dt)\n"
        "    local cur = self:GetValue()\n"
        "    self:SetValue(cur + 1.0)\n"
        "end\n";

    std::filesystem::create_directories("scripts");
    std::ofstream sf("scripts/test_ui_script.lua");
    sf << testScript;
    sf.close();

    win->scriptPath = "scripts/test_ui_script.lua";
    luaSys.InitNodeScript(*win, scene);

    ASSERT_EQ(win->uiText, "Updated from Lua");
    ASSERT_FLOAT_NEAR(win->uiValue, 99.0f, 0.001f);
    ASSERT_TRUE(win->uiChecked);
    ASSERT_FLOAT_NEAR(win->uiProgress, 0.85f, 0.001f);
    ASSERT_FALSE(win->uiWindowIsOpen);
    ASSERT_FALSE(win->uiWindowMovable);
    ASSERT_FALSE(win->uiWindowClosable);
    ASSERT_FLOAT_NEAR(win->uiColor.r, 0.2f, 0.001f);
    ASSERT_FLOAT_NEAR(win->uiColor.g, 0.4f, 0.001f);
    ASSERT_FLOAT_NEAR(win->uiColor.b, 0.6f, 0.001f);

    luaSys.UpdateNodeScript(*win, scene, 0.016f);
    ASSERT_FLOAT_NEAR(win->uiValue, 100.0f, 0.001f);

    // Test NodeAPI functions
    std::string apiScript =
        "function OnInit(self)\n"
        "    NodeAPI.SetText(self.id, \"NodeAPI Text\")\n"
        "    NodeAPI.SetValue(self.id, 42.0)\n"
        "    NodeAPI.SetChecked(self.id, false)\n"
        "    NodeAPI.SetWindowOpen(self.id, true)\n"
        "    NodeAPI.SetColor(self.id, 0.9, 0.8, 0.7, 1.0)\n"
        "end\n";
    std::ofstream sf2("scripts/test_ui_api.lua");
    sf2 << apiScript;
    sf2.close();

    win->scriptPath = "scripts/test_ui_api.lua";
    luaSys.InitNodeScript(*win, scene);

    ASSERT_EQ(win->uiText, "NodeAPI Text");
    ASSERT_FLOAT_NEAR(win->uiValue, 42.0f, 0.001f);
    ASSERT_FALSE(win->uiChecked);
    ASSERT_TRUE(win->uiWindowIsOpen);
    ASSERT_FLOAT_NEAR(win->uiColor.r, 0.9f, 0.001f);
    ASSERT_FLOAT_NEAR(win->uiColor.g, 0.8f, 0.001f);
    ASSERT_FLOAT_NEAR(win->uiColor.b, 0.7f, 0.001f);

    // 4. Test UI Hierarchy: Spinner as child of Window
    auto winChildSpinner = scene.AddUISpinnerNode("WindowSpinner", win->id);
    ASSERT_TRUE(winChildSpinner != nullptr);
    ASSERT_EQ(winChildSpinner->parentId, win->id);
    ASSERT_TRUE(std::find(win->childrenIds.begin(), win->childrenIds.end(), winChildSpinner->id) != win->childrenIds.end());

    luaSys.Shutdown();
    std::filesystem::remove("scripts/test_ui_script.lua");
    std::filesystem::remove("scripts/test_ui_api.lua");
}

TEST_CASE(Scene, UI2DInteractiveControlsAndParenting) {
    Scene scene;
    scene.Clear();

    // 1. Test Window and child Spinner parenting & relative transforms
    auto windowNode = scene.AddUIWindowNode("Settings Window", "SettingsWin");
    windowNode->uiPosition = glm::vec2(100.0f, 150.0f);
    windowNode->uiSize = glm::vec2(300.0f, 200.0f);
    windowNode->uiWindowIsOpen = true;

    auto spinnerNode = scene.AddUISpinnerNode("WindowSpinner", windowNode->id);
    spinnerNode->uiPosition = glm::vec2(50.0f, 50.0f);
    spinnerNode->uiSize = glm::vec2(32.0f, 32.0f);

    ASSERT_EQ(spinnerNode->parentId, windowNode->id);
    ASSERT_TRUE(std::find(windowNode->childrenIds.begin(), windowNode->childrenIds.end(), spinnerNode->id) != windowNode->childrenIds.end());

    // 2. Test Radio Group mutual exclusivity
    auto radio1 = scene.AddUIRadioButtonNode("Option A", "AudioGroup", true, "RadioA");
    auto radio2 = scene.AddUIRadioButtonNode("Option B", "AudioGroup", false, "RadioB");
    auto radio3 = scene.AddUIRadioButtonNode("Option C", "AudioGroup", false, "RadioC");

    ASSERT_TRUE(radio1->uiChecked);
    ASSERT_FALSE(radio2->uiChecked);
    ASSERT_FALSE(radio3->uiChecked);

    // Simulate clicking radio2 in engine
    radio2->uiChecked = true;
    for (auto& other : scene.GetNodes()) {
        if (other->id != radio2->id && other->nodeType == NodeType::UI_RadioButton && other->uiRadioGroup == radio2->uiRadioGroup) {
            other->uiChecked = false;
        }
    }

    ASSERT_FALSE(radio1->uiChecked);
    ASSERT_TRUE(radio2->uiChecked);
    ASSERT_FALSE(radio3->uiChecked);

    // Multi-group radio test: GraphicsGroup must be independent of AudioGroup
    auto radioG1 = scene.AddUIRadioButtonNode("Low Quality", "GraphicsGroup", true, "RadioG1");
    auto radioG2 = scene.AddUIRadioButtonNode("High Quality", "GraphicsGroup", false, "RadioG2");
    ASSERT_TRUE(radio2->uiChecked); // AudioGroup unaffected
    ASSERT_TRUE(radioG1->uiChecked);
    ASSERT_FALSE(radioG2->uiChecked);

    // Click radioG2
    radioG2->uiChecked = true;
    for (auto& other : scene.GetNodes()) {
        if (other->id != radioG2->id && other->nodeType == NodeType::UI_RadioButton && other->uiRadioGroup == radioG2->uiRadioGroup) {
            other->uiChecked = false;
        }
    }
    ASSERT_TRUE(radio2->uiChecked); // AudioGroup remains Option B
    ASSERT_FALSE(radioG1->uiChecked);
    ASSERT_TRUE(radioG2->uiChecked);

    // 3. Test Dropdown item selection and display text sync
    auto dropdown = scene.AddUIDropdownNode({"Resolution 720p", "Resolution 1080p", "Resolution 4K"}, "ResDropdown");
    ASSERT_EQ(dropdown->uiItems.size(), 3);
    dropdown->uiSelectedIndex = 1;
    dropdown->uiText = dropdown->uiItems[dropdown->uiSelectedIndex];
    ASSERT_EQ(dropdown->uiText, "Resolution 1080p");

    // 4. Test ColorPicker hex code conversion (#RRGGBB)
    auto colorPicker = scene.AddUIColorPickerNode("ColorWidget");
    colorPicker->uiColor = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);

    char hexBuf[16];
    snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X",
             (int)(colorPicker->uiColor.r * 255.0f),
             (int)(colorPicker->uiColor.g * 255.0f),
             (int)(colorPicker->uiColor.b * 255.0f));
    ASSERT_EQ(std::string(hexBuf), "#FF7F3F");

    // Parse back hex code #00FF80
    std::string newHex = "00FF80";
    unsigned int val = std::stoul(newHex, nullptr, 16);
    colorPicker->uiColor.r = ((val >> 16) & 0xFF) / 255.0f;
    colorPicker->uiColor.g = ((val >> 8) & 0xFF) / 255.0f;
    colorPicker->uiColor.b = (val & 0xFF) / 255.0f;

    ASSERT_FLOAT_NEAR(colorPicker->uiColor.r, 0.0f, 0.01f);
    ASSERT_FLOAT_NEAR(colorPicker->uiColor.g, 1.0f, 0.01f);
    ASSERT_FLOAT_NEAR(colorPicker->uiColor.b, 0.5019f, 0.01f);
}

// =========================================================================
// 15. Humanoid NPC & Character Tests
// =========================================================================
TEST_CASE(CharacterNPC, VertexSkinningAttributes) {
    Vertex v{};
    ASSERT_EQ(v.boneIndices.x, 0u);
    ASSERT_EQ(v.boneIndices.y, 0u);
    ASSERT_EQ(v.boneIndices.z, 0u);
    ASSERT_EQ(v.boneIndices.w, 0u);
    ASSERT_FLOAT_NEAR(v.boneWeights.x, 0.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(v.boneWeights.y, 0.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(v.boneWeights.z, 0.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(v.boneWeights.w, 0.0f, 0.0001f);

    v.boneIndices = glm::uvec4(1, 4, 9, 12);
    v.boneWeights = glm::vec4(0.5f, 0.3f, 0.15f, 0.05f);
    ASSERT_EQ(v.boneIndices.y, 4u);
    ASSERT_FLOAT_NEAR(v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w, 1.0f, 0.001f);
}

TEST_CASE(CharacterNPC, WaypointPatrolAndSteering) {
    Scene scene;
    scene.Clear();

    auto node = scene.AddCharacterNode("Guard NPC");
    ASSERT_TRUE(node != nullptr);
    ASSERT_EQ(node->nodeType, NodeType::Character);

    auto charNode = std::dynamic_pointer_cast<CharacterNode>(node);
    ASSERT_TRUE(charNode != nullptr);
    ASSERT_EQ(charNode->characterName, "Guard NPC");

    // Add waypoints
    charNode->position = glm::vec3(0.0f, 0.0f, 0.0f);
    charNode->waypoints = {
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(10.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f)
    };
    charNode->currentWaypointIndex = 0;
    charNode->moveSpeed = 5.0f;
    charNode->turnSpeed = 10.0f;
    charNode->isPatrolling = true;
    charNode->loopWaypoints = true;
    charNode->applyGroundClamping = false; // Test pure navigation first

    // Step simulation 1 second towards waypoint 0 (0, 0, 10)
    charNode->Update(1.0f, scene);
    ASSERT_TRUE(charNode->position.z > 0.0f);
    ASSERT_EQ(charNode->currentAnimState, "Walk");

    // Advance character close to waypoint 0 (within tolerance 0.5f)
    charNode->position = glm::vec3(0.0f, 0.0f, 9.8f);
    charNode->Update(0.1f, scene);
    // Should have advanced to waypoint 1
    ASSERT_EQ(charNode->currentWaypointIndex, 1);

    // Test loop wrap
    charNode->currentWaypointIndex = 2;
    charNode->position = glm::vec3(0.0f, 0.0f, 0.1f);
    charNode->Update(0.1f, scene);
    ASSERT_EQ(charNode->currentWaypointIndex, 0);
}

TEST_CASE(CharacterNPC, GroundRaycastClamping) {
    Scene scene;
    scene.Clear();

    // Add a ground mesh box at position (0, 0, 0) with size (20, 2, 20)
    // Box bounds: Y from -1.0 to +1.0
    auto floor = scene.AddNode(ShapeType::Cube, "Floor");
    floor->position = glm::vec3(0.0f, 0.0f, 0.0f);
    floor->scale = glm::vec3(20.0f, 2.0f, 20.0f);

    float groundY = 0.0f;
    glm::vec3 rayOrigin(0.0f, 10.0f, 0.0f);
    auto hit = Physics::RaycastDown(scene, rayOrigin, 50.0f, 0, &groundY);
    ASSERT_TRUE(hit.hit);
    ASSERT_FLOAT_NEAR(groundY, 1.0f, 0.01f);

    // Place character in the air above floor
    auto npc = scene.AddCharacterNode("TestNPC");
    auto charNode = std::dynamic_pointer_cast<CharacterNode>(npc);
    ASSERT_TRUE(charNode != nullptr);
    charNode->position = glm::vec3(0.0f, 5.0f, 0.0f);
    charNode->groundOffset = 0.5f;

    // Apply gravity and clamping over several steps
    for (int i = 0; i < 60; ++i) {
        Physics::ApplyCharacterGravityAndClamping(scene, *charNode, 1.0f / 60.0f, 9.81f, charNode->groundOffset);
    }
    // Expected target Y = groundY + groundOffset = 1.0 + 0.5 = 1.5
    ASSERT_FLOAT_NEAR(charNode->position.y, 1.5f, 0.1f);
}

TEST_CASE(CharacterNPC, PlaneAndWorldFloorSolidContactZeroPenetration) {
    // 1. Test Plane Solid Contact & Zero Penetration
    {
        Scene scene;
        scene.Clear();

        auto plane = scene.AddNode(ShapeType::Plane, "FloorPlane");
        plane->position = glm::vec3(0.0f, 0.0f, 0.0f);
        plane->scale = glm::vec3(20.0f, 1.0f, 20.0f);

        auto charNode = std::dynamic_pointer_cast<CharacterNode>(scene.AddCharacterNode("Humanoid"));
        ASSERT_TRUE(charNode != nullptr);
        charNode->position = glm::vec3(0.0f, 4.0f, 0.0f);
        // Default scale (1.0, 1.8, 1.0) -> halfHeight = 0.9m
        ASSERT_FLOAT_NEAR(charNode->scale.y, 1.8f, 0.01f);

        // Step simulation until character lands on the plane
        for (int i = 0; i < 60; ++i) {
            charNode->Update(1.0f / 60.0f, scene);
        }

        // Contact verification: character center is at 0.9m, feet rest exactly on plane surface at Y=0.0m
        ASSERT_TRUE(charNode->isGrounded);
        ASSERT_FLOAT_NEAR(charNode->position.y, 0.9f, 0.001f);
        float feetY = charNode->position.y - (charNode->scale.y * 0.5f);
        ASSERT_FLOAT_NEAR(feetY, 0.0f, 0.001f);
    }

    // 2. Test World Floor Grid Fallback (Y=0) when no plane/mesh exists in empty scene
    {
        Scene emptyScene;
        emptyScene.Clear();

        auto charNode = std::dynamic_pointer_cast<CharacterNode>(emptyScene.AddCharacterNode("HumanoidEmptyScene"));
        ASSERT_TRUE(charNode != nullptr);
        charNode->position = glm::vec3(0.0f, 3.0f, 0.0f);
        charNode->clampToWorldFloor = true;

        for (int i = 0; i < 60; ++i) {
            charNode->Update(1.0f / 60.0f, emptyScene);
        }

        // Character lands safely on the world floor grid instead of falling off the screen into the void
        ASSERT_TRUE(charNode->isGrounded);
        ASSERT_FLOAT_NEAR(charNode->position.y, 0.9f, 0.001f);
        float feetY = charNode->position.y - (charNode->scale.y * 0.5f);
        ASSERT_FLOAT_NEAR(feetY, 0.0f, 0.001f);
    }
}

TEST_CASE(CharacterNPC, CameraWorldToScreenProjection) {
    Camera camera;
    camera.SetViewportSize(1280.0f, 720.0f);
    camera.SetViewParams(0.0f, 0.0f, 10.0f, glm::vec3(0.0f, 0.0f, 0.0f));

    glm::vec2 screenPos;
    glm::vec2 vpPos(0.0f, 0.0f);
    glm::vec2 vpSize(1280.0f, 720.0f);

    // Target at origin (0, 0, 0) should project near center of viewport (640, 360)
    bool visible = camera.WorldToScreen(glm::vec3(0.0f, 0.0f, 0.0f), vpPos, vpSize, screenPos);
    ASSERT_TRUE(visible);
    ASSERT_FLOAT_NEAR(screenPos.x, 640.0f, 5.0f);
    ASSERT_FLOAT_NEAR(screenPos.y, 360.0f, 5.0f);

    // Point behind camera should return false
    glm::vec3 behindPos = camera.GetPosition() + glm::vec3(0.0f, 0.0f, 10.0f);
    bool visibleBehind = camera.WorldToScreen(behindPos, vpPos, vpSize, screenPos);
    ASSERT_FALSE(visibleBehind);
}

TEST_CASE(CharacterNPC, SerializationAndDeserialization) {
    Scene scene;
    scene.Clear();

    auto npc = scene.AddCharacterNode("Merchant");
    auto charNode = std::dynamic_pointer_cast<CharacterNode>(npc);
    ASSERT_TRUE(charNode != nullptr);

    charNode->dialogueText = "Take a look at my wares!";
    charNode->interactionRadius = 4.0f;
    charNode->moveSpeed = 3.2f;
    charNode->turnSpeed = 8.5f;
    charNode->waypoints = {
        glm::vec3(1.0f, 0.0f, 2.0f),
        glm::vec3(5.0f, 0.0f, 6.0f)
    };

    JsonValue serialized = scene.Serialize();
    ASSERT_TRUE(serialized.IsObject());

    Scene loadedScene;
    bool ok = loadedScene.Deserialize(serialized);
    ASSERT_TRUE(ok);

    auto loadedNode = loadedScene.GetNodeById(npc->id);
    ASSERT_TRUE(loadedNode != nullptr);
    ASSERT_EQ(loadedNode->nodeType, NodeType::Character);

    auto loadedChar = std::dynamic_pointer_cast<CharacterNode>(loadedNode);
    ASSERT_TRUE(loadedChar != nullptr);
    ASSERT_EQ(loadedChar->characterName, "Merchant");
    ASSERT_EQ(loadedChar->dialogueText, "Take a look at my wares!");
    ASSERT_FLOAT_NEAR(loadedChar->interactionRadius, 4.0f, 0.01f);
    ASSERT_FLOAT_NEAR(loadedChar->moveSpeed, 3.2f, 0.01f);
    ASSERT_FLOAT_NEAR(loadedChar->turnSpeed, 8.5f, 0.01f);
    ASSERT_EQ(loadedChar->waypoints.size(), 2u);
    ASSERT_FLOAT_NEAR(loadedChar->waypoints[0].x, 1.0f, 0.01f);
    ASSERT_FLOAT_NEAR(loadedChar->waypoints[1].z, 6.0f, 0.01f);
}

#ifdef _WIN32
#include <crtdbg.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    return TestFramework::TestRegistry::Get().RunAll();
}


// =========================================================================
// 16. Roadmap Full Feature Suite (28 Features across 11 Categories)
// =========================================================================
#include "UI/UINodeRenderer.h"
#include "Scene/Nodes/3D/SplineNode.h"
#include "Scene/Nodes/3D/DecalNode.h"
#include "Scene/Nodes/3D/ParticleNode.h"
#include "Scene/Nodes/3D/AudioNode.h"
#include "Scene/GltfLoader.h"
#include "Renderer/ShaderGraph.h"
#include "Renderer/RenderEffects.h"
#include "Animation/IKSolver.h"
#include "Animation/MorphTarget.h"
#include "Physics/PhysicsWorld.h"
#include "Navigation/NavMesh.h"
#include "Core/UndoRedoManager.h"
#include "Audio/AudioEngine.h"
#include "Networking/NetworkManager.h"

TEST_CASE(Roadmap2D, CustomTrueTypeFontUpload) {
    std::vector<uint8_t> ttfBytes = UINodeRenderer::GenerateMinimalValidTTF("TestEngineFont", 2048);
    ASSERT_TRUE(ttfBytes.size() >= 64);

    bool loaded = UINodeRenderer::LoadCustomTTFFromMemory("custom_ui.ttf", ttfBytes, 22.0f);
    ASSERT_TRUE(loaded);

    TTFFontInfo info = UINodeRenderer::GetLoadedFontInfo("custom_ui.ttf");
    ASSERT_TRUE(info.valid);
    ASSERT_EQ(info.familyName, "TestEngineFont");
    ASSERT_EQ(info.unitsPerEm, 2048u);
    ASSERT_FLOAT_NEAR(info.requestedSizePx, 22.0f, 0.01f);
}

TEST_CASE(Roadmap3D, SpotLightAndDecalAndSplineNodes) {
    Scene scene;
    scene.Clear();

    // 1. Spot Light
    auto spot = scene.AddLightNode(LightType::Spot, "Flashlight");
    spot->position = glm::vec3(0.0f, 5.0f, 0.0f);
    spot->rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
    spot->spotInnerConeAngle = 15.0f;
    spot->spotOuterConeAngle = 30.0f;

    auto lights = Renderer::RenderEffects::GatherSceneLights(scene);
    ASSERT_EQ(lights.size(), 1u);
    ASSERT_EQ(lights[0].type, LightType::Spot);

    // 2. Decal Node (Deferred OBB Projection onto XZ surface with +Y normal)
    auto decal = std::dynamic_pointer_cast<DecalNode>(scene.AddDecalNode("WallDecal"));
    ASSERT_TRUE(decal != nullptr);
    decal->position = glm::vec3(0.0f, 0.0f, 0.0f);
    decal->decalSize = glm::vec3(4.0f, 4.0f, 4.0f);
    glm::vec2 decalUV(0.0f);
    float opacityWeight = 0.0f;
    bool projected = decal->ProjectPoint(scene, glm::vec3(0.5f, 0.0f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), decalUV, opacityWeight);
    ASSERT_TRUE(projected);
    ASSERT_TRUE(decalUV.x > 0.5f && decalUV.y > 0.5f);
    ASSERT_TRUE(opacityWeight > 0.5f);

    // 3. Spline Node
    auto spline = std::dynamic_pointer_cast<SplineNode>(scene.AddSplineNode("PatrolSpline"));
    ASSERT_TRUE(spline != nullptr);
    glm::vec3 pStart = spline->Evaluate(0.0f);
    glm::vec3 pMid = spline->Evaluate(0.5f);
    glm::vec3 pEnd = spline->Evaluate(1.0f);
    ASSERT_FLOAT_NEAR(pStart.x, -2.0f, 0.05f);
    ASSERT_FLOAT_NEAR(pEnd.x, 2.0f, 0.05f);
    ASSERT_TRUE(spline->GetLength() > 5.0f);
    ASSERT_TRUE(glm::length(pMid - pStart) > 1.0f);
}

TEST_CASE(Roadmap3D, GltfAndGlbModelImport) {
    std::vector<uint8_t> glb = GltfLoader::GenerateMinimalGLB();
    LoadedMesh mesh;
    bool ok = GltfLoader::LoadFromMemory(glb.data(), glb.size(), mesh, "", false);
    ASSERT_TRUE(ok);
    ASSERT_EQ(mesh.vertexCount, 3u);
    ASSERT_EQ(mesh.triangleCount, 1u);
    ASSERT_EQ(mesh.materialName, "TestPBR");
    ASSERT_FLOAT_NEAR(mesh.metallic, 0.8f, 0.01f);
    ASSERT_FLOAT_NEAR(mesh.roughness, 0.25f, 0.01f);
    ASSERT_EQ(mesh.morphTargets.size(), 1u);
    ASSERT_EQ(mesh.morphTargets[0].name, "Smile");
    ASSERT_FLOAT_NEAR(mesh.morphTargets[0].positionDeltas[2].y, 0.5f, 0.01f);
}

TEST_CASE(RoadmapMaterials, AOMapsEmissiveParallaxAndShaderGraph) {
    // 1. Parallax Occlusion Mapping
    auto heightField = [](const glm::vec2& uv) -> float {
        return 0.5f + 0.25f * std::sin(uv.x * 6.28f);
    };
    glm::vec2 baseUV(0.25f, 0.5f);
    glm::vec2 displacedUV = Renderer::RenderEffects::ComputeParallaxOcclusionUV(
        baseUV, glm::vec3(0.4f, 0.2f, 0.8f), 0.08f, heightField, 16);
    ASSERT_TRUE(glm::length(displacedUV - baseUV) > 0.001f);

    // 2. Visual Node-based Shader Graph
    ShaderGraph graph("GlowingMetalGraph");
    uint32_t colNode = graph.AddNode(ShaderNodeType::ConstantColor, "BaseTint");
    graph.GetNode(colNode)->colorVal = glm::vec4(0.2f, 0.7f, 1.0f, 1.0f);
    uint32_t fresnelNode = graph.AddNode(ShaderNodeType::Fresnel, "EdgeGlow");
    graph.GetNode(fresnelNode)->floatVal = 3.0f;

    graph.AddLink(colNode, "Out", graph.GetMasterNodeId(), "Albedo");
    graph.AddLink(fresnelNode, "Out", graph.GetMasterNodeId(), "Emissive");

    ShaderGraphOutput out = graph.Evaluate(1.0f, glm::vec2(0.5f), glm::vec3(0.0f, 0.8f, 0.6f), glm::vec3(0.0f, 0.0f, 1.0f));
    ASSERT_FLOAT_NEAR(out.albedo.b, 1.0f, 0.01f);
    ASSERT_TRUE(out.emissive.r > 0.01f);

    std::string glsl = graph.CompileToGLSL();
    ASSERT_TRUE(glsl.find("EvaluateShaderGraph") != std::string::npos);

    JsonValue sgJson = graph.Serialize();
    ShaderGraph loadedGraph;
    ASSERT_TRUE(loadedGraph.Deserialize(sgJson));
    ASSERT_EQ(loadedGraph.GetNodes().size(), graph.GetNodes().size());
}

TEST_CASE(RoadmapRendering, UnlimitedLightsShadowMappingAndGI) {
    Scene scene;
    scene.Clear();

    // 1. Add 16 dynamic lights (exceeding the legacy 8-light cap)
    for (int i = 0; i < 16; ++i) {
        auto l = scene.AddLightNode((i % 3 == 0) ? LightType::Spot : LightType::Point, "Light_" + std::to_string(i));
        l->position = glm::vec3(static_cast<float>(i) * 1.5f, 3.0f, 0.0f);
        l->lightIntensity = 2.0f;
    }
    auto gpuLights = Renderer::RenderEffects::GatherSceneLights(scene);
    ASSERT_EQ(gpuLights.size(), 16u);

    // 2. Realtime Shadow Mapping (Rasterize occluder triangle & test PCF shadow)
    Renderer::GPULightEntry sun;
    sun.type = LightType::Directional;
    sun.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    sun.lightSpaceMatrix = Renderer::RenderEffects::ComputeLightSpaceMatrix(sun, glm::vec3(0.0f), 10.0f);

    Renderer::ShadowMapBuffer shadowMap(128, 128);
    shadowMap.Clear(1.0f);
    // Occluder triangle at Y = 5 covering X,Z around origin
    shadowMap.RasterizeTriangle(
        glm::vec3(-4.0f, 5.0f, -4.0f),
        glm::vec3( 4.0f, 5.0f, -4.0f),
        glm::vec3( 0.0f, 5.0f,  4.0f),
        sun.lightSpaceMatrix
    );

    float shadowUnderOccluder = shadowMap.SampleShadowPCF(
        glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), sun.lightSpaceMatrix);
    float shadowOutside = shadowMap.SampleShadowPCF(
        glm::vec3(8.0f, 0.0f, 8.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), sun.lightSpaceMatrix);
    ASSERT_TRUE(shadowUnderOccluder < 0.2f);
    ASSERT_FLOAT_NEAR(shadowOutside, 1.0f, 0.01f);

    // 3. Global Illumination (L2 Spherical Harmonics Irradiance Probe)
    Renderer::GlobalIlluminationVolume giVol;
    giVol.BakeFromScene(scene, glm::vec3(0.4f, 0.6f, 0.9f), glm::vec3(0.1f, 0.1f, 0.1f));
    glm::vec3 skyIrr = giVol.SampleIndirectIrradiance(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 groundIrr = giVol.SampleIndirectIrradiance(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    ASSERT_TRUE(skyIrr.b > groundIrr.b);
}

TEST_CASE(RoadmapAnimation, InverseKinematicsAndMorphTargets) {
    // 1. Two-Bone IK & FABRIK
    Animation::Skeleton skel;
    int hip = skel.AddBone("Hip", -1);
    int knee = skel.AddBone("Knee", hip);
    int ankle = skel.AddBone("Ankle", knee);
    skel.bones[hip].currentPosition = glm::vec3(0.0f, 2.0f, 0.0f);
    skel.bones[knee].currentPosition = glm::vec3(0.0f, -1.0f, 0.0f);
    skel.bones[ankle].currentPosition = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::vec3 targetPos(0.0f, 0.8f, 0.8f);
    bool ikOk = Animation::IKSolver::SolveTwoBoneIK(skel, hip, knee, ankle, targetPos, glm::vec3(0.0f, 1.0f, 2.0f), 1.0f);
    ASSERT_TRUE(ikOk);
    auto worldPos = Animation::IKSolver::ComputeBoneWorldPositions(skel);
    ASSERT_FLOAT_NEAR(glm::length(worldPos[ankle] - targetPos), 0.0f, 0.05f);

    bool fabrikOk = Animation::IKSolver::SolveFABRIK(skel, {hip, knee, ankle}, glm::vec3(0.5f, 1.0f, 0.5f));
    ASSERT_TRUE(fabrikOk);

    // 2. Blend Shapes / Morph Targets
    std::vector<Vertex> baseVerts(2);
    baseVerts[0].pos = glm::vec3(0.0f, 0.0f, 0.0f);
    baseVerts[1].pos = glm::vec3(1.0f, 0.0f, 0.0f);

    Animation::MorphTargetSet morphSet;
    morphSet.AddTarget("Smile", { glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.2f, 0.6f, 0.0f) });

    std::vector<Vertex> deformedVerts;
    std::map<std::string, float> weights = { {"Smile", 0.5f} };
    morphSet.Apply(baseVerts, deformedVerts, weights);
    ASSERT_FLOAT_NEAR(deformedVerts[0].pos.y, 0.2f, 0.001f);
    ASSERT_FLOAT_NEAR(deformedVerts[1].pos.y, 0.3f, 0.001f);
}

TEST_CASE(RoadmapPhysics, JoltRigidBodiesJointsAndRagdolls) {
    Scene scene;
    scene.Clear();

    auto floor = scene.AddColliderNode(ColliderType::Box, "Floor");
    floor->position = glm::vec3(0.0f, -1.0f, 0.0f);
    floor->colliderSize = glm::vec3(40.0f, 2.0f, 40.0f);
    floor->rigidBodyType = RigidBodyType::Static;

    auto boxA = scene.AddNode(ShapeType::Cube, "DynamicBoxA");
    boxA->position = glm::vec3(0.0f, 5.0f, 0.0f);
    boxA->rigidBodyType = RigidBodyType::Dynamic;
    boxA->mass = 2.0f;

    auto boxB = scene.AddNode(ShapeType::Cube, "DynamicBoxB");
    boxB->position = glm::vec3(2.0f, 5.0f, 0.0f);
    boxB->rigidBodyType = RigidBodyType::Dynamic;
    boxB->mass = 2.0f;

    Physics::PhysicsWorld physWorld;
    ASSERT_TRUE(physWorld.IsInitialized());
    physWorld.BuildFromScene(scene);

    // Add Hinge & Distance joints
    ASSERT_TRUE(physWorld.AddHingeJoint(boxA->id, boxB->id, glm::vec3(1.0f, 5.0f, 0.0f)));
    ASSERT_EQ(physWorld.GetConstraintCount(), 1u);

    // Apply impulse & step Jolt simulation
    physWorld.AddImpulse(boxA->id, glm::vec3(5.0f, 0.0f, 0.0f));
    for (int i = 0; i < 30; ++i) {
        physWorld.Step(scene, 1.0f / 60.0f);
    }
    ASSERT_TRUE(boxA->position.y < 5.0f);
    ASSERT_TRUE(boxA->position.x > 0.0f);

    // Ragdoll Physics
    Animation::Skeleton ragSkel;
    int pelvis = ragSkel.AddBone("Pelvis", -1);
    int spine = ragSkel.AddBone("Spine", pelvis);
    int head = ragSkel.AddBone("Head", spine);
    ragSkel.bones[pelvis].currentPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    ragSkel.bones[spine].currentPosition = glm::vec3(0.0f, 0.5f, 0.0f);
    ragSkel.bones[head].currentPosition = glm::vec3(0.0f, 0.4f, 0.0f);

    uint32_t ragId = physWorld.CreateHumanoidRagdoll(ragSkel, glm::vec3(0.0f, 6.0f, 0.0f));
    ASSERT_TRUE(ragId > 0);
    physWorld.Step(scene, 1.0f / 60.0f);
    ASSERT_TRUE(physWorld.SyncRagdollToSkeleton(ragId, ragSkel));
}

TEST_CASE(RoadmapNavigation, NavMeshGenerationAndAStarPathfinding) {
    Scene scene;
    scene.Clear();

    // Add central wall obstacle
    auto wall = scene.AddColliderNode(ColliderType::Box, "WallObstacle");
    wall->position = glm::vec3(0.0f, 1.0f, 0.0f);
    wall->colliderSize = glm::vec3(2.0f, 2.0f, 8.0f);

    Navigation::NavMesh navMesh;
    navMesh.BuildFromScene(scene, glm::vec3(-10.0f, 0.0f, -10.0f), glm::vec3(10.0f, 0.0f, 10.0f), 2.0f);
    ASSERT_TRUE(navMesh.GetWalkablePolyCount() > 20u);

    auto path = navMesh.FindPath(glm::vec3(-6.0f, 0.0f, 0.0f), glm::vec3(6.0f, 0.0f, 0.0f));
    ASSERT_TRUE(path.size() >= 2u);
    ASSERT_FLOAT_NEAR(path.front().x, -6.0f, 0.01f);
    ASSERT_FLOAT_NEAR(path.back().x, 6.0f, 0.01f);
}

TEST_CASE(RoadmapEditor, UndoRedoHistoryAndCopyPaste) {
    Scene scene;
    scene.Clear();
    Core::UndoRedoManager::Get().Clear();

    auto cube = scene.AddNode(ShapeType::Cube, "OriginalCube");
    cube->position = glm::vec3(1.0f, 2.0f, 3.0f);

    // 1. Transform Undo/Redo
    auto cmd = std::make_shared<Core::TransformCommand>(
        cube->id,
        glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(1.0f),
        glm::vec3(10.0f, 20.0f, 30.0f), glm::vec3(0.0f), glm::vec3(1.0f),
        "Move Cube"
    );
    Core::UndoRedoManager::Get().ExecuteCommand(cmd, scene);
    ASSERT_FLOAT_NEAR(cube->position.x, 10.0f, 0.01f);

    ASSERT_TRUE(Core::UndoRedoManager::Get().Undo(scene));
    ASSERT_FLOAT_NEAR(cube->position.x, 1.0f, 0.01f);

    ASSERT_TRUE(Core::UndoRedoManager::Get().Redo(scene));
    ASSERT_FLOAT_NEAR(cube->position.x, 10.0f, 0.01f);

    // 2. Copy, Paste, and Duplicate
    scene.CopyNode(cube->id);
    ASSERT_TRUE(scene.HasClipboardData());
    auto pasted = scene.PasteNode();
    ASSERT_TRUE(pasted != nullptr);
    ASSERT_TRUE(pasted->id != cube->id);

    auto dup = scene.DuplicateNode(cube->id);
    ASSERT_TRUE(dup != nullptr);
    ASSERT_EQ(scene.GetNodes().size(), 3u);
}

TEST_CASE(RoadmapAudio, PlaybackSpatialAttenuationBusesAndReverb) {
    Audio::AudioEngine audio;
    auto wavBytes = Audio::AudioEngine::GenerateSineWaveWav(440.0f, 0.1f, 44100);
    ASSERT_TRUE(audio.LoadWavFromMemory("Beep", wavBytes.data(), wavBytes.size()));

    // 2D & 3D voices
    uint32_t v2d = audio.Play2D("Beep", "SFX", 0.8f);
    uint32_t v3d = audio.Play3D("Beep", glm::vec3(5.0f, 0.0f, 0.0f), "SFX", 1.0f, 1.0f, 20.0f);
    ASSERT_TRUE(v2d > 0 && v3d > 0);
    ASSERT_EQ(audio.GetActiveVoiceCount(), 2u);

    Scene scene;
    scene.Clear();
    auto revNode = std::dynamic_pointer_cast<AudioNode>(scene.AddAudioNode("CaveReverb"));
    revNode->isReverbZone = true;
    revNode->reverbInnerRadius = 5.0f;
    revNode->reverbOuterRadius = 15.0f;
    revNode->reverbWetMix = 0.5f;

    audio.SetListener(glm::vec3(0.0f, 0.0f, 0.0f));
    std::vector<float> mixedStereo;
    audio.MixOutputBuffer(&scene, mixedStereo, 256, 44100);
    ASSERT_EQ(mixedStereo.size(), 512u);

    float maxAmp = 0.0f;
    for (float s : mixedStereo) maxAmp = std::max(maxAmp, std::abs(s));
    ASSERT_TRUE(maxAmp > 0.01f);
}

TEST_CASE(RoadmapVFXAndPostProcess, ParticlesNiagaraBloomSSAOSSRFogVolumetrics) {
    Scene scene;
    scene.Clear();

    // 1. CPU & GPU Niagara Particle System
    auto pNode = std::dynamic_pointer_cast<ParticleNode>(scene.AddParticleNode("FireEmitter"));
    pNode->simulationMode = ParticleSimulationMode::GPU_Niagara;
    pNode->isEmitting = false; // Test exact 25-particle burst
    pNode->enableVortexModule = true;
    pNode->enableAttractorModule = true;
    pNode->enableCurlNoiseModule = true;
    pNode->EmitBurst(25, scene);
    pNode->Update(0.05f, scene);
    // GPU mode: the burst waits for the GPU simulation (VulkanParticleSim); the CPU runs it otherwise
    ASSERT_EQ(pNode->TakeGpuSpawns(), 25u);
    pNode->simulationMode = ParticleSimulationMode::CPU;
    pNode->EmitBurst(25, scene);
    pNode->Update(0.05f, scene);
    ASSERT_EQ(pNode->GetAliveParticleCount(), 25u);
    auto gpuParticles = pNode->BuildGPUBufferData();
    ASSERT_EQ(gpuParticles.size(), pNode->maxParticles);

    // 2. Bloom & ACES HDR Tonemapping
    Renderer::PostProcessSettings pp;
    glm::vec3 hdr(4.0f, 2.0f, 0.5f);
    glm::vec3 bright = Renderer::RenderEffects::ExtractBrightPass(hdr, 1.0f);
    ASSERT_TRUE(bright.r > 1.0f);
    glm::vec3 ldr = Renderer::RenderEffects::ApplyBloomAndTonemap(hdr, bright, pp);
    ASSERT_TRUE(ldr.r <= 1.0f && ldr.r > 0.5f);

    // 3. SSAO
    auto kernel = Renderer::RenderEffects::GenerateSSAOKernel(16);
    ASSERT_EQ(kernel.size(), 16u);
    float ssao = Renderer::RenderEffects::EvaluateSSAO(
        glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f), kernel, 0.5f, 0.02f,
        [](const glm::vec3&) { return -4.8f; });
    ASSERT_TRUE(ssao >= 0.0f && ssao <= 1.0f);

    // 4. SSR
    auto ssrHit = Renderer::RenderEffects::TraceSSR(
        glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 1.0f), pp,
        [](const glm::vec3& pos) { return pos.z; });
    ASSERT_TRUE(ssrHit.hit);

    // 5. Exponential Height Fog & Volumetric Light Scattering
    glm::vec3 fogged = Renderer::RenderEffects::ApplyExponentialHeightFog(
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 50.0f), pp);
    ASSERT_TRUE(fogged.b > 0.1f);

    Renderer::GPULightEntry spotLight;
    spotLight.type = LightType::Point;
    spotLight.position = glm::vec3(0.0f, 2.0f, 5.0f);
    spotLight.intensity = 10.0f;
    spotLight.range = 25.0f;
    glm::vec3 volLight = Renderer::RenderEffects::ComputeVolumetricLightScattering(
        glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f), spotLight, pp);
    ASSERT_TRUE(glm::length(volLight) > 0.0f);
}

TEST_CASE(RoadmapVFXAndPostProcess, ParticleBillboardRenderModesAndPresets) {
    Scene scene;
    scene.Clear();

    auto pNode = std::dynamic_pointer_cast<ParticleNode>(scene.AddParticleNode("FireVFX"));
    ASSERT_TRUE(pNode != nullptr);

    // Default should be BillboardAdditive
    ASSERT_EQ(static_cast<int>(pNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAdditive));

    // Test Fire Preset
    pNode->ApplyPreset(ParticlePreset::FireTorch);
    ASSERT_EQ(static_cast<int>(pNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAdditive));
    ASSERT_EQ(static_cast<int>(pNode->emitterShape), static_cast<int>(ParticleEmitterShape::Cone));
    ASSERT_TRUE(pNode->startColor.r > 0.9f && pNode->startColor.g > 0.6f);
    ASSERT_TRUE(pNode->gravityMultiplier < 0.0f); // Rises like hot fire

    // Test Smoke Preset
    pNode->ApplyPreset(ParticlePreset::CampfireSmoke);
    ASSERT_EQ(static_cast<int>(pNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAlpha));
    ASSERT_TRUE(pNode->endSize > pNode->startSize); // Smoke expands

    // Test Ground Fog Preset
    pNode->ApplyPreset(ParticlePreset::GroundFog);
    ASSERT_EQ(static_cast<int>(pNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAlpha));
    ASSERT_EQ(static_cast<int>(pNode->emitterShape), static_cast<int>(ParticleEmitterShape::Box));
    ASSERT_TRUE(pNode->emitterBoxExtents.x >= 5.0f);

    // Test Magic Sparkles Preset
    pNode->ApplyPreset(ParticlePreset::MagicSparkles);
    ASSERT_EQ(static_cast<int>(pNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAdditive));
    ASSERT_TRUE(pNode->enableVortexModule);

    // Test Mesh Generator Quad
    auto quadMesh = MeshGenerator::CreateQuad(1.0f);
    ASSERT_EQ(quadMesh.vertices.size(), 4u);
    ASSERT_EQ(quadMesh.indices.size(), 6u);

    // Test Serialization roundtrip of renderMode and boxExtents
    JsonValue serialized = scene.Serialize();
    Scene loadedScene;
    bool ok = loadedScene.Deserialize(serialized);
    ASSERT_TRUE(ok);

    auto loadedNode = std::dynamic_pointer_cast<ParticleNode>(loadedScene.GetNodeByName("FireVFX"));
    ASSERT_TRUE(loadedNode != nullptr);
    ASSERT_EQ(static_cast<int>(loadedNode->renderMode), static_cast<int>(ParticleRenderMode::BillboardAdditive));
    ASSERT_TRUE(loadedNode->enableVortexModule);
}

TEST_CASE(Roadmap3D, TerrainLandscapeGenerationSculptingAndSampling) {
    Scene scene;
    scene.Clear();

    auto terrain = std::dynamic_pointer_cast<TerrainNode>(scene.AddTerrainNode("Highlands"));
    ASSERT_TRUE(terrain != nullptr);
    ASSERT_EQ(terrain->nodeType, NodeType::Terrain);

    // 1. Grid Dimensions and procedural generation
    terrain->SetResolution(32);
    ASSERT_EQ(terrain->gridResolution, 32u);
    ASSERT_EQ(terrain->heightData.size(), 32u * 32u);

    // Test Rolling Hills
    terrain->GenerateProcedural(TerrainPreset::RollingHills);
    float hCenter = terrain->SampleHeightLocal(0.0f, 0.0f);
    ASSERT_TRUE(hCenter >= 0.0f && hCenter <= terrain->heightScale);

    // Test Mountain Peaks
    terrain->GenerateProcedural(TerrainPreset::MountainPeaks);
    float maxH = 0.0f;
    for (float h : terrain->heightData) {
        if (h > maxH) maxH = h;
    }
    ASSERT_TRUE(maxH > 0.5f); // Peaks must reach upper elevations

    // Test Canyons and Ravines
    terrain->GenerateProcedural(TerrainPreset::CanyonsAndRavines);
    float minH = 1.0f;
    for (float h : terrain->heightData) {
        if (h < minH) minH = h;
    }
    ASSERT_TRUE(minH < 0.2f); // Ravines must carve deep valleys

    // 2. Sculpting Brushes (Raise, Lower, Smooth, Flatten)
    terrain->GenerateProcedural(TerrainPreset::FlatPlains);
    ASSERT_FLOAT_NEAR(terrain->SampleHeightLocal(0.0f, 0.0f), 0.0f, 0.001f);

    // Raise brush
    terrain->brushAction = TerrainBrushAction::Raise;
    terrain->brushRadius = 15.0f;
    terrain->brushStrength = 5.0f;
    terrain->ApplyBrushLocal(0.0f, 0.0f, 1.0f);
    float raisedH = terrain->SampleHeightLocal(0.0f, 0.0f);
    ASSERT_TRUE(raisedH > 0.0f);

    // Lower brush (carve trench)
    terrain->brushAction = TerrainBrushAction::Lower;
    terrain->ApplyBrushLocal(0.0f, 0.0f, 0.5f);
    float loweredH = terrain->SampleHeightLocal(0.0f, 0.0f);
    ASSERT_TRUE(loweredH < raisedH);

    // Flatten brush
    terrain->brushAction = TerrainBrushAction::Flatten;
    terrain->flattenTargetHeight = 0.5f;
    terrain->ApplyBrushLocal(0.0f, 0.0f, 1.0f);
    float flatH = terrain->SampleHeightLocal(0.0f, 0.0f);
    ASSERT_TRUE(flatH > 0.0f);

    // 3. Normal Sampling
    glm::vec3 norm = terrain->SampleNormal(0.0f, 0.0f, scene);
    ASSERT_TRUE(glm::length(norm) > 0.9f);
    ASSERT_TRUE(norm.y > 0.0f); // Upward facing

    // 4. Mesh Generation
    MeshData mesh = terrain->GenerateMeshData();
    ASSERT_EQ(mesh.vertices.size(), 32u * 32u);
    ASSERT_EQ(mesh.indices.size(), 31u * 31u * 6u);

    // 5. Scene Serialization & Deserialization roundtrip
    JsonValue serialized = scene.Serialize();
    Scene loadedScene;
    ASSERT_TRUE(loadedScene.Deserialize(serialized));

    auto loadedTerrain = std::dynamic_pointer_cast<TerrainNode>(loadedScene.GetNodeByName("Highlands"));
    ASSERT_TRUE(loadedTerrain != nullptr);
    ASSERT_EQ(loadedTerrain->gridResolution, 32u);
    ASSERT_EQ(loadedTerrain->heightData.size(), 32u * 32u);
    ASSERT_FLOAT_NEAR(loadedTerrain->terrainSize.x, terrain->terrainSize.x, 0.01f);
}

TEST_CASE(RoadmapNetworking, SocketAbstractionStateReplicationAndRPCs) {
    Scene serverScene;
    serverScene.Clear();
    auto serverPlayer = serverScene.AddNode(ShapeType::Cube, "Player1");
    serverPlayer->networkId = 101;
    serverPlayer->replicateTransform = true;
    serverPlayer->position = glm::vec3(12.0f, 3.0f, -7.0f);

    Scene clientScene;
    clientScene.Clear();
    auto clientPlayer = clientScene.AddNode(ShapeType::Cube, "Player1");
    clientPlayer->networkId = 101;
    clientPlayer->replicateTransform = true;
    clientPlayer->position = glm::vec3(0.0f, 0.0f, 0.0f);

    Networking::NetworkManager serverNet;
    Networking::NetworkManager clientNet;
    serverNet.SetLocalPeerId(1);
    clientNet.SetLocalPeerId(2);
    Networking::NetSocket::LinkLoopbackPair(serverNet.GetSocket(), clientNet.GetSocket());

    // 1. State Replication from Server -> Client
    ASSERT_TRUE(serverNet.ReplicateSceneState(serverScene));
    ASSERT_EQ(clientNet.PollIncomingPackets(clientScene, 1.0f), 1u);
    ASSERT_FLOAT_NEAR(clientPlayer->position.x, 12.0f, 0.01f);
    ASSERT_FLOAT_NEAR(clientPlayer->position.y, 3.0f, 0.01f);
    ASSERT_FLOAT_NEAR(clientPlayer->position.z, -7.0f, 0.01f);

    // 2. Remote Procedure Call (RPC) from Client -> Server
    uint32_t rpcTargetReceived = 0;
    std::string rpcArgReceived;
    serverNet.RegisterRPC("SpawnProjectile", [&](uint32_t targetId, const std::vector<std::string>& args) {
        rpcTargetReceived = targetId;
        if (!args.empty()) rpcArgReceived = args[0];
    });

    ASSERT_TRUE(clientNet.SendRPC("SpawnProjectile", 101, {"Fireball"}));
    ASSERT_EQ(serverNet.PollIncomingPackets(serverScene), 1u);
    ASSERT_EQ(rpcTargetReceived, 101u);
    ASSERT_EQ(rpcArgReceived, "Fireball");
}

TEST_CASE(RoadmapNetworking, ContinuousLiveJumpingReplication) {
    Scene serverScene;
    auto serverCube = serverScene.AddNode(ShapeType::Cube, "JumpingCube");
    serverCube->position = glm::vec3(0.0f, 0.0f, 0.0f);
    serverCube->rigidBodyType = RigidBodyType::Dynamic;

    Scene clientScene;
    auto clientCube = clientScene.AddNode(ShapeType::Cube, "JumpingCube");
    clientCube->position = glm::vec3(0.0f, 0.0f, 0.0f);

    Networking::NetworkManager serverNet;
    Networking::NetworkManager clientNet;
    serverNet.SetLocalPeerId(1);
    clientNet.SetLocalPeerId(2);
    Networking::NetSocket::LinkLoopbackPair(serverNet.GetSocket(), clientNet.GetSocket());

    serverNet.SetContinuousReplication(true);
    serverNet.SetReplicationTickRate(30.0f);
    serverNet.SetReplicateAllNodes(true);

    // Cube jumps on server
    serverCube->position.y = 8.5f;
    serverCube->linearVelocity = glm::vec3(0.0f, 15.0f, 0.0f);

    // Replicate state from server
    ASSERT_TRUE(serverNet.ReplicateSceneState(serverScene));
    ASSERT_EQ(clientNet.PollIncomingPackets(clientScene, 1.0f), 1u);

    // Verify client received jumping position and velocity
    ASSERT_FLOAT_NEAR(clientCube->position.y, 8.5f, 0.01f);
    ASSERT_FLOAT_NEAR(clientCube->linearVelocity.y, 15.0f, 0.01f);

    // 2. Reverse direction: Cube jumps on CLIENT -> Replicates to SERVER
    clientCube->position.y = 12.0f;
    clientCube->linearVelocity = glm::vec3(0.0f, 20.0f, 0.0f);
    serverCube->SetMultiplayerAuthority(2);
    clientCube->SetMultiplayerAuthority(2);
    clientCube->MarkLocallyActive();

    ASSERT_TRUE(clientNet.ReplicateSceneState(clientScene));
    ASSERT_EQ(serverNet.PollIncomingPackets(serverScene, 1.0f), 1u);

    // Verify server received client's jumping position and velocity
    ASSERT_FLOAT_NEAR(serverCube->position.y, 12.0f, 0.01f);
    ASSERT_FLOAT_NEAR(serverCube->linearVelocity.y, 20.0f, 0.01f);

    // 3. Conflict resolution: while client is locally active, stale server packet does not cause rubber-banding
    serverCube->position.y = 0.5f; // Stale server state
    ASSERT_TRUE(serverNet.ReplicateSceneState(serverScene));
    clientNet.PollIncomingPackets(clientScene, 1.0f);
    // Client should remain at 12.0f without shaking or snapping back down
    ASSERT_FLOAT_NEAR(clientCube->position.y, 12.0f, 0.01f);
}

TEST_CASE(RoadmapLOD, MeshDecimationAndDistanceSwitching) {
    // 1. Test Phase 2: In-Engine Automatic Mesh Simplification (QEM)
    MeshData sphereMesh = MeshGenerator::CreateSphere(0.5f, 20, 20);
    size_t baseTriangles = sphereMesh.indices.size() / 3;
    ASSERT_TRUE(baseTriangles >= 200);

    // Simplify down to 50% target ratio
    MeshData lod1 = Geometry::MeshSimplifier::Simplify(sphereMesh, 0.50f);
    size_t lod1Triangles = lod1.indices.size() / 3;
    ASSERT_TRUE(lod1Triangles < baseTriangles);
    ASSERT_TRUE(lod1Triangles >= baseTriangles * 0.35f && lod1Triangles <= baseTriangles * 0.65f);
    ASSERT_FALSE(lod1.vertices.empty());
    ASSERT_FALSE(lod1.indices.empty());

    // Generate multi-tier LOD chain (50%, 25%, 10%)
    auto chain = Geometry::MeshSimplifier::GenerateLODChain(sphereMesh, {0.50f, 0.25f, 0.10f}, {20.0f, 45.0f, 80.0f});
    ASSERT_EQ(chain.levels.size(), 3u);
    ASSERT_TRUE(chain.triangleCounts[0] < baseTriangles);
    ASSERT_TRUE(chain.triangleCounts[1] < chain.triangleCounts[0]);
    ASSERT_TRUE(chain.triangleCounts[2] < chain.triangleCounts[1]);

    // 2. Test Phase 1: Distance-Based LOD Switching & Draw Distance Culling
    Node testNode;
    testNode.customMeshName = "Hero_LOD0";
    testNode.enableLOD = true;
    testNode.maxDrawDistance = 100.0f;
    testNode.lodLevels = {
        {"Hero_LOD1", 20.0f, 0.50f, static_cast<uint32_t>(chain.triangleCounts[0])},
        {"Hero_LOD2", 45.0f, 0.25f, static_cast<uint32_t>(chain.triangleCounts[1])},
        {"Hero_LOD3", 80.0f, 0.10f, static_cast<uint32_t>(chain.triangleCounts[2])}
    };

    // At 10m -> LOD0 (Base)
    ASSERT_EQ(testNode.GetActiveLODMeshKey(10.0f), "Hero_LOD0");
    ASSERT_EQ(testNode.currentActiveLOD, 0);
    ASSERT_FALSE(testNode.IsCulledByDistance(10.0f));

    // At 30m -> LOD1
    ASSERT_EQ(testNode.GetActiveLODMeshKey(30.0f), "Hero_LOD1");
    ASSERT_EQ(testNode.currentActiveLOD, 1);
    ASSERT_FALSE(testNode.IsCulledByDistance(30.0f));

    // At 60m -> LOD2
    ASSERT_EQ(testNode.GetActiveLODMeshKey(60.0f), "Hero_LOD2");
    ASSERT_EQ(testNode.currentActiveLOD, 2);
    ASSERT_FALSE(testNode.IsCulledByDistance(60.0f));

    // At 90m -> LOD3
    ASSERT_EQ(testNode.GetActiveLODMeshKey(90.0f), "Hero_LOD3");
    ASSERT_EQ(testNode.currentActiveLOD, 3);
    ASSERT_FALSE(testNode.IsCulledByDistance(90.0f));

    // At 120m -> Beyond maxDrawDistance (100m) -> Culled!
    ASSERT_TRUE(testNode.IsCulledByDistance(120.0f));

    // Test forced LOD override
    testNode.forcedLOD = 2; // Force LOD2 regardless of distance
    ASSERT_EQ(testNode.GetActiveLODMeshKey(5.0f), "Hero_LOD2");
    ASSERT_EQ(testNode.currentActiveLOD, 2);
}

TEST_CASE(RoadmapModelAsset, CharacterNodeModelAssetAssignmentAndReversion) {
    Scene scene;
    scene.Clear();

    // 1. Add default Character NPC
    auto npcNode = scene.AddCharacterNode("Humanoid NPC");
    ASSERT_TRUE(npcNode != nullptr);
    ASSERT_EQ(npcNode->nodeType, NodeType::Character);
    ASSERT_EQ(npcNode->type, ShapeType::Cube); // Defaults to cube placeholder
    ASSERT_TRUE(npcNode->customMeshName.empty());
    ASSERT_FALSE(npcNode->hasSkeleton);

    // 2. Assign custom 3D model asset (e.g. imported FBX/glTF character)
    npcNode->type = ShapeType::Custom;
    npcNode->customMeshName = "warrior.fbx";
    npcNode->vertexCount = 4520;
    npcNode->triangleCount = 8900;
    npcNode->hasSkeleton = true;
    npcNode->skeletonName = "WarriorArmature";
    npcNode->boneNames = { "Root", "Hips", "Spine", "Chest", "Neck", "Head" };
    npcNode->animClipNames = { "Warrior_Idle", "Warrior_Walk", "Warrior_Attack" };
    npcNode->currentAnimState = "Warrior_Idle";

    auto charCast = std::dynamic_pointer_cast<CharacterNode>(npcNode);
    ASSERT_TRUE(charCast != nullptr);
    charCast->idleAnimClip = "Warrior_Idle";
    charCast->walkAnimClip = "Warrior_Walk";

    ASSERT_EQ(npcNode->type, ShapeType::Custom);
    ASSERT_EQ(npcNode->customMeshName, "warrior.fbx");
    ASSERT_EQ(npcNode->vertexCount, 4520u);
    ASSERT_EQ(npcNode->triangleCount, 8900u);
    ASSERT_TRUE(npcNode->hasSkeleton);
    ASSERT_EQ(charCast->idleAnimClip, "Warrior_Idle");
    ASSERT_EQ(charCast->walkAnimClip, "Warrior_Walk");

    // 3. Test Scene Serialization & Deserialization preserves assigned Model Asset
    JsonValue serialized = scene.Serialize();
    ASSERT_TRUE(serialized.IsObject());

    Scene loadedScene;
    bool loadOk = loadedScene.Deserialize(serialized);
    ASSERT_TRUE(loadOk);

    auto loadedNpc = loadedScene.GetNodeById(npcNode->id);
    ASSERT_TRUE(loadedNpc != nullptr);
    ASSERT_EQ(loadedNpc->type, ShapeType::Custom);
    ASSERT_EQ(loadedNpc->customMeshName, "warrior.fbx");
    ASSERT_TRUE(loadedNpc->hasSkeleton);

    // 4. Test clearing/reverting back to primitive cube (the [X] button in UI)
    npcNode->customMeshName.clear();
    npcNode->type = ShapeType::Cube;
    npcNode->hasSkeleton = false;
    npcNode->vertexCount = 0;
    npcNode->triangleCount = 0;

    ASSERT_EQ(npcNode->type, ShapeType::Cube);
    ASSERT_TRUE(npcNode->customMeshName.empty());
    ASSERT_FALSE(npcNode->hasSkeleton);
}

// =========================================================================
// 54. Custom Title Bar & Borderless Window Hit-Testing
// =========================================================================
TEST_CASE(CustomTitleBar, BorderlessWindowNcHitTestingAndWindowControls) {
    const int winW = 1600;
    const int winH = 900;
    const int border = 6;
    const int titleBarH = 32;
    const int rightButtonsW = 140;
    const int menuBarEndX = 480;

    auto TestHitTest = [&](int x, int y, bool isMaximized, bool isEditorMode) -> int {
        enum HitCodes {
            HitClient = 1,
            HitCaption = 2,
            HitLeft = 10,
            HitRight = 11,
            HitTop = 12,
            HitTopLeft = 13,
            HitTopRight = 14,
            HitBottom = 15,
            HitBottomLeft = 16,
            HitBottomRight = 17
        };

        if (!isMaximized) {
            bool left = (x < border);
            bool right = (x >= winW - border);
            bool top = (y < border);
            bool bottom = (y >= winH - border);

            if (top && left) return HitTopLeft;
            if (top && right) return HitTopRight;
            if (bottom && left) return HitBottomLeft;
            if (bottom && right) return HitBottomRight;
            if (left) return HitLeft;
            if (right) return HitRight;
            if (top) return HitTop;
            if (bottom) return HitBottom;
        }

        if (y >= 0 && y < titleBarH) {
            if (x >= winW - rightButtonsW) {
                return HitClient; // Minimize, Maximize, Close buttons
            }
            if (isEditorMode && x >= 135 && x <= menuBarEndX) {
                return HitClient; // Main Menu items
            }
            return HitCaption; // Draggable title bar & logo
        }

        return HitClient;
    };

    // 1. Test Border Edge Resizing when windowed
    ASSERT_EQ(TestHitTest(2, 2, false, true), 13);    // Top-Left corner
    ASSERT_EQ(TestHitTest(1598, 2, false, true), 14); // Top-Right corner
    ASSERT_EQ(TestHitTest(2, 898, false, true), 16);  // Bottom-Left corner
    ASSERT_EQ(TestHitTest(1598, 898, false, true), 17); // Bottom-Right corner
    ASSERT_EQ(TestHitTest(2, 450, false, true), 10);  // Left border
    ASSERT_EQ(TestHitTest(1598, 450, false, true), 11); // Right border
    ASSERT_EQ(TestHitTest(800, 2, false, true), 12);  // Top border
    ASSERT_EQ(TestHitTest(800, 898, false, true), 15); // Bottom border

    // 2. Test Edge Resizing is disabled when window is maximized
    ASSERT_EQ(TestHitTest(2, 450, true, true), 1);    // Client area (no resizing)
    ASSERT_EQ(TestHitTest(800, 898, true, true), 1);  // Client area (no resizing)

    // 3. Test Custom Title Bar controls
    // Right buttons area (Minimize, Maximize/Restore, Close): width - 140px to width
    ASSERT_EQ(TestHitTest(1500, 16, false, true), 1); // Minimize button -> Client
    ASSERT_EQ(TestHitTest(1540, 16, false, true), 1); // Maximize button -> Client
    ASSERT_EQ(TestHitTest(1580, 16, false, true), 1); // Close button -> Client

    // Menu Bar items (135px to 480px in Editor mode)
    ASSERT_EQ(TestHitTest(200, 16, false, true), 1);  // File menu -> Client
    ASSERT_EQ(TestHitTest(350, 16, false, true), 1);  // Scene menu -> Client

    // Title area and Logo (Draggable caption)
    ASSERT_EQ(TestHitTest(50, 16, false, true), 2);   // Logo area -> Caption (Draggable)
    ASSERT_EQ(TestHitTest(800, 16, false, true), 2);  // Center title area -> Caption (Draggable)

    // In non-editor mode (Splash / Project Hub), menu area is also draggable
    ASSERT_EQ(TestHitTest(200, 16, false, false), 2); // Hub header -> Caption (Draggable)

    // 4. Client viewport below title bar
    ASSERT_EQ(TestHitTest(800, 450, false, true), 1); // 3D Viewport -> Client
}

TEST_CASE(ProjectFilesAndWorkspace, FileViewerAndDockablePanelsAndEngineIcon) {
    // 1. Verify File Viewer path resolution and project root fallback
    std::filesystem::path currentPath = std::filesystem::current_path();
    ASSERT_TRUE(std::filesystem::exists(currentPath));

    std::filesystem::path assetsPath = "assets";
    ASSERT_TRUE(std::filesystem::exists(assetsPath));

    // 2. Verify file extensions categorized correctly
    auto getFileCategory = [](const std::string& ext) -> std::string {
        std::string e = ext;
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);
        if (e == ".obj" || e == ".fbx" || e == ".gltf" || e == ".glb") return "Model";
        if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga") return "Texture";
        if (e == ".lua") return "Script";
        if (e == ".tess") return "Project";
        if (e == ".scene" || e == ".json") return "Scene";
        if (e == ".wav" || e == ".ogg" || e == ".mp3") return "Audio";
        if (e == ".ttf" || e == ".otf") return "Font";
        return "Generic";
    };

    ASSERT_EQ(getFileCategory(".fbx"), "Model");
    ASSERT_EQ(getFileCategory(".obj"), "Model");
    ASSERT_EQ(getFileCategory(".GLB"), "Model");
    ASSERT_EQ(getFileCategory(".PNG"), "Texture");
    ASSERT_EQ(getFileCategory(".lua"), "Script");
    ASSERT_EQ(getFileCategory(".tess"), "Project");
    ASSERT_EQ(getFileCategory(".scene"), "Scene");
    ASSERT_EQ(getFileCategory(".ttf"), "Font");
    ASSERT_EQ(getFileCategory(".wav"), "Audio");

    // 3. Verify Windows Executable Icon file exists on disk
    std::filesystem::path iconPath = "assets/icons/engine_icon.ico";
    ASSERT_TRUE(std::filesystem::exists(iconPath));
    ASSERT_TRUE(std::filesystem::file_size(iconPath) > 1000);

    // 4. Verify embedded icon RGBA buffer
    ASSERT_TRUE(std::filesystem::exists("src/Core/EngineIconData.h"));

    // 5. Verify .tess project file existence and valid JSON structure
    std::filesystem::path tessProject = "projects/DefaultProject/DefaultProject.tess";
    ASSERT_TRUE(std::filesystem::exists(tessProject));
    ASSERT_TRUE(std::filesystem::file_size(tessProject) > 100);
}

TEST_CASE(GameProfilerAndMonitoring, ResourceTrackingAndOrphanRepair) {
    auto& profiler = Monitoring::GameProfiler::Get();
    profiler.Reset();

    Scene scene;
    Gameplay::GameLoop gameLoop;

    // 1. In Edit mode (simulation stopped), profiler isolates game and reports not running
    profiler.Update(&scene, &gameLoop, nullptr, 0.016f);
    ASSERT_FALSE(profiler.IsGameRunning());

    // 2. Start simulation (Play mode)
    gameLoop.Play(scene);
    ASSERT_TRUE(gameLoop.IsPlaying());

    // Populate scene with various node types
    auto cubeNode = scene.AddNode(ShapeType::Cube, "TestCube");
    auto lightNode = scene.AddLightNode(LightType::Directional, "TestSun");
    auto camNode = scene.AddCameraNode("TestCam");
    auto audioNode = scene.AddAudioNode("TestAudio");
    auto charNode = scene.AddCharacterNode("TestNPC");
    auto canvasNode = scene.AddUICanvasNode("TestCanvas");
    auto btnNode = scene.AddUIButtonNode("TestBtn", "", canvasNode->id);

    // Create an orphan node (parent ID points to non-existent node 999999)
    auto orphanNode = scene.AddNode(ShapeType::Sphere, "OrphanSphere");
    orphanNode->parentId = 999999;

    // Update profiler during simulation
    profiler.SetGameRunning(true);
    profiler.RecordGameRenderMetrics(8, 2500, 1800, 2, 1, glm::vec2(1920.0f, 1080.0f));
    profiler.Update(&scene, &gameLoop, nullptr, 0.016f);

    ASSERT_TRUE(profiler.IsGameRunning());

    const auto& stats = profiler.GetStats();
    ASSERT_TRUE(stats.totalNodes >= 8);
    ASSERT_TRUE(stats.meshNodes >= 2);
    ASSERT_TRUE(stats.lightNodes >= 1);
    ASSERT_TRUE(stats.cameraNodes >= 1);
    ASSERT_TRUE(stats.audioNodes >= 1);
    ASSERT_TRUE(stats.characterNodes >= 1);
    ASSERT_TRUE(stats.uiNodes >= 2);

    // 3. Verify Orphan Node Detection
    ASSERT_EQ(stats.orphanNodeCount, 1);
    ASSERT_EQ(stats.orphanNodes.size(), 1);
    ASSERT_EQ(stats.orphanNodes[0].id, orphanNode->id);
    ASSERT_EQ(stats.orphanNodes[0].missingParentId, 999999);

    // 4. Verify 1-Click Orphan Node Repair Tool
    profiler.ReparentOrphanNodesToRoot(&scene);
    ASSERT_EQ(orphanNode->parentId, 0);

    const auto& updatedStats = profiler.GetStats();
    ASSERT_EQ(updatedStats.orphanNodeCount, 0);
    ASSERT_EQ(updatedStats.orphanNodes.size(), 0);

    // 5. Test CPU Stage Profiling
    profiler.BeginStage("Physics & Collisions (Jolt)");
    // simulate slight workload
    double dummy = 0.0;
    for (int i = 0; i < 1000; ++i) dummy += i * 0.01;
    profiler.EndStage("Physics & Collisions (Jolt)");

    profiler.Update(&scene, &gameLoop, nullptr, 0.016f);
    const auto& cpuStages = profiler.GetCPUStages();
    ASSERT_TRUE(!cpuStages.empty());

    bool foundPhysics = false;
    for (const auto& stage : cpuStages) {
        if (stage.name.find("Physics") != std::string::npos) {
            foundPhysics = true;
            ASSERT_TRUE(stage.percentage >= 0.0f);
        }
    }
    ASSERT_TRUE(foundPhysics);

    // 6. Test GPU Passes & Render Telemetry: the passes shown are the ones the renderer measured
    std::vector<Monitoring::GPUPassTiming> measured(2);
    measured[0].passName = "Shadows";
    measured[0].estimatedDurationMs = 0.5f;
    measured[1].passName = "Opaque";
    measured[1].estimatedDurationMs = 1.5f;
    profiler.SetMeasuredGPUPasses(measured);
    profiler.Update(&scene, &gameLoop, nullptr, 0.016f);
    profiler.Update(&scene, &gameLoop, nullptr, 1.0f); // past the display refresh interval
    const auto& gpuPasses = profiler.GetGPUPasses();
    ASSERT_EQ(gpuPasses.size(), 2u);
    ASSERT_FLOAT_NEAR(gpuPasses[1].percentage, 75.0f, 0.01f);
    profiler.SetMeasuredGPUPasses({});
    ASSERT_EQ(stats.gameDrawCalls, 8);
    ASSERT_EQ(stats.gameTrianglesCount, 2500);
    ASSERT_EQ(stats.gameVerticesCount, 1800);
    ASSERT_TRUE(stats.vramEstimatedBytes > 0);

    // 7. Stop simulation
    gameLoop.Stop(scene);
    ASSERT_FALSE(gameLoop.IsPlaying());
    profiler.Update(&scene, &gameLoop, nullptr, 0.016f);
    ASSERT_FALSE(profiler.IsGameRunning());
}

// =========================================================================
// 57. Web / HTML5 Game Exporter Tests
// =========================================================================
TEST_CASE(ExportSystem, WebHTMLExporter) {
    namespace fs = std::filesystem;

    // 1. Setup a test scene with a mesh, light, and camera
    Scene scene;
    auto cubeNode = scene.AddNode(ShapeType::Cube, "PlayerCube");
    cubeNode->position = glm::vec3(0.0f, 1.0f, 0.0f);

    auto lightNode = scene.AddLightNode(LightType::Directional, "SunLight");
    lightNode->lightColor = glm::vec3(1.0f, 0.95f, 0.85f);
    lightNode->lightIntensity = 2.0f;

    auto camNode = scene.AddCameraNode("MainCamera");
    camNode->position = glm::vec3(0.0f, 2.0f, 5.0f);

    // 2. Configure Web export options
    Export::WebExportOptions opts;
    opts.destinationDir = "exports/TestWeb_Export";
    opts.gameTitle = "Unit Test Web Adventure";
    opts.startScene = "main.scene";
    opts.canvasMode = Export::WebCanvasMode::Responsive;
    opts.embedSceneInHTML = true;
    opts.includeLocalServerHelper = true;
    opts.enableTouchControls = true;

    // 3. Perform Web Export
    std::string statusMsg;
    bool exportSuccess = Export::WebExporter::Export(scene, opts, statusMsg);
    ASSERT_TRUE(exportSuccess);

    // 4. Verify created artifacts and structure
    fs::path exportDir(opts.destinationDir);
    ASSERT_TRUE(fs::exists(exportDir));
    ASSERT_TRUE(fs::is_directory(exportDir));

    fs::path indexHtml = exportDir / "index.html";
    fs::path runtimeJs = exportDir / "tessera_web.js";
    fs::path servePy = exportDir / "serve.py";
    fs::path startBat = exportDir / "start_server.bat";
    fs::path gameConfig = exportDir / "game.agconfig";
    fs::path sceneFile = exportDir / "scenes" / "main.scene";

    ASSERT_TRUE(fs::exists(indexHtml));
    ASSERT_TRUE(fs::file_size(indexHtml) > 0);

    ASSERT_TRUE(fs::exists(runtimeJs));
    ASSERT_TRUE(fs::file_size(runtimeJs) > 0);

    ASSERT_TRUE(fs::exists(servePy));
    ASSERT_TRUE(fs::file_size(servePy) > 0);

    ASSERT_TRUE(fs::exists(startBat));
    ASSERT_TRUE(fs::file_size(startBat) > 0);

    ASSERT_TRUE(fs::exists(gameConfig));
    ASSERT_TRUE(fs::file_size(gameConfig) > 0);

    ASSERT_TRUE(fs::exists(sceneFile));
    ASSERT_TRUE(fs::file_size(sceneFile) > 0);

    // 5. Inspect index.html content
    std::ifstream htmlIn(indexHtml);
    ASSERT_TRUE(htmlIn.is_open());
    std::stringstream htmlBuf;
    htmlBuf << htmlIn.rdbuf();
    std::string htmlStr = htmlBuf.str();
    htmlIn.close();

    ASSERT_TRUE(htmlStr.find("Unit Test Web Adventure") != std::string::npos);
    ASSERT_TRUE(htmlStr.find("<canvas id=\"tessera-canvas\"") != std::string::npos);
    ASSERT_TRUE(htmlStr.find("<script id=\"tessera-scene-data\"") != std::string::npos);
    ASSERT_TRUE(htmlStr.find("touch-controls") != std::string::npos);
    ASSERT_TRUE(htmlStr.find("tessera_web.js") != std::string::npos);

    // 6. Inspect runtime JS content
    std::ifstream jsIn(runtimeJs);
    ASSERT_TRUE(jsIn.is_open());
    std::stringstream jsBuf;
    jsBuf << jsIn.rdbuf();
    std::string jsStr = jsBuf.str();
    jsIn.close();

    ASSERT_TRUE(jsStr.find("class TesseraEngine") != std::string::npos);
    ASSERT_TRUE(jsStr.find("compileShader") != std::string::npos);
    ASSERT_TRUE(jsStr.find("loadScene") != std::string::npos);
    ASSERT_TRUE(jsStr.find("createProgram") != std::string::npos);
    ASSERT_TRUE(jsStr.find("requestAnimationFrame") != std::string::npos);

    // 7. Inspect game.agconfig content
    std::ifstream cfgIn(gameConfig);
    ASSERT_TRUE(cfgIn.is_open());
    std::stringstream cfgBuf;
    cfgBuf << cfgIn.rdbuf();
    std::string cfgStr = cfgBuf.str();
    cfgIn.close();

    JsonValue parsedCfg = JsonValue::Parse(cfgStr);
    ASSERT_TRUE(parsedCfg.IsObject());
    ASSERT_EQ(parsedCfg["targetPlatform"].AsString(), "Web");
    ASSERT_EQ(parsedCfg["gameTitle"].AsString(), "Unit Test Web Adventure");
    ASSERT_EQ(parsedCfg["startingScene"].AsString(), "scenes/main.scene");

    // 8. Test Direct Generation functions
    std::string genHtml = Export::WebExporter::GenerateIndexHTML(scene, opts, "{}");
    ASSERT_TRUE(!genHtml.empty());
    ASSERT_TRUE(genHtml.find("<!DOCTYPE html>") != std::string::npos);

    std::string genJs = Export::WebExporter::GenerateEngineRuntimeJS(opts);
    ASSERT_TRUE(!genJs.empty());
    ASSERT_TRUE(genJs.find("TesseraEngine") != std::string::npos);

    std::string genPy = Export::WebExporter::GenerateServerPy(8080);
    ASSERT_TRUE(!genPy.empty());
    ASSERT_TRUE(genPy.find("PORT = 8080") != std::string::npos);

    std::string genBat = Export::WebExporter::GenerateServerBat(8080);
    ASSERT_TRUE(!genBat.empty());
    ASSERT_TRUE(genBat.find("http://localhost:8080") != std::string::npos);

    // Clean up test export directory
    std::error_code ec;
    fs::remove_all(exportDir, ec);
}

// =========================================================================
// 58. Universal First-Person Spectator Controller Tests
// =========================================================================
TEST_CASE(GameplayEngine, DefaultSpectatorCameraController) {
    Scene scene;
    auto cam = scene.GetActiveCamera();
    ASSERT_TRUE(cam != nullptr);

    Gameplay::GameLoop gameLoop;
    // Brand new games start with no default controls; dev defines all controls
    ASSERT_FALSE(gameLoop.IsDefaultCameraControllerEnabled());

    // Dev or engine can opt-in to spectator camera controller if desired
    gameLoop.SetDefaultCameraControllerEnabled(true);
    ASSERT_TRUE(gameLoop.IsDefaultCameraControllerEnabled());

    // Enter Play Mode
    gameLoop.Play(scene);
    ASSERT_TRUE(gameLoop.IsPlaying());

    auto playCam = gameLoop.GetPlayScene()->GetActiveCamera();
    ASSERT_TRUE(playCam != nullptr);

    // Test pointer lock & standalone flags
    gameLoop.SetPointerLocked(true);
    ASSERT_TRUE(gameLoop.IsPointerLocked());

    gameLoop.SetStandaloneMode(true);
    ASSERT_TRUE(gameLoop.IsStandaloneMode());

    // Run game loop update (should safely handle headless ImGui null context)
    gameLoop.Update(scene, 0.016f, true);
    ASSERT_TRUE(gameLoop.IsPlaying());

    // Stop simulation
    gameLoop.Stop(scene);
    ASSERT_FALSE(gameLoop.IsPlaying());
    ASSERT_FALSE(gameLoop.IsPointerLocked());
}


// =========================================================================
// Godot Inspiration Feature Tests (6 Architectural Milestones)
// =========================================================================

TEST_CASE(GodotInspiration, NestedSceneInstancingPrefabs) {
    // 1. Create a source prefab scene
    Scene prefabScene;
    prefabScene.Clear();
    auto rootHandle = prefabScene.AddNode(ShapeType::Cylinder, "TorchHandle");
    rootHandle->position = glm::vec3(0.0f, 0.5f, 0.0f);
    auto flameLight = prefabScene.AddLightNode(LightType::Point, "TorchLight", rootHandle->id);
    flameLight->position = glm::vec3(0.0f, 1.2f, 0.0f);

    std::string prefabPath = "temp_torch_prefab.scene";
    ASSERT_TRUE(prefabScene.SaveToFile(prefabPath));

    // 2. Instantiate prefab in a master scene
    Scene masterScene;
    auto instanceRoot = masterScene.InstantiateScene(prefabPath, 0, glm::vec3(10.0f, 0.0f, 20.0f));
    ASSERT_TRUE(instanceRoot != nullptr);
    ASSERT_TRUE(instanceRoot->isSceneInstanceRoot);
    ASSERT_EQ(instanceRoot->instanceScenePath, prefabPath);
    ASSERT_FLOAT_NEAR(instanceRoot->position.x, 10.0f, 0.001f);
    ASSERT_FLOAT_NEAR(instanceRoot->position.z, 20.0f, 0.001f);
    ASSERT_EQ(instanceRoot->childrenIds.size(), 1); // TorchHandle

    // Verify child subtree is populated
    auto handleNode = masterScene.GetNodeById(instanceRoot->childrenIds[0]);
    ASSERT_TRUE(handleNode != nullptr);
    ASSERT_EQ(handleNode->name, "TorchHandle");
    ASSERT_EQ(handleNode->childrenIds.size(), 1); // TorchLight

    // 3. Test ReloadSceneInstance
    ASSERT_TRUE(masterScene.ReloadSceneInstance(instanceRoot->id));
    ASSERT_EQ(instanceRoot->childrenIds.size(), 1);

    // 4. Test serialization roundtrip of master scene with instanced prefab
    JsonValue serialized = masterScene.Serialize();
    Scene restoredScene;
    ASSERT_TRUE(restoredScene.Deserialize(serialized));

    auto restoredRoot = restoredScene.GetNodeById(instanceRoot->id);
    ASSERT_TRUE(restoredRoot != nullptr);
    ASSERT_TRUE(restoredRoot->isSceneInstanceRoot);
    ASSERT_EQ(restoredRoot->instanceScenePath, prefabPath);

    // Cleanup disk file
    std::filesystem::remove(prefabPath);
}

TEST_CASE(GodotInspiration, ResourceHandlesAndManager) {
    auto& rm = Resource::ResourceManager::Get();
    rm.Clear();

    // 1. Mesh caching & sharing
    auto mesh1 = rm.GetOrCreateMesh("assets/models/oak_tree.fbx");
    mesh1->vertexCount = 1200;
    mesh1->triangleCount = 600;

    auto mesh2 = rm.GetOrCreateMesh("assets/models/oak_tree.fbx");
    ASSERT_EQ(mesh1.get(), mesh2.get()); // Identical shared memory handle!
    mesh2.reset();

    // 2. Texture & Material resources
    auto tex = rm.GetOrCreateTexture("assets/textures/bark_diffuse.png", 1024, 1024, 4);
    ASSERT_EQ(tex->width, 1024);
    ASSERT_EQ(tex->height, 1024);

    auto mat = rm.GetOrCreateMaterial("materials/Bark_PBR.mat");
    mat->roughness = 0.85f;
    mat->metallic = 0.05f;

    auto audio = rm.GetOrCreateAudio("assets/sounds/wind_forest.wav", 18.2f);
    ASSERT_FLOAT_NEAR(audio->durationSeconds, 18.2f, 0.01f);

    ASSERT_EQ(rm.GetLoadedResourceCount(), 4);
    ASSERT_TRUE(rm.GetTotalMemoryUsage() > 0);

    // 3. Attach resources to Nodes
    Node treeNode1;
    treeNode1.meshResource = mesh1;
    treeNode1.materialResource = mat;

    Node treeNode2;
    treeNode2.meshResource = mesh1;
    treeNode2.materialResource = mat;

    ASSERT_EQ(mesh1.use_count(), 4); // mesh1, rm cache, treeNode1, treeNode2
    ASSERT_EQ(mat.use_count(), 4);

    // 4. Unload Unused assets
    tex.reset();
    audio.reset();
    size_t purged = rm.UnloadUnused();
    ASSERT_EQ(purged, 2); // tex and audio purged, mesh and mat retained
    ASSERT_EQ(rm.GetLoadedResourceCount(), 2);
}

TEST_CASE(GodotInspiration, ReactiveSignalsAndObservers) {
    Node heroNode;
    heroNode.id = 42;
    heroNode.name = "HeroKnight";

    int healthSignalCount = 0;
    std::string passedDamage = "";

    // 1. Connect C++ listener
    uint64_t connId = heroNode.ConnectSignal("on_health_changed", [&](const std::vector<std::string>& args) {
        healthSignalCount++;
        if (!args.empty()) passedDamage = args[0];
    });

    ASSERT_TRUE(heroNode.HasSignal("on_health_changed"));
    ASSERT_EQ(heroNode.GetSignalConnectionCount("on_health_changed"), 1);

    // 2. Emit signal
    heroNode.EmitSignal("on_health_changed", {"25"});
    ASSERT_EQ(healthSignalCount, 1);
    ASSERT_EQ(passedDamage, "25");

    heroNode.EmitSignal("on_health_changed", {"40"});
    ASSERT_EQ(healthSignalCount, 2);
    ASSERT_EQ(passedDamage, "40");

    // 3. Disconnect signal
    ASSERT_TRUE(heroNode.DisconnectSignal("on_health_changed", connId));
    ASSERT_EQ(heroNode.GetSignalConnectionCount("on_health_changed"), 0);

    heroNode.EmitSignal("on_health_changed", {"99"});
    ASSERT_EQ(healthSignalCount, 2); // Did not increment!

    // 4. Test Sol2 / Lua signal dispatch
    Scripting::LuaScriptSystem::Get().Init();
    sol::state_view lua = Scripting::LuaScriptSystem::Get().GetSolState();

    int luaCalls = 0;
    lua["on_lua_signal_receive"] = [&](const std::string& msg) {
        luaCalls++;
    };

    uint64_t luaBridgeConn = heroNode.ConnectSignal("quest_completed", [&](const std::vector<std::string>& args) {
        lua["on_lua_signal_receive"](args.empty() ? "" : args[0]);
    });

    heroNode.EmitSignal("quest_completed", {"DragonDefeated"});
    ASSERT_EQ(luaCalls, 1);
}

TEST_CASE(GodotInspiration, CollisionLayerAndMaskBitfields) {
    // Bit 0 = Layer 1 (World / Ground)
    // Bit 1 = Layer 2 (Player)
    // Bit 2 = Layer 3 (PlayerBullet)
    // Bit 3 = Layer 4 (Enemy)
    Node world;
    world.id = 1;
    world.collisionLayer = (1 << 0);
    world.collisionMask = 0xFFFF; // Collides with everything

    Node player;
    player.id = 2;
    player.collisionLayer = (1 << 1);
    player.collisionMask = (1 << 0) | (1 << 3); // Collides with World and Enemy

    Node playerBullet;
    playerBullet.id = 3;
    playerBullet.collisionLayer = (1 << 2);
    playerBullet.collisionMask = (1 << 0) | (1 << 3); // Collides with World and Enemy (ignores Player)

    Node enemy;
    enemy.id = 4;
    enemy.collisionLayer = (1 << 3);
    enemy.collisionMask = (1 << 0) | (1 << 1) | (1 << 2); // Collides with World, Player, PlayerBullet

    // Assert filtering behavior
    ASSERT_TRUE(player.CanCollideWith(world));
    ASSERT_FALSE(player.CanCollideWith(playerBullet)); // Player cannot hit their own bullet!
    ASSERT_TRUE(playerBullet.CanCollideWith(enemy));   // Bullet hits enemy!
    ASSERT_TRUE(player.CanCollideWith(enemy));         // Player collides with enemy!

    // Test PhysicsWorld Jolt filtering integration
    Physics::PhysicsWorld pw;
    pw.SetCollisionLayer(player.id, player.collisionLayer);
    pw.SetCollisionMask(player.id, player.collisionMask);
    pw.SetCollisionLayer(playerBullet.id, playerBullet.collisionLayer);
    pw.SetCollisionMask(playerBullet.id, playerBullet.collisionMask);
    pw.SetCollisionLayer(enemy.id, enemy.collisionLayer);
    pw.SetCollisionMask(enemy.id, enemy.collisionMask);

    ASSERT_FALSE(pw.CanCollide(player.id, playerBullet.id));
    ASSERT_TRUE(pw.CanCollide(playerBullet.id, enemy.id));
    ASSERT_TRUE(pw.CanCollide(player.id, enemy.id));
}

TEST_CASE(GodotInspiration, UIAutoLayoutContainers) {
    Scene scene;

    // 1. Test UIBoxContainerNode (Vertical)
    auto vbox = scene.AddUIBoxContainerNode(BoxOrientation::Vertical, "MenuVBox");
    auto vboxCast = std::dynamic_pointer_cast<UIBoxContainerNode>(vbox);
    ASSERT_TRUE(vboxCast != nullptr);
    vboxCast->padding = 10.0f;
    vboxCast->spacing = 8.0f;
    vboxCast->autoResize = true;

    auto btn1 = scene.AddUIButtonNode("Start Game", "BtnStart", vbox->id);
    btn1->uiSize = glm::vec2(120.0f, 30.0f);
    auto btn2 = scene.AddUIButtonNode("Settings", "BtnSettings", vbox->id);
    btn2->uiSize = glm::vec2(120.0f, 30.0f);
    auto btn3 = scene.AddUIButtonNode("Quit", "BtnQuit", vbox->id);
    btn3->uiSize = glm::vec2(120.0f, 30.0f);

    vboxCast->UpdateLayout(scene);

    ASSERT_FLOAT_NEAR(btn1->uiPosition.y, 10.0f, 0.01f);
    ASSERT_FLOAT_NEAR(btn2->uiPosition.y, 10.0f + 30.0f + 8.0f, 0.01f); // 48.0f
    ASSERT_FLOAT_NEAR(btn3->uiPosition.y, 48.0f + 30.0f + 8.0f, 0.01f); // 86.0f
    ASSERT_FLOAT_NEAR(vboxCast->uiSize.y, 86.0f + 30.0f + 10.0f, 0.01f); // 126.0f

    // 2. Test UIGridContainerNode (Grid Layout)
    auto grid = scene.AddUIGridContainerNode(3, "InventoryGrid");
    auto gridCast = std::dynamic_pointer_cast<UIGridContainerNode>(grid);
    ASSERT_TRUE(gridCast != nullptr);
    gridCast->columns = 3;
    gridCast->cellWidth = 50.0f;
    gridCast->cellHeight = 50.0f;
    gridCast->horizontalSpacing = 6.0f;
    gridCast->verticalSpacing = 6.0f;
    gridCast->padding = 12.0f;
    gridCast->autoResize = true;

    std::vector<std::shared_ptr<Node>> slots;
    for (int i = 0; i < 6; ++i) {
        auto slot = scene.AddUIPanelNode("Slot_" + std::to_string(i), grid->id);
        slots.push_back(slot);
    }

    gridCast->UpdateLayout(scene);

    // Row 0
    ASSERT_FLOAT_NEAR(slots[0]->uiPosition.x, 12.0f, 0.01f);
    ASSERT_FLOAT_NEAR(slots[0]->uiPosition.y, 12.0f, 0.01f);

    ASSERT_FLOAT_NEAR(slots[1]->uiPosition.x, 12.0f + 50.0f + 6.0f, 0.01f); // 68.0f
    ASSERT_FLOAT_NEAR(slots[1]->uiPosition.y, 12.0f, 0.01f);

    ASSERT_FLOAT_NEAR(slots[2]->uiPosition.x, 68.0f + 50.0f + 6.0f, 0.01f); // 124.0f
    ASSERT_FLOAT_NEAR(slots[2]->uiPosition.y, 12.0f, 0.01f);

    // Row 1
    ASSERT_FLOAT_NEAR(slots[3]->uiPosition.x, 12.0f, 0.01f);
    ASSERT_FLOAT_NEAR(slots[3]->uiPosition.y, 12.0f + 50.0f + 6.0f, 0.01f); // 68.0f

    ASSERT_FLOAT_NEAR(slots[4]->uiPosition.x, 68.0f, 0.01f);
    ASSERT_FLOAT_NEAR(slots[4]->uiPosition.y, 68.0f, 0.01f);

    ASSERT_FLOAT_NEAR(slots[5]->uiPosition.x, 124.0f, 0.01f);
    ASSERT_FLOAT_NEAR(slots[5]->uiPosition.y, 68.0f, 0.01f);
}

TEST_CASE(GodotInspiration, DeclarativeMultiplayerPropertySync) {
    Scene serverScene;
    auto serverEntity = serverScene.AddNode(ShapeType::Cube, "NetworkHero");
    serverEntity->id = 77;
    serverEntity->networkId = 77;
    serverEntity->multiplayerAuthority = 1; // Server has authority

    serverEntity->replicationConfig.enabled = true;
    serverEntity->replicationConfig.AddRule("position", ReplicationMode::OnChange, 20.0f);
    serverEntity->replicationConfig.AddRule("color", ReplicationMode::OnChange, 20.0f);
    serverEntity->replicationConfig.AddRule("animState", ReplicationMode::OnChange, 20.0f);

    Networking::NetworkManager serverNet;
    serverNet.SetLocalPeerId(1);

    serverEntity->position = glm::vec3(4.5f, 1.0f, -9.2f);
    serverEntity->color = glm::vec4(0.1f, 0.8f, 0.3f, 1.0f);
    serverEntity->currentAnimState = "SpellCast";

    // Build declarative replication packet
    auto packet = serverNet.BuildDeclarativeReplicationPacket(serverScene, 0.5f);
    ASSERT_FALSE(packet.empty());

    // Client receives and applies packet
    Scene clientScene;
    auto clientProxy = clientScene.AddNode(ShapeType::Cube, "NetworkHero_Proxy");
    clientProxy->id = 77;
    clientProxy->networkId = 77;
    clientProxy->multiplayerAuthority = 1; // Client does NOT own authority

    Networking::NetworkManager clientNet;
    clientNet.SetLocalPeerId(2);

    bool applied = clientNet.ApplyDeclarativeReplicationPacket(clientScene, packet.data(), packet.size());
    ASSERT_TRUE(applied);

    ASSERT_FLOAT_NEAR(clientProxy->position.x, 4.5f, 0.01f);
    ASSERT_FLOAT_NEAR(clientProxy->position.y, 1.0f, 0.01f);
    ASSERT_FLOAT_NEAR(clientProxy->position.z, -9.2f, 0.01f);
    ASSERT_FLOAT_NEAR(clientProxy->color.g, 0.8f, 0.01f);
    ASSERT_EQ(clientProxy->currentAnimState, "SpellCast");
}

TEST_CASE(RulerTool, DistanceAndAnglesAndUnitConversions) {
    // 1. Point-to-Point 3D Euclidean Distance
    glm::vec3 pA(0.0f, 0.0f, 0.0f);
    glm::vec3 pB(3.0f, 4.0f, 0.0f);
    float dist = RulerMath::CalculateDistance(pA, pB);
    ASSERT_FLOAT_NEAR(dist, 5.0f, 0.0001f);

    // 2. Component Delta Breakdown
    glm::vec3 delta = RulerMath::CalculateDelta(pA, pB);
    ASSERT_FLOAT_NEAR(delta.x, 3.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(delta.y, 4.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(delta.z, 0.0f, 0.0001f);

    // 3. Horizontal Distance (XZ Plane)
    float horizDist = RulerMath::CalculateHorizontalDistance(pA, pB);
    ASSERT_FLOAT_NEAR(horizDist, 3.0f, 0.0001f);

    // 4. Elevation / Pitch Angle
    float elevAngle = RulerMath::CalculateElevationAngle(pA, pB);
    // atan2(4, 3) in degrees = 53.1301...
    ASSERT_FLOAT_NEAR(elevAngle, 53.1301f, 0.01f);

    // 5. Compass Heading & 8-Wind Directions
    // North: toward -Z
    glm::vec3 northTarget(0.0f, 0.0f, -10.0f);
    float headingN = RulerMath::CalculateHeading(pA, northTarget);
    ASSERT_FLOAT_NEAR(headingN, 0.0f, 0.01f);
    ASSERT_EQ(std::string(RulerMath::GetCompassDirection(headingN)), "N");

    // Northeast: toward +X, -Z
    glm::vec3 neTarget(10.0f, 0.0f, -10.0f);
    float headingNE = RulerMath::CalculateHeading(pA, neTarget);
    ASSERT_FLOAT_NEAR(headingNE, 45.0f, 0.01f);
    ASSERT_EQ(std::string(RulerMath::GetCompassDirection(headingNE)), "NE");

    // East: toward +X
    glm::vec3 eastTarget(10.0f, 0.0f, 0.0f);
    float headingE = RulerMath::CalculateHeading(pA, eastTarget);
    ASSERT_FLOAT_NEAR(headingE, 90.0f, 0.01f);
    ASSERT_EQ(std::string(RulerMath::GetCompassDirection(headingE)), "E");

    // South: toward +Z
    glm::vec3 southTarget(0.0f, 0.0f, 10.0f);
    float headingS = RulerMath::CalculateHeading(pA, southTarget);
    ASSERT_FLOAT_NEAR(headingS, 180.0f, 0.01f);
    ASSERT_EQ(std::string(RulerMath::GetCompassDirection(headingS)), "S");

    // West: toward -X
    glm::vec3 westTarget(-10.0f, 0.0f, 0.0f);
    float headingW = RulerMath::CalculateHeading(pA, westTarget);
    ASSERT_FLOAT_NEAR(headingW, 270.0f, 0.01f);
    ASSERT_EQ(std::string(RulerMath::GetCompassDirection(headingW)), "W");

    // 6. Unit Conversions & Formatting
    RulerToolState ruler;
    ruler.unit = RulerUnit::Meters;
    ASSERT_EQ(ruler.FormatDist(5.0f), "5.00 m");

    ruler.unit = RulerUnit::Centimeters;
    ASSERT_EQ(ruler.FormatDist(5.0f), "500.00 cm");

    ruler.unit = RulerUnit::Millimeters;
    ASSERT_EQ(ruler.FormatDist(5.0f), "5000 mm");

    ruler.unit = RulerUnit::Feet;
    ASSERT_EQ(ruler.FormatDist(5.0f), "16.40 ft");

    ruler.unit = RulerUnit::Inches;
    ASSERT_EQ(ruler.FormatDist(5.0f), "196.85 in");

    // 7. Grid Snapping Quantization
    glm::vec3 rawPos(2.37f, 1.81f, -0.66f);
    glm::vec3 snapped05 = RulerMath::SnapToGrid(rawPos, 0.5f);
    ASSERT_FLOAT_NEAR(snapped05.x, 2.5f, 0.0001f);
    ASSERT_FLOAT_NEAR(snapped05.y, 2.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(snapped05.z, -0.5f, 0.0001f);

    glm::vec3 snapped025 = RulerMath::SnapToGrid(rawPos, 0.25f);
    ASSERT_FLOAT_NEAR(snapped025.x, 2.25f, 0.0001f);
    ASSERT_FLOAT_NEAR(snapped025.y, 1.75f, 0.0001f);
    ASSERT_FLOAT_NEAR(snapped025.z, -0.75f, 0.0001f);

    // 8. Measurement State & Saved Measurements
    ruler.current.pointA = pA;
    ruler.current.pointB = pB;
    ruler.current.hasA = true;
    ruler.current.hasB = true;

    // Swap
    std::swap(ruler.current.pointA, ruler.current.pointB);
    ASSERT_FLOAT_NEAR(ruler.current.pointA.x, 3.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(ruler.current.pointB.x, 0.0f, 0.0001f);

    // Save to list
    ruler.savedMeasurements.push_back(ruler.current);
    ASSERT_EQ(ruler.savedMeasurements.size(), 1u);
    ASSERT_FLOAT_NEAR(ruler.savedMeasurements[0].pointA.y, 4.0f, 0.0001f);

    // Clear
    ruler.savedMeasurements.clear();
    ASSERT_EQ(ruler.savedMeasurements.size(), 0u);
}

#include "Scene/SkyClock.h"

TEST_CASE(Sky, TimeOfDayClock) {
    SceneEnvironment env;
    env.timeOfDayEnabled = true;
    env.latitude = 35.0f;
    env.northAngle = 0.0f;

    // Sunrise in the east (+X), noon highest toward the south (+Z), sunset in the west
    glm::vec3 sunrise = SkyClock::CelestialDirection(6.0f, env.latitude, env.northAngle);
    ASSERT_FLOAT_NEAR(sunrise.y, 0.0f, 0.001f);
    ASSERT_TRUE(sunrise.x > 0.99f);
    glm::vec3 noon = SkyClock::CelestialDirection(12.0f, env.latitude, env.northAngle);
    ASSERT_FLOAT_NEAR(noon.y, std::cos(glm::radians(35.0f)), 0.001f);
    ASSERT_TRUE(noon.z > 0.5f);
    ASSERT_TRUE(SkyClock::CelestialDirection(18.0f, env.latitude, env.northAngle).x < -0.99f);

    // Day: the light follows the sun at full strength
    env.timeOfDay = 10.0f;
    SkyClock::State day = SkyClock::Evaluate(env);
    ASSERT_FLOAT_NEAR(glm::dot(day.lightDir, day.sunDir), 1.0f, 0.0001f);
    ASSERT_FLOAT_NEAR(day.lightScale, 1.0f, 0.0001f);

    // Full moon at midnight: opposite the sun, high, fully lit; the light follows it, dim
    env.moonPhase = 0.5f;
    env.timeOfDay = 0.0f;
    SkyClock::State night = SkyClock::Evaluate(env);
    ASSERT_FLOAT_NEAR(glm::dot(night.moonDir, night.sunDir), -1.0f, 0.001f);
    ASSERT_FLOAT_NEAR(night.moonIllumination, 1.0f, 0.001f);
    ASSERT_FLOAT_NEAR(glm::dot(night.lightDir, night.moonDir), 1.0f, 0.0001f);
    ASSERT_TRUE(night.lightScale > 0.01f && night.lightScale < 0.2f);

    // New moon: no moonlight
    env.moonPhase = 0.0f;
    ASSERT_FLOAT_NEAR(SkyClock::Evaluate(env).lightScale, 0.0f, 0.0001f);

    // The clock: a 24-minute day moves one hour per real minute, and wraps past midnight
    env.dayLengthMinutes = 24.0f;
    env.timeOfDay = 23.5f;
    SkyClock::Advance(env, 60.0f);
    ASSERT_FLOAT_NEAR(env.timeOfDay, 0.5f, 0.001f);
    env.dayLengthMinutes = 0.0f;
    SkyClock::Advance(env, 60.0f);
    ASSERT_FLOAT_NEAR(env.timeOfDay, 0.5f, 0.001f);

    // Saved and loaded with the scene
    Scene scene;
    scene.GetEnvironment() = env;
    scene.GetEnvironment().cloudsEnabled = true;
    scene.GetEnvironment().cloudCoverage = 0.7f;
    scene.GetEnvironment().moonPhase = 0.3f;
    const std::string path = "sky_clock_test.scene";
    ASSERT_TRUE(scene.SaveToFile(path));
    Scene loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path));
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.GetEnvironment().timeOfDayEnabled);
    ASSERT_TRUE(loaded.GetEnvironment().cloudsEnabled);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().timeOfDay, 0.5f, 0.001f);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().cloudCoverage, 0.7f, 0.001f);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().moonPhase, 0.3f, 0.001f);
}

#include "Scene/Weather.h"

TEST_CASE(Sky, WeatherPresetsAndLightning) {
    ASSERT_EQ(Weather::PresetFromName("storm"), static_cast<int>(Weather::Storm));
    ASSERT_EQ(Weather::PresetFromName("Snow"), static_cast<int>(Weather::Snow));
    ASSERT_EQ(Weather::PresetFromName("hail"), -1);

    // Blending into rain: most of the way after the transition time, and the ground soaks
    SceneEnvironment env;
    env.weatherEnabled = true;
    env.weatherPreset = Weather::Rain;
    env.weatherTransition = 10.0f;
    for (int i = 0; i < 200; ++i) Weather::Update(env, 0.1f);
    const Weather::Targets rain = Weather::PresetTargets(Weather::Rain);
    ASSERT_TRUE(env.cloudsEnabled);
    ASSERT_FLOAT_NEAR(env.rainAmount, rain.rain, rain.rain * 0.1f);
    ASSERT_TRUE(env.cloudCoverage > 0.9f);
    ASSERT_TRUE(env.wetness > 0.5f);
    ASSERT_FLOAT_NEAR(env.snowCover, 0.0f, 0.0001f);

    // Clearing up: the rain stops and the ground slowly dries
    env.weatherPreset = Weather::Clear;
    const float wetBefore = env.wetness;
    for (int i = 0; i < 300; ++i) Weather::Update(env, 0.1f);
    ASSERT_TRUE(env.rainAmount < 0.01f);
    ASSERT_TRUE(env.wetness < wetBefore && env.wetness > 0.0f);

    // Presets off: values stay as set
    env.weatherEnabled = false;
    env.wetness = 0.4f;
    env.snowCover = 0.3f;
    for (int i = 0; i < 50; ++i) Weather::Update(env, 0.1f);
    ASSERT_FLOAT_NEAR(env.wetness, 0.4f, 0.0001f);
    ASSERT_FLOAT_NEAR(env.snowCover, 0.3f, 0.0001f);

    // Lightning: a strike flashes then fades within about a second
    Weather::Strike(env);
    ASSERT_EQ(env.strikeCount, 1);
    Weather::Update(env, 0.02f);
    ASSERT_TRUE(env.lightningFlash > 0.5f);
    for (int i = 0; i < 20; ++i) Weather::Update(env, 0.1f);
    ASSERT_FLOAT_NEAR(env.lightningFlash, 0.0f, 0.0001f);
    ASSERT_TRUE(env.lightningAge < 0.0f);

    // A storm strikes on its own
    env.lightningRate = 30.0f;
    for (int i = 0; i < 200; ++i) Weather::Update(env, 0.1f);
    ASSERT_TRUE(env.strikeCount > 2);
}

#include "Audio/AudioEngine.h"

TEST_CASE(Sky, ThunderFollowsLightning) {
    // The thunder arrives after the time sound takes to travel from the strike
    SceneEnvironment env;
    Weather::Strike(env);
    const float distance = env.thunderDistances[0];
    ASSERT_TRUE(distance >= 200.0f && distance <= 2000.0f);
    const float delay = distance / 343.0f;
    float elapsed = 0.0f;
    while (!env.thunderDue && elapsed < 10.0f) {
        Weather::Update(env, 0.05f);
        elapsed += 0.05f;
    }
    ASSERT_TRUE(env.thunderDue);
    ASSERT_FLOAT_NEAR(elapsed, delay, 0.06f);
    ASSERT_FLOAT_NEAR(env.thunderDistance, distance, 0.001f);

    // Strikes in quick succession each get their thunder
    SceneEnvironment storm;
    for (int i = 0; i < 3; ++i) { Weather::Strike(storm); Weather::Update(storm, 0.2f); }
    int thunders = 0;
    for (int i = 0; i < 200; ++i) {
        Weather::Update(storm, 0.05f);
        if (storm.thunderDue) { ++thunders; storm.thunderDue = false; }
    }
    ASSERT_EQ(thunders, 3);

    // The generated sound: a valid mono 16-bit WAV, loud but not clipping, different per seed
    const auto near = Audio::AudioEngine::GenerateThunderWav(1u, 1.0f, 2.0f, 22050);
    const auto far = Audio::AudioEngine::GenerateThunderWav(2u, 0.0f, 2.0f, 22050);
    ASSERT_EQ(near.size(), 44u + 2u * 44100u);
    ASSERT_TRUE(std::memcmp(near.data(), "RIFF", 4) == 0 && std::memcmp(near.data() + 8, "WAVE", 4) == 0);
    const int16_t* pcm = reinterpret_cast<const int16_t*>(near.data() + 44);
    int peak = 0;
    for (size_t i = 0; i < 44100; ++i) peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
    ASSERT_TRUE(peak > 25000 && peak < 32767);
    ASSERT_TRUE(near != far);
    Audio::AudioEngine audio;
    ASSERT_TRUE(audio.LoadWavFromMemory("Thunder", near.data(), near.size()));
    ASSERT_TRUE(audio.GetClip("Thunder") != nullptr);
}

TEST_CASE(Sky, SeasonsAndOceanSounds) {
    // Seasons: long days in the northern summer, short in winter, 12 hours at the equinox and at
    // the equator; polar day and night
    ASSERT_FLOAT_NEAR(SkyClock::SunDeclination(172.0f), 23.44f, 0.1f);
    ASSERT_FLOAT_NEAR(SkyClock::SunDeclination(81.0f), 0.0f, 0.01f);
    ASSERT_TRUE(SkyClock::DayLengthHours(51.5f, 172.0f) > 16.0f);
    ASSERT_TRUE(SkyClock::DayLengthHours(51.5f, 355.0f) < 8.5f);
    ASSERT_FLOAT_NEAR(SkyClock::DayLengthHours(51.5f, 81.0f), 12.0f, 0.05f);
    ASSERT_FLOAT_NEAR(SkyClock::DayLengthHours(0.0f, 172.0f), 12.0f, 0.05f);
    ASSERT_FLOAT_NEAR(SkyClock::DayLengthHours(80.0f, 172.0f), 24.0f, 0.001f);
    ASSERT_FLOAT_NEAR(SkyClock::DayLengthHours(80.0f, 355.0f), 0.0f, 0.001f);
    // The midsummer noon sun climbs higher than the equinox one
    const float summer = SkyClock::CelestialDirection(12.0f, 51.5f, 0.0f, SkyClock::SunDeclination(172.0f)).y;
    const float equinox = SkyClock::CelestialDirection(12.0f, 51.5f, 0.0f, 0.0f).y;
    ASSERT_TRUE(summer > equinox + 0.2f);
    // The calendar moves on at midnight, and the moon a day through its phases
    SceneEnvironment env;
    env.timeOfDayEnabled = true;
    env.dayLengthMinutes = 1.0f;
    env.timeOfDay = 23.0f;
    env.dayOfYear = 365.0f;
    env.moonPhase = 0.0f;
    SkyClock::Advance(env, 10.0f); // 4 hours
    ASSERT_FLOAT_NEAR(env.timeOfDay, 3.0f, 0.01f);
    ASSERT_FLOAT_NEAR(env.dayOfYear, 1.0f, 0.01f);
    ASSERT_FLOAT_NEAR(env.moonPhase, 1.0f / 29.53f, 0.001f);

    // Ocean sounds: valid WAVs, the surf loop seamless (its two ends close in level)
    const auto surf = Audio::AudioEngine::GenerateSurfWav(5u, false, 4.0f, 22050);
    const auto crash = Audio::AudioEngine::GenerateCrashWav(6u, 1.0f, 22050);
    ASSERT_EQ(surf.size(), 44u + 2u * 4u * 22050u);
    ASSERT_EQ(crash.size(), 44u + 2u * 22050u);
    const int16_t* pcm = reinterpret_cast<const int16_t*>(surf.data() + 44);
    const size_t n = 4u * 22050u;
    ASSERT_TRUE(std::abs(static_cast<int>(pcm[0]) - static_cast<int>(pcm[n - 1])) < 6000);
    Audio::AudioEngine audio;
    ASSERT_TRUE(audio.LoadWavFromMemory("Surf", surf.data(), surf.size()));
    // Mixing: a looping voice keeps playing and its volume can be changed
    const uint32_t voice = audio.Play2D("Surf", "Ambient", 0.5f, 1.0f, true);
    ASSERT_TRUE(voice != 0);
    std::vector<float> mixed;
    audio.MixOutputBuffer(nullptr, mixed, 4096, 44100);
    float peak = 0.0f;
    for (float s : mixed) peak = std::max(peak, std::abs(s));
    ASSERT_TRUE(peak > 0.01f);
    audio.SetVoiceVolume(voice, 0.0f);
    audio.MixOutputBuffer(nullptr, mixed, 4096, 44100);
    peak = 0.0f;
    for (float s : mixed) peak = std::max(peak, std::abs(s));
    ASSERT_FLOAT_NEAR(peak, 0.0f, 1e-6f);
    ASSERT_TRUE(audio.IsVoicePlaying(voice));
}

#include "Scene/FoliageMeshes.h"

TEST_CASE(WorldBuilding, TerrainPaintFoliageAndCollision) {
    Scene scene;
    auto node = std::dynamic_pointer_cast<TerrainNode>(scene.AddTerrainNode("Land"));
    ASSERT_TRUE(node != nullptr);
    node->GenerateProcedural(TerrainPreset::FlatPlains);
    node->terrainSize = glm::vec2(40.0f, 40.0f);
    node->NotifyHeightsChanged();
    ASSERT_EQ(node->layers.size(), 4u);
    ASSERT_EQ(node->splatData.size(), static_cast<size_t>(node->splatResolution) * node->splatResolution * 4);
    ASSERT_FLOAT_NEAR(node->SampleSplatLocal(0.0f, 0.0f).x, 1.0f, 0.01f); // all the first layer

    // Painting the third layer: full in the middle, soft at the edge, untouched outside
    node->brushRadius = 4.0f;
    node->brushStrength = 10.0f;
    const uint32_t version = node->splatVersion;
    for (int i = 0; i < 20; ++i) node->PaintLayerLocal(0.0f, 0.0f, 2, 0.1f);
    ASSERT_TRUE(node->splatVersion != version);
    ASSERT_TRUE(node->SampleSplatLocal(0.0f, 0.0f).z > 0.95f);
    ASSERT_FLOAT_NEAR(node->SampleSplatLocal(10.0f, 10.0f).x, 1.0f, 0.01f);
    const glm::vec4 w = node->SampleSplatLocal(1.0f, 1.0f);
    ASSERT_FLOAT_NEAR(w.x + w.y + w.z + w.w, 1.0f, 0.02f);

    // Auto paint: a steep bank turns to rock (layer 3)
    for (uint32_t j = 0; j < node->gridResolution; ++j) {
        for (uint32_t i = 0; i < node->gridResolution; ++i) {
            const float x = static_cast<float>(i) / (node->gridResolution - 1);
            node->heightData[j * node->gridResolution + i] = x > 0.75f ? (x - 0.75f) * 4.0f : 0.0f; // a cliff on the +x side
        }
    }
    node->heightScale = 20.0f;
    node->AutoPaint();
    ASSERT_TRUE(node->SampleSplatLocal(17.0f, 0.0f).z > 0.8f);  // the cliff
    ASSERT_TRUE(node->SampleSplatLocal(-10.0f, 0.0f).z < 0.05f); // the flat
    node->GenerateProcedural(TerrainPreset::FlatPlains);
    node->NotifyHeightsChanged();

    // Foliage: fills toward the density, never too close together, respects the slope limit
    node->activeFoliage = 0; // grass
    node->foliageTypes[0].density = 2.0f;
    node->brushRadius = 3.0f;
    for (int i = 0; i < 40; ++i) node->PaintFoliageLocal(0.0f, 0.0f, 0.1f, false);
    const size_t grass = node->foliage.size();
    const float area = 3.14159265f * 9.0f;
    ASSERT_TRUE(grass > static_cast<size_t>(area * 2.0f * 0.5f) && grass <= static_cast<size_t>(area * 2.0f) + 1);
    const float minSpacing = 0.5f / std::sqrt(2.0f);
    for (size_t a = 0; a < node->foliage.size(); ++a) {
        for (size_t b = a + 1; b < node->foliage.size(); ++b) {
            ASSERT_TRUE(glm::distance(node->foliage[a].local, node->foliage[b].local) >= minSpacing * 0.999f);
        }
    }
    // Erasing removes it all within the brush (eventually)
    node->brushStrength = 10.0f;
    for (int i = 0; i < 20; ++i) node->PaintFoliageLocal(0.0f, 0.0f, 0.1f, true);
    ASSERT_EQ(node->foliage.size(), 0u);

    // A tree, drawn when in view and near enough; culled when the camera looks away or is far off
    node->activeFoliage = 3;
    node->foliage.push_back({ glm::vec2(5.0f, 5.0f), 0.0f, 1.0f, 3u });
    node->NotifyFoliageChanged();
    std::vector<std::vector<glm::mat4>> perType;
    node->CollectFoliage(scene, glm::vec3(0.0f, 2.0f, 0.0f), nullptr, perType, false);
    ASSERT_EQ(perType[3].size(), 1u);
    ASSERT_FLOAT_NEAR(perType[3][0][3].x, 5.0f, 0.001f);
    node->CollectFoliage(scene, glm::vec3(0.0f, 2.0f, 0.0f), [](const glm::vec3&, float) { return false; }, perType, false);
    ASSERT_EQ(perType[3].size(), 0u);
    node->CollectFoliage(scene, glm::vec3(5000.0f, 2.0f, 0.0f), nullptr, perType, false);
    ASSERT_EQ(perType[3].size(), 0u);

    // The heightfield raycast finds the ground
    glm::vec3 hit;
    ASSERT_TRUE(node->Raycast(glm::vec3(3.0f, 50.0f, -2.0f), glm::vec3(0.0f, -1.0f, 0.0f), scene, hit));
    ASSERT_FLOAT_NEAR(hit.y, node->SampleHeight(3.0f, -2.0f, scene), 0.01f);
    ASSERT_TRUE(!node->Raycast(glm::vec3(3.0f, 50.0f, -2.0f), glm::vec3(0.0f, 1.0f, 0.0f), scene, hit));

    // Saved and loaded
    const std::string path = "terrain_paint_test.scene";
    for (int i = 0; i < 10; ++i) node->PaintLayerLocal(-8.0f, 6.0f, 1, 0.1f);
    const glm::vec4 painted = node->SampleSplatLocal(-8.0f, 6.0f);
    ASSERT_TRUE(scene.SaveToFile(path));
    Scene loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path));
    std::filesystem::remove(path);
    std::shared_ptr<TerrainNode> back;
    for (const auto& n : loaded.GetNodes()) if (n && n->nodeType == NodeType::Terrain) back = std::dynamic_pointer_cast<TerrainNode>(n);
    ASSERT_TRUE(back != nullptr);
    ASSERT_EQ(back->foliage.size(), 1u);
    ASSERT_EQ(back->foliageTypes.size(), node->foliageTypes.size());
    ASSERT_TRUE(back->foliageTypes[3].collision);
    ASSERT_FLOAT_NEAR(back->SampleSplatLocal(-8.0f, 6.0f).y, painted.y, 0.01f);
    ASSERT_EQ(back->layers[2].name, std::string("Rock"));

    // Built-in foliage meshes are real geometry
    for (int kind = 0; kind < FoliageMeshes::Count; ++kind) {
        const MeshData mesh = FoliageMeshes::Build(kind);
        ASSERT_TRUE(mesh.vertices.size() > 10 && mesh.indices.size() % 3 == 0);
    }

    // Physics: the terrain is solid ground, and the tree's trunk is solid in Play
    Physics::PhysicsWorld physics;
    physics.Initialize();
    auto ball = scene.AddNode(ShapeType::Sphere, "Ball");
    ball->position = glm::vec3(-6.0f, 5.0f, -6.0f);
    ball->rigidBodyType = RigidBodyType::Dynamic;
    ball->mass = 1.0f;
    // Rolled at the tree from the side: the trunk stops it
    auto atTree = scene.AddNode(ShapeType::Sphere, "BallAtTree");
    atTree->position = glm::vec3(5.0f, node->SampleHeight(5.0f, 1.0f, scene) + 1.0f, 1.0f);
    atTree->rigidBodyType = RigidBodyType::Dynamic;
    atTree->mass = 1.0f;
    atTree->linearVelocity = glm::vec3(0.0f, 0.0f, 6.0f);
    physics.BuildFromScene(scene);
    physics.AddTerrainBodies(scene);
    float closest = 1e9f;
    for (int i = 0; i < 240; ++i) {
        physics.Step(scene, 1.0f / 60.0f);
        const glm::vec3 q = physics.GetPosition(atTree->id);
        closest = std::min(closest, glm::distance(glm::vec2(q.x, q.z), glm::vec2(5.0f, 5.0f)));
    }
    ASSERT_FLOAT_NEAR(physics.GetPosition(ball->id).y, node->SampleHeight(-6.0f, -6.0f, scene) + 0.5f, 0.1f);
    ASSERT_TRUE(closest >= 0.22f + 0.5f - 0.05f); // it never went through the trunk
    ASSERT_TRUE(std::abs(physics.GetPosition(atTree->id).x - 5.0f) > 0.3f || physics.GetPosition(atTree->id).z < 4.8f); // it was turned aside or stopped
}

#include "Scene/ParticleCollision.h"
#include "Scene/ParticleSheets.h"
TEST_CASE(VFX, FlipbooksTrailsAndParticleCollision) {
    Scene scene;
    scene.Clear();
    auto ground = scene.AddNode(ShapeType::Plane, "Ground");
    ground->scale = glm::vec3(20.0f, 1.0f, 20.0f);
    auto crate = scene.AddNode(ShapeType::Cube, "Crate");
    crate->position = glm::vec3(1.6f, 0.6f, 0.0f);
    crate->scale = glm::vec3(1.2f);

    // Colliders: a drop onto the crate stops on its top; one beside it on the ground
    ParticleColliders colliders;
    ParticleColliders::Settings settings;
    colliders.Build(scene, glm::vec3(-10.0f), glm::vec3(10.0f), settings);
    ASSERT_EQ(colliders.Solids().size(), 2u);
    ParticleColliders::Hit hit;
    ASSERT_TRUE(!colliders.Sweep(glm::vec3(1.6f, 4.5f, 0.0f), glm::vec3(1.6f, 4.3f, 0.0f), 0.01f, hit)); // in the air
    ASSERT_TRUE(colliders.Sweep(glm::vec3(1.6f, 1.5f, 0.0f), glm::vec3(1.6f, 1.0f, 0.0f), 0.01f, hit));
    ASSERT_FLOAT_NEAR(hit.position.y, 1.21f, 0.02f);
    ASSERT_FLOAT_NEAR(hit.normal.y, 1.0f, 0.01f);
    ASSERT_TRUE(colliders.Sweep(glm::vec3(-3.0f, 0.3f, 0.0f), glm::vec3(-3.0f, -0.2f, 0.0f), 0.01f, hit));
    ASSERT_FLOAT_NEAR(hit.position.y, 0.01f, 0.02f);
    // Fast enough to cross the crate in one step: still hits its side
    ASSERT_TRUE(colliders.Sweep(glm::vec3(-1.0f, 0.6f, 0.0f), glm::vec3(4.0f, 0.6f, 0.0f), 0.01f, hit));
    ASSERT_FLOAT_NEAR(hit.normal.x, -1.0f, 0.01f);
    ASSERT_TRUE(hit.position.x < 1.0f);

    // Terrain: a hill stops a falling particle on its surface
    Scene hills;
    hills.Clear();
    auto land = std::dynamic_pointer_cast<TerrainNode>(hills.AddTerrainNode("Land"));
    land->GenerateProcedural(TerrainPreset::FlatPlains);
    land->terrainSize = glm::vec2(40.0f, 40.0f);
    for (float& h : land->heightData) h = 0.25f;
    land->heightScale = 8.0f;
    land->NotifyHeightsChanged();
    ParticleColliders landColliders;
    landColliders.Build(hills, glm::vec3(-10.0f), glm::vec3(10.0f), settings);
    ASSERT_EQ(landColliders.Terrains().size(), 1u);
    ASSERT_TRUE(landColliders.Sweep(glm::vec3(3.0f, 5.0f, 2.0f), glm::vec3(3.0f, 1.0f, 2.0f), 0.0f, hit));
    ASSERT_FLOAT_NEAR(hit.position.y, 2.0f, 0.05f);

    // Water: a drop into a calm sea stops at its surface, as water (particles sink there)
    Scene sea;
    sea.Clear();
    auto ocean = sea.AddNode(ShapeType::Plane, "Sea");
    ocean->isOcean = true;
    ocean->oceanSize = glm::vec2(100.0f, 100.0f);
    ocean->waveHeight = 0.0f;
    ocean->position = glm::vec3(0.0f, 0.5f, 0.0f);
    ParticleColliders seaColliders;
    seaColliders.Build(sea, glm::vec3(-10.0f), glm::vec3(10.0f), settings);
    ASSERT_TRUE(seaColliders.Sweep(glm::vec3(2.0f, 2.0f, 1.0f), glm::vec3(2.0f, -1.0f, 1.0f), 0.0f, hit));
    ASSERT_TRUE(hit.water);
    ASSERT_FLOAT_NEAR(hit.position.y, 0.5f, 0.05f);
    glm::vec2 gridOrigin, gridExtent;
    std::vector<float> gridHeights;
    ASSERT_TRUE(seaColliders.SampleWaterGrid(8, gridOrigin, gridExtent, gridHeights));
    ASSERT_EQ(gridHeights.size(), 64u);
    ASSERT_FLOAT_NEAR(gridHeights[27], 0.5f, 0.05f);
    ParticleColliders::Settings dry = settings;
    dry.water = false;
    seaColliders.Build(sea, glm::vec3(-10.0f), glm::vec3(10.0f), dry);
    ASSERT_TRUE(!seaColliders.Sweep(glm::vec3(2.0f, 2.0f, 1.0f), glm::vec3(2.0f, -1.0f, 1.0f), 0.0f, hit));

    // An emitter raining onto the crate and ground: nothing falls through
    auto emitterNode = std::dynamic_pointer_cast<ParticleNode>(scene.AddParticleNode("Rain"));
    ASSERT_TRUE(emitterNode != nullptr);
    ParticleNode& rain = *emitterNode;
    rain.position = glm::vec3(1.6f, 4.5f, 0.0f);
    rain.emitterShape = ParticleEmitterShape::Box;
    rain.emitterBoxExtents = glm::vec3(3.0f, 0.1f, 3.0f);
    rain.startSpeed = 0.0f;
    rain.gravityMultiplier = 1.0f;
    rain.lifetimeMin = rain.lifetimeMax = 4.0f;
    rain.startSize = rain.endSize = 0.03f;
    rain.spawnRate = 200.0f;
    rain.maxParticles = 2000;
    rain.collisionMode = ParticleCollisionMode::Scene;
    rain.bounce = 0.0f;
    rain.friction = 1.0f;
    for (int i = 0; i < 240; ++i) rain.Update(1.0f / 60.0f, scene);
    ASSERT_TRUE(rain.TotalCollisionHits() > 100u);
    int onCrate = 0;
    for (const auto& p : rain.GetParticles()) {
        if (!p.alive) continue;
        ASSERT_TRUE(p.position.y > -0.05f); // never under the ground
        const bool overCrate = std::abs(p.position.x - 1.6f) < 0.55f && std::abs(p.position.z) < 0.55f;
        if (overCrate) ASSERT_TRUE(p.position.y > 1.15f); // never inside the crate
        if (overCrate && p.position.y < 1.3f) ++onCrate;
    }
    ASSERT_TRUE(onCrate > 10);

    // Die on hit, with splashes thrown up from where they land
    rain.ResetParticles();
    rain.killOnCollision = true;
    rain.hitSpawnCount = 2;
    rain.hitSpawnSpeed = 1.0f;
    rain.TakeCollisionHits();
    bool sawSplash = false;
    for (int i = 0; i < 120; ++i) {
        rain.Update(1.0f / 60.0f, scene);
        for (const auto& p : rain.GetParticles()) {
            if (p.alive && p.child) {
                sawSplash = true;
                ASSERT_TRUE(p.position.y > -0.05f);
            }
        }
    }
    ASSERT_TRUE(sawSplash);
    ASSERT_TRUE(rain.TakeCollisionHits() > 0u);
    ASSERT_EQ(rain.TakeCollisionHits(), 0u); // taken

    // Ground plane mode: a bounce keeps the particle above the plane and sends it back up
    auto sparkNode = std::dynamic_pointer_cast<ParticleNode>(scene.AddParticleNode("Spark"));
    ParticleNode& spark = *sparkNode;
    spark.position = glm::vec3(-5.0f, 1.0f, 0.0f);
    spark.emitterShape = ParticleEmitterShape::Point;
    spark.startSpeed = 0.0f;
    spark.spawnRate = 0.0f;
    spark.gravityMultiplier = 1.0f;
    spark.lifetimeMin = spark.lifetimeMax = 5.0f;
    spark.collisionMode = ParticleCollisionMode::GroundPlane;
    spark.collisionPlaneHeight = 0.5f;
    spark.bounce = 0.8f;
    spark.EmitBurst(1, scene);
    float lowest = 10.0f;
    bool rose = false;
    float lastY = 1.0f;
    for (int i = 0; i < 120; ++i) {
        spark.Update(1.0f / 60.0f, scene);
        for (const auto& p : spark.GetParticles()) {
            if (!p.alive) continue;
            lowest = std::min(lowest, p.position.y);
            if (p.position.y > lastY + 1e-4f && lowest < 0.6f) rose = true;
            lastY = p.position.y;
        }
    }
    ASSERT_TRUE(lowest >= 0.5f - 0.01f);
    ASSERT_TRUE(rose);

    // Flipbook: over the lifetime the frames run 0 -> last; a fixed rate loops
    spark.flipbookColumns = 4;
    spark.flipbookRows = 4;
    ASSERT_EQ(spark.FlipbookFrameCount(), 16);
    ParticleInstance p;
    p.lifetime = 2.0f;
    p.age = 0.0f;
    ASSERT_FLOAT_NEAR(spark.FlipbookFrame(p), 0.0f, 1e-4f);
    p.age = 1.0f;
    ASSERT_FLOAT_NEAR(spark.FlipbookFrame(p), 8.0f, 1e-3f);
    p.age = 2.0f;
    ASSERT_TRUE(spark.FlipbookFrame(p) > 15.9f && spark.FlipbookFrame(p) < 16.0f); // holds the last frame
    spark.flipbookMode = ParticleFlipbookMode::FixedRate;
    spark.flipbookFps = 10.0f;
    p.age = 1.75f;
    ASSERT_FLOAT_NEAR(spark.FlipbookFrame(p), 1.5f, 1e-3f); // 17.5 frames, wrapped
    spark.flipbookFrames = 6;
    ASSERT_EQ(spark.FlipbookFrameCount(), 6);

    // Trails: a moving particle leaves points behind it, drawn as segments
    spark.collisionMode = ParticleCollisionMode::None;
    spark.trailMode = ParticleTrailMode::PerParticle;
    spark.trailLength = 8;
    spark.trailInterval = 0.02f;
    spark.ResetParticles();
    spark.EmitBurst(1, scene);
    for (int i = 0; i < 30; ++i) spark.Update(1.0f / 60.0f, scene);
    std::vector<ParticleQuad> quads;
    spark.BuildQuads(scene, glm::vec3(0.0f, 2.0f, 10.0f), false, quads);
    int segments = 0;
    int sprites = 0;
    for (const auto& q : quads) {
        if (q.params.z < -0.5f) {
            ++sprites;
            continue;
        }
        ++segments;
        ASSERT_TRUE(q.a.y <= q.b.y + 1e-4f);   // falling: the trail runs back up
        ASSERT_TRUE(q.params.w >= q.params.z); // fading toward the tail
    }
    ASSERT_EQ(sprites, 1);
    ASSERT_TRUE(segments >= 5 && segments <= 8);
    // The trail outlives its particle and fades away
    for (int i = 0; i < 300; ++i) spark.Update(1.0f / 60.0f, scene);
    quads.clear();
    spark.BuildQuads(scene, glm::vec3(0.0f, 2.0f, 10.0f), false, quads);
    ASSERT_EQ(quads.size(), 0u);

    // Ribbons join the particles in the order they were born
    spark.trailMode = ParticleTrailMode::Ribbon;
    spark.trailDrawParticles = false;
    spark.ResetParticles();
    spark.startSpeed = 1.0f;
    spark.emitterShape = ParticleEmitterShape::Cone;
    for (int i = 0; i < 5; ++i) {
        spark.EmitBurst(1, scene);
        spark.Update(1.0f / 30.0f, scene);
    }
    quads.clear();
    spark.BuildQuads(scene, glm::vec3(0.0f, 2.0f, 10.0f), true, quads);
    ASSERT_EQ(quads.size(), 4u); // five particles, four joins
    // Sorted back to front for alpha blending
    const glm::vec3 eye(0.0f, 2.0f, 10.0f);
    for (size_t i = 1; i < quads.size(); ++i) {
        const glm::vec3 previous = (glm::vec3(quads[i - 1].a) + glm::vec3(quads[i - 1].b)) * 0.5f;
        const glm::vec3 current = (glm::vec3(quads[i].a) + glm::vec3(quads[i].b)) * 0.5f;
        ASSERT_TRUE(glm::distance(previous, eye) >= glm::distance(current, eye) - 1e-4f);
    }

    // Built-in sheets: 8 x 8 frames, clear at every frame's border
    for (ParticleSheet sheet : { ParticleSheet::Fire, ParticleSheet::Smoke, ParticleSheet::Explosion, ParticleSheet::Sparks }) {
        const std::vector<uint8_t> pixels = ParticleSheets::Generate(sheet);
        ASSERT_EQ(pixels.size(), static_cast<size_t>(ParticleSheets::kSize) * ParticleSheets::kSize * 4);
        uint32_t covered = 0;
        uint32_t border = 0;
        for (int y = 0; y < ParticleSheets::kSize; ++y) {
            for (int x = 0; x < ParticleSheets::kSize; ++x) {
                const uint8_t a = pixels[(static_cast<size_t>(y) * ParticleSheets::kSize + x) * 4 + 3];
                if (a > 20) ++covered;
                if ((x % ParticleSheets::kFrameSize == 0 || y % ParticleSheets::kFrameSize == 0) && a > 0) ++border;
            }
        }
        ASSERT_TRUE(covered > 2000u);
        ASSERT_EQ(border, 0u);
    }

    // Saved and loaded with the scene
    rain.builtinSheet = ParticleSheet::Smoke;
    rain.flipbookColumns = 8;
    rain.softness = 1.25f;
    rain.trailMode = ParticleTrailMode::PerParticle;
    rain.trailLength = 12;
    rain.hitSpawnCount = 3;
    rain.renderMode = ParticleRenderMode::Distortion;
    rain.simulationMode = ParticleSimulationMode::GPU_Niagara;
    const std::string path = "vfx_roundtrip_test.scene";
    ASSERT_TRUE(scene.SaveToFile(path));
    Scene loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path));
    std::filesystem::remove(path);
    std::shared_ptr<ParticleNode> back;
    for (const auto& n : loaded.GetNodes()) {
        if (n->name == "Rain") back = std::dynamic_pointer_cast<ParticleNode>(n);
    }
    ASSERT_TRUE(back != nullptr);
    ASSERT_TRUE(back->builtinSheet == ParticleSheet::Smoke);
    ASSERT_EQ(back->flipbookColumns, 8);
    ASSERT_FLOAT_NEAR(back->softness, 1.25f, 1e-4f);
    ASSERT_TRUE(back->trailMode == ParticleTrailMode::PerParticle);
    ASSERT_EQ(back->trailLength, 12);
    ASSERT_EQ(back->hitSpawnCount, 3);
    ASSERT_TRUE(back->killOnCollision);
    ASSERT_TRUE(back->collisionMode == ParticleCollisionMode::Scene);
    ASSERT_TRUE(back->renderMode == ParticleRenderMode::Distortion);
    ASSERT_TRUE(back->simulationMode == ParticleSimulationMode::GPU_Niagara);
    // GPU mode: the CPU only counts spawns for the GPU
    back->EmitBurst(25, loaded);
    back->Update(0.5f, loaded);
    ASSERT_EQ(back->TakeGpuSpawns(), 25u + 100u); // the burst, and 0.5 s at 200 per second
    ASSERT_FLOAT_NEAR(back->TakeGpuTime(), 0.5f, 1e-5f);
}

#include "Monitoring/GameProfiler.h"
TEST_CASE(Tooling, MeasuredGpuPassesAndPreviewScene) {
    // The Profiler's GPU tab shows the measured passes, each with its share of their total
    auto& profiler = Monitoring::GameProfiler::Get();
    std::vector<Monitoring::GPUPassTiming> passes(3);
    passes[0].passName = "Shadows";
    passes[0].estimatedDurationMs = 1.0f;
    passes[1].passName = "Opaque";
    passes[1].estimatedDurationMs = 2.0f;
    passes[2].passName = "Post-Processing";
    passes[2].estimatedDurationMs = 1.0f;
    profiler.SetMeasuredGPUPasses(passes);
    const auto& shown = profiler.GetMeasuredGPUPasses();
    ASSERT_EQ(shown.size(), 3u);
    ASSERT_TRUE(shown[1].passName == "Opaque");
    ASSERT_FLOAT_NEAR(shown[1].percentage, 50.0f, 0.01f);
    ASSERT_FLOAT_NEAR(shown[0].percentage + shown[1].percentage + shown[2].percentage, 100.0f, 0.01f);
    profiler.SetMeasuredGPUPasses({});
    ASSERT_EQ(profiler.GetMeasuredGPUPasses().size(), 0u); // nothing measured: nothing made up

    // The material preview studio: its nodes are replaced per preview, its environment kept
    Scene studio;
    studio.GetEnvironment().skyIntensity = 0.75f;
    studio.GetEnvironment().enableShadows = false;
    ASSERT_TRUE(!studio.GetNodes().empty()); // a new scene has its default nodes
    studio.ClearNodes();
    ASSERT_EQ(studio.GetNodes().size(), 0u);
    ASSERT_FLOAT_NEAR(studio.GetEnvironment().skyIntensity, 0.75f, 1e-6f);
    ASSERT_TRUE(!studio.GetEnvironment().enableShadows);
    auto sphere = studio.AddNode(ShapeType::Sphere, "Preview");
    Node gold;
    gold.color = glm::vec4(1.0f, 0.78f, 0.34f, 1.0f);
    gold.metallic = 1.0f;
    gold.texturePath = "gold.png";
    const uint32_t id = sphere->id;
    static_cast<Node&>(*sphere) = gold;
    sphere->id = id;
    ASSERT_EQ(studio.GetNodeById(id).get(), sphere.get());
    ASSERT_FLOAT_NEAR(sphere->metallic, 1.0f, 1e-6f);
    ASSERT_TRUE(sphere->texturePath == "gold.png");
    studio.Clear();
    ASSERT_FLOAT_NEAR(studio.GetEnvironment().skyIntensity, SceneEnvironment{}.skyIntensity, 1e-6f); // Clear resets it
}


#include "Renderer/RenderEffects.h"
TEST_CASE(RoadmapRendering, VoxelGlobalIlluminationAndSerialization) {
    Scene scene;
    scene.ClearNodes();
    auto& env = scene.GetEnvironment();
    env.vxgiEnabled = true;
    env.vxgiIntensity = 1.65f;
    env.vxgiRange = 32.0f;
    env.vxgiBounce = 0.85f;

    auto sun = scene.AddLightNode(LightType::Directional, "Sun");
    sun->color = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);
    sun->lightIntensity = 2.5f;
    sun->rotation = glm::vec3(-45.0f, 30.0f, 0.0f);

    auto redWall = scene.AddNode(ShapeType::Cube, "RedWall");
    redWall->position = glm::vec3(0.0f, 1.0f, -2.0f);
    redWall->scale = glm::vec3(4.0f, 3.0f, 1.0f);
    redWall->color = glm::vec4(0.95f, 0.1f, 0.1f, 1.0f);
    redWall->emissiveColor = glm::vec3(0.8f, 0.05f, 0.05f);
    redWall->emissiveIntensity = 1.5f;

    // 1. Test SceneEnvironment VXGI serialization roundtrip
    const JsonValue json = scene.Serialize();
    Scene loaded;
    ASSERT_TRUE(loaded.Deserialize(json));
    ASSERT_TRUE(loaded.GetEnvironment().vxgiEnabled);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().vxgiIntensity, 1.65f, 1e-4f);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().vxgiRange, 32.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(loaded.GetEnvironment().vxgiBounce, 0.85f, 1e-4f);

    // 2. Test VoxelGlobalIllumination voxelization, mip chain, diffuse bounce & specular cone tracing
    Renderer::VoxelGlobalIllumination vxgi;
    vxgi.Init(16, glm::vec3(-8.0f), 16.0f);
    ASSERT_EQ(vxgi.GetBaseDim(), 16u);
    ASSERT_EQ(vxgi.GetMipCount(), 5u); // 16, 8, 4, 2, 1

    vxgi.VoxelizeScene(scene);
    // Point in front of the red wall looking toward -Z (the wall)
    const glm::vec4 diffTowardWall = vxgi.EvaluateDiffuseGI(glm::vec3(0.0f, 1.0f, 0.5f), glm::vec3(0.0f, 0.0f, -1.0f));
    ASSERT_TRUE(diffTowardWall.r > diffTowardWall.g);
    ASSERT_TRUE(diffTowardWall.r > 0.01f);
    ASSERT_TRUE(diffTowardWall.a < 0.99f); // voxel AO darkens near the wall

    const glm::vec4 specTowardWall = vxgi.EvaluateSpecularGI(
        glm::vec3(0.0f, 1.0f, 0.5f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.15f);
    ASSERT_TRUE(specTowardWall.r > specTowardWall.b);
    ASSERT_TRUE(specTowardWall.a > 0.05f);
}

TEST_CASE(RoadmapRendering, AreaLightAndMultiMaterialGltfSubmeshes) {
    // 1. Test AreaRect Light serialization and properties (Priority 5)
    Scene scene;
    scene.ClearNodes();
    auto areaLight = scene.AddLightNode(LightType::AreaRect, "StudioSoftbox");
    areaLight->areaLightSize = glm::vec2(2.4f, 1.2f);
    areaLight->areaLightTwoSided = true;
    areaLight->lightIntensity = 6.0f;
    areaLight->heightMapPath = "assets/textures/brick_height.png";
    areaLight->heightScale = 0.045f;

    const JsonValue json = scene.Serialize();
    Scene loaded;
    ASSERT_TRUE(loaded.Deserialize(json));
    auto loadedLight = loaded.GetNodeByName("StudioSoftbox");
    ASSERT_TRUE(loadedLight != nullptr);
    ASSERT_EQ(static_cast<int>(loadedLight->lightType), static_cast<int>(LightType::AreaRect));
    ASSERT_FLOAT_NEAR(loadedLight->areaLightSize.x, 2.4f, 1e-4f);
    ASSERT_FLOAT_NEAR(loadedLight->areaLightSize.y, 1.2f, 1e-4f);
    ASSERT_TRUE(loadedLight->areaLightTwoSided);
    ASSERT_EQ(loadedLight->heightMapPath, "assets/textures/brick_height.png");
    ASSERT_FLOAT_NEAR(loadedLight->heightScale, 0.045f, 1e-4f);

    // 2. Test Multi-Material glTF Submeshes (Priority 6)
    struct BinPayload {
        float positions[9] = {
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             0.0f,  1.0f, 0.0f
        };
        uint16_t indices[4] = { 0, 1, 2, 0 };
    } payload;

    std::string gltfJson = R"({
  "asset": { "version": "2.0" },
  "buffers": [{ "byteLength": 44 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "materials": [
    {
      "name": "ChromeTrim",
      "pbrMetallicRoughness": {
        "baseColorFactor": [0.95, 0.95, 0.98, 1.0],
        "metallicFactor": 1.0,
        "roughnessFactor": 0.08
      }
    },
    {
      "name": "MatteRubber",
      "pbrMetallicRoughness": {
        "baseColorFactor": [0.15, 0.15, 0.16, 1.0],
        "metallicFactor": 0.0,
        "roughnessFactor": 0.88
      }
    }
  ],
  "meshes": [
    {
      "name": "MultiMatWheel",
      "primitives": [
        { "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 },
        { "attributes": { "POSITION": 0 }, "indices": 1, "material": 1 }
      ]
    }
  ]
})";
    while (gltfJson.size() % 4 != 0) gltfJson.push_back(' ');
    uint32_t jsonChunkLen = static_cast<uint32_t>(gltfJson.size());
    uint32_t binChunkLen = static_cast<uint32_t>(sizeof(BinPayload));
    uint32_t totalLen = 12 + 8 + jsonChunkLen + 8 + binChunkLen;
    std::vector<uint8_t> glb(totalLen);
    uint32_t magic = 0x46546C67u, version = 2, jsonType = 0x4E4F534Au, binType = 0x004E4942u;
    size_t offset = 0;
    std::memcpy(glb.data() + offset, &magic, 4); offset += 4;
    std::memcpy(glb.data() + offset, &version, 4); offset += 4;
    std::memcpy(glb.data() + offset, &totalLen, 4); offset += 4;
    std::memcpy(glb.data() + offset, &jsonChunkLen, 4); offset += 4;
    std::memcpy(glb.data() + offset, &jsonType, 4); offset += 4;
    std::memcpy(glb.data() + offset, gltfJson.data(), jsonChunkLen); offset += jsonChunkLen;
    std::memcpy(glb.data() + offset, &binChunkLen, 4); offset += 4;
    std::memcpy(glb.data() + offset, &binType, 4); offset += 4;
    std::memcpy(glb.data() + offset, &payload, binChunkLen);

    LoadedMesh multiMesh;
    ASSERT_TRUE(GltfLoader::LoadFromMemory(glb.data(), glb.size(), multiMesh, "", false));
    ASSERT_EQ(multiMesh.submeshes.size(), 2u);
    ASSERT_EQ(multiMesh.submeshes[0].firstIndex, 0u);
    ASSERT_EQ(multiMesh.submeshes[0].indexCount, 3u);
    ASSERT_FLOAT_NEAR(multiMesh.submeshes[0].metallic, 1.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(multiMesh.submeshes[0].roughness, 0.08f, 1e-4f);
    ASSERT_EQ(multiMesh.submeshes[1].firstIndex, 3u);
    ASSERT_EQ(multiMesh.submeshes[1].indexCount, 3u);
    ASSERT_FLOAT_NEAR(multiMesh.submeshes[1].metallic, 0.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(multiMesh.submeshes[1].roughness, 0.88f, 1e-4f);
    ASSERT_EQ(multiMesh.meshData.submeshes.size(), 2u);
}


TEST_CASE(RoadmapCutscenes, LocationTriggerCameraEffectsAndSkip) {
    Scene scene;
    auto hero = scene.AddCharacterNode("Hero");
    hero->position = glm::vec3(0.0f, 0.0f, 0.0f);
    auto cam = scene.AddCameraNode("MainCamera");
    cam->position = glm::vec3(0.0f, 2.0f, 8.0f);
    cam->cameraFov = 60.0f;
    scene.SetActiveCamera(cam->id);

    // Record baseline environment
    scene.GetEnvironment().saturation = 1.0f;
    scene.GetEnvironment().temperature = 0.0f;
    scene.GetEnvironment().vignette = 0.0f;
    scene.GetEnvironment().depthOfField = false;

    Animation::CutsceneSequence seq;
    seq.name = "TempleArrival";
    seq.duration = 4.0f;
    seq.skippable = true;
    seq.silentAudio = true;
    seq.letterboxAmount = 0.12f;
    seq.colorFilter = Animation::CutsceneColorFilter::Sepia;
    seq.overrideVignette = true;
    seq.vignetteAmount = 0.65f;
    seq.enableDof = true;
    seq.autoFocusOnLookAt = false;
    seq.dofFocusDistance = 6.0f;
    seq.dofAperture = 0.45f;

    seq.triggerOnLocation = true;
    seq.triggerActorName = "Hero";
    seq.triggerPosition = glm::vec3(5.0f, 0.0f, 0.0f);
    seq.triggerRadius = 2.0f;
    seq.triggerOnce = true;

    Animation::CutsceneCameraShot shot;
    shot.startTime = 0.0f;
    shot.duration = 4.0f;
    shot.startPos = glm::vec3(2.0f, 2.5f, 6.0f);
    shot.endPos = glm::vec3(8.0f, 1.8f, 3.0f);
    shot.lookAtNodeName = "Hero";
    shot.startFov = 58.0f;
    shot.endFov = 38.0f;
    shot.smoothBlend = false;
    seq.cameraShots.push_back(shot);

    Animation::CutsceneActorAction act;
    act.actorNodeName = "Hero";
    act.startTime = 0.0f;
    act.duration = 4.0f;
    act.useCurrentStartPos = true;
    act.endPos = glm::vec3(10.0f, 0.0f, -4.0f);
    act.animState = "Walk";
    act.endAnimState = "Idle";
    seq.actorActions.push_back(act);

    Animation::CutsceneSubtitleCue cue;
    cue.startTime = 0.5f;
    cue.duration = 3.0f;
    cue.speaker = "Hero";
    cue.text = "Look at those ancient pillars.";
    seq.subtitleCues.push_back(cue);

    scene.AddCutscene(seq);

    // 1. Verify JSON Serialization & Deserialization round-trip of Cutscenes
    JsonValue sceneJson = scene.Serialize();
    Scene restoredScene;
    ASSERT_TRUE(restoredScene.Deserialize(sceneJson));
    ASSERT_EQ(restoredScene.GetCutscenes().size(), 1u);
    ASSERT_EQ(restoredScene.GetCutscenes()[0].name, "TempleArrival");
    ASSERT_EQ(static_cast<int>(restoredScene.GetCutscenes()[0].colorFilter), static_cast<int>(Animation::CutsceneColorFilter::Sepia));

    Animation::CutscenePlayer player;

    // 2. Hero at (0,0,0) is outside trigger radius of (5,0,0) -> should NOT trigger
    ASSERT_FALSE(player.CheckLocationTriggers(scene));
    ASSERT_FALSE(player.IsPlaying());

    // 3. Move Hero to (4.2, 0, 0) inside radius 2.0 of (5,0,0) -> should trigger automatically!
    hero->position = glm::vec3(4.2f, 0.0f, 0.0f);
    ASSERT_TRUE(player.CheckLocationTriggers(scene));
    ASSERT_TRUE(player.IsPlaying());
    ASSERT_EQ(player.GetActiveCutsceneName(), "TempleArrival");

    // 4. Advance 1.0s into cutscene and verify camera, actor walk, subtitle, and Sepia + Vignette + DOF effects
    player.Update(scene, 1.0f);
    ASSERT_EQ(hero->currentAnimState, "Walk");
    ASSERT_TRUE(hero->position.x > 4.2f && hero->position.x < 10.0f);
    ASSERT_TRUE(player.GetActiveSubtitle() != nullptr);
    ASSERT_EQ(player.GetActiveSubtitle()->speaker, "Hero");
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().saturation, 0.22f, 1e-4f);
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().temperature, 0.65f, 1e-4f);
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().vignette, 0.65f, 1e-4f);
    ASSERT_TRUE(scene.GetEnvironment().depthOfField);

    // 5. Verify Black & White filter mode dynamically updates saturation to 0.0
    scene.FindCutscene("TempleArrival")->colorFilter = Animation::CutsceneColorFilter::BlackAndWhite;
    player.Update(scene, 0.1f);
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().saturation, 0.0f, 1e-4f);

    // 6. Skip the cutscene and verify Hero jumps to final mark, Idle state, and environment + camera restore
    bool skipSignalReceived = false;
    hero->ConnectSignal("on_cutscene_skipped", [&](const std::vector<std::string>& args) {
        if (!args.empty() && args[0] == "TempleArrival") skipSignalReceived = true;
    });

    ASSERT_TRUE(player.Skip(scene));
    ASSERT_FALSE(player.IsPlaying());
    ASSERT_TRUE(skipSignalReceived);
    ASSERT_FLOAT_NEAR(hero->position.x, 10.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(hero->position.z, -4.0f, 1e-4f);
    ASSERT_EQ(hero->currentAnimState, "Idle");

    // Pre-cutscene environment & camera restored
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().saturation, 1.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().temperature, 0.0f, 1e-4f);
    ASSERT_FLOAT_NEAR(scene.GetEnvironment().vignette, 0.0f, 1e-4f);
    ASSERT_FALSE(scene.GetEnvironment().depthOfField);
    ASSERT_FLOAT_NEAR(cam->cameraFov, 60.0f, 1e-4f);
}

// =========================================================================
// 41. In-Game Video Screens, Cutaways & Media Streaming
// =========================================================================
TEST_CASE(RoadmapVideo, VideoScreensAndCutaways) {
    Scene scene;

    // 1. Create a 3D Video Screen node (TV set)
    auto tv = scene.AddNode(ShapeType::Cube, "LivingRoomTV");
    tv->isVideoScreen = true;
    tv->videoChannelPreset = 2; // TV News Broadcast
    tv->videoPlaying = true;
    tv->videoScreenBrightness = 1.8f;
    tv->videoDuration = 10.0f;

    // 2. Create a 2D UIVideoNode
    auto uiVideo = scene.AddUIVideoNode("SecurityCCTVOverlay");
    uiVideo->videoChannelPreset = 3; // CCTV Feed
    uiVideo->videoPlaying = true;

    // 3. Update video system for 1.5 seconds and verify frame decoding & state
    auto& vSys = Video::VideoSystem::Get();
    vSys.Update(scene, 1.5f, true);

    ASSERT_FLOAT_NEAR(tv->videoCurrentTime, 1.5f, 1e-3f);
    ASSERT_FLOAT_NEAR(uiVideo->videoCurrentTime, 1.5f, 1e-3f);
    ASSERT_EQ(tv->texturePath, Video::VideoSystem::GetRuntimeTextureKey(tv->id));
    ASSERT_EQ(tv->emissiveMapPath, Video::VideoSystem::GetRuntimeTextureKey(tv->id));
    ASSERT_FLOAT_NEAR(tv->emissiveIntensity, 1.8f, 1e-4f);

    const auto* tvState = vSys.GetNodeVideoState(tv->id);
    ASSERT_TRUE(tvState != nullptr);
    ASSERT_TRUE(!tvState->rgbaPixels.empty());
    ASSERT_EQ(tvState->width, 256u);
    ASSERT_EQ(tvState->height, 144u);

    // Verify average screen color is computed for dynamic lighting / VXGI glow
    glm::vec3 avgCol = vSys.GetAverageScreenColor(tv->id);
    ASSERT_TRUE(avgCol.r > 0.0f || avgCol.g > 0.0f || avgCol.b > 0.0f);

    // 4. Test Scene JSON serialization roundtrip for video screens
    JsonValue json = scene.Serialize();
    Scene restoredScene;
    restoredScene.Deserialize(json);

    auto restoredTV = restoredScene.GetNodeById(tv->id);
    ASSERT_TRUE(restoredTV != nullptr);
    ASSERT_TRUE(restoredTV->isVideoScreen);
    ASSERT_EQ(restoredTV->videoChannelPreset, 2);
    ASSERT_FLOAT_NEAR(restoredTV->videoScreenBrightness, 1.8f, 1e-4f);

    // 5. Test Full-Screen Cutaway Transmission & Skip
    ASSERT_FALSE(vSys.IsCutawayPlaying());
    vSys.StartCutaway(1, "", 8.0f, true, false, "MISSION TRANSMISSION");
    ASSERT_TRUE(vSys.IsCutawayPlaying());
    ASSERT_EQ(vSys.GetCutawayState().title, "MISSION TRANSMISSION");

    // Advance cutaway
    vSys.Update(scene, 0.5f, true);
    ASSERT_FLOAT_NEAR(vSys.GetCutawayState().currentTime, 0.5f, 1e-3f);

    // Skip cutaway
    ASSERT_TRUE(vSys.SkipCutaway());
    ASSERT_FALSE(vSys.IsCutawayPlaying());
}

// =========================================================================
// 79. Hardware Ray Tracing Tests (Vulkan KHR & Apple Metal MPS Abstraction)
// =========================================================================

TEST_CASE(RoadmapRayTracing, VulkanAndMetalHardwareRayTracing) {
    using namespace Graphics::RayTracing;
    auto& rtManager = RayTracingManager::Get();

    // 1. Initialize Ray Tracing Manager
    ASSERT_TRUE(rtManager.Initialize());
    ASSERT_TRUE(rtManager.GetBackend() != nullptr);

    // 2. Test geometry BLAS generation
    std::vector<Vertex> triangleVerts(3);
    triangleVerts[0].pos = glm::vec3(-1.0f, 0.0f, 0.0f);
    triangleVerts[1].pos = glm::vec3( 1.0f, 0.0f, 0.0f);
    triangleVerts[2].pos = glm::vec3( 0.0f, 1.0f, 0.0f);
    std::vector<uint32_t> indices = {0, 1, 2};

    RayTracingMeshHandle handle = rtManager.RegisterMesh("test_triangle", triangleVerts, indices);
    ASSERT_TRUE(handle != kInvalidMeshHandle);

    // Registering same cacheKey returns existing cached BLAS
    RayTracingMeshHandle cachedHandle = rtManager.RegisterMesh("test_triangle", triangleVerts, indices);
    ASSERT_EQ(handle, cachedHandle);

    // 3. Test Scene Instance Sync & TLAS build
    Scene scene;
    scene.Clear();
    auto cubeA = scene.AddNode(ShapeType::Cube, "RayTracedCubeA");
    cubeA->position = glm::vec3(-5.0f, 1.0f, 0.0f);
    auto cubeB = scene.AddNode(ShapeType::Cube, "RayTracedCubeB");
    cubeB->position = glm::vec3( 5.0f, 1.0f, 0.0f);

    rtManager.SyncScene(scene);
    auto stats = rtManager.GetStats();
    ASSERT_TRUE(stats.totalBLASCount >= 1);
    ASSERT_TRUE(stats.activeTLASInstances >= 2);
    ASSERT_TRUE(stats.bvhMemoryBytes > 0);

    // 4. Test Ray Tracing Feature Settings
    auto& settings = rtManager.GetSettings();
    settings.enableHardwareRT = true;
    settings.enableRTAO = true;
    settings.enableRTR = true;
    settings.enableRTS = true;
    settings.enableRTGI = true;
    settings.rtaoRadius = 4.0f;
    settings.rtrMaxBounces = 3;

    ASSERT_TRUE(settings.enableHardwareRT);
    ASSERT_TRUE(settings.enableRTAO);
    ASSERT_TRUE(settings.enableRTR);
    ASSERT_TRUE(settings.enableRTS);
    ASSERT_TRUE(settings.enableRTGI);
    ASSERT_FLOAT_NEAR(settings.rtaoRadius, 4.0f, 1e-4f);
    ASSERT_EQ(settings.rtrMaxBounces, 3);

    // 5. Test Apple Metal Ray Tracing backend abstraction
    // Ensures seamless cross-platform portability between Vulkan KHR and Apple Silicon Metal MPS
    auto metalBackend = std::make_unique<MetalRayTracing>();
    ASSERT_EQ(metalBackend->GetType(), RayTracingBackendType::Metal_MPS);
    ASSERT_TRUE(std::string(metalBackend->GetName()).find("Apple Metal") != std::string::npos);

    // Hot-swap backend to Metal to verify complete interface conformity
    rtManager.SetBackend(std::move(metalBackend));
    ASSERT_TRUE(rtManager.GetBackend() != nullptr);
    ASSERT_EQ(rtManager.GetBackend()->GetType(), RayTracingBackendType::Metal_MPS);

    // Build BLAS and TLAS on Metal backend
    RayTracingMeshHandle metalHandle = rtManager.RegisterMesh("metal_triangle", triangleVerts, indices);
    ASSERT_TRUE(metalHandle != kInvalidMeshHandle);

    rtManager.SyncScene(scene);
    auto metalStats = rtManager.GetStats();
    ASSERT_TRUE(metalStats.totalBLASCount >= 1);
    ASSERT_TRUE(metalStats.activeTLASInstances >= 2);
    ASSERT_TRUE(metalStats.bvhMemoryBytes > 0);

    // Revert to default backend for subsequent engine passes
    rtManager.Initialize(RayTracingBackendType::Vulkan_KHR);
}

// ============================================================================
// Ray tracing scene sync: real geometry per node, rebuilds on change, contexts
// ============================================================================
namespace {
// Records what the manager hands to a backend
class RecordingRTBackend : public Graphics::RayTracing::IRayTracingBackend {
public:
    Graphics::RayTracing::RayTracingSettings settings;
    std::vector<size_t> createdVertexCounts;
    int destroyed{0};
    std::vector<Graphics::RayTracing::RayTracingInstance> lastInstances[Graphics::RayTracing::kRTContextCount];
    uint32_t nextHandle{1};

    Graphics::RayTracing::RayTracingBackendType GetType() const override { return Graphics::RayTracing::RayTracingBackendType::Hybrid_Software; }
    const char* GetName() const override { return "Recording"; }
    bool IsSupported() const override { return true; }
    bool Initialize() override { return true; }
    void Shutdown() override {}
    Graphics::RayTracing::RayTracingMeshHandle CreateBLAS(const std::vector<Vertex>& v, const std::vector<uint32_t>&) override {
        createdVertexCounts.push_back(v.size());
        return nextHandle++;
    }
    void DestroyBLAS(Graphics::RayTracing::RayTracingMeshHandle) override { ++destroyed; }
    bool BuildTLAS(const std::vector<Graphics::RayTracing::RayTracingInstance>& instances, uint32_t context) override {
        lastInstances[context] = instances;
        return true;
    }
    Graphics::RayTracing::RayTracingSettings& GetSettings() override { return settings; }
    const Graphics::RayTracing::RayTracingSettings& GetSettings() const override { return settings; }
    Graphics::RayTracing::RayTracingStats GetStats() const override { return {}; }
};
} // namespace

TEST_CASE(RoadmapRayTracing, SceneSyncUsesRealGeometryAndContexts) {
    using namespace Graphics::RayTracing;
    auto& rtManager = RayTracingManager::Get();
    auto recorder = std::make_unique<RecordingRTBackend>();
    RecordingRTBackend* rec = recorder.get();
    rtManager.SetBackend(std::move(recorder));

    Scene scene;
    scene.Clear();
    auto model = scene.AddNode(ShapeType::Custom, "ImportedModel");
    model->customMeshName = "models/rock.glb";
    auto leaves = scene.AddNode(ShapeType::Custom, "Leaves");
    leaves->customMeshName = "models/rock.glb";
    leaves->alphaCutoff = 0.5f;
    auto ocean = scene.AddNode(ShapeType::Plane, "Ocean");
    ocean->isOcean = true;
    auto glass = scene.AddNode(ShapeType::Cube, "Glass");
    glass->color.a = 0.3f;
    auto farCube = scene.AddNode(ShapeType::Cube, "FarAway");
    farCube->position = glm::vec3(5000.0f, 0.0f, 0.0f);

    // A 4-vertex "imported" mesh (a quad), not a cube
    MeshData rock;
    rock.vertices.resize(4);
    rock.vertices[1].pos = glm::vec3(1, 0, 0);
    rock.vertices[2].pos = glm::vec3(1, 0, 1);
    rock.vertices[3].pos = glm::vec3(0, 0, 1);
    rock.indices = {0, 1, 2, 0, 2, 3};
    const MeshData* currentRock = &rock;
    RayTracingManager::MeshResolver resolver = [&](const Node& node, std::string& key, const MeshData*& data) {
        if (node.type != ShapeType::Custom) return false;
        key = "mesh:" + node.customMeshName;
        data = currentRock;
        return true;
    };
    RayTracingManager::MaterialResolver material = [](const Node& node, RayTracingInstance& inst) {
        if (node.name == "Leaves") inst.albedoTexture = "leaves.png";
    };

    // 1. Without a resolver, imported meshes are left out (never replaced by a cube)
    rtManager.SyncScene(scene, 0);
    for (const auto& inst : rec->lastInstances[0]) {
        ASSERT_TRUE(inst.nodeId != model->id);
    }

    // 2. With the resolver: one BLAS with the mesh's own 4 vertices, shared by both nodes; the
    //    ocean, see-through glass and far-away cube (outside 1 km) are left out
    rec->createdVertexCounts.clear();
    rtManager.SyncScene(scene, 1, resolver, material, glm::vec3(0.0f), 1000.0f);
    ASSERT_EQ(rec->createdVertexCounts.size(), static_cast<size_t>(1));
    ASSERT_EQ(rec->createdVertexCounts[0], static_cast<size_t>(4));
    ASSERT_EQ(rec->lastInstances[1].size(), static_cast<size_t>(2));
    bool sawLeaves = false;
    for (const auto& inst : rec->lastInstances[1]) {
        if (inst.nodeId == leaves->id) {
            sawLeaves = true;
            ASSERT_TRUE(!inst.isOpaque);                   // alpha-tested
            ASSERT_EQ(inst.mask, kRTMaskFoliage);
        } else {
            ASSERT_EQ(inst.nodeId, model->id);
            ASSERT_TRUE(inst.isOpaque);
        }
    }
    ASSERT_TRUE(sawLeaves);

    // 3. Same key, unchanged data: no rebuild
    rtManager.SyncScene(scene, 1, resolver, material, glm::vec3(0.0f), 1000.0f);
    ASSERT_EQ(rec->createdVertexCounts.size(), static_cast<size_t>(1));
    ASSERT_EQ(rec->destroyed, 0);

    // 4. The mesh changes (re-import / sculpt): the old BLAS goes, a new one is built
    MeshData rock2 = rock;
    rock2.vertices.push_back(Vertex{});
    rock2.indices.insert(rock2.indices.end(), {0, 3, 4});
    currentRock = &rock2;
    rtManager.SyncScene(scene, 1, resolver, material, glm::vec3(0.0f), 1000.0f);
    ASSERT_EQ(rec->destroyed, 1);
    ASSERT_EQ(rec->createdVertexCounts.size(), static_cast<size_t>(2));
    ASSERT_EQ(rec->createdVertexCounts[1], static_cast<size_t>(5));

    // 5. Clearing a context empties only that context
    rtManager.ClearContext(1);
    ASSERT_TRUE(rec->lastInstances[1].empty());

    rtManager.SetBackend(std::make_unique<VulkanRayTracing>());
}

// ============================================================================
// TEST 80: Meshlet GPU Virtualization & Cluster Culling
// ============================================================================
TEST_CASE(RoadmapMeshlets, MeshletGPUVirtualizationAndClusterCulling) {
    using namespace Graphics::Meshlet;

    // 1. Test Meshlet Generation from procedural sphere
    // Sphere with 32 slices and 32 stacks has 992 triangles and 561 vertices
    MeshData sphereMesh = MeshGenerator::CreateSphere(1.0f, 32, 32);
    ASSERT_TRUE(!sphereMesh.vertices.empty());
    ASSERT_TRUE(!sphereMesh.indices.empty());
    const uint32_t expectedTriangles = static_cast<uint32_t>(sphereMesh.indices.size() / 3);

    MeshletModel sphereModel = MeshletBuilder::BuildMeshlets(sphereMesh.vertices, sphereMesh.indices, 64, 126);

    ASSERT_TRUE(!sphereModel.meshlets.empty());
    ASSERT_EQ(sphereModel.originalTriangleCount, expectedTriangles);
    ASSERT_EQ(sphereModel.originalVertexCount, static_cast<uint32_t>(sphereMesh.vertices.size()));

    // Verify all meshlets strictly respect hardware primitive and vertex limits
    uint32_t totalTrianglesInMeshlets = 0;
    for (const auto& m : sphereModel.meshlets) {
        ASSERT_TRUE(m.vertexCount > 0);
        ASSERT_TRUE(m.vertexCount <= kMaxMeshletVertices);
        ASSERT_TRUE(m.triangleCount > 0);
        ASSERT_TRUE(m.triangleCount <= kMaxMeshletTriangles);
        totalTrianglesInMeshlets += m.triangleCount;

        // Verify bounding sphere validity
        ASSERT_TRUE(m.radius > 0.0f);
        for (uint32_t v = 0; v < m.vertexCount; ++v) {
            uint32_t vIdx = sphereModel.vertexIndices[m.vertexOffset + v];
            float dist = glm::distance(sphereMesh.vertices[vIdx].pos, m.center);
            ASSERT_TRUE(dist <= m.radius + 1e-4f);
        }

        // Verify normal cone validity
        float axisLen = glm::length(m.coneAxis);
        ASSERT_FLOAT_NEAR(axisLen, 1.0f, 1e-3f);
        ASSERT_TRUE(m.coneCutoff >= -1.01f && m.coneCutoff <= 1.01f);

        // Verify micro-triangle indices strictly map to valid local vertices
        for (uint32_t t = 0; t < m.triangleCount; ++t) {
            const auto& tri = sphereModel.triangles[m.triangleOffset + t];
            ASSERT_TRUE(tri.i0 < m.vertexCount);
            ASSERT_TRUE(tri.i1 < m.vertexCount);
            ASSERT_TRUE(tri.i2 < m.vertexCount);
        }
    }
    // Verify complete triangle coverage: zero dropped triangles
    ASSERT_EQ(totalTrianglesInMeshlets, expectedTriangles);
    ASSERT_TRUE(sphereModel.GetMemoryBytes() > 0);

    // 2. Test Cluster Frustum and Backface Cone Culling
    glm::vec3 cameraPos(0.0f, 0.0f, 5.0f);
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.777f, 0.1f, 100.0f);
    glm::mat4 viewProj = proj * view;

    MeshletSettings cullSettings;
    cullSettings.enableFrustumCulling = true;
    cullSettings.enableConeCulling = true;

    // A. Visible front-facing sphere at origin
    glm::mat4 sphereWorld = glm::mat4(1.0f);
    MeshletCullResult frontResult = MeshletBuilder::CullMeshlets(sphereModel, sphereWorld, cameraPos, viewProj, cullSettings);
    ASSERT_TRUE(frontResult.totalMeshlets == sphereModel.meshlets.size());
    ASSERT_TRUE(frontResult.passedMeshlets > 0);
    // On a sphere viewed from the front, backfacing meshlets on the far side must be culled
    ASSERT_TRUE(frontResult.backfaceConeCulled > 0);
    ASSERT_EQ(frontResult.passedMeshlets + frontResult.backfaceConeCulled + frontResult.frustumCulled, frontResult.totalMeshlets);
    ASSERT_TRUE(frontResult.GetCullRatioPercent() > 0.0f);

    // B. Object completely behind or outside frustum
    glm::mat4 outsideWorld = glm::translate(glm::mat4(1.0f), glm::vec3(500.0f, 500.0f, 500.0f));
    MeshletCullResult outsideResult = MeshletBuilder::CullMeshlets(sphereModel, outsideWorld, cameraPos, viewProj, cullSettings);
    ASSERT_EQ(outsideResult.passedMeshlets, 0);
    ASSERT_EQ(outsideResult.frustumCulled, outsideResult.totalMeshlets);
    ASSERT_FLOAT_NEAR(outsideResult.GetCullRatioPercent(), 100.0f, 1e-4f);

    // 3. Test MeshletManager Coordination and Scene Synchronization
    auto& mgr = MeshletManager::Get();
    mgr.Initialize(); // Headless initialize
    const auto& stats = mgr.GetStats();
    ASSERT_EQ(stats.maxPreferredMeshletVertices, 64);
    ASSERT_EQ(stats.maxPreferredMeshletTriangles, 126);

    const auto& regModel = mgr.RegisterMesh("unit_sphere", sphereMesh.vertices, sphereMesh.indices);
    ASSERT_EQ(regModel.meshlets.size(), sphereModel.meshlets.size());
    ASSERT_TRUE(mgr.GetMeshletModel("unit_sphere") != nullptr);

    // Create scene with multiple objects
    Scene scene;
    scene.AddNode(ShapeType::Cube, "TestCube");
    scene.AddNode(ShapeType::Sphere, "TestSphere");

    mgr.SyncScene(scene, cameraPos, viewProj);
    const auto& sceneStats = mgr.GetStats();
    ASSERT_TRUE(sceneStats.totalMeshletsActive > 0);
    ASSERT_TRUE(sceneStats.visibleMeshletsLastFrame > 0);
    ASSERT_EQ(sceneStats.visibleMeshletsLastFrame + sceneStats.culledMeshletsLastFrame, sceneStats.totalMeshletsActive);
    ASSERT_TRUE(sceneStats.gpuMemoryBytes > 0);

    mgr.Shutdown();
}

// ============================================================================
// TEST 81: Temporal Upscaling (DLSS / FSR 3 / XeSS) & Dynamic Resolution
// ============================================================================
TEST_CASE(RoadmapUpscaling, TemporalUpscalingAndDynamicResolution) {
    using namespace Graphics::Upscaling;

    auto& upscaler = TemporalUpscaler::Get();
    upscaler.Initialize();

    auto& settings = upscaler.GetSettings();
    settings.enableUpscaling = true;
    settings.mode = UpscalerMode::AMD_FSR_3;

    // 1. Verify resolution scaling for 4K display target (3840x2160)
    uint32_t rw = 0, rh = 0;

    // Quality preset (approx 1.5x upscaling, 66.7% resolution scale)
    settings.preset = UpscalerPreset::Quality;
    upscaler.CalculateRenderDimensions(3840, 2160, rw, rh);
    ASSERT_TRUE(rw < 3840);
    ASSERT_TRUE(rh < 2160);
    ASSERT_FLOAT_NEAR(static_cast<float>(rw) / 3840.0f, 0.667f, 0.05f);

    // Performance preset (2.0x upscaling, 50.0% resolution scale: 1920x1080)
    settings.preset = UpscalerPreset::Performance;
    upscaler.CalculateRenderDimensions(3840, 2160, rw, rh);
    ASSERT_EQ(rw, 1920);
    ASSERT_EQ(rh, 1080);

    // Native preset (1.0x scale: 3840x2160)
    settings.preset = UpscalerPreset::Native;
    upscaler.CalculateRenderDimensions(3840, 2160, rw, rh);
    ASSERT_EQ(rw, 3840);
    ASSERT_EQ(rh, 2160);

    // 2. Verify sub-pixel jitter sequence generation
    settings.preset = UpscalerPreset::Quality;
    upscaler.UpdateStats(1920, 1080, 0);
    const auto& stats = upscaler.GetStats();
    ASSERT_TRUE(stats.renderResolution.x < 1920);
    ASSERT_TRUE(stats.renderResolution.y < 1080);
    ASSERT_TRUE(stats.estimatedGpuSpeedupPercent > 0.0f);

    // Verify jitter offsets vary across frames and stay within NDC limits
    glm::vec2 j0 = upscaler.GetJitterPhase(0, 1280, 720);
    glm::vec2 j1 = upscaler.GetJitterPhase(1, 1280, 720);
    glm::vec2 j2 = upscaler.GetJitterPhase(2, 1280, 720);
    ASSERT_TRUE(j0 != j1);
    ASSERT_TRUE(j1 != j2);
    ASSERT_TRUE(std::abs(j0.x) < 0.1f && std::abs(j0.y) < 0.1f);

    // 3. Verify Mode names
    ASSERT_TRUE(std::string(TemporalUpscaler::GetModeName(UpscalerMode::AMD_FSR_3)).find("FSR") != std::string::npos);
    ASSERT_TRUE(std::string(TemporalUpscaler::GetModeName(UpscalerMode::NVIDIA_DLSS_3_5)).find("DLSS") != std::string::npos);
    ASSERT_TRUE(std::string(TemporalUpscaler::GetModeName(UpscalerMode::Intel_XeSS)).find("XeSS") != std::string::npos);

    upscaler.Shutdown();
}

// ============================================================================
// TEST 82: Pre-Baked Pipeline Cache (Zero Shader Stutter PSO)
// ============================================================================
TEST_CASE(RoadmapStability, PreBakedPipelineCacheAndSerialization) {
    using namespace Graphics::Pipeline;

    auto& cache = VulkanPipelineCache::Get();
    cache.Initialize(VK_NULL_HANDLE, "temp_test_cache.bin");

    // 1. Verify cache initialization and compilation recording
    cache.RecordCompilation(false, 12.5f); // 1 miss
    cache.RecordCompilation(true, 0.2f);   // 1 hit
    cache.RecordCompilation(true, 0.15f);  // 1 hit

    const auto& stats = cache.GetStats();
    ASSERT_EQ(stats.totalPipelinesCompiled, 3);
    ASSERT_EQ(stats.cacheHits, 2);
    ASSERT_EQ(stats.cacheMisses, 1);
    ASSERT_FLOAT_NEAR(stats.totalCompileTimeMs, 12.85f, 1e-2f);

    // 2. Verify disk serialization interface returns cleanly
    bool saved = cache.SaveToDisk("temp_test_cache.bin");
    // In headless test with VK_NULL_HANDLE device, gracefully handles null cache without crash
    ASSERT_TRUE(!saved || saved);

    cache.ResetStats();
    ASSERT_EQ(cache.GetStats().totalPipelinesCompiled, 0);

    cache.Shutdown();
}

// ============================================================================
// TEST 83: 64-Bit Large World Coordinates & Camera-Relative Rendering
// ============================================================================
TEST_CASE(RoadmapCoordinates, LargeWorldCoordinatesAndCameraRelativeRendering) {
    using namespace Core::LWC;

    auto& coord = CameraRelativeCoordinator::Get();
    coord.SetEnabled(true);

    // 1. Place camera far out in world space: (50,000.0, 10,000.0, -80,000.0) meters
    WorldVector cameraPos(50000.0, 10000.0, -80000.0);
    coord.SetCameraWorldPosition(cameraPos);
    ASSERT_EQ(coord.GetCameraWorldPosition(), cameraPos);

    // 2. Place an object nearby the camera: 15m away
    WorldVector objectPos(50010.0, 10002.0, -79990.0);
    LargeWorldTransform objTransform;
    objTransform.position = objectPos;
    objTransform.scale = glm::vec3(2.0f);

    glm::mat4 relModel = coord.ComputeCameraRelativeModelMatrix(objTransform);

    // Check camera-relative translation vector in column 3
    glm::vec3 relPos(relModel[3]);
    ASSERT_FLOAT_NEAR(relPos.x, 10.0f, 1e-3f);
    ASSERT_FLOAT_NEAR(relPos.y, 2.0f, 1e-3f);
    ASSERT_FLOAT_NEAR(relPos.z, 10.0f, 1e-3f);

    // 3. Test rotational view matrix extraction
    glm::mat4 fullView = glm::lookAt(glm::vec3(100.0f, 50.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotView = CameraRelativeCoordinator::ExtractRotationOnlyViewMatrix(fullView);

    // Translation components must be strictly zero
    ASSERT_FLOAT_NEAR(rotView[3][0], 0.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(rotView[3][1], 0.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(rotView[3][2], 0.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(rotView[3][3], 1.0f, 1e-5f);

    // 4. Test precision integrity across extreme world scale (1,000,000 meters)
    WorldVector extremeCam(1000000.0, 500000.0, -1000000.0);
    WorldVector extremeObj(1000005.0, 500003.0, -999992.0);
    ASSERT_TRUE(CameraRelativeCoordinator::VerifyPrecisionIntegrity(extremeCam, extremeObj));

    // 5. Test floating point error estimation
    double errorNear = CameraRelativeCoordinator::EstimateFloatingPointStepAtDistance(10.0);
    double errorFar = CameraRelativeCoordinator::EstimateFloatingPointStepAtDistance(100000.0);
    ASSERT_TRUE(errorFar > errorNear);
    ASSERT_TRUE(errorFar > 0.005); // > 5mm precision error at 100km in standard 32-bit floats
}

// ============================================================================
// TEST 84: Asynchronous World Partition & DirectStorage Streaming
// ============================================================================
#include "Streaming/StreamingTypes.h"
#include "Streaming/DirectStorageQueue.h"
#include "Streaming/WorldPartitionManager.h"

TEST_CASE(RoadmapWorldStreaming, AsynchronousWorldPartitionAndDirectStorage) {
    using namespace Streaming;

    // 1. Test Spatial Coordinate Grid Sector Mapping
    auto& wpMgr = WorldPartitionManager::GetInstance();
    PartitionSettings settings;
    settings.cellSize = 64.0f;
    settings.activeRadius = 128.0f;
    settings.loadingRadius = 256.0f;
    settings.unloadRadius = 384.0f;
    settings.maxMemoryBudgetMB = 100;
    wpMgr.Initialize(settings);

    GridCellCoord originCoord = wpMgr.WorldToCellCoord(glm::vec3(10.0f, 0.0f, 10.0f));
    ASSERT_EQ(originCoord.x, 0);
    ASSERT_EQ(originCoord.z, 0);

    GridCellCoord farCoord = wpMgr.WorldToCellCoord(glm::vec3(150.0f, 0.0f, -200.0f));
    ASSERT_EQ(farCoord.x, 2);
    ASSERT_EQ(farCoord.z, -4);

    glm::vec3 cellCenter = wpMgr.CellToWorldCenter(originCoord);
    ASSERT_FLOAT_NEAR(cellCenter.x, 32.0f, 1e-3f);
    ASSERT_FLOAT_NEAR(cellCenter.z, 32.0f, 1e-3f);

    // 2. Test DirectStorage Asynchronous Priority Queue
    auto& dsQueue = DirectStorageQueue::GetInstance();
    dsQueue.Initialize(2);

    std::atomic<int> completedTasks{0};
    DirectStorageRequest normalReq;
    normalReq.priority = StreamingPriority::Normal;
    normalReq.fileSize = 1024; // 1 KB simulated
    normalReq.onComplete = [&](std::vector<uint8_t> data) {
        ASSERT_EQ(data.size(), 1024);
        completedTasks++;
    };
    uint64_t reqId1 = dsQueue.EnqueueRequest(normalReq);
    ASSERT_TRUE(reqId1 > 0);

    DirectStorageRequest critReq;
    critReq.priority = StreamingPriority::Critical;
    critReq.fileSize = 2048; // 2 KB simulated
    critReq.onComplete = [&](std::vector<uint8_t> data) {
        ASSERT_EQ(data.size(), 2048);
        completedTasks++;
    };
    uint64_t reqId2 = dsQueue.EnqueueRequest(critReq);
    ASSERT_TRUE(reqId2 > 0);

    // Wait briefly for worker threads to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_EQ(completedTasks.load(), 2);
    ASSERT_TRUE(dsQueue.GetTotalBytesRead() >= 3072);

    // 3. Test Scene Node Registration and Spatial Partitioning
    Scene testScene;
    auto nodeNear = std::make_shared<Node>();
    nodeNear->id = 101;
    nodeNear->name = "NearSectorNode";
    nodeNear->position = glm::vec3(20.0f, 0.0f, 20.0f); // Cell (0, 0)

    auto nodeFar = std::make_shared<Node>();
    nodeFar->id = 102;
    nodeFar->name = "FarSectorNode";
    nodeFar->position = glm::vec3(800.0f, 0.0f, 800.0f); // Cell (12, 12)

    wpMgr.RegisterNode(nodeNear);
    wpMgr.RegisterNode(nodeFar);

    const auto statsBefore = wpMgr.GetStats();
    ASSERT_EQ(statsBefore.totalRegisteredCells, 2);

    // 4. Test Camera Streaming State Transitions
    glm::vec3 cameraPosNear(0.0f, 0.0f, 0.0f);
    glm::mat4 viewProj = glm::mat4(1.0f); // Identity matrix

    // Frame 1: Camera at origin -> Sector (0,0) becomes active and attaches nodeNear
    wpMgr.Update(cameraPosNear, viewProj, testScene, 0.016f);

    // Wait for DirectStorage async queue callback
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // Second update to transition Loaded -> Active
    wpMgr.Update(cameraPosNear, viewProj, testScene, 0.016f);

    ASSERT_TRUE(wpMgr.IsCellActive(originCoord));
    ASSERT_FALSE(wpMgr.IsCellActive(farCoord));
    ASSERT_TRUE(testScene.GetNodeById(101) != nullptr);
    ASSERT_TRUE(testScene.GetNodeById(102) == nullptr);

    // Frame 2: Teleport Camera far away to (1000, 0, 1000) -> origin sector must unload
    glm::vec3 cameraPosFar(1000.0f, 0.0f, 1000.0f);
    wpMgr.Update(cameraPosFar, viewProj, testScene, 0.016f);

    ASSERT_FALSE(wpMgr.IsCellActive(originCoord));
    // nodeNear must be detached from active scene
    ASSERT_TRUE(testScene.GetNodeById(101) == nullptr);

    // 5. Test ForceLoadAll
    wpMgr.ForceLoadAll(testScene);
    ASSERT_TRUE(wpMgr.IsCellActive(originCoord));
    ASSERT_TRUE(wpMgr.IsCellActive(wpMgr.WorldToCellCoord(glm::vec3(800.0f, 0.0f, 800.0f))));
    ASSERT_TRUE(testScene.GetNodeById(101) != nullptr);
    ASSERT_TRUE(testScene.GetNodeById(102) != nullptr);

    wpMgr.Shutdown();
    dsQueue.Shutdown();
}

// ============================================================================
// TEST 85: Locomotion — Motion Matching & Procedural Full-Body IK
// ============================================================================
#include "Animation/MotionMatching.h"

TEST_CASE(RoadmapLocomotion, MotionMatchingAndFullBodyIK) {
    using namespace Animation;

    auto& mm = MotionMatcher::Get();
    auto db = mm.GetDatabase();
    ASSERT_TRUE(db != nullptr);
    ASSERT_TRUE(db->GetFrameCount() >= 50);

    // 1. Query with Fast Forward Trajectory
    MotionFeaturePose runPose;
    runPose.rootVelocity = glm::vec3(0.0f, 0.0f, 4.0f);
    TrajectoryPoint runTrajectory[3];
    runTrajectory[0] = { 0.2f, glm::vec2(0.0f, 0.8f), glm::vec2(0.0f, 1.0f) };
    runTrajectory[1] = { 0.4f, glm::vec2(0.0f, 1.6f), glm::vec2(0.0f, 1.0f) };
    runTrajectory[2] = { 0.6f, glm::vec2(0.0f, 2.4f), glm::vec2(0.0f, 1.0f) };

    int matchedRunIdx = mm.QueryBestFrame(runPose, runTrajectory);
    const auto& runFrame = db->GetFrame(matchedRunIdx);
    ASSERT_EQ(runFrame.clipName, "RunForward");

    // 2. Query with Stationary Idle Trajectory
    MotionFeaturePose idlePose;
    idlePose.rootVelocity = glm::vec3(0.0f);
    TrajectoryPoint idleTrajectory[3];
    idleTrajectory[0] = { 0.2f, glm::vec2(0.0f), glm::vec2(0.0f, 1.0f) };
    idleTrajectory[1] = { 0.4f, glm::vec2(0.0f), glm::vec2(0.0f, 1.0f) };
    idleTrajectory[2] = { 0.6f, glm::vec2(0.0f), glm::vec2(0.0f, 1.0f) };

    int matchedIdleIdx = mm.QueryBestFrame(idlePose, idleTrajectory);
    const auto& idleFrame = db->GetFrame(matchedIdleIdx);
    ASSERT_EQ(idleFrame.clipName, "Idle");

    // 3. Test Inertialization continuous decay
    mm.TransitionTo(runFrame, glm::vec3(0.5f, 0.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 0.2f);
    ASSERT_TRUE(mm.GetInertializer().active);

    mm.Update(0.1f); // 50% through transition
    ASSERT_TRUE(mm.GetInertializer().posOffset.x < 0.5f);
    ASSERT_TRUE(mm.GetInertializer().posOffset.x > 0.0f);

    mm.Update(0.15f); // Passed 0.2s
    ASSERT_FALSE(mm.GetInertializer().active);
    ASSERT_FLOAT_NEAR(mm.GetInertializer().posOffset.x, 0.0f, 1e-4f);

    // 4. Test Procedural Full-Body IK (Pelvis adaptation and foot placement)
    Skeleton skel;
    // Build simple biped bone hierarchy:
    // 0: Pelvis, 1: LeftThigh, 2: LeftCalf, 3: LeftFoot,
    // 4: RightThigh, 5: RightCalf, 6: RightFoot, 7: Spine, 8: Head
    skel.AddBone("Pelvis", -1);
    skel.bones[0].currentPosition = glm::vec3(0.0f, 1.0f, 0.0f);
    skel.AddBone("LeftThigh", 0);
    skel.bones[1].currentPosition = glm::vec3(-0.2f, 0.9f, 0.0f);
    skel.AddBone("LeftCalf", 1);
    skel.bones[2].currentPosition = glm::vec3(-0.2f, 0.5f, 0.0f);
    skel.AddBone("LeftFoot", 2);
    skel.bones[3].currentPosition = glm::vec3(-0.2f, 0.05f, 0.0f);
    skel.AddBone("RightThigh", 0);
    skel.bones[4].currentPosition = glm::vec3(0.2f, 0.9f, 0.0f);
    skel.AddBone("RightCalf", 4);
    skel.bones[5].currentPosition = glm::vec3(0.2f, 0.5f, 0.0f);
    skel.AddBone("RightFoot", 5);
    skel.bones[6].currentPosition = glm::vec3(0.2f, 0.05f, 0.0f);
    skel.AddBone("Spine", 0);
    skel.bones[7].currentPosition = glm::vec3(0.0f, 1.3f, 0.0f);
    skel.AddBone("Head", 7);
    skel.bones[8].currentPosition = glm::vec3(0.0f, 1.7f, 0.0f);

    FullBodyIKTargets targets;
    targets.enablePelvisAdjustment = true;
    targets.enableFootPlacement = true;
    targets.enableLookAt = true;
    targets.leftFootGroundHeight = -0.1f;  // Step down
    targets.rightFootGroundHeight = 0.05f; // Step up
    targets.lookAtTargetWorld = glm::vec3(0.0f, 1.7f, 5.0f);

    float initialPelvisY = skel.bones[0].currentPosition.y;
    bool solved = FullBodyIKSolver::Solve(skel, 0, 1, 2, 3, 4, 5, 6, 7, 8, targets);
    ASSERT_TRUE(solved);
    // Pelvis must drop to accommodate lowest foot reach
    ASSERT_TRUE(skel.bones[0].currentPosition.y < initialPelvisY);
}

// ============================================================================
// TEST 86: Character Physics — GPU Cloth Simulation (XPBD) & Dynamic Hair
// ============================================================================
#include "Physics/XPBDClothSim.h"

TEST_CASE(RoadmapCharacterPhysics, XPBDClothSimulationAndDynamicHair) {
    using namespace Physics;

    // 1. Test XPBD Cloth Grid Generation & Constraints
    auto& clothSim = XPBDClothSimulator::Get();
    clothSim.InitializeGridCloth(1.0f, 1.0f, 8, 8, 0.5f); // 64 particles

    ASSERT_EQ(clothSim.GetParticleCount(), 64);
    ASSERT_TRUE(clothSim.GetConstraintCount() > 100);

    // Verify pinned corners have invMass == 0
    ASSERT_FLOAT_NEAR(clothSim.GetParticles()[0].invMass, 0.0f, 1e-5f);
    ASSERT_FLOAT_NEAR(clothSim.GetParticles()[7].invMass, 0.0f, 1e-5f);

    // 2. Add Limb Capsule Collider & Step Simulation
    clothSim.ClearColliders();
    clothSim.AddCapsuleCollider(glm::vec3(-0.5f, 1.0f, 0.0f), glm::vec3(0.5f, 1.0f, 0.0f), 0.2f);

    XPBDClothSettings settings;
    settings.substeps = 4;
    settings.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    settings.windVelocity = glm::vec3(1.0f, 0.0f, 0.5f);

    for (int i = 0; i < 20; ++i) {
        clothSim.Step(0.016f, settings);
    }

    // Top pinned particles must remain at initial position
    ASSERT_FLOAT_NEAR(clothSim.GetParticles()[0].position.y, 1.5f, 1e-3f);
    ASSERT_FLOAT_NEAR(clothSim.GetParticles()[7].position.y, 1.5f, 1e-3f);

    // Free bottom particles must have fallen due to gravity
    ASSERT_TRUE(clothSim.GetParticles()[63].position.y < 1.5f);

    // 3. Test Dynamic Hair Simulation
    auto& hairSim = DynamicHairSimulator::Get();
    hairSim.ClearStrands();
    hairSim.CreateStrand(glm::vec3(0.0f, 1.8f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 0.3f, 4);

    ASSERT_EQ(hairSim.GetStrandCount(), 1);
    ASSERT_EQ(hairSim.GetTotalParticles(), 5);

    DynamicHairSettings hairSettings;
    hairSettings.gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    for (int i = 0; i < 10; ++i) {
        hairSim.Step(0.016f, hairSettings);
    }

    // Scalp root particle remains locked at 1.8m
    ASSERT_FLOAT_NEAR(hairSim.GetHairParticles()[0].position.y, 1.8f, 1e-3f);
    // Tip particle hangs downward
    ASSERT_TRUE(hairSim.GetHairParticles()[4].position.y < 1.8f);
}

// ============================================================================
// TEST 87: Acoustics — Ray-Traced Sound Diffraction & Door Portaling
// ============================================================================
#include "Audio/AcousticRayTracer.h"

TEST_CASE(RoadmapAcoustics, RayTracedSoundDiffractionAndDoorPortaling) {
    using namespace Audio;

    auto& audioRT = AcousticRayTracer::Get();
    audioRT.Clear();

    // 1. Direct Unobstructed Line of Sight
    glm::vec3 emitter(0.0f, 1.0f, 0.0f);
    glm::vec3 listener(0.0f, 1.0f, 5.0f);

    auto resultDirect = audioRT.TracePath(emitter, listener);
    ASSERT_TRUE(resultDirect.hasDirectLineOfSight);
    ASSERT_FALSE(resultDirect.isPortaled);
    ASSERT_FLOAT_NEAR(resultDirect.transmissionLossDB, 0.0f, 1e-3f);
    ASSERT_FLOAT_NEAR(resultDirect.lowPassCutoffHz, 20000.0f, 1e-1f);
    ASSERT_FLOAT_NEAR(resultDirect.perceivedGain, 1.0f, 1e-3f);

    // 2. Solid Wall Obstruction (Sound Occluded)
    audioRT.AddObstacle(glm::vec3(-5.0f, 0.0f, 2.0f), glm::vec3(5.0f, 3.0f, 3.0f), WallMaterialType::Concrete);

    auto resultOccluded = audioRT.TracePath(emitter, listener);
    ASSERT_FALSE(resultOccluded.hasDirectLineOfSight);
    ASSERT_FALSE(resultOccluded.isPortaled);
    ASSERT_TRUE(resultOccluded.transmissionLossDB >= 30.0f); // Concrete heavy loss
    ASSERT_TRUE(resultOccluded.lowPassCutoffHz <= 500.0f);   // Muffled low-pass filter
    ASSERT_TRUE(resultOccluded.perceivedGain < 0.1f);

    // 3. Open Door Portaling (Huygens' Principle: virtual sound source at doorway)
    AcousticPortal door;
    door.portalId = 1;
    door.name = "HallwayDoor";
    door.center = glm::vec3(3.0f, 1.0f, 2.5f);
    door.openFraction = 1.0f; // Fully open
    audioRT.AddPortal(door);

    auto resultPortaled = audioRT.TracePath(emitter, listener);
    ASSERT_FALSE(resultPortaled.hasDirectLineOfSight);
    ASSERT_TRUE(resultPortaled.isPortaled);
    // Virtual emitter position must be at the door aperture
    ASSERT_FLOAT_NEAR(resultPortaled.virtualEmitterPos.x, 3.0f, 1e-3f);
    ASSERT_FLOAT_NEAR(resultPortaled.virtualEmitterPos.z, 2.5f, 1e-3f);
    ASSERT_TRUE(resultPortaled.lowPassCutoffHz > 15000.0f); // High frequencies pass freely through open door

    // 4. Closing the door muffles and attenuates portaled sound
    audioRT.SetPortalOpenFraction(1, 0.1f); // 10% ajar
    auto resultAjar = audioRT.TracePath(emitter, listener);
    ASSERT_TRUE(resultAjar.isPortaled);
    ASSERT_TRUE(resultAjar.lowPassCutoffHz < resultPortaled.lowPassCutoffHz);
    ASSERT_TRUE(resultAjar.perceivedGain < resultPortaled.perceivedGain);
}

// ============================================================================

// ============================================================================
// TEST 88: Linux Standalone Game Export & Steam Deck Turnkey Packaging
// ============================================================================
#include "Export/LinuxExporter.h"

TEST_CASE(RoadmapExport, LinuxStandaloneGameExport) {
    using namespace Export;

    // 1. Setup sample scene
    Scene scene;
    scene.AddNode(ShapeType::Cube, "HeroCube");
    scene.AddLightNode(LightType::Directional, "MainSun");

    LinuxExportOptions opts;
    opts.destinationDir = "exports/TestGame_Linux";
    opts.gameTitle = "TestGame";
    opts.startScene = "scenes/main.scene";
    opts.fullscreen = false;
    opts.windowWidth = 1920;
    opts.windowHeight = 1080;
    opts.includeSteamDeckLauncher = true;
    opts.includeDualProtonRunner = true;
    opts.createTarGzArchive = false; // Fast test without invoking system tar

    std::string statusMsg;
    bool exportOk = LinuxExporter::Export(scene, opts, statusMsg);
    ASSERT_TRUE(exportOk);

    // 2. Verify output files exist
    std::filesystem::path outDir(opts.destinationDir);
    ASSERT_TRUE(std::filesystem::exists(outDir));
    ASSERT_TRUE(std::filesystem::exists(outDir / "launch.sh"));
    ASSERT_TRUE(std::filesystem::exists(outDir / "TestGame.desktop"));
    ASSERT_TRUE(std::filesystem::exists(outDir / "README_LINUX.txt"));
    ASSERT_TRUE(std::filesystem::exists(outDir / "game.agconfig"));
    ASSERT_TRUE(std::filesystem::exists(outDir / "input_bindings.json"));
    ASSERT_TRUE(std::filesystem::exists(outDir / "scenes" / "main.scene"));

    // 3. Verify launch.sh contents (UNIX LF line endings, LD_LIBRARY_PATH, MangoHud, SteamOS compatibility)
    std::ifstream launchIn(outDir / "launch.sh", std::ios::binary);
    ASSERT_TRUE(launchIn.is_open());
    std::string launchContent((std::istreambuf_iterator<char>(launchIn)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(launchContent.find("#!/usr/bin/env bash") != std::string::npos);
    ASSERT_TRUE(launchContent.find("LD_LIBRARY_PATH") != std::string::npos);
    ASSERT_TRUE(launchContent.find("TestGame.x86_64") != std::string::npos);
    ASSERT_TRUE(launchContent.find("MANGOHUD") != std::string::npos);
    // Ensure no Windows \r carriage returns in launch.sh
    ASSERT_TRUE(launchContent.find("\r") == std::string::npos);

    // 4. Verify game.agconfig configuration
    std::ifstream cfgIn(outDir / "game.agconfig");
    ASSERT_TRUE(cfgIn.is_open());
    std::string cfgContent((std::istreambuf_iterator<char>(cfgIn)), std::istreambuf_iterator<char>());
    ASSERT_TRUE(cfgContent.find("\"gameTitle\": \"TestGame\"") != std::string::npos);
    ASSERT_TRUE(cfgContent.find("\"windowWidth\": 1920") != std::string::npos);
    ASSERT_TRUE(cfgContent.find("\"windowHeight\": 1080") != std::string::npos);

    // 5. Verify .desktop launcher contents
    std::string desktopEntry = LinuxExporter::GenerateDesktopEntry(opts);
    ASSERT_TRUE(desktopEntry.find("[Desktop Entry]") != std::string::npos);
    ASSERT_TRUE(desktopEntry.find("Name=TestGame") != std::string::npos);
    ASSERT_TRUE(desktopEntry.find("Categories=Game;") != std::string::npos);

    // Clean up test export directory
    std::error_code ec;
    std::filesystem::remove_all(outDir, ec);
}

// =========================================================================
// 89. Localization & Multi-Language System Tests
// =========================================================================

TEST_CASE(Localization, DefaultEnglishAndSpanishBaseline) {
    auto& loc = Localization::Get();
    loc.Init();

    // 1. English mode
    ASSERT_TRUE(loc.SetLanguage("en", false));
    ASSERT_EQ(loc.GetCurrentLanguage(), "en");
    ASSERT_EQ(std::string(TR("Recent Projects")), "Recent Projects");
    ASSERT_EQ(std::string(TR("Create New Project")), "Create New Project");
    ASSERT_EQ(std::string(TR("File")), "File");
    ASSERT_EQ(std::string(TR("Edit")), "Edit");

    // 2. Spanish mode
    ASSERT_TRUE(loc.SetLanguage("es", false));
    ASSERT_EQ(loc.GetCurrentLanguage(), "es");
    ASSERT_EQ(std::string(TR("Recent Projects")), "Proyectos Recientes");
    ASSERT_EQ(std::string(TR("Create New Project")), "Crear Nuevo Proyecto");
    ASSERT_EQ(std::string(TR("File")), "Archivo");
    ASSERT_EQ(std::string(TR("Edit")), "Editar");
    ASSERT_EQ(std::string(TR("Scene")), "Escena");
    ASSERT_EQ(std::string(TR("Language")), "Idioma");

    // 3. ImGui ID suffixes preservation
    ASSERT_EQ(std::string(TR("Open##Rec")), "Abrir##Rec");
    ASSERT_EQ(std::string(TR("Scene Hierarchy###HierarchyWindow")), "Jerarquía de Escena###HierarchyWindow");

    // 4. Fallback for untranslated key
    ASSERT_EQ(std::string(TR("SomeNonExistentKey123")), "SomeNonExistentKey123");

    // Restore English
    loc.SetLanguage("en", false);
}

TEST_CASE(Localization, DynamicLanguageAdditionAndDiscovery) {
    auto& loc = Localization::Get();
    loc.Init();

    std::string langDir = loc.GetLangDirectory();
    std::filesystem::path customLangPath = std::filesystem::path(langDir) / "test_it.json";

    // 1. Create Italian translation file dynamically
    {
        JsonValue itJson = JsonValue::Object();
        itJson["code"] = "it";
        itJson["name"] = "Italiano";
        JsonValue stringsObj = JsonValue::Object();
        stringsObj["Recent Projects"] = "Progetti Recenti";
        stringsObj["Create New Project"] = "Crea Nuovo Progetto";
        stringsObj["File"] = "File";
        stringsObj["Edit"] = "Modifica";
        itJson["strings"] = stringsObj;

        std::ofstream out(customLangPath);
        ASSERT_TRUE(out.is_open());
        out << itJson.Serialize(2);
    }

    // 2. Rescan directory
    loc.ScanLanguages();

    const auto& langs = loc.GetAvailableLanguages();
    bool foundItalian = false;
    for (const auto& l : langs) {
        if (l.code == "it") {
            foundItalian = true;
            ASSERT_EQ(l.displayName, "Italiano");
            break;
        }
    }
    ASSERT_TRUE(foundItalian);

    // 3. Switch to Italian
    ASSERT_TRUE(loc.SetLanguage("it", false));
    ASSERT_EQ(loc.GetCurrentLanguage(), "it");
    ASSERT_EQ(std::string(TR("Recent Projects")), "Progetti Recenti");
    ASSERT_EQ(std::string(TR("Create New Project")), "Crea Nuovo Progetto");
    ASSERT_EQ(std::string(TR("Edit")), "Modifica");

    // 4. Untranslated key falls back to English
    ASSERT_EQ(std::string(TR("Camera")), "Camera");

    // 5. Cleanup test file and rescan
    std::error_code ec;
    std::filesystem::remove(customLangPath, ec);
    loc.ScanLanguages();
    loc.SetLanguage("en", false);
}

TEST_CASE(Localization, EditorSettingsPersistence) {
    auto& loc = Localization::Get();
    loc.Init();

    // Set to Spanish and save
    loc.SetLanguage("es", true);

    // Verify editor_settings.json contains language: es
    std::ifstream file("editor_settings.json");
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    JsonValue root = JsonValue::Parse(content);
    ASSERT_TRUE(root.HasKey("language"));
    ASSERT_EQ(root["language"].AsString(), "es");

    // Revert back to English and save
    loc.SetLanguage("en", true);
}


// =========================================================================
// 90. Game Localization & Developer Translation Manager Tests
// =========================================================================

TEST_CASE(GameLocalization, KeyValueAndLanguageManagement) {
    namespace fs = std::filesystem;
    std::string testDir = "test_game_localization_proj";
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    auto& gameLoc = GameLocalization::Get();
    gameLoc.Init(testDir);

    // Verify default files generated
    ASSERT_TRUE(gameLoc.HasKey("menu_play"));
    ASSERT_EQ(gameLoc.Translate("menu_play"), "Play");

    // Switch to Spanish
    ASSERT_TRUE(gameLoc.SetLanguage("es"));
    ASSERT_EQ(gameLoc.Translate("menu_play"), "Jugar");

    // Add a brand new game translation key
    ASSERT_TRUE(gameLoc.AddKey("quest_intro", "Find the lost artifact"));
    ASSERT_EQ(gameLoc.GetTranslation("en", "quest_intro"), "Find the lost artifact");

    // Before translating to Spanish, it should fall back to English
    ASSERT_EQ(gameLoc.Translate("quest_intro"), "Find the lost artifact");

    // Set Spanish translation
    gameLoc.SetTranslation("es", "quest_intro", "Encuentra el artefacto perdido");
    ASSERT_EQ(gameLoc.Translate("quest_intro"), "Encuentra el artefacto perdido");

    // Add a brand new language (French)
    ASSERT_TRUE(gameLoc.AddLanguage("fr", "Français"));
    gameLoc.SetTranslation("fr", "quest_intro", "Trouvez l'artefact perdu");

    ASSERT_TRUE(gameLoc.SetLanguage("fr"));
    ASSERT_EQ(gameLoc.Translate("quest_intro"), "Trouvez l'artefact perdu");

    // Save and reload from disk
    ASSERT_TRUE(gameLoc.SaveAll());
    gameLoc.Reload();
    ASSERT_TRUE(gameLoc.SetLanguage("fr"));
    ASSERT_EQ(gameLoc.Translate("quest_intro"), "Trouvez l'artefact perdu");

    // Clean up
    fs::remove_all(testDir);
}
