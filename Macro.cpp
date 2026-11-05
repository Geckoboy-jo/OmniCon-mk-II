#include "Macro.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ─── Enum names ───────────────────────────────────────────────────────────────

const char* MacroAction::typeName(Type t) {
    switch (t) {
        case Type::Key:      return "key";
        case Type::KeyDown:  return "keydown";
        case Type::KeyUp:    return "keyup";
        case Type::Click:    return "click";
        case Type::Move:     return "move";
        case Type::Wait:     return "wait";
        case Type::TypeText: return "type";
    }
    return "key";
}

MacroAction::Type MacroAction::typeFromName(const std::string& n, Type fallback) {
    if (n == "key")      return Type::Key;
    if (n == "keydown")  return Type::KeyDown;
    if (n == "keyup")    return Type::KeyUp;
    if (n == "click")    return Type::Click;
    if (n == "move")     return Type::Move;
    if (n == "wait")     return Type::Wait;
    if (n == "type")     return Type::TypeText;
    return fallback;
}

const char* MacroTrigger::typeName(Type t) {
    switch (t) {
        case Type::None:       return "none";
        case Type::PixelColor: return "pixel";
        case Type::Button:     return "button";
    }
    return "none";
}

MacroTrigger::Type MacroTrigger::typeFromName(const std::string& n, Type fallback) {
    if (n == "pixel")  return Type::PixelColor;
    if (n == "button") return Type::Button;
    if (n == "none")   return Type::None;
    return fallback;
}

const char* Macro::modeName(Mode m) {
    return m == Mode::Timeline ? "timeline" : "trigger";
}

Macro::Mode Macro::modeFromName(const std::string& n, Mode fallback) {
    if (n == "timeline") return Mode::Timeline;
    if (n == "trigger")  return Mode::Trigger;
    return fallback;
}

bool Macro::matches(const std::string& systemName, const std::string& gameName) const {
    if (!enabled) return false;
    if (mode == Mode::Trigger && trigger.type == MacroTrigger::Type::None) return false;
    if (actions.empty()) return false;
    if (!system.empty() && system != "any" && system != systemName) return false;
    if (!game.empty() && game != gameName) return false;
    return true;
}

// ─── JSON writing ─────────────────────────────────────────────────────────────

static void jsonEscape(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

std::string macroToJson(const Macro& m) {
    std::string j;
    j += "{\n  \"name\": ";        jsonEscape(j, m.name);
    j += ",\n  \"mode\": ";        jsonEscape(j, Macro::modeName(m.mode));
    j += ",\n  \"enabled\": ";     j += m.enabled ? "true" : "false";
    j += ",\n  \"system\": ";      jsonEscape(j, m.system);
    j += ",\n  \"game\": ";        jsonEscape(j, m.game);
    j += ",\n  \"loop\": ";        j += m.loop ? "true" : "false";
    j += ",\n  \"loopDelayMs\": "; j += std::to_string(m.loopDelayMs);

    const MacroTrigger& t = m.trigger;
    j += ",\n  \"trigger\": {\n    \"type\": ";
    jsonEscape(j, MacroTrigger::typeName(t.type));
    j += ",\n    \"x\": ";          j += std::to_string(t.x);
    j += ",\n    \"y\": ";          j += std::to_string(t.y);
    j += ",\n    \"r\": ";          j += std::to_string(t.r);
    j += ",\n    \"g\": ";          j += std::to_string(t.g);
    j += ",\n    \"b\": ";          j += std::to_string(t.b);
    j += ",\n    \"tolerance\": ";  j += std::to_string(t.tolerance);
    j += ",\n    \"button\": ";     jsonEscape(j, t.button);
    j += ",\n    \"intervalMs\": "; j += std::to_string(t.intervalMs);
    j += ",\n    \"cooldownMs\": "; j += std::to_string(t.cooldownMs);
    j += ",\n    \"once\": ";       j += t.once ? "true" : "false";
    j += "\n  }";

    j += ",\n  \"actions\": [";
    for (size_t i = 0; i < m.actions.size(); ++i) {
        const MacroAction& a = m.actions[i];
        j += i == 0 ? "\n" : ",\n";
        j += "    {\"type\": ";
        jsonEscape(j, MacroAction::typeName(a.type));
        j += ", \"key\": ";   jsonEscape(j, a.key);
        j += ", \"text\": ";  jsonEscape(j, a.text);
        j += ", \"x\": ";     j += std::to_string(a.x);
        j += ", \"y\": ";     j += std::to_string(a.y);
        j += ", \"ms\": ";    j += std::to_string(a.ms);
        j += "}";
    }
    j += m.actions.empty() ? "]\n" : "\n  ]\n";
    j += "}\n";
    return j;
}

// ─── Minimal JSON parser ──────────────────────────────────────────────────────
// Just enough for objects, arrays, strings, numbers, booleans and null — the
// shapes macroToJson produces and the Python engine may hand back.

namespace {

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Object, Array };
    Kind                                kind = Kind::Null;
    bool                                boolean = false;
    double                              number = 0.0;
    std::string                         str;
    std::vector<std::pair<std::string, JsonValue>> object;
    std::vector<JsonValue>              array;

    const JsonValue* find(const std::string& key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& kv : object)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    bool parse(JsonValue& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ == s_.size();
    }

private:
    const std::string& s_;
    size_t             pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWs();
        if (pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') { out.kind = JsonValue::Kind::String; return parseString(out.str); }
        if (c == 't') return parseLiteral("true",  out, JsonValue::Kind::Bool, true);
        if (c == 'f') return parseLiteral("false", out, JsonValue::Kind::Bool, false);
        if (c == 'n') return parseLiteral("null",  out, JsonValue::Kind::Null, false);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(out);
        return false;
    }

    bool parseLiteral(const char* word, JsonValue& out, JsonValue::Kind kind, bool boolean) {
        const size_t len = std::strlen(word);
        if (s_.compare(pos_, len, word) != 0) return false;
        pos_ += len;
        out.kind = kind;
        out.boolean = boolean;
        return true;
    }

    bool parseNumber(JsonValue& out) {
        const size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
                s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' ||
                s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        if (pos_ == start) return false;
        try {
            out.number = std::stod(s_.substr(start, pos_ - start));
        } catch (...) {
            return false;
        }
        out.kind = JsonValue::Kind::Number;
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return false;
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                const char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = s_[pos_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= h - '0';
                            else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                            else return false;
                        }
                        // Encode as UTF-8 (surrogate pairs collapsed to '?').
                        if (code < 0x80) {
                            out += static_cast<char>(code);
                        } else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else if (code >= 0xD800 && code <= 0xDFFF) {
                            out += '?';
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    }

    bool parseObject(JsonValue& out) {
        if (!consume('{')) return false;
        out.kind = JsonValue::Kind::Object;
        out.object.clear();
        skipWs();
        if (consume('}')) return true;
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (!consume(':')) return false;
            JsonValue value;
            if (!parseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            skipWs();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }

    bool parseArray(JsonValue& out) {
        if (!consume('[')) return false;
        out.kind = JsonValue::Kind::Array;
        out.array.clear();
        skipWs();
        if (consume(']')) return true;
        while (true) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.array.push_back(std::move(value));
            skipWs();
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }
};

std::string jsonString(const JsonValue& obj, const char* key, const std::string& fallback) {
    const JsonValue* v = obj.find(key);
    return v && v->kind == JsonValue::Kind::String ? v->str : fallback;
}

int jsonInt(const JsonValue& obj, const char* key, int fallback) {
    const JsonValue* v = obj.find(key);
    return v && v->kind == JsonValue::Kind::Number ? static_cast<int>(v->number) : fallback;
}

bool jsonBool(const JsonValue& obj, const char* key, bool fallback) {
    const JsonValue* v = obj.find(key);
    return v && v->kind == JsonValue::Kind::Bool ? v->boolean : fallback;
}

} // namespace

bool macroFromJson(const std::string& json, Macro& out) {
    JsonValue root;
    if (!JsonParser(json).parse(root) || root.kind != JsonValue::Kind::Object)
        return false;

    Macro m;
    m.name        = jsonString(root, "name", "Unnamed");
    m.mode        = Macro::modeFromName(jsonString(root, "mode", "timeline"));
    m.enabled     = jsonBool(root, "enabled", true);
    m.system      = jsonString(root, "system", "any");
    m.game        = jsonString(root, "game", "");
    m.loop        = jsonBool(root, "loop", false);
    m.loopDelayMs = jsonInt(root, "loopDelayMs", 0);

    if (const JsonValue* t = root.find("trigger");
        t && t->kind == JsonValue::Kind::Object) {
        m.trigger.type       = MacroTrigger::typeFromName(jsonString(*t, "type", "none"));
        m.trigger.x          = jsonInt(*t, "x", 0);
        m.trigger.y          = jsonInt(*t, "y", 0);
        m.trigger.r          = jsonInt(*t, "r", 255);
        m.trigger.g          = jsonInt(*t, "g", 255);
        m.trigger.b          = jsonInt(*t, "b", 255);
        m.trigger.tolerance  = jsonInt(*t, "tolerance", 10);
        m.trigger.button     = jsonString(*t, "button", "");
        m.trigger.intervalMs = jsonInt(*t, "intervalMs", 250);
        m.trigger.cooldownMs = jsonInt(*t, "cooldownMs", 1000);
        m.trigger.once       = jsonBool(*t, "once", false);
    }

    if (const JsonValue* a = root.find("actions");
        a && a->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : a->array) {
            if (item.kind != JsonValue::Kind::Object) continue;
            MacroAction action;
            action.type = MacroAction::typeFromName(jsonString(item, "type", "key"));
            action.key  = jsonString(item, "key", "");
            action.text = jsonString(item, "text", "");
            action.x    = jsonInt(item, "x", -1);
            action.y    = jsonInt(item, "y", -1);
            action.ms   = jsonInt(item, "ms", 0);
            m.actions.push_back(std::move(action));
        }
    }

    out = std::move(m);
    return true;
}

// ─── Store ────────────────────────────────────────────────────────────────────

std::string MacroStore::fileNameFor(const std::string& macroName) {
    std::string safe;
    safe.reserve(macroName.size());
    for (const char c : macroName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            safe += c;
        else if (c == ' ')
            safe += '_';
    }
    if (safe.empty()) safe = "macro";
    return safe + ".json";
}

std::vector<Macro> MacroStore::loadAll() const {
    std::vector<Macro> macros;
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return macros;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        Macro m;
        if (macroFromJson(ss.str(), m))
            macros.push_back(std::move(m));
    }
    std::sort(macros.begin(), macros.end(),
              [](const Macro& a, const Macro& b) { return a.name < b.name; });
    return macros;
}

bool MacroStore::save(const Macro& macro) const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const fs::path path = fs::path(dir_) / fileNameFor(macro.name);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << macroToJson(macro);
    return static_cast<bool>(out);
}

bool MacroStore::remove(const std::string& name) const {
    std::error_code ec;
    return fs::remove(fs::path(dir_) / fileNameFor(name), ec);
}

std::vector<Macro> MacroStore::forGame(const std::string& systemName,
                                       const std::string& gameName) const {
    std::vector<Macro> result;
    for (const Macro& m : loadAll())
        if (m.matches(systemName, gameName))
            result.push_back(m);
    return result;
}
