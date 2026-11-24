// voxsynth - 命令行：内置示例乐谱，输出 wav
#include "dsp.h"
#include "wav.h"
#include <chrono>
#include <cstdio>

int main(int argc, char** argv) {
    const double fs = 44100.0;
    vx::Synth synth(fs);
    vx::Config cfg;

    std::vector<vx::Note> score;
    double t = 0;
    auto add = [&](int v, int c, int coda, double dur, double f0, int glide) {
        vx::Note n; n.start = t; n.dur = dur; n.f0 = f0; n.vowel = v;
        n.cons = c; n.coda = coda; n.glideTo = glide;
        if (coda) n.codaDur = std::min(0.12, dur * 0.25);
        if (glide >= 0) n.glideDur = std::min(0.25, dur * 0.55);
        score.push_back(n);
        t += dur;
    };
    // ni hao shi jie zhong guo
    add(3, vx::kM, 0, 0.34, 220, -1);
    add(0, vx::kF, 0, 0.44, 215, -1);
    add(3, vx::kSh, 0, 0.32, 230, -1);
    add(2, vx::kJX, 0, 0.58, 210, -1);
    add(1, vx::kSHX, 2, 0.42, 225, -1);
    add(1, vx::kB, 0, 0.46, 215, 4);

    const auto t0 = std::chrono::steady_clock::now();
    auto pcm = synth.render(score, t + 0.05);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    const double dur = pcm.size() / fs;

    const char* out = (argc > 1) ? argv[1] : "/tmp/voxsynth.wav";
    vx::writeWav16(out, pcm, (int)fs);
    std::printf("%s  %.2fs  %.1fms  RTF %.4f (%.0fx)\n",
                out, dur, ms, ms / 1000.0 / dur, dur / (ms / 1000.0));
    return 0;
}
