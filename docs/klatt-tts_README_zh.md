# 文本转语音（klatt-tts 官方 README 中文翻译）

> 原文：`micro/klatt-tts/README.md`（moonshine-ai/moonshine，MIT 许可）
> 这是一个**便携、零依赖的共振峰（Klatt 风格）语音合成核心**。

设计目标是「**机械但能听懂**」的语音，并且要塞进很小的资源预算里。
它用流式方式合成音频，以把内存占用压到最低。

核心是 C++17，**只用浮点运算、音频内循环不分配内存、没有任何第三方依赖**
（连 kissfft 和 TFLM 都不用）。

## 公开接口

只有一个公开入口头文件 `include/tts/tts.h`，只包含它即可：

```cpp
tts::VoiceParams voice = tts::DefaultVoiceParams();
tts::StreamSynth synth(voice, arena, arena_size);   // 由调用方提供内存池
tts::StreamOptions opts; opts.sample_rate = 22050.0f;
synth.BeginText("hello world", opts);               // 设备端 G2P
// 或 synth.BeginIpa("h\u0259lo\u028a", opts);      // 直接喂 IPA，跳过 G2P
float buf[256];
for (int n; (n = synth.Read(buf, 256)) > 0; )       // 流式输出，不缓存整句
  emit(buf, n);
```

`include/tts/` 下其余头文件（`synth_stream.h`、`config.h`、`klatt.h`、
`synth_internal.h`、`phonemes.h`、`g2p_dict.h`）都是 `StreamSynth`
按值暴露的传递类型；G2P 的内部实现放在 `src/` 里，不对外。

## 内存与算力

| 资源 | 大小 | 说明 |
| --- | --- | --- |
| Flash（词典） | 约 150 KiB | 烧录的常用词 G2P 表（`src/g2p_dict_data.h`） |
| Flash（代码） | 几十 KiB | Klatt 合成器 + G2P 规则（链进固件 `.text`） |
| RAM（静态） | 约 0 | 逐帧参数都放在调用方提供的内存池里 |
| RAM（内存池峰值） | 几 KiB | 复用语音识别结束后闲置的 TFLM 张量池 |
| 堆（临时） | 几 KiB | 只有 `BeginText` 里的分词器和段列表；`Read()` 里完全没有 |

`Read()` 一次只渲染一帧，**整句 PCM 从不整块缓存**。
识别和合成不会同时跑，所以合成器除了共享内存池之外，几乎**不占额外静态 SRAM**。

### 时延（250 MHz 主频）

| 操作 | 时延 | 算力（约） | 说明 |
| --- | --- | --- | --- |
| `StreamSynth` 念字母/数字 | 约 400–800ms 出声 | 约 0.5 MMAC（约 1 MMAC/秒输出） | 如 22.05kHz 下 "bee" 约 430ms，"zero" 约 830ms |
| `Read()` 内循环 | 快于实时 | 每 5ms 帧约 5 KMAC（约 1 MMAC/秒输出） | 6 个双二阶谐振器 × 每帧约 110 采样 |

`DefaultVoiceParams()` + 22.05kHz 下实测输出时长：
"ee" 约 320ms，"bee" 约 430ms，"seven" 约 760ms，"double u" 约 1.0 秒。

## 测试

`tests/tts_test.cc`（基于 TFLM 的 `micro_test.h`）覆盖：
G2P 前端（文本 → 音素 token、数字规范化）以及端到端的 `StreamSynth`
冒烟测试（输出非空、数值在范围内）。全部在主机上跑。

## 调参

语音参数以编译期默认值的形式写在 `DefaultVoiceParams()` 里
（`src/config.cc`）。桌面版构建另外支持用 `.cfg` 文件在运行时覆盖，
方便做听感测试；固件只用默认值。

## 附：这个引擎和我方项目的差异

| | klatt-tts 原版 | 我们这里 |
|---|---|---|
| 用途 | 语音播报（TTS） | 歌声合成（SVS） |
| 输入 | 英文文本 / IPA | 中文拼音、日语假名、MIDI 旋律 |
| 音高 | 固定句调 | 按音符 + 颤音 + 滑音 |
| 段结构 | 每个音素顺序排布 | 同左（这是我们目前最大的瓶颈） |
