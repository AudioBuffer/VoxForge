#include "zhtext.h"
#include <cstdio>

int main() {
    const char* cases[] = {"你好世界", "中国人民", "天气不错"};
    bool ok = true;
    for (const char* c : cases) {
        auto ws = vx::g2p(c);
        std::printf("%s ->", c);
        for (const auto& w : ws) std::printf(" %s", w.phoneme.c_str());
        std::printf("\n");
        if (ws.empty()) ok = false;
        for (const auto& w : ws) if (w.phoneme.empty()) ok = false;
    }
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
