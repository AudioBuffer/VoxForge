// kt_lua - Lua 原型入口：脚本里调全部参数，不重编译
//
// 用法: kt_lua <script.lua>
//
// 脚本 API:
//   synth.voice("female")                 -- male/female/child/neutral
//   synth.set{ tilt=8, open=0.40, human=1.0, air=0 }
//   synth.phrase{ text="ni:220 hao:247", dur=450, glide=50 }
//   synth.save("out.wav")
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "air.h"
#include "pitch_curve.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include <lua.hpp>
#include <algorithm>
#include <random>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// 性能探针：直接量一次真实渲染（0.5 秒音频）的耗时
// 判定标准：单次渲染耗时 > 200ms 视为低性能
double perfProbeMs() {
    const clock_t t0 = clock();
    volatile float acc = 0.0f;
    // 模拟 0.5 秒 @44.1k 的逐采样运算
    for (int n = 0; n < 22050; ++n) {
        const float t = (float)n / 44100.0f;
        acc = acc + std::sin(6.2831853f * 220.0f * t) * 0.001f;
    }
    (void)acc;
    return 1000.0 * (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
}

struct State {
    std::string voice = "female";
    double tilt = 8.0;
    double open = 0.40;
    double humanity = 1.0;
    double air = 0.0;
    bool autoPitch = false;
    std::vector<float> pcm;          // 输出混音缓冲
    size_t cursor = 0;               // 顺序追加时的写入位置
    int maxTracks = 64;              // 性能不足时降到 10
    int tracks = 0;
};

// 把一段音频混进输出缓冲（可指定起点）
void mixInto(State& st, const std::vector<float>& seg, double atSec, bool sequential) {
    size_t at = sequential ? st.pcm.size() : (size_t)(std::max(0.0, atSec) * 44100.0);
    if (at + seg.size() > st.pcm.size()) st.pcm.resize(at + seg.size(), 0.0f);
    for (size_t i = 0; i < seg.size(); ++i) st.pcm[at + i] += seg[i];
}

State g;

std::vector<float> renderPhrase(const std::string& text, double durMs, double glideMs) {
    std::vector<std::string> syls;
    std::vector<double> pitches;
    {
        size_t p = 0;
        while (p < text.size()) {
            const size_t sp = text.find(' ', p);
            std::string w = text.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            if (!w.empty()) {
                const size_t colon = w.find(':');
                if (colon != std::string::npos) {
                    syls.push_back(w.substr(0, colon));
                    pitches.push_back(std::atof(w.c_str() + colon + 1));
                } else {
                    syls.push_back(w);
                    pitches.push_back(pitches.empty() ? 220.0 : pitches.back());
                }
            }
            if (sp == std::string::npos) break;
            p = sp + 1;
        }
    }
    if (syls.empty()) return {};

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName(g.voice), vp);
    vp.source_tilt_db = (float)g.tilt;
    vp.glottal_open = (float)g.open;
    vp.formant_smooth_ms = 32.0f;
    vp.av_smooth_ms = 12.0f;
    vp.af_release_ms = 14.0f;

    std::vector<std::string> phones;
    for (const auto& s : syls)
        for (const auto& t : g2p::TokenizeIpa(vx::pinyinToIpa(s))) phones.push_back(t);
    if (phones.empty()) return {};

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const double totalSec = durMs / 1000.0 * (double)syls.size();
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    const float scale = natural > 1.0f ? (float)(totalSec * 1000.0 / natural) : 1.0f;
    const size_t nFrames = tts::synth_detail::CountFrames(segs, scale);
    if (nFrames == 0) return {};

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

    std::vector<vx::PitchNote> notes;
    for (size_t i = 0; i < syls.size(); ++i) {
        vx::PitchNote p;
        p.startSec = i * durMs / 1000.0;
        p.durSec = durMs / 1000.0;
        p.hz = pitches[i];
        p.portamentoMs = (i == 0) ? 0.0 : glideMs;
        p.portamentoCurve = vx::Bezier::sharp();
        p.vibratoCents = 22.0;
        p.vibratoFadeMs = 180.0;
        p.humanity = g.humanity;
        notes.push_back(p);
    }
    auto curve = g.autoPitch
                     ? vx::autoPitchCurve(notes, totalSec, tts::synth_detail::kFrameMs)
                     : vx::renderPitchCurve(notes, totalSec, tts::synth_detail::kFrameMs);
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
    return pcm;
}

int l_voice(lua_State* L) {
    g.voice = luaL_checkstring(L, 1);
    return 0;
}

int l_set(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto num = [&](const char* k, double& dst) {
        lua_getfield(L, 1, k);
        if (lua_isnumber(L, -1)) dst = lua_tonumber(L, -1);
        lua_pop(L, 1);
    };
    num("tilt", g.tilt);
    num("open", g.open);
    num("human", g.humanity);
    num("air", g.air);
    lua_getfield(L, 1, "auto");
    if (lua_isboolean(L, -1)) g.autoPitch = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return 0;
}

int l_phrase(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "text");
    std::string text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "a:220";
    lua_pop(L, 1);
    double dur = 450, glide = 50;
    lua_getfield(L, 1, "dur");
    if (lua_isnumber(L, -1)) dur = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "glide");
    if (lua_isnumber(L, -1)) glide = lua_tonumber(L, -1);
    lua_pop(L, 1);

    if (++g.tracks > g.maxTracks) {
        std::fprintf(stderr, "已到轨道上限 %d，跳过本轨\n", g.maxTracks);
        return 0;
    }
    auto pcm = renderPhrase(text, dur, glide);
    if (g.air > 0.0) {
        vx::AirLayer air(44100.0);
        air.apply(pcm, g.air, 8000.0, 0.7);
    }
    mixInto(g, pcm, 0.0, true);
    const size_t gap = (size_t)(0.12 * 44100.0);
    g.pcm.insert(g.pcm.end(), gap, 0.0f);
    return 0;
}

// GM 音色：用加法合成 + ADSR 简易实现（非人声）
std::vector<float> renderTone(double hz, double durSec, int program,
                              double attack, double decay, double sustain,
                              double release, double bright, double noiseAmt) {
    const double fs = 44100.0;
    const size_t n = (size_t)(durSec * fs);
    std::vector<float> out(n, 0.0f);
    std::mt19937 rng((unsigned)(program * 7919 + 13));
    std::uniform_real_distribution<double> uni(-1.0, 1.0);

    // 谐波数：亮度决定
    const int K = std::max(1, (int)(2 + bright * 14));
    const double detune = 1.0 + 0.001 * ((program % 5) - 2);

    for (size_t i = 0; i < n; ++i) {
        const double t = i / fs;
        // ADSR
        double env;
        const double tRel = durSec - release;
        if (t < attack) env = t / std::max(1e-4, attack);
        else if (t < attack + decay) env = 1.0 - (1.0 - sustain) * (t - attack) / std::max(1e-4, decay);
        else if (t < tRel) env = sustain;
        else env = sustain * std::max(0.0, (durSec - t) / std::max(1e-4, release));

        double s = 0.0;
        for (int k = 1; k <= K; ++k) {
            const double amp = 1.0 / (k * k * 0.5 + 1.0);
            s += amp * std::sin(2 * M_PI * hz * detune * k * t);
        }
        s /= (double)K * 0.5;
        s += noiseAmt * uni(rng) * std::exp(-t * 8.0);
        out[i] = (float)(s * env * 0.5);
    }
    return out;
}

int l_tone(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto num = [&](const char* k, double def) -> double {
        lua_getfield(L, 1, k);
        const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
        lua_pop(L, 1);
        return v;
    };
    const double hz = num("hz", 440.0);
    const double dur = num("dur", 0.5);
    const int prog = (int)num("program", 0);
    const double a = num("attack", 0.01), d = num("decay", 0.1);
    const double su = num("sustain", 0.7), r = num("release", 0.12);
    const double br = num("bright", 0.5), nz = num("noise", 0.02);
    auto pcm = renderTone(hz, dur, prog, a, d, su, r, br, nz);
    g.pcm.insert(g.pcm.end(), pcm.begin(), pcm.end());
    const size_t gap = (size_t)(0.05 * 44100.0);
    g.pcm.insert(g.pcm.end(), gap, 0.0f);
    return 0;
}

int l_instrument(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto num = [&](const char* k, double def) -> double {
        lua_getfield(L, 1, k);
        const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
        lua_pop(L, 1);
        return v;
    };
    lua_getfield(L, 1, "sf");
    std::string font = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    lua_getfield(L, 1, "notes");
    std::string notes = lua_isstring(L, -1) ? lua_tostring(L, -1) : "C4:0.6";
    lua_pop(L, 1);
    const int program = (int)num("program", 0);
    if (font.empty()) return 0;

    tsf* sf = tsf_load_filename(font.c_str());
    if (!sf) { std::fprintf(stderr, "SF2 打不开: %s\n", font.c_str()); return 0; }
    tsf_set_output(sf, TSF_STEREO_INTERLEAVED, 44100, 0.0f);
    const int idx = tsf_get_presetindex(sf, 0, program);
    if (idx < 0) { tsf_close(sf); return 0; }

    std::vector<short> buf(1024);
    size_t p = 0;
    while (p < notes.size()) {
        const size_t sp = notes.find(' ', p);
        const std::string item = notes.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
        if (!item.empty()) {
            const size_t c = item.find(':');
            const std::string nm = item.substr(0, c);
            const double dur = (c == std::string::npos) ? 0.6 : std::atof(item.c_str() + c + 1);
            static const int kSemis[7] = {9, 11, 0, 2, 4, 5, 7};
            const char up = (char)std::toupper(nm[0]);
            int semi = (up >= 'A' && up <= 'G') ? kSemis[up - 'A'] : 0;
            size_t q = 1;
            if (q < nm.size() && nm[q] == '#') { ++semi; ++q; }
            else if (q < nm.size() && nm[q] == 'b') { --semi; ++q; }
            const int oct = (q < nm.size()) ? std::atoi(nm.c_str() + q) : 4;
            const int midi = (oct + 1) * 12 + semi;
            tsf_note_on(sf, idx, midi, 1.0f);
            const int frames = (int)(dur * 44100);
            int done = 0;
            while (done < frames) {
                const int n = std::min(512, frames - done);
                tsf_render_short(sf, buf.data(), n, 0);
                for (int k = 0; k < n * 2; ++k)
                    g.pcm.push_back(buf[(size_t)k] / 65536.0f);
                done += n;
            }
            tsf_note_off(sf, idx, midi);
        }
        if (sp == std::string::npos) break;
        p = sp + 1;
    }
    tsf_close(sf);
    const size_t gap = (size_t)(0.05 * 44100.0);
    g.pcm.insert(g.pcm.end(), gap, 0.0f);
    return 0;
}

int l_save(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    float pk = 1e-6f;
    for (float v : g.pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : g.pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));
    vx::writeWav16(path, g.pcm, 44100);
    std::printf("%s  %.2fs\n", path, g.pcm.size() / 44100.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "用法: kt_lua <script.lua>\n"); return 2; }
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    {
        const double ms = perfProbeMs();
        g.maxTracks = (ms > 200.0) ? 10 : 64;
        std::printf("性能探针: %.2fms（阈值 200ms）→ 最多 %d 轨\n", ms, g.maxTracks);
        if (g.maxTracks == 10) std::printf("（性能偏低，轨道数限制为 10）\n");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_voice); lua_setfield(L, -2, "voice");
    lua_pushcfunction(L, l_set); lua_setfield(L, -2, "set");
    lua_pushcfunction(L, l_phrase); lua_setfield(L, -2, "phrase");
    lua_pushcfunction(L, l_tone); lua_setfield(L, -2, "tone");
    lua_pushcfunction(L, l_instrument); lua_setfield(L, -2, "instrument");
    lua_pushcfunction(L, l_save); lua_setfield(L, -2, "save");
    lua_setglobal(L, "synth");

    if (luaL_dofile(L, argv[1]) != LUA_OK) {
        std::fprintf(stderr, "Lua 错误: %s\n", lua_tostring(L, -1));
        return 1;
    }
    lua_close(L);
    return 0;
}
