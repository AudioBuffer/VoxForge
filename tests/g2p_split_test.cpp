#include "g2p_svs.h"
#include "g2p_tts.h"
#include <cstdio>

int main() {
    struct C { const char* py; const char* want; };
    const C cases[] = {
        {"ni", "n/i"}, {"hao", "h/ao"}, {"shi", "sh/i"}, {"zhong", "zh/ong"},
        {"an", "an"}, {"ai", "ai"}, {"jie", "j/ie"}, {"guo", "g/uo"},
        {"yi", "i"}, {"wu", "u"}, {"er", "er"}, {"qiu", "q/iu"},
    };
    bool ok = true;
    for (const auto& c : cases) {
        const std::string got = vx::svsSplit(c.py);
        const bool pass = (got == c.want);
        std::printf("%-6s -> %-8s 期望 %-8s %s\n", c.py, got.c_str(), c.want, pass ? "" : "FAIL");
        if (!pass) ok = false;
    }
    // TTS 版：无模型时回退到同一套
    vx::G2PTts tts;
    auto ph = tts.toPhonemes("你好");
    std::printf("TTS 回退: %zu 个音素\n", ph.size());
    if (ph.size() != 2) ok = false;
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
