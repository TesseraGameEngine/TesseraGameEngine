#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <stdexcept>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type{Type::Null};
    bool boolVal{false};
    double numVal{0.0};
    std::string strVal{""};
    std::vector<JsonValue> arrVal;
    std::map<std::string, JsonValue> objVal;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : type(Type::Null) {}
    JsonValue(bool b) : type(Type::Bool), boolVal(b) {}
    JsonValue(int n) : type(Type::Number), numVal(static_cast<double>(n)) {}
    JsonValue(uint32_t n) : type(Type::Number), numVal(static_cast<double>(n)) {}
    JsonValue(int64_t n) : type(Type::Number), numVal(static_cast<double>(n)) {}
    JsonValue(float n) : type(Type::Number), numVal(static_cast<double>(n)) {}
    JsonValue(double n) : type(Type::Number), numVal(n) {}
    JsonValue(const char* s) : type(Type::String), strVal(s ? s : "") {}
    JsonValue(const std::string& s) : type(Type::String), strVal(s) {}

    static JsonValue Array() {
        JsonValue val;
        val.type = Type::Array;
        return val;
    }

    static JsonValue Object() {
        JsonValue val;
        val.type = Type::Object;
        return val;
    }

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool AsBool(bool def = false) const { return IsBool() ? boolVal : def; }
    int AsInt(int def = 0) const { return IsNumber() ? static_cast<int>(numVal) : def; }
    uint32_t AsUInt(uint32_t def = 0) const { return IsNumber() ? static_cast<uint32_t>(numVal) : def; }
    float AsFloat(float def = 0.0f) const { return IsNumber() ? static_cast<float>(numVal) : def; }
    double AsDouble(double def = 0.0) const { return IsNumber() ? numVal : def; }
    std::string AsString(const std::string& def = "") const { return IsString() ? strVal : def; }

    void Push(const JsonValue& item) {
        if (type != Type::Array) {
            type = Type::Array;
            arrVal.clear();
        }
        arrVal.push_back(item);
    }

    JsonValue& operator[](size_t index) {
        if (type != Type::Array) {
            type = Type::Array;
            arrVal.clear();
        }
        if (index >= arrVal.size()) {
            arrVal.resize(index + 1);
        }
        return arrVal[index];
    }

    const JsonValue& operator[](size_t index) const {
        static JsonValue nullVal;
        if (type == Type::Array && index < arrVal.size()) {
            return arrVal[index];
        }
        return nullVal;
    }

    JsonValue& operator[](const std::string& key) {
        if (type != Type::Object) {
            type = Type::Object;
            objVal.clear();
        }
        return objVal[key];
    }

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue nullVal;
        if (type == Type::Object) {
            auto it = objVal.find(key);
            if (it != objVal.end()) return it->second;
        }
        return nullVal;
    }

    bool HasKey(const std::string& key) const {
        return type == Type::Object && objVal.find(key) != objVal.end();
    }

    std::string Serialize(int indent = 2, int currentIndent = 0) const {
        std::string ind(currentIndent, ' ');
        std::string nextInd(currentIndent + indent, ' ');

        switch (type) {
            case Type::Null: return "null";
            case Type::Bool: return boolVal ? "true" : "false";
            case Type::Number: {
                std::ostringstream ss;
                if (numVal == static_cast<int64_t>(numVal)) {
                    ss << static_cast<int64_t>(numVal);
                } else {
                    ss << std::setprecision(6) << numVal;
                }
                return ss.str();
            }
            case Type::String: {
                std::ostringstream ss;
                ss << '"';
                for (char c : strVal) {
                    if (c == '"') ss << "\\\"";
                    else if (c == '\\') ss << "\\\\";
                    else if (c == '\n') ss << "\\n";
                    else if (c == '\r') ss << "\\r";
                    else if (c == '\t') ss << "\\t";
                    else ss << c;
                }
                ss << '"';
                return ss.str();
            }
            case Type::Array: {
                if (arrVal.empty()) return "[]";
                // If short primitive array (like 3 floats), keep inline
                bool isShortNumeric = (arrVal.size() <= 4);
                for (const auto& item : arrVal) {
                    if (!item.IsNumber()) { isShortNumeric = false; break; }
                }
                if (isShortNumeric) {
                    std::ostringstream ss;
                    ss << "[";
                    for (size_t i = 0; i < arrVal.size(); ++i) {
                        if (i > 0) ss << ", ";
                        ss << arrVal[i].Serialize(0, 0);
                    }
                    ss << "]";
                    return ss.str();
                }

                std::ostringstream ss;
                ss << "[\n";
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    ss << nextInd << arrVal[i].Serialize(indent, currentIndent + indent);
                    if (i + 1 < arrVal.size()) ss << ",";
                    ss << "\n";
                }
                ss << ind << "]";
                return ss.str();
            }
            case Type::Object: {
                if (objVal.empty()) return "{}";
                std::ostringstream ss;
                ss << "{\n";
                size_t count = 0;
                for (auto it = objVal.begin(); it != objVal.end(); ++it, ++count) {
                    ss << nextInd << '"' << it->first << "\": "
                       << it->second.Serialize(indent, currentIndent + indent);
                    if (count + 1 < objVal.size()) ss << ",";
                    ss << "\n";
                }
                ss << ind << "}";
                return ss.str();
            }
        }
        return "null";
    }

    static JsonValue Parse(const std::string& src) {
        size_t idx = 0;
        return ParseValue(src, idx);
    }

private:
    static void SkipWhitespace(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            ++i;
        }
    }

    static JsonValue ParseValue(const std::string& s, size_t& i) {
        SkipWhitespace(s, i);
        if (i >= s.size()) return JsonValue();

        char c = s[i];
        if (c == '{') return ParseObject(s, i);
        if (c == '[') return ParseArray(s, i);
        if (c == '"') return ParseString(s, i);
        if (c == 't' || c == 'f') return ParseBool(s, i);
        if (c == 'n') return ParseNull(s, i);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber(s, i);

        return JsonValue();
    }

    static JsonValue ParseObject(const std::string& s, size_t& i) {
        JsonValue obj = JsonValue::Object();
        ++i; // skip '{'
        SkipWhitespace(s, i);
        if (i < s.size() && s[i] == '}') {
            ++i;
            return obj;
        }

        while (i < s.size()) {
            SkipWhitespace(s, i);
            if (i >= s.size() || s[i] != '"') break;
            std::string key = ParseString(s, i).AsString();
            SkipWhitespace(s, i);
            if (i >= s.size() || s[i] != ':') break;
            ++i; // skip ':'
            obj.objVal[key] = ParseValue(s, i);
            SkipWhitespace(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
            } else if (i < s.size() && s[i] == '}') {
                ++i;
                break;
            } else {
                break;
            }
        }
        return obj;
    }

    static JsonValue ParseArray(const std::string& s, size_t& i) {
        JsonValue arr = JsonValue::Array();
        ++i; // skip '['
        SkipWhitespace(s, i);
        if (i < s.size() && s[i] == ']') {
            ++i;
            return arr;
        }

        while (i < s.size()) {
            arr.Push(ParseValue(s, i));
            SkipWhitespace(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
            } else if (i < s.size() && s[i] == ']') {
                ++i;
                break;
            } else {
                break;
            }
        }
        return arr;
    }

    static JsonValue ParseString(const std::string& s, size_t& i) {
        ++i; // skip '"'
        std::string res;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                    case '"': res += '"'; break;
                    case '\\': res += '\\'; break;
                    case 'n': res += '\n'; break;
                    case 'r': res += '\r'; break;
                    case 't': res += '\t'; break;
                    default: res += s[i]; break;
                }
            } else {
                res += s[i];
            }
            ++i;
        }
        if (i < s.size() && s[i] == '"') ++i;
        return JsonValue(res);
    }

    static JsonValue ParseNumber(const std::string& s, size_t& i) {
        size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
            ++i;
        }
        try {
            double v = std::stod(s.substr(start, i - start));
            return JsonValue(v);
        } catch (...) {
            return JsonValue(0.0);
        }
    }

    static JsonValue ParseBool(const std::string& s, size_t& i) {
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            return JsonValue(true);
        } else if (s.compare(i, 5, "false") == 0) {
            i += 5;
            return JsonValue(false);
        }
        return JsonValue(false);
    }

    static JsonValue ParseNull(const std::string& s, size_t& i) {
        if (s.compare(i, 4, "null") == 0) {
            i += 4;
        }
        return JsonValue();
    }
};

