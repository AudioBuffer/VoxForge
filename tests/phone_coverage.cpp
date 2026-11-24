// phone_coverage - 音素覆盖自检
//
// 把中文全量拼音、日语全量假名、常用英文音素转成 token，
// 检查是否每个 token 都能在引擎音素表里找到。
#include "g2p.h"
#include "japanese.h"
#include "pinyin_ipa.h"
#include "tts/phonemes.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

void check(const std::vector<std::string>& tokens, const char* tag, int& miss, std::set<std::string>& missing) {
    int bad = 0;
    for (const auto& t : tokens) {
        if (t == " " || t == ".") continue;
        if (!tts::LookupPhone(t)) {
            ++bad;
            missing.insert(t + " (" + tag + ")");
        }
    }
    std::printf("%-8s token %4zu  缺失 %d\n", tag, tokens.size(), bad);
    miss += bad;
}

}  // namespace

int main() {
    int miss = 0;
    std::set<std::string> missing;

    // 中文：遍历字典里所有拼音
    {
        std::ifstream f("data/g2p_char.txt");
        std::string line;
        std::vector<std::string> toks;
        while (std::getline(f, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            std::string rest = line.substr(tab + 1);
            size_t p = 0;
            while (p < rest.size()) {
                const size_t comma = rest.find(',', p);
                const std::string item = rest.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
                const size_t colon = item.find(':');
                if (colon != std::string::npos) {
                    const std::string syl = item.substr(0, colon);
                    for (const auto& t : g2p::TokenizeIpa(vx::pinyinToIpa(syl))) toks.push_back(t);
                }
                if (comma == std::string::npos) break;
                p = comma + 1;
            }
        }
        check(toks, "中文", miss, missing);
    }

    // 日语：全假名表
    {
        std::vector<std::string> toks;
        for (const auto& kv : vx::kanaTable())
            for (const auto& t : g2p::TokenizeIpa(kv.second)) toks.push_back(t);
        check(toks, "日语", miss, missing);
    }

    // 英语：IPA 全表遍历
    {
        std::vector<std::string> toks;
        for (const auto& p : tts::DefaultPhoneTable())
            for (const auto& t : g2p::TokenizeIpa(p.ipa)) toks.push_back(t);
        check(toks, "英语", miss, missing);
    }

    if (!missing.empty()) {
        std::printf("缺失 token:\n");
        for (const auto& m : missing) std::printf("  %s\n", m.c_str());
    }
    std::printf(miss == 0 ? "PASS\n" : "FAIL\n");
    return miss == 0 ? 0 : 1;
}
