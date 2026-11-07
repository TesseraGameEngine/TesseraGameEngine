#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include "Scene/Scene.h"
#include "Core/Camera.h"

// Forward declaration
class EditorUI;

class Project {
public:
    Project() = default;
    ~Project() = default;

    // Create a new project directory, structure, and .tess file
    static std::shared_ptr<Project> CreateNew(const std::string& parentDirectory, const std::string& projectName, Scene& scene);

    // Load an existing .tess project file
    static std::shared_ptr<Project> Load(const std::string& projectFilePath,
                                         Scene& scene,
                                         Camera& camera,
                                         EditorUI& editorUI,
                                         std::function<bool(const std::string&, const std::string&)> loadMeshCallback);

    // Save project state to disk (including active scene)
    bool Save(const Scene& scene, const Camera& camera, const EditorUI& editorUI);

    // Save project as a new project in another directory
    bool SaveAs(const std::string& newParentDir, const std::string& newName,
                const Scene& scene, const Camera& camera, const EditorUI& editorUI);

    // Multi-Scene Management
    std::vector<std::string> GetAvailableScenes() const;
    const std::string& GetActiveSceneFile() const { return m_activeSceneFile; }
    void SetActiveSceneFile(const std::string& sceneFile) { m_activeSceneFile = sceneFile; }
    std::string GetScenesDirectory() const {
        return (std::filesystem::path(m_rootPath) / "scenes").string();
    }

    bool CreateNewScene(const std::string& sceneName, Scene& scene);
    bool LoadScene(const std::string& sceneFileName, Scene& scene,
                   std::function<bool(const std::string&, const std::string&)> loadMeshCallback = nullptr);
    bool SaveActiveScene(const Scene& scene);
    bool SaveSceneAs(const std::string& newSceneName, Scene& scene);

    // Copy an asset into this project's assets/models folder, returning the relative path
    std::string ImportAsset(const std::string& sourceFilePath);
    // Copy a texture image into this project's assets/textures folder, returning the copied path
    std::string ImportTexture(const std::string& sourceFilePath);

    // Accessors
    const std::string& GetName() const { return m_projectName; }
    const std::string& GetRootPath() const { return m_rootPath; }
    const std::string& GetFilePath() const { return m_filePath; }
    bool IsLoaded() const { return !m_filePath.empty(); }

    std::string GetAssetsDirectory() const {
        return (std::filesystem::path(m_rootPath) / "assets").string();
    }
    std::string GetModelsDirectory() const {
        return (std::filesystem::path(m_rootPath) / "assets" / "models").string();
    }
    std::string GetTexturesDirectory() const {
        return (std::filesystem::path(m_rootPath) / "assets" / "textures").string();
    }

    // Recent Projects tracking
    static std::vector<std::string> GetRecentProjects();
    static void AddRecentProject(const std::string& path);

private:
    std::string m_projectName{"Untitled"};
    std::string m_rootPath{""};
    std::string m_filePath{""};
    std::string m_activeSceneFile{"main.scene"};

    static std::string GetEditorSettingsFilePath();
};
