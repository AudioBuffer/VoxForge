// 16bit 单声道 WAV 写出（只依赖标准库）
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vx {

inline bool writeWav16(const std::string& path, const std::vector<float>& pcm, int sr) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t n = (uint32_t)pcm.size();
    const uint32_t dataSize = n * 2;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(1);
    w32((uint32_t)sr); w32((uint32_t)sr * 2); w16(2); w16(16);
    std::fwrite("data", 1, 4, f); w32(dataSize);
    std::vector<int16_t> buf(n);
    for (uint32_t i = 0; i < n; ++i) {
        const float v = pcm[i] < -1.0f ? -1.0f : (pcm[i] > 1.0f ? 1.0f : pcm[i]);
        buf[i] = (int16_t)(v * 32767.0f);
    }
    std::fwrite(buf.data(), 2, n, f);
    std::fclose(f);
    return true;
}

}  // namespace vx
