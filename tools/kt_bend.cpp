// kt_bend - 真实音节的转音测试（短区间，非长滑音）
//   用法: kt_bend <out.wav> [音节] [起始Hz] [目标Hz] [转音ms] [类人度]
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "pitch_curve.h"
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
    const std::string out = (argc > 1) ? argv[1] : "/tmp/bend.wav";
    const std::string syl = (argc > 2) ? argv[2] : "a";
    const double hz0 = (argc > 3) ? std::atof(argv[3]) : 220.0;
    const double hz1 = (argc > 4) ? std::atof(argv[4]) : 247.0;   // 上一个小二度
    const double glideMs = (argc > 5) ? std::atof(argv[5]) : 45.0;
    const double humanity = (argc > 6) ? std::atof(argv[6]) : 1.0;

    const float fs = 44100.0f;
    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);

    auto toks = g2p::TokenizeIpa(vx::pinyinToIpa(syl));
    std::vector<std::string> phones(toks.begin(), toks.end());
    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    const float scale = natural > 1.0f ? (900.0f / natural) : 1.0f;  // 总长约 0.9 秒
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

    // 两个音：先在 hz0 保持，再转到 hz1（未滑满前继续哼）
    vx::PitchNote a;
    a.startSec = 0.0;
    a.durSec = 0.45;
    a.hz = hz0;
    a.portamentoMs = 0;
    a.vibratoCents = 0;
    a.humanity = humanity;

    vx::PitchNote b;
    b.startSec = 0.45;
    b.durSec = 0.45;
    b.hz = hz1;
    b.portamentoMs = glideMs;
    b.portamentoCurve = vx::Bezier::sharp();
    b.vibratoCents = 25.0;
    b.vibratoFadeMs = 150.0;
    b.humanity = humanity;

    auto curve = vx::renderPitchCurve({a, b}, 0.90, tts::synth_detail::kFrameMs);
    for (size_t i = 0; i < nFrames && i < curve.size(); ++i) f0[i] = curve[i];

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
    std::printf("%s  %.2fs  %s %.0f→%.0fHz 转音%.0fms 类人度%.1f\n", out.c_str(),
                pcm.size() / fs, syl.c_str(), hz0, hz1, glideMs, humanity);
    return 0;
}
