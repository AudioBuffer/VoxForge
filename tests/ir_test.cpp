// IR 解析自测：用用户给的真实示例
#include "ir.h"
#include <cstdio>
#include <string>

int main() {
    const std::string sample = R"({
[synthesizer_start]
[歌词,歌词音素切分,发音时长,发音位置,特殊参数]
[ni,ni,1000,10,<"TS,1""FP,'F1,50''F2,25''F3,89''F4,59''F5,87''F6,78'">]
[hao,h/ao,1000,1020,NaN]
[synthesizer_end]
}
{
[F0_start]
["按毫秒计的控制点起始位置,按秒计的控制点起始位置","按毫秒计的控制点持续时间,按秒计的控制点持续时间","按hz的频率信息,按khz的频率信息",<可选的特殊参数>]
["10,0.01","1000,1","1030,1.03",NaN]
["1020,1.02","500,0.5","980,0.98",<"GI,'440''880'">]
[F0_end]
}
)";
    vx::Project p;
    std::string err;
    if (!p.parseText(sample, err)) { std::printf("FAIL %s\n", err.c_str()); return 1; }

    std::printf("歌词 %zu 行, F0 %zu 点\n", p.lyric.size(), p.f0.size());
    for (const auto& l : p.lyric)
        std::printf("  %s / %s  dur=%.0fms pos=%.0fms special=%d\n",
                    l.lyric.c_str(), l.phoneme.c_str(), l.durMs, l.posMs, l.sp.kind);
    for (const auto& f : p.f0)
        std::printf("  F0 pos=%.0fms dur=%.0fms hz=%.1f special=%d\n",
                    f.posMs, f.durMs, f.hz, f.sp.kind);

    // 二进制往返
    auto bin = p.saveBinary();
    vx::Project q;
    if (!q.loadBinary(bin, err)) { std::printf("FAIL 二进制 %s\n", err.c_str()); return 1; }
    if (q.lyric.size() != p.lyric.size() || q.f0.size() != p.f0.size()) {
        std::printf("FAIL 二进制往返数量不一致\n");
        return 1;
    }
    std::printf("二进制 %zu 字节，往返一致\n", bin.size());
    std::printf("PASS\n");
    return 0;
}
