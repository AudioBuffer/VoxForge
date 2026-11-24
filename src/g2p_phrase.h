// VoxSynth G2P —— 词组优先 + 单字兜底
//
// 数据（均由 tools/mkpin.cpp 从 mozillazg 的 MIT 数据转换）：
//   data/g2p_char.txt    单字所有读音（多音字自动可得）
//   data/g2p_phrase.txt  词组读音（上下文正确性靠它）
//
// 策略：最长匹配词组（最多 8 字），命中即用；否则查单字取首读音。
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "zhtext.h"      // utf8Chars

namespace vx {

struct Read {
    std::string syl;
    int tone = 5;
};

class G2PPhrase {
public:
    bool loadChars(const std::string& path) {
        chars_.clear();
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char* line = nullptr;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, f)) > 0) {
            std::string s(line, (size_t)n);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            const size_t tab = s.find('\t');
            if (tab == std::string::npos) continue;
            std::vector<Read> rs;
            parseList(s.substr(tab + 1), rs);
            if (!rs.empty()) chars_[s.substr(0, tab)] = rs;
        }
        std::free(line);
        std::fclose(f);
        return !chars_.empty();
    }

    bool loadPhrases(const std::string& path) {
        phrases_.clear();
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char* line = nullptr;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, f)) > 0) {
            std::string s(line, (size_t)n);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            const size_t tab = s.find('\t');
            if (tab == std::string::npos) continue;
            std::vector<Read> rs;
            parseList(s.substr(tab + 1), rs);
            if (!rs.empty()) phrases_[s.substr(0, tab)] = rs;
        }
        std::free(line);
        std::fclose(f);
        return !phrases_.empty();
    }

    size_t charCount() const { return chars_.size(); }
    size_t phraseCount() const { return phrases_.size(); }

    bool isPolyphone(const std::string& ch) const {
        auto it = chars_.find(ch);
        return it != chars_.end() && it->second.size() > 1;
    }

    // 整句 -> 每个字一个读音（词组优先，最长 8 字）
    std::vector<Read> analyze(const std::string& text) const {
        const auto chars = utf8Chars(text);
        std::vector<Read> out;
        size_t i = 0;
        while (i < chars.size()) {
            bool matched = false;
            for (int len = std::min<int>(8, (int)(chars.size() - i)); len >= 2; --len) {
                std::string phrase;
                for (int k = 0; k < len; ++k) phrase += chars[(size_t)i + (size_t)k];
                auto it = phrases_.find(phrase);
                if (it != phrases_.end() && it->second.size() == (size_t)len) {
                    out.insert(out.end(), it->second.begin(), it->second.end());
                    i += (size_t)len;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            auto it = chars_.find(chars[i]);
            if (it != chars_.end()) out.push_back(it->second.front());
            else out.push_back({chars[i], 5});
            ++i;
        }
        return out;
    }

private:
    static void parseList(const std::string& s, std::vector<Read>& out) {
        size_t p = 0;
        while (p < s.size()) {
            const size_t comma = s.find(',', p);
            const std::string item = s.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) {
                Read r;
                r.syl = item.substr(0, colon);
                r.tone = std::atoi(item.c_str() + colon + 1);
                out.push_back(r);
            }
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
    }

    std::unordered_map<std::string, std::vector<Read>> chars_;
    std::unordered_map<std::string, std::vector<Read>> phrases_;
};

}  // namespace vx
