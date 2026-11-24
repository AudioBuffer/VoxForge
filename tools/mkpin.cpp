// mkpin - 把 pinyin-data / phrase-pinyin-data 的文本转成紧凑表
//   字符表：U+XXXX: 读音1,读音2...   # 字
//   词组表：词语: 拼音 拼音 ...
// 输出：data/g2p_char.txt 与 data/g2p_phrase.txt（制表符分隔，去掉声调符号只留数字）
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
std::string toneFold(const std::string& s, int& tone) {
    static const std::unordered_map<std::string, std::pair<std::string, int>> kMarks = {
        {"ā", {"a", 1}}, {"á", {"a", 2}}, {"ǎ", {"a", 3}}, {"à", {"a", 4}},
        {"ē", {"e", 1}}, {"é", {"e", 2}}, {"ě", {"e", 3}}, {"è", {"e", 4}},
        {"ī", {"i", 1}}, {"í", {"i", 2}}, {"ǐ", {"i", 3}}, {"ì", {"i", 4}},
        {"ō", {"o", 1}}, {"ó", {"o", 2}}, {"ǒ", {"o", 3}}, {"ò", {"o", 4}},
        {"ū", {"u", 1}}, {"ú", {"u", 2}}, {"ǔ", {"u", 3}}, {"ù", {"u", 4}},
        {"ǖ", {"v", 1}}, {"ǘ", {"v", 2}}, {"ǚ", {"v", 3}}, {"ǜ", {"v", 4}},
        {"ü", {"v", 5}},
    };
    std::string out;
    tone = 5;
    for (size_t i = 0; i < s.size();) {
        // 尝试匹配 3 字节 UTF-8（声调字符）
        bool matched = false;
        for (int len = 3; len >= 2 && !matched; --len) {
            if (i + (size_t)len <= s.size()) {
                const std::string k = s.substr(i, (size_t)len);
                auto it = kMarks.find(k);
                if (it != kMarks.end()) {
                    out += it->second.first;
                    tone = it->second.second;
                    i += (size_t)len;
                    matched = true;
                }
            }
        }
        if (!matched) { out += s[i]; ++i; }
    }
    return out;
}

std::string utf8Of(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "用法: mkpin <chars.txt> <phrases.txt> <out_char.txt> <out_phrase.txt>\n");
        return 2;
    }
    // 单字表
    {
        std::ifstream f(argv[1]);
        FILE* o = std::fopen(argv[3], "wb");
        std::string line;
        size_t n = 0;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string u = line.substr(0, colon);
            if (u.rfind("U+", 0) != 0) continue;
            const uint32_t cp = (uint32_t)std::strtoul(u.c_str() + 2, nullptr, 16);
            std::string rest = line.substr(colon + 1);
            const size_t hash = rest.find('#');
            if (hash != std::string::npos) rest = rest.substr(0, hash);
            // 逗号分隔的多读音
            std::string out;
            size_t p = 0;
            while (p < rest.size()) {
                size_t c = rest.find(',', p);
                std::string item = rest.substr(p, c == std::string::npos ? std::string::npos : c - p);
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(item.begin());
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
                if (!item.empty()) {
                    int tone = 5;
                    const std::string syl = toneFold(item, tone);
                    if (!out.empty()) out += ',';
                    out += syl + ":" + std::to_string(tone);
                }
                if (c == std::string::npos) break;
                p = c + 1;
            }
            if (out.empty()) continue;
            std::fprintf(o, "%s\t%s\n", utf8Of(cp).c_str(), out.c_str());
            ++n;
        }
        std::fclose(o);
        std::printf("单字 %zu -> %s\n", n, argv[3]);
    }
    // 词组表
    {
        std::ifstream f(argv[2]);
        FILE* o = std::fopen(argv[4], "wb");
        std::string line;
        size_t n = 0;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string phrase = line.substr(0, colon);
            std::string rest = line.substr(colon + 1);
            std::string out;
            size_t p = 0;
            while (p < rest.size()) {
                size_t sp = rest.find(' ', p);
                std::string item = rest.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                if (!item.empty()) {
                    int tone = 5;
                    const std::string syl = toneFold(item, tone);
                    if (!out.empty()) out += ',';
                    out += syl + ":" + std::to_string(tone);
                }
                if (sp == std::string::npos) break;
                p = sp + 1;
            }
            if (out.empty()) continue;
            std::fprintf(o, "%s\t%s\n", phrase.c_str(), out.c_str());
            ++n;
        }
        std::fclose(o);
        std::printf("词组 %zu -> %s\n", n, argv[4]);
    }
    return 0;
}
