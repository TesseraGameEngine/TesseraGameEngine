#include "Core/InputManager.h"
#include "Project.h"
#include "Utils/SimpleJson.h"
#include "Core/Version.h"
#include "UI/EditorUI.h"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

std::string Project::GetEditorSettingsFilePath() {
    return "editor_settings.json";
}

std::vector<std::string> Project::GetRecentProjects() {
    std::vector<std::string> recents;
    std::string path = GetEditorSettingsFilePath();
    if (!fs::exists(path)) return recents;

    std::ifstream file(path);
    if (!file.is_open()) return recents;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    JsonValue root = JsonValue::Parse(content);
    if (root.HasKey("recentProjects") && root["recentProjects"].IsArray()) {
        const auto& arr = root["recentProjects"];
        for (size_t i = 0; i < arr.arrVal.size(); ++i) {
            std::string p = arr[i].AsString();
            if (!p.empty() && fs::exists(p)) {
                recents.push_back(p);
            }
        }
    }
    return recents;
}

void Project::AddRecentProject(const std::string& path) {
    if (path.empty()) return;
    auto recents = GetRecentProjects();

    // Remove if already in list
    std::vector<std::string> updated;
    updated.push_back(path);
    for (const auto& r : recents) {
        if (r != path && updated.size() < 5) {
            updated.push_back(r);
        }
    }

    JsonValue root = JsonValue::Object();
    std::string settingsPath = GetEditorSettingsFilePath();
    if (fs::exists(settingsPath)) {
        std::ifstream inFile(settingsPath);
        if (inFile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            inFile.close();
            root = JsonValue::Parse(content);
            if (!root.IsObject()) root = JsonValue::Object();
        }
    }
    JsonValue arr = JsonValue::Array();
    for (const auto& r : updated) {
        arr.Push(r);
    }
    root["recentProjects"] = arr;

    std::ofstream file(GetEditorSettingsFilePath());
    if (file.is_open()) {
        file << root.Serialize(2);
        file.close();
    }
}

std::shared_ptr<Project> Project::CreateNew(const std::string& parentDirectory, const std::string& projectName, Scene& scene) {
    if (parentDirectory.empty() || projectName.empty()) return nullptr;

    fs::path projectDir = fs::path(parentDirectory) / projectName;
    try {
        fs::create_directories(projectDir / "assets" / "models");
        fs::create_directories(projectDir / "assets" / "textures");
        fs::create_directories(projectDir / "assets" / "materials");
        fs::create_directories(projectDir / "scenes");
    } catch (const std::exception& e) {
        std::cerr << "[Project] Failed to create directories: " << e.what() << std::endl;
        return nullptr;
    }

    auto project = std::make_shared<Project>();
    project->m_projectName = projectName;
    project->m_rootPath = projectDir.string();
    project->m_filePath = (projectDir / (projectName + ".tess")).string();
    project->m_activeSceneFile = "main.scene";

    // Reset scene to a clean default state and save initial main.scene
    scene.ResetToDefault();
    scene.SetName("Main Scene");
    std::string scenePath = (projectDir / "scenes" / "main.scene").string();
    scene.SetFilePath(scenePath);
    scene.SaveToFile(scenePath);

    // Save initial .tess project descriptor file
    JsonValue root = JsonValue::Object();
    root["formatVersion"] = 2;
    root["projectName"] = projectName;
    root["engineVersion"] = Tessera::Version::String;
    root["activeScene"] = "main.scene";
    JsonValue scenesArr = JsonValue::Array();
    scenesArr.Push("main.scene");
    root["scenes"] = scenesArr;

    std::ofstream file(project->m_filePath);
    if (file.is_open()) {
        file << root.Serialize(2);
        file.close();
    }

    AddRecentProject(project->m_filePath);
    std::cout << "[Project] Created new project: " << project->m_filePath << std::endl;
    return project;
}

std::vector<std::string> Project::GetAvailableScenes() const {
    std::vector<std::string> sceneFiles;
    if (m_rootPath.empty()) return sceneFiles;

    fs::path scenesDir = fs::path(m_rootPath) / "scenes";
    if (!fs::exists(scenesDir)) {
        try { fs::create_directories(scenesDir); } catch (...) {}
        return sceneFiles;
    }

    for (const auto& entry : fs::directory_iterator(scenesDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".scene") {
            sceneFiles.push_back(entry.path().filename().string());
        }
    }

    std::sort(sceneFiles.begin(), sceneFiles.end());
    return sceneFiles;
}

bool Project::CreateNewScene(const std::string& sceneName, Scene& scene) {
    if (m_rootPath.empty() || sceneName.empty()) return false;

    // Save current active scene before switching to a new scene
    SaveActiveScene(scene);

    std::string fileName = sceneName;
    if (fileName.length() < 6 || fileName.substr(fileName.length() - 6) != ".scene") {
        fileName += ".scene";
    }

    fs::path scenePath = fs::path(m_rootPath) / "scenes" / fileName;
    scene.ResetToDefault();
    scene.SetName(fs::path(fileName).stem().string());
    scene.SetFilePath(scenePath.string());

    bool ok = scene.SaveToFile(scenePath.string());
    if (ok) {
        m_activeSceneFile = fileName;
    }
    return ok;
}

bool Project::LoadScene(const std::string& sceneFileName, Scene& scene,
                        std::function<bool(const std::string&, const std::string&)> loadMeshCallback) {
    if (m_rootPath.empty() || sceneFileName.empty()) return false;

    fs::path scenePath = fs::path(m_rootPath) / "scenes" / sceneFileName;
    if (!fs::exists(scenePath)) {
        std::cerr << "[Project] Scene file not found: " << scenePath << std::endl;
        return false;
    }

    // Wrapped callback that prepends assets/models path if needed
    auto meshResolver = [this, loadMeshCallback](const std::string& meshName, const std::string& outKey) -> bool {
        if (!loadMeshCallback) return false;
        fs::path localMeshPath = fs::path(m_rootPath) / "assets" / "models" / meshName;
        if (fs::exists(localMeshPath)) {
            return loadMeshCallback(localMeshPath.string(), meshName);
        } else if (fs::exists(fs::path(m_rootPath) / meshName)) {
            // Project-relative path (e.g. assets/external/...)
            return loadMeshCallback((fs::path(m_rootPath) / meshName).string(), meshName);
        } else if (fs::exists(meshName)) {
            return loadMeshCallback(meshName, meshName);
        }
        return false;
    };

    bool ok = scene.LoadFromFile(scenePath.string(), meshResolver);
    if (ok) {
        m_activeSceneFile = sceneFileName;
        std::cout << "[Project] Loaded active scene: " << sceneFileName << std::endl;
    }
    return ok;
}

bool Project::SaveActiveScene(const Scene& scene) {
    if (m_rootPath.empty()) return false;

    if (m_activeSceneFile.empty()) {
        m_activeSceneFile = "main.scene";
    }

    fs::path scenePath = fs::path(m_rootPath) / "scenes" / m_activeSceneFile;
    return scene.SaveToFile(scenePath.string());
}

bool Project::SaveSceneAs(const std::string& newSceneName, Scene& scene) {
    if (m_rootPath.empty() || newSceneName.empty()) return false;

    std::string fileName = newSceneName;
    if (fileName.length() < 6 || fileName.substr(fileName.length() - 6) != ".scene") {
        fileName += ".scene";
    }

    fs::path scenePath = fs::path(m_rootPath) / "scenes" / fileName;
    scene.SetName(fs::path(fileName).stem().string());
    scene.SetFilePath(scenePath.string());

    bool ok = scene.SaveToFile(scenePath.string());
    if (ok) {
        m_activeSceneFile = fileName;
    }
    return ok;
}

bool Project::Save(const Scene& scene, const Camera& camera, const EditorUI& editorUI) {
    if (m_filePath.empty()) return false;

    // 1. Save current active scene to scenes/ directory
    SaveActiveScene(scene);

    // 2. Build .tess JSON
    JsonValue root = JsonValue::Object();
    root["formatVersion"] = 2;
    root["projectName"] = m_projectName;
    root["engineVersion"] = Tessera::Version::String;
    root["activeScene"] = m_activeSceneFile;

    // List of available scenes in project
    auto availableScenes = GetAvailableScenes();
    JsonValue scenesArr = JsonValue::Array();
    for (const auto& s : availableScenes) {
        scenesArr.Push(s);
    }
    root["scenes"] = scenesArr;

    // Editor state
    JsonValue editor = JsonValue::Object();
    JsonValue camObj = JsonValue::Object();
    JsonValue camTarget = JsonValue::Array();
    camTarget.Push(camera.GetTarget().x);
    camTarget.Push(camera.GetTarget().y);
    camTarget.Push(camera.GetTarget().z);
    camObj["target"] = camTarget;
    camObj["distance"] = camera.GetDistance();
    camObj["yaw"] = camera.GetYaw();
    camObj["pitch"] = camera.GetPitch();
    camObj["fov"] = camera.GetFov();
    editor["camera"] = camObj;

    JsonValue viewport = JsonValue::Object();
    viewport["showGrid"] = editorUI.IsGridEnabled();
    viewport["polygonMode"] = static_cast<int>(editorUI.GetPolygonMode());
    editor["viewport"] = viewport;

    auto activeCam = scene.GetActiveCamera();
    editor["activeCameraId"] = activeCam ? activeCam->id : 0;
    root["editor"] = editor;
    root["inputBindings"] = Core::InputManager::Get().Serialize();

    std::ofstream file(m_filePath);
    if (!file.is_open()) {
        std::cerr << "[Project] Failed to open for writing: " << m_filePath << std::endl;
        return false;
    }
    file << root.Serialize(2);
    file.close();

    AddRecentProject(m_filePath);
    std::cout << "[Project] Successfully saved project: " << m_filePath << std::endl;
    return true;
}

bool Project::SaveAs(const std::string& newParentDir, const std::string& newName,
                     const Scene& scene, const Camera& camera, const EditorUI& editorUI) {
    if (newParentDir.empty() || newName.empty()) return false;

    fs::path newDir = fs::path(newParentDir) / newName;
    try {
        fs::create_directories(newDir / "assets" / "models");
        fs::create_directories(newDir / "scenes");

        // Copy assets from old project if existing
        if (!m_rootPath.empty() && fs::exists(fs::path(m_rootPath) / "assets")) {
            fs::copy(fs::path(m_rootPath) / "assets", newDir / "assets",
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        // Copy scenes from old project if existing
        if (!m_rootPath.empty() && fs::exists(fs::path(m_rootPath) / "scenes")) {
            fs::copy(fs::path(m_rootPath) / "scenes", newDir / "scenes",
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
    } catch (const std::exception& e) {
        std::cerr << "[Project] SaveAs failed to create directory: " << e.what() << std::endl;
        return false;
    }

    m_projectName = newName;
    m_rootPath = newDir.string();
    m_filePath = (newDir / (newName + ".tess")).string();

    return Save(scene, camera, editorUI);
}

std::shared_ptr<Project> Project::Load(const std::string& projectFilePath,
                                      Scene& scene,
                                      Camera& camera,
                                      EditorUI& editorUI,
                                      std::function<bool(const std::string&, const std::string&)> loadMeshCallback) {
    std::ifstream file(projectFilePath);
    if (!file.is_open()) {
        std::cerr << "[Project] Failed to open project file: " << projectFilePath << std::endl;
        return nullptr;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    JsonValue root = JsonValue::Parse(content);
    if (!root.IsObject() || !root.HasKey("projectName")) {
        std::cerr << "[Project] Invalid project file format (.tess): " << projectFilePath << std::endl;
        return nullptr;
    }

    auto project = std::make_shared<Project>();
    project->m_projectName = root["projectName"].AsString("Project");
    project->m_filePath = projectFilePath;
    project->m_rootPath = fs::path(projectFilePath).parent_path().string();
    if (root.HasKey("inputBindings")) {
        Core::InputManager::Get().Deserialize(root["inputBindings"]);
    }

    // 1. Restore editor camera & settings
    if (root.HasKey("editor")) {
        const auto& editor = root["editor"];
        if (editor.HasKey("camera")) {
            const auto& cam = editor["camera"];
            glm::vec3 target(0.0f);
            if (cam.HasKey("target") && cam["target"].arrVal.size() >= 3) {
                target.x = cam["target"][0].AsFloat(0.0f);
                target.y = cam["target"][1].AsFloat(0.0f);
                target.z = cam["target"][2].AsFloat(0.0f);
            }
            float distance = cam["distance"].AsFloat(5.0f);
            float yaw = cam["yaw"].AsFloat(45.0f);
            float pitch = cam["pitch"].AsFloat(25.0f);
            camera.SetViewParams(yaw, pitch, distance, target);
        }

        if (editor.HasKey("viewport")) {
            const auto& vp = editor["viewport"];
            if (vp.HasKey("showGrid")) {
                editorUI.SetGridEnabled(vp["showGrid"].AsBool(true));
            }
            if (vp.HasKey("polygonMode")) {
                editorUI.SetPolygonMode(static_cast<PolygonMode>(vp["polygonMode"].AsInt(0)));
            }
        }
    }

    // 2. Load active scene from scenes/ directory (or migrate from legacy embedded format)
    bool sceneLoaded = false;
    std::string targetScene = root.HasKey("activeScene") ? root["activeScene"].AsString("main.scene") : "main.scene";

    fs::path targetScenePath = fs::path(project->m_rootPath) / "scenes" / targetScene;
    if (fs::exists(targetScenePath)) {
        sceneLoaded = project->LoadScene(targetScene, scene, loadMeshCallback);
    } else if (root.HasKey("scene") && root["scene"].HasKey("nodes")) {
        // Legacy format migration: deserialize from .tess and save to scenes/main.scene
        auto meshResolver = [project, loadMeshCallback](const std::string& meshName, const std::string& outKey) -> bool {
            if (!loadMeshCallback) return false;
            fs::path localMeshPath = fs::path(project->m_rootPath) / "assets" / "models" / meshName;
            if (fs::exists(localMeshPath)) {
                return loadMeshCallback(localMeshPath.string(), meshName);
            } else if (fs::exists(meshName)) {
                return loadMeshCallback(meshName, meshName);
            }
            return false;
        };

        scene.Deserialize(root["scene"], meshResolver);
        project->m_activeSceneFile = "main.scene";
        fs::create_directories(fs::path(project->m_rootPath) / "scenes");
        project->SaveActiveScene(scene);
        sceneLoaded = true;
    } else {
        // Auto-discover any .scene file in scenes/ directory
        auto scenes = project->GetAvailableScenes();
        if (!scenes.empty()) {
            sceneLoaded = project->LoadScene(scenes[0], scene, loadMeshCallback);
        } else {
            // Create default scene
            scene.ResetToDefault();
            project->m_activeSceneFile = "main.scene";
            project->SaveActiveScene(scene);
            sceneLoaded = true;
        }
    }

    AddRecentProject(projectFilePath);
    std::cout << "[Project] Successfully opened project: " << projectFilePath << " with scene: " << project->m_activeSceneFile << std::endl;
    return project;
}

std::string Project::ImportAsset(const std::string& sourceFilePath) {
    if (m_rootPath.empty() || sourceFilePath.empty()) return "";

    try {
        fs::path src(sourceFilePath);
        if (!fs::exists(src)) return "";

        fs::path destDir = fs::path(m_rootPath) / "assets" / "models";
        fs::create_directories(destDir);

        fs::path destFile = destDir / src.filename();
        fs::copy_file(src, destFile, fs::copy_options::overwrite_existing);

        // If a companion .fbm folder exists next to the model (standard FBX texture directory), import it too
        fs::path srcFbm = src.parent_path() / (src.stem().string() + ".fbm");
        if (fs::exists(srcFbm) && fs::is_directory(srcFbm)) {
            fs::path destFbm = destDir / (src.stem().string() + ".fbm");
            fs::create_directories(destFbm);
            fs::copy(srcFbm, destFbm, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            std::cout << "[Project] Companion textures folder imported: " << destFbm.string() << std::endl;
        }

        // If a textures/ folder exists next to the model, import it too
        fs::path srcTex = src.parent_path() / "textures";
        if (fs::exists(srcTex) && fs::is_directory(srcTex)) {
            fs::path destTex = destDir / "textures";
            fs::create_directories(destTex);
            fs::copy(srcTex, destTex, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            std::cout << "[Project] Companion textures folder imported: " << destTex.string() << std::endl;
        }

        std::cout << "[Project] Asset imported to: " << destFile.string() << std::endl;
        return src.filename().string();
    } catch (const std::exception& e) {
        std::cerr << "[Project] Failed to import asset: " << e.what() << std::endl;
        return "";
    }
}

std::string Project::ImportTexture(const std::string& sourceFilePath) {
    if (m_rootPath.empty() || sourceFilePath.empty()) return sourceFilePath;

    try {
        fs::path src(sourceFilePath);
        if (!fs::exists(src)) return sourceFilePath;

        // If the file is already inside the project directory, return it without duplicating
        std::string canonicalSrc = fs::weakly_canonical(src).lexically_normal().string();
        std::string canonicalRoot = fs::weakly_canonical(fs::path(m_rootPath)).lexically_normal().string();
        if (canonicalSrc.rfind(canonicalRoot, 0) == 0) {
            std::replace(canonicalSrc.begin(), canonicalSrc.end(), '\\', '/');
            return canonicalSrc;
        }

        fs::path destDir = fs::path(m_rootPath) / "assets" / "textures";
        fs::create_directories(destDir);

        fs::path destFile = destDir / src.filename();
        fs::copy_file(src, destFile, fs::copy_options::overwrite_existing);

        std::string destStr = fs::absolute(destFile).lexically_normal().string();
        std::replace(destStr.begin(), destStr.end(), '\\', '/');
        std::cout << "[Project] Texture imported to project: " << destStr << std::endl;
        return destStr;
    } catch (const std::exception& e) {
        std::cerr << "[Project] Failed to import texture: " << e.what() << std::endl;
        return sourceFilePath;
    }
}
