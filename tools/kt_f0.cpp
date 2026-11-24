// kt_f0 - 按外部 F0 曲线渲染一个持续元音（残差数据生成用）
//   用法: kt_f0 <out.wav> <f0.bin> [音节] [帧移ms]
//   f0.bin: float32 逐帧基频，0 表示清音帧
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: kt_f0 <out.wav> <f0.bin> [音节] [帧移ms]\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string f0path = argv[2];
    const std::string syl = (argc > 3) ? argv[3] : "a";
    const double hopMs = (argc > 4) ? std::atof(argv[4]) : 11.6;

    // 读 F0
    std::vector<float> f0in;
    if (FILE* f = std::fopen(f0path.c_str(), "rb")) {
        float v;
        while (std::fread(&v, sizeof v, 1, f) == 1) f0in.push_back(v);
        std::fclose(f);
    }
    if (f0in.empty()) { std::fprintf(stderr, "F0 为空\n"); return 1; }

    const double totalSec = f0in.size() * hopMs / 1000.0;
    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);
    vp.formant_smooth_ms = 32.0f;

    auto phones = g2p::TokenizeIpa(vx::pinyinToIpa(syl));
    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    // 把元音段拉满整段时长
    const float scale = natural > 1.0f ? (float)(totalSec * 1000.0 / natural) : 1.0f;
    const size_t nFrames = tts::synth_detail::CountFrames(segs, scale);

    std::vector<float> f1(nFrames), f2(nFrames), f3(nFrames), b1(nFrames), b2(nFrames),
        b3(nFrames), av(nFrames), af(nFrames), ah(nFrames), nasal(nFrames),
        fnp(nFrames), fnz(nFrames), fcf(nFrames), accent(nFrames), f0(nFrames);
    std::vector<uint8_t> major(nFrames);
    tts::synth_detail::ParamTracks tr;
    tr.f1 = f1.data(); tr.f2 = f2.data(); tr.f3 = f3.data();
    tr.b1 = b1.data(); tr.b2 = b2.data(); tr.b3 = b3.data();
    tr.av = av.data(); tr.af = af.data(); tr.ah = ah.data();
    tr.nasal = nasal.data(); tr.fnp = fnp.data(); tr.fnz = fnz.data();
    tr.fric_cf = fcf.data(); tr.accent = accent.data(); tr.f0 = f0.data();
    tr.major = major.data(); tr.n = nFrames;
    tts::synth_detail::FillParamTracks(segs, vp, scale, false, tr);

    // 把外部 F0 映射到帧并施加清浊
    for (size_t i = 0; i < nFrames; ++i) {
        const double tSec = i * tts::synth_detail::kFrameMs / 1000.0;
        size_t idx = (size_t)(tSec * 1000.0 / hopMs);
        const float hz = (idx < f0in.size()) ? f0in[idx] : 0.0f;
        if (hz > 20.0f) {
            f0[i] = hz;
        } else {
            f0[i] = 220.0f;
            av[i] = 0.0f;                       // 清音帧：关浊音
            ah[i] = std::max(ah[i], 0.35f);     // 补一点气声
        }
    }

    tts::KlattSynth synth(44100.0f, tts::synth_detail::MakeKlattParams(vp));
    std::vector<float> pcm;
    pcm.reserve(nFrames * 220);
    std::vector<float> buf(220);
    for (size_t i = 0; i < nFrames; ++i) {
        const auto cur = tts::synth_detail::FrameAt(tr, i);
        const auto nxt = tts::synth_detail::FrameAt(tr, std::min(nFrames - 1, i + 1));
        synth.RenderFrame(cur, nxt, 220, buf.data());
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }

    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));
    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs  F0 帧 %zu\n", out.c_str(), pcm.size() / 44100.0, f0in.size());
    return 0;
}
