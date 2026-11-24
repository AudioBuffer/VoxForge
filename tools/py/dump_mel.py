#!/usr/bin/env python3
"""导出输入梅尔与输出频谱（转梅尔）的灰度图，归一化到 0-255

用法: python3 tools/py/dump_mel.py <wav> <out_prefix>
产出: <prefix>_in_mel.png  <prefix>_out_mel.png
"""
import sys
import numpy as np
import soundfile as sf
import torch
import onnxruntime as ort
from PIL import Image

SR, NFFT, HOP, MELS = 48000, 2048, 256, 128


def hz2mel(f):
    return 2595.0 * np.log10(1.0 + f / 700.0)


def mel2hz(m):
    return 700.0 * (np.power(10.0, m / 2595.0) - 1.0)


def mel_bank():
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
    return fb


FB = mel_bank()


def to_mel(mag):
    """mag: [bins][frames] -> log mel [mels][frames]"""
    return np.log(np.maximum(FB @ mag, 1e-7))


def save_gray(img: np.ndarray, path: str):
    """img [rows][cols] -> 归一化 0-255 的灰度 PNG"""
    a = img.astype(np.float32)
    lo, hi = np.percentile(a, 1), np.percentile(a, 99)
    if hi <= lo:
        hi = lo + 1e-6
    a = np.clip((a - lo) / (hi - lo), 0, 1) * 255.0
    Image.fromarray(a.astype(np.uint8), mode="L").save(path)
    print(f"{path}  {img.shape[1]}x{img.shape[0]}")


def main():
    src, prefix = sys.argv[1], sys.argv[2]
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

    mel_in = to_mel(mag_in)                       # [128][frames]

    sess = ort.InferenceSession("models/vocos48_head.onnx", providers=["CPUExecutionProvider"])
    mag_out, _phase = sess.run(None, {"mel": mel_in[None].astype(np.float32)})
    mel_out = to_mel(mag_out[0])                  # [128][frames]

    # 时间在横轴：转置后翻转 y 轴（低频在下）
    save_gray(np.flipud(mel_in), f"{prefix}_in_mel.png")
    save_gray(np.flipud(mel_out), f"{prefix}_out_mel.png")


if __name__ == "__main__":
    main()
