// VoxSynth G2P 字典 —— 汉字 -> (拼音, 声调)
//
// 数据：data/g2p_dict.txt（由 tools/mkdict.cpp 从 pypinyin 数据生成，MIT）
// 多音字按「最后写入优先」覆盖；上下文消歧交给 g2p_model.h 的小网络。
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace vx {

struct DictEntry {
    std::string syl;
    int tone = 5;      // 1..4，5=轻声
};

class G2PDict {
public:
    bool load(const std::string& path) {
        map_.clear();
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        char line[512];
        while (std::fgets(line, sizeof line, f)) {
            char* t1 = std::strchr(line, '\t');
            if (!t1) continue;
            *t1 = 0;
            char* t2 = std::strchr(t1 + 1, '\t');
            if (!t2) continue;
            *t2 = 0;
            DictEntry e;
            e.syl = t1 + 1;
            e.tone = std::atoi(t2 + 1);
            map_[line] = e;
        }
        std::fclose(f);
        return !map_.empty();
    }

    size_t size() const { return map_.size(); }

    bool lookup(const std::string& ch, DictEntry& out) const {
        auto it = map_.find(ch);
        if (it == map_.end()) return false;
        out = it->second;
        return true;
    }

private:
    std::unordered_map<std::string, DictEntry> map_;
};

}  // namespace vx
