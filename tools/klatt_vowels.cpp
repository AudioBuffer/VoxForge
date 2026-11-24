// 用 vendored MIT Klatt 核心渲染六个元音（不依赖其 G2P/分段层）
#include "klatt.h"
#include "wav.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    const float fs = 44100.0f;
    const int spf = (int)(fs * 0.005f);          // 5ms 帧

    struct V { const char* n; float f1, f2, f3; };
    const V vs[6] = {
        {"a", 800, 1250, 2550}, {"o", 520, 880, 2500}, {"e", 520, 1250, 2400},
        {"i", 300, 2300, 3000}, {"u", 320, 780, 2400}, {"v", 300, 1900, 2300},
    };

    std::vector<float> all;
    for (const auto& v : vs) {
        tts::KlattParams p;
        p.voice_gain = 18.0f;
        p.lf_rd = 1.3f;
        p.tilt_db = 6.0f;
        p.breath = 0.15f;
        p.jitter = 0.002f;
        p.shimmer = 0.01f;
        tts::KlattSynth synth(fs, p);

        std::vector<tts::SynthFrame> frames;
        const int nFrames = (int)(0.9f / 0.005f);
        for (int i = 0; i < nFrames; ++i) {
            tts::SynthFrame fr;
            fr.f0 = 175.0f;
            fr.f1 = v.f1; fr.f2 = v.f2; fr.f3 = v.f3;
            const float t = i * 0.005f;
            const float env = std::min(1.0f, t / 0.008f) * std::min(1.0f, (0.9f - t) / 0.02f);
            fr.av = 0.85f * std::max(0.0f, env);
            frames.push_back(fr);
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto pcm = synth.Render(frames, spf);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        // 归一化：核心输出未定标
        float pk = 1e-6f;
        for (float v2 : pcm) pk = std::max(pk, std::fabs(v2));
        const float g = 0.80f / pk;
        for (float& v2 : pcm) v2 *= g;
        std::printf("%s: %.2fs %.1fms RTF %.4f\n", v.n, pcm.size() / fs, ms,
                    ms / 1000.0 / (pcm.size() / fs));
        all.insert(all.end(), pcm.begin(), pcm.end());
    }
    const char* out = (argc > 1) ? argv[1] : "/tmp/klatt_vowels.wav";
    vx::writeWav16(out, all, (int)fs);
    std::printf("%s\n", out);
    return 0;
}
