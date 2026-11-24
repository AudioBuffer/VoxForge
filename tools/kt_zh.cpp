// kt_zh - 中文：G2P -> 拼音 -> IPA -> klatt-tts 官方管线
//   用法: kt_zh <out.wav> <文本>
#include "pinyin_dict.h"
#include "pinyin_ipa.h"
#include "phoneme_names.h"
#include "g2p_phrase.h"
#include "tts/tts.h"
#include "wav.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "/tmp/kt_zh.wav";
    const std::string text = (argc > 2) ? argv[2] : "你好世界";

    vx::G2PPhrase g2p;
    g2p.loadChars("data/g2p_char.txt");
    g2p.loadPhrases("data/g2p_phrase.txt");
    const auto reads = g2p.analyze(text);

    std::string ipa;
    std::printf("拼音:");
    for (const auto& r : reads) {
        std::printf(" %s(%d)", r.syl.c_str(), r.tone);
        ipa += vx::pinyinToIpa(r.syl);
        ipa += " ";
    }
    std::printf("\n音素: %s\n", vx::ipaToAscii(ipa).c_str());

    std::vector<uint8_t> arena(512 * 1024);
    tts::VoiceParams voice = tts::DefaultVoiceParams();
    tts::StreamSynth synth(voice, arena.data(), arena.size());
    tts::StreamOptions opts;
    opts.sample_rate = 44100.0f;
    synth.BeginIpa(ipa.c_str(), opts);

    std::vector<float> pcm;
    float buf[256];
    int n = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while ((n = synth.Read(buf, 256)) > 0) pcm.insert(pcm.end(), buf, buf + n);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    // 定标
    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));

    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs  %.0fms\n", out.c_str(), pcm.size() / 44100.0, ms);
    return 0;
}
