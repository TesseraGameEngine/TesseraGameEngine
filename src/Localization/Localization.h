#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

struct LanguageInfo {
    std::string code;         // e.g. "en", "es"
    std::string displayName;  // e.g. "English", "Español"
    std::string filePath;     // e.g. "lang/es.json"
};

class Localization {
public:
    static Localization& Get();

    // Initialize localization system, scan lang/ directory, load settings
    void Init(const std::string& customLangDir = "");

    // Rescan the lang/ directory for newly added translation files (.json)
    void ScanLanguages();

    // Available languages found in lang/
    const std::vector<LanguageInfo>& GetAvailableLanguages() const { return m_languages; }
    
    // Currently active language code ("en", "es", etc.)
    const std::string& GetCurrentLanguage() const { return m_currentCode; }
    
    // Display name of currently active language ("English", "Español", etc.)
    std::string GetCurrentLanguageName() const;

    // Switch active language and persist selection to editor_settings.json
    bool SetLanguage(const std::string& code, bool saveSettings = true);

    // Reload active language translations from disk
    void ReloadCurrentLanguage();

    // Main translation lookup:
    // If not found in current language, falls back to English, then to the key itself.
    // Handles Dear ImGui ID suffixes ("Label##ID" -> translates "Label" and preserves "##ID").
    const char* Translate(const char* key);
    std::string Translate(const std::string& key);

    // Load/Save preference from/to editor_settings.json
    void LoadSettings();
    void SaveSettings();

    // Ensure default lang directory and baseline translation files (en.json, es.json) exist
    void EnsureDefaultFiles();

    // Current resolved directory containing lang files
    std::string GetLangDirectory() const { return m_langDir; }

    // Manually register or override a string translation in memory
    void AddTranslation(const std::string& langCode, const std::string& key, const std::string& value);

private:
    Localization();
    ~Localization() = default;
    Localization(const Localization&) = delete;
    Localization& operator=(const Localization&) = delete;

    std::string FindLangDirectory(const std::string& preferred = "") const;
    bool LoadLanguageFile(const std::string& filePath, std::string& outCode, std::string& outName,
                          std::unordered_map<std::string, std::string>& outStrings);
    void LoadEmbeddedFallbacks();

    std::string m_langDir;
    std::string m_currentCode{"en"};
    std::vector<LanguageInfo> m_languages;

    // Active loaded translations: key -> translated string
    std::unordered_map<std::string, std::string> m_translations;
    // Fallback translations (English): key -> translated string
    std::unordered_map<std::string, std::string> m_fallbackTranslations;

    // Cache for Dear ImGui ID composite strings (e.g. "Translated Label##ID")
    // so returning const char* is always valid and persistent
    std::unordered_map<std::string, std::string> m_idStringCache;

    bool m_initialized{false};
};

// Global shorthand translation functions
inline const char* TR(const char* key) {
    return Localization::Get().Translate(key);
}

inline std::string TR(const std::string& key) {
    return Localization::Get().Translate(key);
}

