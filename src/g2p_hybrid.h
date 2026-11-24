// VoxSynth G2P —— 混合方案
//
//   已知字：查表（100%），不经过网络
//   多音字：查表给出默认读音；若挂了上下文模型，则用上下文重新判定
//
// 这样网络的职责被收窄到「几百个多音字」，训练数据和精度都可控。
#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include "pinyin_dict.h"
#include "zhtext.h"

namespace vx {

class G2PHybrid {
public:
    bool loadDict(const std::string& path) { return dict_.load(path); }

    // 多音字表：每行一个汉字
    bool loadPolyphones(const std::string& path) {
        poly_.clear();
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char line[64];
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) poly_.insert(s);
        }
        std::fclose(f);
        return !poly_.empty();
    }

    bool isPolyphone(const std::string& ch) const { return poly_.count(ch) > 0; }

    // 整句：返回每字的拼音与声调（default 读音）
    struct Word {
        std::string ch;
        std::string syl;
        int tone{5};
        bool poly{false};
    };

    std::vector<Word> analyze(const std::string& text) const {
        std::vector<Word> out;
        for (const auto& ch : utf8Chars(text)) {
            Word w;
            w.ch = ch;
            DictEntry e;
            if (dict_.lookup(ch, e)) {
                w.syl = e.syl;
                w.tone = e.tone;
            }
            w.poly = isPolyphone(ch);
            out.push_back(w);
        }
        return out;
    }

private:
    G2PDict dict_;
    std::unordered_set<std::string> poly_;
};

}  // namespace vx
