#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <filesystem>

struct GameLanguage {
    std::string code;         // e.g. "en", "es", "fr", "de", "ja"
    std::string displayName;  // e.g. "English", "Español", "Français"
    std::string filePath;     // Path to .json file
};

class GameLocalization {
public:
    static GameLocalization& Get();

    void Init(const std::string& projectRoot = "");
    void SetProjectRoot(const std::string& projectRoot);
    const std::string& GetProjectRoot() const { return m_projectRoot; }
    std::string GetLocalizationDirectory() const;

    // Language management
    void ScanLanguages();
    const std::vector<GameLanguage>& GetAvailableLanguages() const { return m_languages; }
    bool SetLanguage(const std::string& code);
    const std::string& GetCurrentLanguage() const { return m_currentLanguage; }
    std::string GetLanguageDisplayName(const std::string& code) const;

    bool AddLanguage(const std::string& code, const std::string& displayName);
    bool RemoveLanguage(const std::string& code);

    // Key management
    const std::set<std::string>& GetAllKeys() const { return m_allKeys; }
    bool AddKey(const std::string& key, const std::string& defaultEnglishVal = "");
    bool RemoveKey(const std::string& key);
    bool HasKey(const std::string& key) const;

    // Translation values
    void SetTranslation(const std::string& langCode, const std::string& key, const std::string& value);
    std::string GetTranslation(const std::string& langCode, const std::string& key) const;

    // In-game translation lookup (current language -> en -> defaultVal -> key)
    std::string Translate(const std::string& key, const std::string& defaultVal = "") const;

    // Persistence
    bool SaveLanguage(const std::string& code);
    bool SaveAll();
    bool Reload();

    // Stats
    int GetKeyCount() const { return static_cast<int>(m_allKeys.size()); }
    int GetTranslatedCount(const std::string& langCode) const;

private:
    GameLocalization() = default;
    ~GameLocalization() = default;

    std::string m_projectRoot;
    std::string m_currentLanguage{"en"};
    std::vector<GameLanguage> m_languages;
    std::set<std::string> m_allKeys;
    // m_translations[langCode][key] = translatedString
    std::map<std::string, std::map<std::string, std::string>> m_translations;
    std::map<std::string, std::string> m_languageNames;

    void EnsureDefaultProjectFiles(const std::string& locDir);
    bool LoadLanguageFile(const std::string& code, const std::string& filePath);
};
