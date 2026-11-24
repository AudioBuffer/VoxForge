// kt_dsl - 解析工程 DSL（ir.h）并渲染
//
// 支持：
//   [synthesizer_start] 歌词行：歌词,音素,时长ms,位置ms,特殊参数
//   [F0_start]          控制点：位置,时长,频率,特殊参数
//   特殊参数：TS(颤音力度) FP(共振峰) FI(强度) GI(滑音) VI(震动) BZCG(贝塞尔)
#include "ir.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "g2p.h"
#include "pitch_curve.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// "h/ao" 或 "ao" -> IPA
std::string phonemeToIpa(const std::string& ph) {
    std::string fin = ph, ini;
    const size_t slash = ph.find('/');
    if (slash != std::string::npos) {
        ini = ph.substr(0, slash);
        fin = ph.substr(slash + 1);
    }
    std::string out;
    if (!ini.empty()) {
        // 声母直接映射
        static const std::vector<std::pair<std::string, std::string>> kIni = {
            {"zh", "\u0288\u0282"}, {"ch", "\u0288\u0282\u02B0"}, {"sh", "\u0282"},
            {"b", "p"}, {"p", "p\u02B0"}, {"m", "m"}, {"f", "f"},
            {"d", "t"}, {"t", "t\u02B0"}, {"n", "n"}, {"l", "l"},
            {"g", "k"}, {"k", "k\u02B0"}, {"h", "x"},
            {"j", "t\u0255"}, {"q", "t\u0255\u02B0"}, {"x", "\u0255"},
            {"r", "\u027B"}, {"z", "ts"}, {"c", "ts\u02B0"}, {"s", "s"},
        };
        for (const auto& [k, v] : kIni)
            if (ini == k) { out += v; break; }
    }
    out += vx::pinyinToIpa(fin);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: kt_dsl <in.dsl> <out.wav> [声线]\n");
        return 2;
    }
    const std::string inPath = argv[1];
    const std::string outPath = argv[2];
    const std::string voiceName = (argc > 3) ? argv[3] : "female";

    // 读文件
    std::string src;
    if (FILE* f = std::fopen(inPath.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) src.append(buf, n);
        std::fclose(f);
    } else {
        std::fprintf(stderr, "打不开 %s\n", inPath.c_str());
        return 1;
    }

    vx::Project proj;
    std::string err;
    if (!proj.parseText(src, err)) {
        std::fprintf(stderr, "DSL 解析失败: %s\n", err.c_str());
        return 1;
    }
    std::printf("歌词 %zu 行，F0 %zu 点\n", proj.lyric.size(), proj.f0.size());

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName(voiceName), vp);
    vp.source_tilt_db = 8.0f;
    vp.formant_smooth_ms = 32.0f;
    vp.av_smooth_ms = 12.0f;

    // 拼音素序列
    std::vector<std::string> phones;
    for (const auto& l : proj.lyric)
        for (const auto& t : g2p::TokenizeIpa(phonemeToIpa(l.phoneme))) phones.push_back(t);
    if (phones.empty()) {
        std::fprintf(stderr, "没有可用音素（检查音素字段格式，例如 h/ao）\n");
        return 1;
    }

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const double totalMs = proj.totalMs();
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    const float scale = natural > 1.0f ? (float)(totalMs / natural) : 1.0f;
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

    // 音高：用 F0 控制点插值；没有则给默认
    std::vector<std::pair<double, double>> pts;   // (时间ms, Hz)
    for (const auto& p : proj.f0) {
        if (p.hz > 0 && p.durMs > 0)
            pts.push_back({p.posMs + p.durMs * 0.5, p.hz});
    }
    if (pts.empty()) pts.push_back({0.0, 220.0});

    // 颤音强度：取第一个 TS 参数
    double vibCents = 20.0;
    for (const auto& l : proj.lyric)
        if (l.sp.kind == 1) { vibCents = 20.0 * std::min(2.0, (double)l.sp.ts); break; }

    for (size_t i = 0; i < nFrames; ++i) {
        const double t = i * tts::synth_detail::kFrameMs;
        // 分段线性插值
        double hz = pts.front().second;
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            if (t >= pts[k].first && t <= pts[k + 1].first) {
                const double u = (t - pts[k].first) / std::max(1.0, pts[k + 1].first - pts[k].first);
                hz = pts[k].second + (pts[k + 1].second - pts[k].second) * u;
                break;
            }
            if (t > pts[k + 1].first) hz = pts[k + 1].second;
        }
        if (vibCents > 0.0) {
            const double depth = vibCents * std::min(1.0, t / 400.0);
            hz *= std::pow(2.0, depth * std::sin(2 * M_PI * 5.5 * t / 1000.0) / 1200.0);
        }
        f0[i] = (float)hz;
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
    vx::writeWav16(outPath, pcm, 44100);
    std::printf("%s  %.2fs  音素 %zu  F0 点 %zu  颤音 %.0f 音分\n", outPath.c_str(),
                pcm.size() / 44100.0, phones.size(), pts.size(), vibCents);
    return 0;
}
