# VoxForgeSynth-101

**English** | [简体中文](#简体中文)

A hybrid **SVS (Singing Voice Synthesis)** engine. It combines classical
DSP-based formant synthesis with a Mel-domain diffusion stage, and exposes an
extensible Lua control layer.

> 混合式歌声合成引擎：传统共振峰 DSP + 梅尔域扩散模型 + Lua 扩展层。

---

## Architecture / 架构

```
 score (MIDI / DSL / Lua)
            │
            ▼
   ┌──────────────────┐
   │ G2P  (zh/ja/en)  │  pinyin / kana → phonemes
   └──────────────────┘
            │
            ▼
   ┌──────────────────┐
   │ Formant DSP      │  Klatt-style cascade, 6 formants
   │ (Klatt core)     │  + pitch curve, air, breath
   └──────────────────┘
            │  128-bin log-Mel  (+ f0)
            ▼
   ┌──────────────────┐
   │ Mel diffusion    │  residual / refinement in latent space
   └──────────────────┘
            │
            ▼
   ┌──────────────────┐
   │ Vocoder          │  NSF-HiFiGAN / iSTFT
   └──────────────────┘
            │
            ▼
        48 kHz audio
```

The DSP stage runs at **~95× real-time** on an ARM64 phone (RTF 0.011);
all neural stages are optional and can be disabled for fully offline operation.

---

## Licensing / 许可证分区 —— 单程序双协议聚合体（SPDA）

*Single-Program Dual-License Aggregate (SPDA)*

本仓库的协议边界**不是目录，而是进程**。引擎被切成两个独立进程，只通过
IPC（文本 IR + 共享内存环形缓冲）通信，**没有 DSO 链接**。

The license boundary follows **processes, not folders**. The engine is split
into two processes that communicate only via IPC (text IR over stdin, PCM over
a lock-free shared-memory ring). There is **no shared-library linking**.

```
┌──────────────────────┐        IR (stdin)        ┌──────────────────────┐
│  Frontend  (MIT)     │ ───────────────────────► │  Backend  (AGPL-3.0) │
│  Lua, UI, DSL/score  │ ◄─────────────────────── │  DSP, formants, IR   │
└──────────────────────┘  PCM (shared memory)     └──────────────────────┘
```

| Component / 组件 | Path | Process | License |
|---|---|---|---|
| Lua control layer, scripts | `scripts/`, `tools/kt_lua.cpp` | frontend | **MIT** |
| Score / DSL / IR formatting | `src/ir.h`, `src/dsl_*` | frontend | **MIT** |
| Frontend driver | `src/frontend/` | frontend | **MIT** |
| Formant engine, G2P, pitch, MIDI | `src/core/`, `src/backend/` | backend | **AGPL-3.0-or-later** |
| CLI batch tools | `tools/kt_sing`, `kt_multi`, `kt_dsl`, `bench` | backend | **AGPL-3.0-or-later** |
| Vendored Klatt core | `third_party/klatt-tts/` | backend | MIT (upstream) |
| TinySoundFont | `third_party/tsf/` | backend | MIT (upstream) |

**Rules / 规则**

1. The two processes exchange only bytes (IR text, PCM), never function calls.
   两个进程只交换字节，不共享地址空间、不互相链接。
2. Because of that isolation, the AGPL backend **does not extend** to the MIT
   frontend or to user scores — this is the "aggregate" in SPDA.
   正因为隔离，AGPL 后端**不传染** MIT 前端与用户乐谱，这就是「聚合体」的含义。
3. Vendored MIT notices must be preserved in binary distributions.
   第三方 MIT 声明在二进制分发时必须保留。
4. `espeak-ng` (GPLv3) is a development-time comparison tool only and
   **must not be bundled**. `espeak-ng` 仅开发期使用，禁止打包。

---

## Build / 编译

Cross-platform: **Windows · Linux · Android (Termux) · macOS**.
跨平台：**Windows · Linux · Android · macOS**，单一核心源码。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

核心库 `vfcore` 只用标准库；POSIX 相关的 IPC 双进程只在 Unix 上构建。
The `vfcore` library is standard-library only; the IPC pair builds on Unix only.

### Manual / 手编（无 CMake 时）

```bash
# Full engine (DSP + Lua)
g++ -O2 -std=c++17 -Isrc \
    -Ithird_party/klatt-tts/include \
    -Ithird_party/klatt-tts/include/tts \
    -Ithird_party/klatt-tts/include/g2p \
    -Ithird_party/tsf \
    -I/usr/include/luajit-2.1 \
    -o kt_lua tools/kt_lua.cpp third_party/klatt-tts/src/*.cc \
    -lluajit-5.1 -lm
```

---

## Quick start / 快速上手

### Lua (easiest)

```lua
synth.voice("female")
synth.set{ tilt = 8, open = 0.40, auto = true }

-- singing: syllable:pitch
synth.phrase{ text = "ni:262 hao:294 shi:330 jie:349", dur = 400 }

-- instrument (needs a SoundFont)
synth.instrument{ sf = "TimGM6mb.sf2", program = 0, notes = "C4:0.4 E4:0.4 G4:0.4" }

synth.save("out.wav")
```

```bash
./kt_lua scripts/demo.lua
```

### MIDI

```bash
./kt_multi out.wav song.mid lyrics.txt female,male
```

### Project DSL

```
{
[synthesizer_start]
[歌词,歌词音素切分,发音时长,发音位置,特殊参数]
[你,n/i,450,10,<"TS,1">]
[好,h/ao,450,470,NaN]
[synthesizer_end]
}
```

```bash
./kt_lint in.dsl -o checked.dsl   # validate, warnings appended as >W:...<
./kt_dsl  in.dsl out.wav female   # render
```

---

## Features / 已实现

| Feature | Status |
|---|---|
| Chinese G2P (44 440 chars, polyphones) | ✅ |
| Japanese kana → phonemes | ✅ |
| English IPA passthrough | ✅ |
| Phoneme inventory (56 symbols) | ✅ 0 missing across zh/ja/en |
| Formant DSP (Klatt, LF source) | ✅ 95× real-time |
| Portamento / vibrato / Bezier easing | ✅ |
| "Humanity" slider (0 = straight, 1 = human) | ✅ |
| Auto pitch line (OpenUtau style) | ✅ |
| Whisper / breath / inhale-exhale | ✅ |
| MIDI multi-track, per-track voice | ✅ |
| SF2 instruments (136 presets) | ✅ |
| DSL validation (`>W:` annotations) | ✅ |
| Mel diffusion / residual model | 🚧 designed, not trained |
| GUI | 🚧 TUI only |

---

## Known limitations / 已知限制

- The DSP stage sounds *robotic but intelligible*; timbre realism requires the
  planned diffusion stage.
- Very fast singing (< 120 ms per syllable) exceeds the segment model and
  produces artifacts.
- Mel→vocoder round-trips must use **librosa-compatible** Mel
  (slaney scale, `center=True`, `power=2.0`), otherwise timbre shifts.

---

## Acknowledgements / 致谢

- Formant core: [moonshine-ai/moonshine](https://github.com/moonshine-ai/moonshine) (`micro/klatt-tts`, MIT)
- SoundFont playback: [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIT)
- Pinyin data: [mozillazg/pinyin-data](https://github.com/mozillazg/pinyin-data),
  [phrase-pinyin-data](https://github.com/mozillazg/phrase-pinyin-data) (MIT)
- Transition and pitch-line ideas inspired by [OpenUtau](https://github.com/stakira/OpenUtau)

---

## 简体中文

**VoxForgeSynth-101** 是一个混合式歌声合成引擎：用经典的共振峰 DSP 打底，
再用梅尔域扩散模型补音色，控制层用 Lua 扩展。

### 许可证（重要）

本仓库是**双协议聚合体**，按目录划分：

| 路径 | 内容 | 协议 |
|---|---|---|
| `src/` | 核心引擎（G2P、共振峰合成、音高曲线、DSL/MIDI） | **AGPL-3.0-or-later** |
| `tools/` | 命令行工具 | **AGPL-3.0-or-later** |
| `scripts/` | Lua 脚本与示例 | **MIT** |
| `tests/` `docs/` | 测试与文档 | **MIT** |
| `third_party/` | 第三方库（各自原协议） | MIT |

**要点**：

1. 把 `src/`、`tools/` 链接进程序 → 整个程序需遵守 AGPL-3.0
2. Lua 脚本、DSL 工程、用户乐谱是**独立作品**，可以闭源分发
3. 第三方 MIT 代码的版权声明必须保留
4. **禁止**把 `espeak-ng`（GPLv3，仅开发期用它做对比）打包进发行物

### 编译与使用

见上文的 Build 与 Quick start 章节，命令完全一致。

### 设计要点

- 纯 DSP 部分在 ARM64 手机上约 **95 倍实时**（RTF 0.011）
- 神经声码器与扩散模型都是**可选**的，关掉也能完整运行
- 参数是显式的（共振峰频率/带宽/音高/气声），不是黑盒
- 未来残差模型只在 **30 维参数空间**工作，算力开销与频谱维度无关
