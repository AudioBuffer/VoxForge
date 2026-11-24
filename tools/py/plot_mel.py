#!/usr/bin/env python3
"""输入梅尔 vs 输出（频谱转梅尔）对比图，带频率/时间刻度

用法: python3 tools/py/plot_mel.py <wav> <out.png>
"""
import sys
import numpy as np
import soundfile as sf
import torch
import onnxruntime as ort
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

# 中文字体（Android 系统字体）
for _p in ["/system/fonts/NotoSansCJK-Regular.ttc"]:
    try:
        font_manager.fontManager.addfont(_p)
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=_p).get_name()
        break
    except Exception:
        pass
plt.rcParams["axes.unicode_minus"] = False

SR, NFFT, HOP, MELS = 48000, 2048, 256, 128


def hz2mel(f):
    return 2595.0 * np.log10(1.0 + f / 700.0)


def mel2hz(m):
    return 700.0 * (np.power(10.0, m / 2595.0) - 1.0)


def build_bank():
    pts = mel2hz(np.linspace(hz2mel(0.0), hz2mel(SR / 2), MELS + 2))
    bins = NFFT // 2 + 1
    freqs = np.linspace(0, SR / 2, bins)
    fb = np.zeros((MELS, bins), dtype=np.float32)
    for m in range(MELS):
        a, b, c = pts[m], pts[m + 1], pts[m + 2]
        for k, f in enumerate(freqs):
            if a <= f <= b and b > a:
                fb[m, k] = (f - a) / (b - a)
            elif b < f <= c and c > b:
                fb[m, k] = (c - f) / (c - b)
    return fb, pts


FB, PTS = build_bank()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x, sr = sf.read(src, dtype="float32")
    if x.ndim > 1:
        x = x.mean(1)
    if sr != SR:
        idx = np.linspace(0, len(x) - 1, int(len(x) * SR / sr))
        x = np.interp(idx, np.arange(len(x)), x).astype(np.float32)

    t = torch.from_numpy(x).float()
    pad = torch.nn.functional.pad(t[None, None], (NFFT // 2, NFFT // 2), mode="reflect")[0, 0]
    win = torch.hann_window(NFFT, periodic=True)
    stft = torch.stft(pad, NFFT, HOP, window=win, center=False, return_complex=True)
    mag_in = stft.abs().numpy()

    mel_in = np.log(np.maximum(FB @ mag_in, 1e-7))

    sess = ort.InferenceSession("models/vocos48_head.onnx", providers=["CPUExecutionProvider"])
    mag_out, _ = sess.run(None, {"mel": mel_in[None].astype(np.float32)})
    mel_out = np.log(np.maximum(FB @ mag_out[0], 1e-7))

    dur = len(x) / SR
    frames = mel_in.shape[1]
    # 每个梅尔带的中心频率
    centers = [np.sqrt(PTS[m] * PTS[m + 1]) for m in range(MELS)]

    vmin = min(np.percentile(mel_in, 2), np.percentile(mel_out, 2))
    vmax = max(np.percentile(mel_in, 98), np.percentile(mel_out, 98))

    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    diff = mel_out - mel_in
    for ax, data, title, rng in ((axes[0], mel_in, "输入梅尔（DSP 合成）", (vmin, vmax)),
                                 (axes[1], mel_out, "输出（神经声码器频谱转梅尔）", (vmin, vmax)),
                                 (axes[2], diff, "差值（输出 − 输入），亮=模型新增", (-3, 3))):
        im = ax.imshow(data, aspect="auto", origin="lower",
                       cmap="magma" if rng is not None and rng[0] == vmin else "RdBu_r",
                       extent=[0, dur, 0, MELS], vmin=rng[0], vmax=rng[1])
        ax.set_title(title, fontsize=11)
        ax.set_ylabel("梅尔频带")
        # 频率刻度：每 16 个带标一次
        ticks = list(range(0, MELS, 16))
        ax.set_yticks(ticks)
        ax.set_yticklabels([f"{centers[i]:.0f}Hz" for i in ticks], fontsize=8)
        ax.set_xlim(0, dur)
    axes[1].set_xlabel("时间 (秒)")
    for ax in axes:
        ax.set_xticks(np.arange(0, dur + 0.01, 0.5))
        ax.grid(alpha=0.15, linestyle=":", color="w")
    fig.colorbar(im, ax=axes, label="log 能量（归一化显示）", fraction=0.02, pad=0.01)
    fig.suptitle("VoxForge 合成 → 梅尔 → Vocos 频谱 对比", fontsize=12)
    fig.savefig(dst, dpi=110, bbox_inches="tight")
    print(f"{dst}  {dur:.2f}s  {frames} 帧")


if __name__ == "__main__":
    main()
