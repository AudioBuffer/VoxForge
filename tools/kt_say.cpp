// kt_say - 统一入口：中文/日语 × 多声线
//
// 用法: kt_say <out.wav> <zh|ja> <neutral|male|female|child> <文本>
//   例: kt_say out.wav zh female "你好世界"
//       kt_say out.wav ja male   "こんにちは"
#include "japanese.h"
#include "phoneme_names.h"
#include "pinyin_ipa.h"
#include "prosody.h"
#include "voice.h"
#include "g2p_phrase.h"
#include "tts/tts.h"
#include "wav.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "用法: kt_say <out.wav> <zh|ja> <voice> <文本>\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string lang = argv[2];
    const std::string voiceName = argv[3];
    const std::string text = argv[4];

    std::string ipa;
    if (lang == "ja") {
        ipa = vx::kanaToIpa(text);
    } else {
        vx::G2PPhrase g2p;
        g2p.loadChars("data/g2p_char.txt");
        g2p.loadPhrases("data/g2p_phrase.txt");
        for (const auto& r : g2p.analyze(text)) ipa += vx::pinyinToIpa(r.syl) + " ";
    }
    std::printf("语言=%s 声线=%s\n", lang.c_str(), voiceName.c_str());
    std::printf("音素: %s\n", vx::ipaToAscii(ipa).c_str());

    const vx::VoicePreset& vp = vx::voiceByName(voiceName);
    std::vector<uint8_t> arena(512 * 1024);
    tts::VoiceParams params = tts::DefaultVoiceParams();
    vx::applyVoice(vp, params);
    const char* sm = std::getenv("VOX_SMOOTH");
    params.formant_smooth_ms = sm ? (float)std::atof(sm) : 24.0f;
    params.av_smooth_ms = 10.0f;
    tts::StreamSynth synth(params, arena.data(), arena.size());
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

    float pk = 1e-6f;
    for (float s : pcm) pk = std::max(pk, std::fabs(s));
    for (float& s : pcm) s = std::max(-0.95f, std::min(0.95f, s * (0.85f / pk)));

    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs  %.0fms\n", out.c_str(), pcm.size() / 44100.0, ms);
    return 0;
}
