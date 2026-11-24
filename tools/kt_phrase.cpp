// kt_phrase - 两三个音节的连唱 + 转音，辅音到元音平滑
//   用法: kt_phrase <out.wav> "ni hao" [起始Hz] [目标Hz] [转音ms] [类人度]
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "pitch_curve.h"
#include "air.h"
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
#include <cstdlib>

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "/tmp/phrase.wav";
    // 文本：每个音节可写成 "ni" 或 "ni:220"（带音高）
    const std::string text = (argc > 2) ? argv[2] : "ni:220 hao:247";
    const double durMs = (argc > 3) ? std::atof(argv[3]) : 450.0;      // 每音节时长
    const double glideMs = (argc > 4) ? std::atof(argv[4]) : 50.0;     // 转音时长
    const double humanity = (argc > 5) ? std::atof(argv[5]) : 1.0;     // 类人度

    std::vector<std::string> syls;
    std::vector<double> pitches;   // 0 表示未指定
    {
        size_t p = 0;
        while (p < text.size()) {
            const size_t sp = text.find(' ', p);
            const std::string w = text.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            if (!w.empty()) {
                const size_t colon = w.find(':');
                if (colon != std::string::npos) {
                    syls.push_back(w.substr(0, colon));
                    pitches.push_back(std::atof(w.c_str() + colon + 1));
                } else {
                    syls.push_back(w);
                    pitches.push_back(0.0);
                }
            }
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
    }
    if (syls.empty()) return 1;

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);
    // 辅音→元音更顺：加大共振峰与包络的平滑
    // 第 7/8 参数：谱倾斜(dB) 与 开商
    if (argc > 7) vp.source_tilt_db = (float)std::atof(argv[7]);
    if (argc > 8) vp.glottal_open = (float)std::atof(argv[8]);
    vp.formant_smooth_ms = 32.0f;
    vp.av_smooth_ms = 12.0f;
    vp.af_release_ms = 14.0f;

    // 所有音素拼成一条序列（音节间不留停顿）
    std::vector<std::string> phones;
    std::vector<int> phoneSyl;
    for (size_t i = 0; i < syls.size(); ++i) {
        for (const auto& t : g2p::TokenizeIpa(vx::pinyinToIpa(syls[i]))) {
            phones.push_back(t);
            phoneSyl.push_back((int)i);
        }
    }
    if (phones.empty()) return 1;

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const double totalSec = durMs / 1000.0 * (double)syls.size();
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
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

    // 每个音节一个音高，之间用短转音
    std::vector<vx::PitchNote> notes;
    for (size_t i = 0; i < syls.size(); ++i) {
        vx::PitchNote p;
        p.startSec = i * durMs / 1000.0;
        p.durSec = durMs / 1000.0;
        p.hz = (pitches[i] > 0.0) ? pitches[i]
                                  : (pitches[0] > 0.0 ? pitches[0] : 220.0);
        p.portamentoMs = (i == 0) ? 0.0 : glideMs;
        p.portamentoCurve = vx::Bezier::sharp();
        p.vibratoCents = 22.0;
        p.vibratoFadeMs = 180.0;
        p.humanity = humanity;
        notes.push_back(p);
    }
    auto curve = vx::renderPitchCurve(notes, totalSec, tts::synth_detail::kFrameMs);
    for (size_t i = 0; i < nFrames && i < curve.size(); ++i) f0[i] = curve[i];

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

    // 空气感：高频气声层（可用第 6 个参数调整，默认 0.035）
    const double air = (argc > 6) ? std::atof(argv[6]) : 0.0;
    vx::AirLayer airLayer(44100.0);
    airLayer.apply(pcm, air, 8000.0, 0.7);

    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));
    vx::writeWav16(out, pcm, (int)44100.0f);

    std::string f0path = out + ".f0";
    if (FILE* ff = std::fopen(f0path.c_str(), "wb")) {
        std::fwrite(f0.data(), sizeof(float), f0.size(), ff);
        std::fclose(ff);
    }
    std::printf("%s  %.2fs  「%s」 每音%.0fms 转音%.0fms 类人度%.1f\n", out.c_str(),
                pcm.size() / 44100.0, text.c_str(), durMs, glideMs, humanity);
    for (size_t i = 0; i < syls.size(); ++i)
        std::printf("  %zu: %s %.0fHz\n", i + 1, syls[i].c_str(), pitches[i]);
    return 0;
}
