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

## Licensing / 许可证分区

This repository is a **dual-licensed aggregate**. Each path carries exactly one
license; the boundary is by directory, not by file.

| Path / 路径 | Contents / 内容 | License |
|---|---|---|
| `src/` | Core engine: G2P, formant DSP, pitch curve, IR/DSL, MIDI | **AGPL-3.0-or-later** |
| `third_party/klatt-tts/` | Klatt formant core (vendored) | MIT (upstream) |
| `third_party/tsf/` | TinySoundFont (vendored) | MIT (upstream) |
| `tools/` | CLI drivers: `kt_sing`, `kt_multi`, `kt_dsl`, `kt_lua`, `bench` … | **AGPL-3.0-or-later** |
| `scripts/` | Lua scripts and examples | **MIT** |
| `tests/` | Self-tests | **MIT** |
| `docs/` | Documentation | **MIT** |

**Rules / 规则**

1. Linking `src/` or `tools/` into a program makes that program subject to
   AGPL-3.0. 将 `src/` 或 `tools/` 链接进程序，该程序需遵循 AGPL-3.0。
2. Lua scripts, DSL files and user scores are **independent works** and may be
   distributed under any license, including proprietary.
   Lua 脚本、DSL 工程与用户乐谱属于**独立作品**，可用任意许可（含闭源）。
3. Vendored third-party code keeps its upstream MIT notice; those notices must
   be preserved in binary distributions.
   第三方代码保留原 MIT 声明，二进制分发时同样必须保留。

> ⚠️ `espeak-ng` is used **only** as an external comparison tool during
> development. It is GPLv3 and **must not be bundled** into any distribution.
> `espeak-ng` 仅作为开发期对比工具调用，禁止打包进发行物。

---

## Build / 编译

Requirements: C++17 compiler, LuaJIT headers (`/usr/include/luajit-2.1`).

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
