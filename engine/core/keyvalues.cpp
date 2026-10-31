#include "core/keyvalues.h"

#include "core/cvar.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace k {
namespace {

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    enum class Kind { End, String, Open, Close };
    struct Token {
        Kind kind;
        std::string text;
    };

    Token next() {
        skipSpaceAndComments();
        if (pos_ >= text_.size()) return {Kind::End, {}};
        char c = text_[pos_];
        if (c == '{') { ++pos_; return {Kind::Open, "{"}; }
        if (c == '}') { ++pos_; return {Kind::Close, "}"}; }
        std::string s;
        if (c == '"') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] != '"') {
                char ch = text_[pos_++];
                if (ch == '\n') ++line_;
                if (ch == '\\' && pos_ < text_.size()) {
                    char e = text_[pos_++];
                    switch (e) {
                        case 'n': s += '\n'; break;
                        case 't': s += '\t'; break;
                        case '\\': s += '\\'; break;
                        case '"': s += '"'; break;
                        default: s += '\\'; s += e; break;
                    }
                } else {
                    s += ch;
                }
            }
            if (pos_ >= text_.size()) {
                error_ = "unterminated string";
                return {Kind::End, {}};
            }
            ++pos_;
        } else {
            while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) &&
                   text_[pos_] != '{' && text_[pos_] != '}' && text_[pos_] != '"')
                s += text_[pos_++];
        }
        return {Kind::String, std::move(s)};
    }

    int line() const { return line_; }
    const std::string& error() const { return error_; }

private:
    void skipSpaceAndComments() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == '\n') { ++line_; ++pos_; }
            else if (std::isspace(static_cast<unsigned char>(c))) ++pos_;
            else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
            } else if (c == '[' ) {
                // Conditional blocks like [$WIN32] after a value: skip them.
                size_t close = text_.find(']', pos_);
                if (close == std::string_view::npos) return;
                // Only treat as a conditional if it starts with '$' or '!'.
                if (pos_ + 1 < text_.size() && (text_[pos_ + 1] == '$' || text_[pos_ + 1] == '!')) pos_ = close + 1;
                else return;
            } else return;
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
    int line_ = 1;
    std::string error_;
};

// Parses entries into `parent` until the matching '}' (or end of file at top level).
// Sections are filled in place: the reference returned by addSection stays valid while we
// recurse because siblings are only appended after the recursion returns.
bool parseBlock(Lexer& lex, KeyValues& parent, bool topLevel, std::string& error) {
    for (;;) {
        auto key = lex.next();
        if (!lex.error().empty()) { error = lex.error(); return false; }
        if (key.kind == Lexer::Kind::End) {
            if (!topLevel) { error = "unexpected end of file, missing '}'"; return false; }
            return true;
        }
        if (key.kind == Lexer::Kind::Close) {
            if (topLevel) { error = "unexpected '}'"; return false; }
            return true;
        }
        if (key.kind == Lexer::Kind::Open) { error = "expected a key, got '{'"; return false; }

        auto val = lex.next();
        if (!lex.error().empty()) { error = lex.error(); return false; }
        if (val.kind == Lexer::Kind::String) {
            parent.add(key.text, val.text);
        } else if (val.kind == Lexer::Kind::Open) {
            if (!parseBlock(lex, parent.addSection(key.text), false, error)) return false;
        } else {
            error = "expected a value or '{' after \"" + key.text + "\"";
            return false;
        }
    }
}

} // namespace

std::optional<KeyValues> KeyValues::parse(std::string_view text, std::string* error) {
    Lexer lex(text);
    KeyValues root;
    root.section_ = true;
    std::string err;
    if (!parseBlock(lex, root, true, err)) {
        if (error) *error = "line " + std::to_string(lex.line()) + ": " + err;
        return std::nullopt;
    }
    return root;
}

const KeyValues* KeyValues::find(std::string_view key) const {
    for (auto& c : children_)
        if (iequals(c.name_, key)) return &c;
    return nullptr;
}

std::string KeyValues::getString(std::string_view key, std::string_view def) const {
    auto* c = find(key);
    return (c && !c->section_) ? c->value_ : std::string(def);
}

float KeyValues::getFloat(std::string_view key, float def) const {
    auto* c = find(key);
    if (!c || c->section_ || c->value_.empty()) return def;
    return std::strtof(c->value_.c_str(), nullptr);
}

int KeyValues::getInt(std::string_view key, int def) const {
    auto* c = find(key);
    if (!c || c->section_ || c->value_.empty()) return def;
    return static_cast<int>(std::strtol(c->value_.c_str(), nullptr, 10));
}

bool KeyValues::getBool(std::string_view key, bool def) const {
    auto* c = find(key);
    if (!c || c->section_ || c->value_.empty()) return def;
    return getFloat(key) != 0.0f;
}

Vec3 KeyValues::getVec3(std::string_view key, Vec3 def) const {
    auto* c = find(key);
    if (!c || c->section_) return def;
    return parseVec3(c->value_, def);
}

KeyValues& KeyValues::add(std::string name, std::string value) {
    section_ = true;
    children_.emplace_back(std::move(name), std::move(value));
    return children_.back();
}

KeyValues& KeyValues::addSection(std::string name) {
    section_ = true;
    children_.emplace_back(std::move(name), std::string());
    children_.back().section_ = true;
    return children_.back();
}

std::string KeyValues::serialize(int indent) const {
    std::string pad(static_cast<size_t>(indent) * 4, ' ');
    std::string out;
    for (auto& c : children_) {
        if (c.section_) {
            out += pad + "\"" + c.name_ + "\"\n" + pad + "{\n" + c.serialize(indent + 1) + pad + "}\n";
        } else {
            out += pad + "\"" + c.name_ + "\" \"" + c.value_ + "\"\n";
        }
    }
    return out;
}

Vec3 parseVec3(std::string_view text, Vec3 def) {
    std::string s(text);
    for (char& c : s)
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') c = ' ';
    std::istringstream in(s);
    Vec3 v = def;
    for (int i = 0; i < 3; ++i) {
        float f;
        if (!(in >> f)) break;
        v[i] = f;
    }
    return v;
}

} // namespace k
