// kt_demo - 用 vendored 的 MIT Klatt TTS 合成一句话，输出 wav
#include "tts/tts.h"
#include "wav.h"
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

int main(int argc, char** argv) {
    const char* text = (argc > 1) ? argv[1] : "hello world";
    const char* out  = (argc > 2) ? argv[2] : "/tmp/kt.wav";

    std::vector<uint8_t> arena(256 * 1024);
    tts::VoiceParams voice = tts::DefaultVoiceParams();
    tts::StreamSynth synth(voice, arena.data(), arena.size());
    tts::StreamOptions opts;
    opts.sample_rate = 22050.0f;
    synth.BeginText(text, opts);

    std::vector<float> pcm;
    float buf[256];
    const auto t0 = std::chrono::steady_clock::now();
    int n = 0;
    while ((n = synth.Read(buf, 256)) > 0)
        pcm.insert(pcm.end(), buf, buf + n);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    vx::writeWav16(out, pcm, 22050);
    const double dur = pcm.size() / 22050.0;
    std::printf("%s  \"%s\"  %.2fs  %.1fms  RTF %.4f (%.0fx)\n",
                out, text, dur, ms, ms / 1000.0 / (dur > 0 ? dur : 1), dur / (ms / 1000.0));
    return 0;
}
