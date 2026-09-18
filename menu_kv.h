#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <fstream>
#include <algorithm>

struct MenuKVNode {
    std::string name;
    std::vector<std::string> values;
    std::vector<MenuKVNode> children;
};

class MenuKV {
public:
    static std::vector<MenuKVNode> parse(const std::string& text) {
        const std::vector<std::string> tokens = tokenize(text);
        size_t index = 0;
        std::vector<MenuKVNode> out;
        parseChildren(tokens, index, out, false);
        return out;
    }

    static bool readFile(const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    static std::string value(const MenuKVNode& node, const std::string& key,
                             const std::string& fallback = "") {
        const std::string want = lower(key);
        for (const auto& child : node.children) {
            if (child.children.empty() && lower(child.name) == want && !child.values.empty())
                return child.values.front();
        }
        return fallback;
    }

    static const MenuKVNode* child(const MenuKVNode& node, const std::string& key) {
        const std::string want = lower(key);
        for (const auto& c : node.children) {
            if (lower(c.name) == want) return &c;
        }
        return nullptr;
    }

private:
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        size_t i = 0;
        if (text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF) {
            i = 3;
        }

        while (i < text.size()) {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= text.size()) break;

            if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
                i += 2;
                while (i < text.size() && text[i] != '\n') ++i;
                continue;
            }
            if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
                if (i + 1 < text.size()) i += 2;
                continue;
            }

            if (text[i] == '{' || text[i] == '}') {
                tokens.emplace_back(1, text[i++]);
                continue;
            }

            if (text[i] == '"') {
                ++i;
                std::string token;
                while (i < text.size()) {
                    const char c = text[i++];
                    if (c == '"') break;
                    if (c == '\\' && i < text.size()) {
                        const char next = text[i];
                        if (next == '"' || next == '\\') {
                            token.push_back(next);
                            ++i;
                            continue;
                        }
                    }
                    token.push_back(c);
                }
                tokens.push_back(token);
                continue;
            }

            const size_t start = i;
            while (i < text.size()) {
                const char c = text[i];
                if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}') break;
                if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') break;
                ++i;
            }
            if (i > start) tokens.push_back(text.substr(start, i - start));
        }

        return tokens;
    }

    static void parseChildren(const std::vector<std::string>& tokens, size_t& index,
                              std::vector<MenuKVNode>& out, bool stopAtBrace) {
        while (index < tokens.size()) {
            const std::string key = tokens[index++];
            if (stopAtBrace && key == "}") return;
            if (key == "{" || key.empty()) continue;
            if (index >= tokens.size()) {
                MenuKVNode node;
                node.name = key;
                out.push_back(node);
                return;
            }

            const std::string next = tokens[index++];
            if (next == "{") {
                MenuKVNode node;
                node.name = key;
                parseChildren(tokens, index, node.children, true);
                out.push_back(node);
                continue;
            }

            if (next == "}") {
                                                                                
                                                             
                MenuKVNode node;
                node.name = key;
                out.push_back(node);
                if (stopAtBrace) return;
                continue;
            }

            MenuKVNode node;
            node.name = key;
            node.values.push_back(next);
            out.push_back(node);
        }
    }
};
