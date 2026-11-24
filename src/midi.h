// 标准 MIDI 文件（SMF）读写
//
// 只做歌唱合成需要的部分：
//   - 读 MThd（格式 0/1，division）
//   - 读 MTrk：delta-time、note on/off、tempo（0x51）、拍号、轨道名
//   - 写出单轨 MIDI（用于测试）
//
// 不依赖任何库，纯字节解析。
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vx {

struct MidiNote {
    int pitch = 60;        // 0-127
    double startSec = 0.0;
    double durSec = 0.5;
    int velocity = 96;
    int track = 0;         // 来源轨道号
};

struct MidiSong {
    double bpm = 120.0;
    std::vector<MidiNote> notes;
};

namespace midi_detail {

inline uint32_t readBE(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

inline uint32_t readVarLen(const std::vector<uint8_t>& d, size_t& pos) {
    uint32_t v = 0;
    while (pos < d.size()) {
        const uint8_t b = d[pos++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

// 把所有事件展开成「绝对 tick + 类型」，再统一换算成秒
struct RawNote {
    int pitch = 0, vel = 0;
    uint64_t onTick = 0, offTick = 0;
    int track = 0;
};

}  // namespace midi_detail

// 解析 .mid，返回按开始时间排序的音符
inline bool loadMidi(const std::string& path, MidiSong& song, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "打不开文件"; return false; }
    std::vector<uint8_t> d;
    uint8_t buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    if (d.size() < 14 || std::string((const char*)d.data(), 4) != "MThd") {
        err = "不是 MIDI 文件";
        return false;
    }
    const uint32_t hlen = midi_detail::readBE(d.data() + 4, 4);
    const uint32_t ntrk = midi_detail::readBE(d.data() + 10, 2);
    const uint32_t division = midi_detail::readBE(d.data() + 12, 2);
    double ticksPerQuarter = division & 0x8000 ? 96.0 : (double)division;
    if (ticksPerQuarter <= 0) ticksPerQuarter = 96.0;

    size_t pos = 8 + hlen;
    std::vector<midi_detail::RawNote> raw;
    std::vector<std::pair<uint64_t, double>> tempos;   // tick -> us/quarter
    tempos.push_back({0, 500000.0});

    for (uint32_t t = 0; t < ntrk && pos + 8 <= d.size(); ++t) {
        if (std::string((const char*)d.data() + pos, 4) != "MTrk") break;
        const uint32_t len = midi_detail::readBE(d.data() + pos + 4, 4);
        size_t p = pos + 8;
        const size_t end = std::min(d.size(), p + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        struct Open { int pitch = -1; int vel = 0; uint64_t tick = 0; };
        std::vector<Open> open(16);

        while (p < end) {
            tick += midi_detail::readVarLen(d, p);
            if (p >= end) break;
            uint8_t st = d[p];
            if (st & 0x80) { ++p; running = st; }
            else st = running;
            if (st == 0) break;

            const uint8_t type = st & 0xF0;
            const int ch = st & 0x0F;
            if (type == 0x90 || type == 0x80) {
                if (p + 2 > end) break;
                const int pitch = d[p], vel = d[p + 1];
                p += 2;
                if (type == 0x90 && vel > 0) {
                    open[(size_t)ch] = {pitch, vel, tick};
                } else if (open[(size_t)ch].pitch == pitch) {
                    midi_detail::RawNote rn;
                    rn.track = (int)t;
                    rn.pitch = pitch;
                    rn.vel = open[(size_t)ch].vel;
                    rn.onTick = open[(size_t)ch].tick;
                    rn.offTick = std::max(tick, rn.onTick + 1);
                    raw.push_back(rn);
                    open[(size_t)ch].pitch = -1;
                }
            } else if (type == 0xA0 || type == 0xB0 || type == 0xE0) {
                p += 2;
            } else if (type == 0xC0 || type == 0xD0) {
                p += 1;
            } else if (st == 0xFF) {
                if (p >= end) break;
                const uint8_t meta = d[p++];
                const uint32_t mlen = midi_detail::readVarLen(d, p);
                if (meta == 0x51 && mlen == 3 && p + 3 <= end) {
                    const double usq = (d[p] << 16) | (d[p + 1] << 8) | d[p + 2];
                    tempos.push_back({tick, usq});
                }
                p += mlen;
            } else if (st == 0xF0 || st == 0xF7) {
                const uint32_t slen = midi_detail::readVarLen(d, p);
                p += slen;
            } else {
                ++p;
            }
        }
        pos = end;
    }

    // tick -> 秒
    std::sort(tempos.begin(), tempos.end());
    auto tickToSec = [&](uint64_t tick) {
        double sec = 0.0;
        uint64_t lastTick = 0;
        double usq = 500000.0;
        for (const auto& [tk, val] : tempos) {
            if (tk == 0) { usq = val; continue; }
            if (tk > tick) break;
            sec += (double)(tk - lastTick) * usq / 1e6 / ticksPerQuarter;
            lastTick = tk;
            usq = val;
        }
        sec += (double)(tick - lastTick) * usq / 1e6 / ticksPerQuarter;
        return sec;
    };

    song.notes.clear();
    for (const auto& r : raw) {
        MidiNote m;
        m.track = r.track;
        m.pitch = r.pitch;
        m.velocity = r.vel;
        m.startSec = tickToSec(r.onTick);
        m.durSec = std::max(0.02, tickToSec(r.offTick) - m.startSec);
        song.notes.push_back(m);
    }
    std::sort(song.notes.begin(), song.notes.end(),
              [](const MidiNote& a, const MidiNote& b) { return a.startSec < b.startSec; });
    if (song.notes.empty()) { err = "MIDI 里没有音符"; return false; }
    return true;
}

// 写一个简单的单轨 MIDI（测试用）
inline bool writeMidi(const std::string& path, const std::vector<MidiNote>& notes, int bpm = 120) {
    const int tpq = 480;
    auto varLen = [](uint32_t v) {
        std::vector<uint8_t> out;
        out.push_back(v & 0x7F);
        v >>= 7;
        while (v) { out.push_back(0x80 | (v & 0x7F)); v >>= 7; }
        std::reverse(out.begin(), out.end());
        return out;
    };

    std::vector<uint8_t> trk;
    auto push = [&](const std::vector<uint8_t>& b) { trk.insert(trk.end(), b.begin(), b.end()); };
    auto meta = [&](uint8_t type, const std::vector<uint8_t>& data) {
        trk.push_back(0x00); trk.push_back(0xFF); trk.push_back(type);
        push(varLen((uint32_t)data.size()));
        push(data);
    };
    {
        const uint32_t usq = (uint32_t)(60000000.0 / bpm);
        meta(0x51, {(uint8_t)(usq >> 16), (uint8_t)(usq >> 8), (uint8_t)usq});
        meta(0x58, {4, 2, 24, 8});
    }
    for (const auto& n : notes) {
        const uint32_t on = (uint32_t)(n.startSec * bpm / 60.0 * tpq);
        const uint32_t off = (uint32_t)((n.startSec + n.durSec) * bpm / 60.0 * tpq);
        // 每个音符单独写（不排序合并，够用）
        push({0x00, 0x90, (uint8_t)n.pitch, (uint8_t)n.velocity});
        const uint32_t d = off > on ? off - on : 1;
        push(varLen(d));
        push({0x80, (uint8_t)n.pitch, 0x00});
        (void)on;
    }
    push({0x00, 0xFF, 0x2F, 0x00});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint8_t hdr[14] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                             (uint8_t)(tpq >> 8), (uint8_t)(tpq & 0xFF)};
    std::fwrite(hdr, 1, 14, f);
    const uint8_t tl[8] = {'M', 'T', 'r', 'k',
                           (uint8_t)(trk.size() >> 24), (uint8_t)(trk.size() >> 16),
                           (uint8_t)(trk.size() >> 8), (uint8_t)(trk.size() & 0xFF)};
    std::fwrite(tl, 1, 8, f);
    if (!trk.empty()) std::fwrite(trk.data(), 1, trk.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace vx
