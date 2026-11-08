#include "GameLocalization.h"
#include "Utils/SimpleJson.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

GameLocalization& GameLocalization::Get() {
    static GameLocalization instance;
    return instance;
}

void GameLocalization::Init(const std::string& projectRoot) {
    SetProjectRoot(projectRoot);
}

void GameLocalization::SetProjectRoot(const std::string& projectRoot) {
    m_projectRoot = projectRoot;
    std::replace(m_projectRoot.begin(), m_projectRoot.end(), '\\', '/');
    ScanLanguages();
}

std::string GameLocalization::GetLocalizationDirectory() const {
    if (!m_projectRoot.empty()) {
        fs::path p1 = fs::path(m_projectRoot) / "assets" / "localization";
        if (fs::exists(p1)) return p1.generic_string();
        fs::path p2 = fs::path(m_projectRoot) / "localization";
        if (fs::exists(p2)) return p2.generic_string();
        return (fs::path(m_projectRoot) / "assets" / "localization").generic_string();
    }

    if (fs::exists("assets/localization")) return "assets/localization";
    if (fs::exists("localization")) return "localization";
    return "assets/localization";
}

std::string GameLocalization::GetLanguageDisplayName(const std::string& code) const {
    auto it = m_languageNames.find(code);
    if (it != m_languageNames.end() && !it->second.empty()) {
        return it->second;
    }
    if (code == "en") return "English";
    if (code == "es") return "Español";
    if (code == "fr") return "Français";
    if (code == "de") return "Deutsch";
    if (code == "ja") return "日本語";
    if (code == "it") return "Italiano";
    if (code == "pt") return "Português";
    if (code == "ru") return "Русский";
    if (code == "zh") return "简体中文";
    return code;
}

void GameLocalization::EnsureDefaultProjectFiles(const std::string& locDir) {
    std::error_code ec;
    if (!fs::exists(locDir)) {
        fs::create_directories(locDir, ec);
    }

    fs::path enPath = fs::path(locDir) / "en.json";
    if (!fs::exists(enPath)) {
        JsonValue enObj = JsonValue::Object();
        enObj["__language_name"] = "English";
        enObj["game_title"] = "My Awesome Game";
        enObj["menu_play"] = "Play";
        enObj["menu_options"] = "Options";
        enObj["menu_quit"] = "Quit";
        enObj["menu_language"] = "Language";
        enObj["hud_score"] = "Score: ";
        enObj["hud_health"] = "Health: ";
        enObj["dialog_welcome"] = "Welcome, adventurer!";

        std::ofstream f(enPath);
        if (f.is_open()) {
            f << enObj.Serialize(2);
            f.close();
        }
    }

    fs::path esPath = fs::path(locDir) / "es.json";
    if (!fs::exists(esPath)) {
        JsonValue esObj = JsonValue::Object();
        esObj["__language_name"] = "Español";
        esObj["game_title"] = "Mi Gran Juego";
        esObj["menu_play"] = "Jugar";
        esObj["menu_options"] = "Opciones";
        esObj["menu_quit"] = "Salir";
        esObj["menu_language"] = "Idioma";
        esObj["hud_score"] = "Puntuación: ";
        esObj["hud_health"] = "Salud: ";
        esObj["dialog_welcome"] = "¡Bienvenido, aventurero!";

        std::ofstream f(esPath);
        if (f.is_open()) {
            f << esObj.Serialize(2);
            f.close();
        }
    }
}

bool GameLocalization::LoadLanguageFile(const std::string& code, const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    file.close();

    JsonValue root = JsonValue::Parse(content);
    if (!root.IsObject()) return false;

    std::string dispName = GetLanguageDisplayName(code);
    if (root.HasKey("__language_name") && root["__language_name"].IsString()) {
        dispName = root["__language_name"].AsString();
    } else if (root.HasKey("LANGUAGE_NAME") && root["LANGUAGE_NAME"].IsString()) {
        dispName = root["LANGUAGE_NAME"].AsString();
    }
    m_languageNames[code] = dispName;

    for (const auto& [k, v] : root.objVal) {
        if (k == "__language_name" || k == "LANGUAGE_NAME") continue;
        if (v.IsString()) {
            m_translations[code][k] = v.AsString();
            m_allKeys.insert(k);
        }
    }

    GameLanguage lang;
    lang.code = code;
    lang.displayName = dispName;
    lang.filePath = filePath;
    m_languages.push_back(lang);

    return true;
}

void GameLocalization::ScanLanguages() {
    m_languages.clear();
    m_allKeys.clear();
    m_translations.clear();
    m_languageNames.clear();

    std::string locDir = GetLocalizationDirectory();
    EnsureDefaultProjectFiles(locDir);

    if (fs::exists(locDir) && fs::is_directory(locDir)) {
        for (const auto& entry : fs::directory_iterator(locDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string code = entry.path().stem().string();
                std::transform(code.begin(), code.end(), code.begin(), ::tolower);
                LoadLanguageFile(code, entry.path().generic_string());
            }
        }
    }

    // Ensure all languages have entries for all known keys
    for (const auto& lang : m_languages) {
        auto& table = m_translations[lang.code];
        for (const auto& key : m_allKeys) {
            if (table.find(key) == table.end()) {
                table[key] = "";
            }
        }
    }

    // Verify current language exists, else default to "en" or first available
    bool foundCurrent = false;
    for (const auto& lang : m_languages) {
        if (lang.code == m_currentLanguage) {
            foundCurrent = true;
            break;
        }
    }
    if (!foundCurrent) {
        if (!m_languages.empty()) {
            m_currentLanguage = m_languages[0].code;
        } else {
            m_currentLanguage = "en";
        }
    }
}

bool GameLocalization::SetLanguage(const std::string& code) {
    std::string lowerCode = code;
    std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(), ::tolower);

    for (const auto& lang : m_languages) {
        if (lang.code == lowerCode) {
            m_currentLanguage = lowerCode;
            return true;
        }
    }
    return false;
}

bool GameLocalization::AddLanguage(const std::string& code, const std::string& displayName) {
    if (code.empty()) return false;
    std::string lowerCode = code;
    std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(), ::tolower);

    for (const auto& l : m_languages) {
        if (l.code == lowerCode) return false;
    }

    std::string name = displayName.empty() ? GetLanguageDisplayName(lowerCode) : displayName;
    m_languageNames[lowerCode] = name;

    std::string locDir = GetLocalizationDirectory();
    fs::path fPath = fs::path(locDir) / (lowerCode + ".json");

    GameLanguage newLang;
    newLang.code = lowerCode;
    newLang.displayName = name;
    newLang.filePath = fPath.generic_string();
    m_languages.push_back(newLang);

    // Initialize keys
    auto& table = m_translations[lowerCode];
    for (const auto& k : m_allKeys) {
        table[k] = "";
    }

    SaveLanguage(lowerCode);
    return true;
}

bool GameLocalization::RemoveLanguage(const std::string& code) {
    if (code == "en" && m_languages.size() <= 1) return false;

    auto it = std::find_if(m_languages.begin(), m_languages.end(), [&](const GameLanguage& l) {
        return l.code == code;
    });

    if (it != m_languages.end()) {
        std::error_code ec;
        fs::remove(it->filePath, ec);
        m_languages.erase(it);
        m_translations.erase(code);
        m_languageNames.erase(code);

        if (m_currentLanguage == code) {
            m_currentLanguage = m_languages.empty() ? "en" : m_languages[0].code;
        }
        return true;
    }
    return false;
}

bool GameLocalization::HasKey(const std::string& key) const {
    return m_allKeys.find(key) != m_allKeys.end();
}

bool GameLocalization::AddKey(const std::string& key, const std::string& defaultEnglishVal) {
    if (key.empty()) return false;
    if (m_allKeys.find(key) != m_allKeys.end()) return false;

    m_allKeys.insert(key);
    m_translations["en"][key] = defaultEnglishVal.empty() ? key : defaultEnglishVal;

    for (const auto& lang : m_languages) {
        if (lang.code != "en") {
            m_translations[lang.code][key] = "";
        }
    }
    return true;
}

bool GameLocalization::RemoveKey(const std::string& key) {
    if (m_allKeys.find(key) == m_allKeys.end()) return false;
    m_allKeys.erase(key);

    for (auto& [code, table] : m_translations) {
        table.erase(key);
    }
    return true;
}

void GameLocalization::SetTranslation(const std::string& langCode, const std::string& key, const std::string& value) {
    m_allKeys.insert(key);
    m_translations[langCode][key] = value;
}

std::string GameLocalization::GetTranslation(const std::string& langCode, const std::string& key) const {
    auto langIt = m_translations.find(langCode);
    if (langIt != m_translations.end()) {
        auto keyIt = langIt->second.find(key);
        if (keyIt != langIt->second.end()) {
            return keyIt->second;
        }
    }
    return "";
}

std::string GameLocalization::Translate(const std::string& key, const std::string& defaultVal) const {
    // 1. Check current language
    auto curIt = m_translations.find(m_currentLanguage);
    if (curIt != m_translations.end()) {
        auto keyIt = curIt->second.find(key);
        if (keyIt != curIt->second.end() && !keyIt->second.empty()) {
            return keyIt->second;
        }
    }

    // 2. Fall back to English
    if (m_currentLanguage != "en") {
        auto enIt = m_translations.find("en");
        if (enIt != m_translations.end()) {
            auto keyIt = enIt->second.find(key);
            if (keyIt != enIt->second.end() && !keyIt->second.empty()) {
                return keyIt->second;
            }
        }
    }

    // 3. Fall back to default value if provided
    if (!defaultVal.empty()) {
        return defaultVal;
    }

    // 4. Return raw key
    return key;
}

bool GameLocalization::SaveLanguage(const std::string& code) {
    std::string locDir = GetLocalizationDirectory();
    std::error_code ec;
    if (!fs::exists(locDir)) {
        fs::create_directories(locDir, ec);
    }

    fs::path fPath = fs::path(locDir) / (code + ".json");
    JsonValue root = JsonValue::Object();
    root["__language_name"] = GetLanguageDisplayName(code);

    auto it = m_translations.find(code);
    if (it != m_translations.end()) {
        for (const auto& key : m_allKeys) {
            auto valIt = it->second.find(key);
            root[key] = (valIt != it->second.end()) ? valIt->second : "";
        }
    }

    std::ofstream f(fPath);
    if (!f.is_open()) return false;
    f << root.Serialize(2);
    f.close();
    return true;
}

bool GameLocalization::SaveAll() {
    bool allOk = true;
    for (const auto& lang : m_languages) {
        if (!SaveLanguage(lang.code)) {
            allOk = false;
        }
    }
    return allOk;
}

bool GameLocalization::Reload() {
    ScanLanguages();
    return true;
}

int GameLocalization::GetTranslatedCount(const std::string& langCode) const {
    auto it = m_translations.find(langCode);
    if (it == m_translations.end()) return 0;

    int count = 0;
    for (const auto& key : m_allKeys) {
        auto valIt = it->second.find(key);
        if (valIt != it->second.end() && !valIt->second.empty()) {
            count++;
        }
    }
    return count;
}
