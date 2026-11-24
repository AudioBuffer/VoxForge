#include "pinyin_dict.h"
#include "g2p_svs.h"
#include "zhtext.h"
#include <cstdio>

int main() {
    vx::G2PDict dict;
    if (!dict.load("data/g2p_dict.txt")) { std::printf("FAIL 加载字典\n"); return 1; }
    std::printf("字典 %zu 条\n", dict.size());

    const char* words[] = {"你好世界", "中国人民", "天气不错"};
    bool ok = true;
    for (const char* w : words) {
        std::printf("%s ->", w);
        for (const auto& ch : vx::utf8Chars(w)) {
            vx::DictEntry e;
            if (dict.lookup(ch, e))
                std::printf(" %s(%d)", vx::svsSplit(e.syl).c_str(), e.tone);
            else
                std::printf(" ?");
        }
        std::printf("\n");
    }
    // 抽样准确率
    int n = 0, hit = 0;
    for (const char* w : words)
        for (const auto& ch : vx::utf8Chars(w)) {
            vx::DictEntry e; ++n;
            if (dict.lookup(ch, e)) ++hit;
        }
    std::printf("命中 %d/%d\n", hit, n);
    if (hit != n) ok = false;
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
