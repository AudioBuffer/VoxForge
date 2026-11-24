// kt_siren - 单音节 + 自定义音高曲线（↗↘ 摇摆测试）
//   用法: kt_siren <out.wav> [音节] [时长秒] [深度音分] [频率Hz]
#include "pinyin_ipa.h"
#include "voice.h"
#include "g2p.h"
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
    const std::string out = (argc > 1) ? argv[1] : "/tmp/siren.wav";
    const std::string syl = (argc > 2) ? argv[2] : "a";
    const double dur = (argc > 3) ? std::atof(argv[3]) : 4.0;
    const double cents = (argc > 4) ? std::atof(argv[4]) : 400.0;
    const double rate = (argc > 5) ? std::atof(argv[5]) : 5.0;
    const double base = (argc > 6) ? std::atof(argv[6]) : 220.0;

    const float fs = 44100.0f;
    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);

    // 音节拉长到目标时长
    auto toks = g2p::TokenizeIpa(vx::pinyinToIpa(syl));
    std::vector<std::string> phones(toks.begin(), toks.end());
    phones.push_back(" ");
    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    const float scale = natural > 1.0f ? (float)(dur * 1000.0 / natural) : 1.0f;
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

    // 摇摆音高：±cents 音分，rate Hz
    for (size_t i = 0; i < nFrames; ++i) {
        const double t = i * tts::synth_detail::kFrameMs / 1000.0;
        const double c = cents * std::sin(2 * M_PI * rate * t);
        f0[i] = (float)(base * std::pow(2.0, c / 1200.0));
    }

    tts::KlattSynth synth(fs, tts::synth_detail::MakeKlattParams(vp));
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

    vx::writeWav16(out, pcm, (int)fs);

    // 同步导出 f0
    std::string f0path = out + ".f0";
    if (FILE* ff = std::fopen(f0path.c_str(), "wb")) {
        std::fwrite(f0.data(), sizeof(float), f0.size(), ff);
        std::fclose(ff);
    }
    std::printf("%s  %.2fs  %zu 帧  摆动 ±%.0f 音分 @ %.1fHz\n", out.c_str(),
                pcm.size() / fs, nFrames, cents, rate);
    return 0;
}
