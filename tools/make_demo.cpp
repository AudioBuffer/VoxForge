// make_demo - 生成演示音轨（多语言、多声线、带停顿）
//
// 输出一条 WAV，依次：
//   中文女声 / 中文男声 / 日语女声 / 日语男声 / 童声
#include "japanese.h"
#include "phoneme_names.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "g2p_phrase.h"
#include "tts/tts.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

struct Line {
    std::string lang;
    std::string voice;
    std::string text;
};

std::string toIpa(const std::string& lang, const std::string& text, vx::G2PPhrase& g2p) {
    if (lang == "ja") return vx::kanaToIpa(text);
    std::string ipa;
    for (const auto& r : g2p.analyze(text)) ipa += vx::pinyinToIpa(r.syl) + " ";
    return ipa;
}

std::vector<float> renderLine(const std::string& ipa, const std::string& voiceName) {
    const vx::VoicePreset& vp = vx::voiceByName(voiceName);
    std::vector<uint8_t> arena(512 * 1024);
    tts::VoiceParams params = tts::DefaultVoiceParams();
    vx::applyVoice(vp, params);
    tts::StreamSynth synth(params, arena.data(), arena.size());
    tts::StreamOptions opts;
    opts.sample_rate = 44100.0f;
    synth.BeginIpa(ipa.c_str(), opts);

    std::vector<float> pcm;
    float buf[256];
    int n = 0;
    while ((n = synth.Read(buf, 256)) > 0) pcm.insert(pcm.end(), buf, buf + n);

    float pk = 1e-6f;
    for (float s : pcm) pk = std::max(pk, std::fabs(s));
    for (float& s : pcm) s = std::max(-0.95f, std::min(0.95f, s * (0.8f / pk)));
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "/sdcard/Download/DSH-Go/voxforge_demo.wav";

    vx::G2PPhrase g2p;
    g2p.loadChars("data/g2p_char.txt");
    g2p.loadPhrases("data/g2p_phrase.txt");

    const std::vector<Line> script = {
        {"zh", "female", "你好世界"},
        {"zh", "male", "中国人民"},
        {"zh", "female", "天气不错"},
        {"ja", "female", "こんにちは"},
        {"ja", "male", "ありがとう"},
        {"zh", "child", "银行行走"},
    };

    const int sr = 44100;
    const size_t pause = (size_t)(sr * 0.45);
    std::vector<float> all;

    for (const auto& l : script) {
        const std::string ipa = toIpa(l.lang, l.text, g2p);
        std::printf("[%s/%s] %s -> %s\n", l.lang.c_str(), l.voice.c_str(),
                    l.text.c_str(), vx::ipaToAscii(ipa).c_str());
        auto pcm = renderLine(ipa, l.voice);
        all.insert(all.end(), pcm.begin(), pcm.end());
        all.insert(all.end(), pause, 0.0f);
    }

    vx::writeWav16(out, all, sr);
    std::printf("%s  %.1fs\n", out.c_str(), all.size() / (double)sr);
    return 0;
}
