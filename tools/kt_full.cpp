// kt_full - 用 vendored klatt-tts 的完整管线（含官方段生成器）
//   用法: kt_full <out.wav> [text|ipa:...]
#include "tts/tts.h"
#include "wav.h"
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "/tmp/kt_full.wav";
    std::string arg = (argc > 2) ? argv[2] : "hello world";
    const bool ipa = arg.rfind("ipa:", 0) == 0;
    if (ipa) arg = arg.substr(4);

    std::vector<uint8_t> arena(512 * 1024);
    tts::VoiceParams voice = tts::DefaultVoiceParams();
    tts::StreamSynth synth(voice, arena.data(), arena.size());
    tts::StreamOptions opts;
    opts.sample_rate = 44100.0f;
    if (ipa) synth.BeginIpa(arg.c_str(), opts);
    else     synth.BeginText(arg.c_str(), opts);

    std::vector<float> pcm;
    float buf[256];
    int n = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while ((n = synth.Read(buf, 256)) > 0)
        pcm.insert(pcm.end(), buf, buf + n);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs  %.0fms  文本=%s%s\n", out.c_str(), pcm.size() / 44100.0, ms,
                ipa ? "IPA:" : "", arg.c_str());
    return 0;
}
