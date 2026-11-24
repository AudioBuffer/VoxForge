#!/usr/bin/env python3
"""用 Vocos 头（ONNX）把梅尔还原成波形

流程：wav -> log-mel(128, 48k) -> ONNX(mag,phase) -> iSTFT -> wav
"""
import sys
import numpy as np
import soundfile as sf
import torch
import onnxruntime as ort

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


def log_mel(x: np.ndarray) -> np.ndarray:
    x = torch.from_numpy(x).float()
    pad = torch.nn.functional.pad(x[None, None], (NFFT // 2, NFFT // 2), mode="reflect")[0, 0]
    win = torch.hann_window(NFFT, periodic=True)
    stft = torch.stft(pad, NFFT, HOP, window=win, center=False, return_complex=True)
    mag = stft.abs().numpy()                      # [bins][frames]
    mel = FB @ mag                                # [mels][frames]
    return np.log(np.maximum(mel, 1e-7)).T        # [frames][mels]


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    x, sr = sf.read(src, dtype="float32")
    if x.ndim > 1:
        x = x.mean(1)
    if sr != SR:
        idx = np.linspace(0, len(x) - 1, int(len(x) * SR / sr))
        x = np.interp(idx, np.arange(len(x)), x).astype(np.float32)

    mel = log_mel(x)                               # [frames][128]
    sess = ort.InferenceSession("models/vocos48_head.onnx", providers=["CPUExecutionProvider"])
    inp = mel.T[None].astype(np.float32)           # [1,128,T]
    mag, phase = sess.run(None, {"mel": inp})
    print("mag", mag.shape, "phase", phase.shape)

    # 静音门限：直接用输入波形做 VAD（以帧能量对齐到梅尔帧率）
    hop_len = 256
    n_frames = inp.shape[2]
    rms = np.zeros(n_frames, dtype=np.float32)
    for i in range(n_frames):
        seg = x[i * hop_len:(i + 1) * hop_len]
        rms[i] = np.sqrt(np.mean(seg ** 2)) if len(seg) else 0.0
    thr = max(1e-4, rms.max() * 0.02)
    gate = np.clip((rms - thr) / thr, 0.0, 1.0).astype(np.float32)
    k = np.ones(7, dtype=np.float32) / 7.0
    gate = np.convolve(gate, k, mode="same")
    mag = mag * gate[None, None, :]

    mag_t = torch.from_numpy(mag[0])
    ph_t = torch.from_numpy(phase[0])
    spec = torch.polar(mag_t, ph_t)
    win = torch.hann_window(NFFT, periodic=True)
    y = torch.istft(spec, NFFT, HOP, window=win, center=True)
    y = y.numpy()

    # 响度跟随：按 20ms 帧把输出 RMS 拉回输入，消除忽大忽小
    hop_ms = int(SR * 0.02)
    n = min(len(x), len(y)) // hop_ms
    if n > 4:
        def env(v):
            return np.array([np.sqrt(np.mean(v[i * hop_ms:(i + 1) * hop_ms] ** 2) + 1e-12)
                             for i in range(n)])
        ea, eb = env(x), env(y)
        g = np.clip(ea / np.maximum(eb, 1e-6), 0.4, 2.5)
        k = np.ones(5) / 5.0
        g = np.convolve(g, k, mode="same")
        pos = np.linspace(0, len(y) - 1, len(g))
        gain = np.interp(np.arange(len(y)), pos, g)
        y = y * gain.astype(np.float32)
    y = 0.85 * y / max(1e-6, np.abs(y).max())
    sf.write(dst, y, SR)
    print(f"{dst} {len(y)/SR:.2f}s")


if __name__ == "__main__":
    main()
