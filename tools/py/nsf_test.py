#!/usr/bin/env python3
"""用 NSF-HiFiGAN 声码器把 128 梅尔还原成波形

用法: nsf_test.py <in.wav> <out.wav>
流程: wav -> log-mel(128, hop512) + f0 -> onnx -> wav
"""
import sys
import numpy as np
import soundfile as sf
import onnxruntime as ort

MODEL = "/root/workspace/VoxSynth/models/pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.onnx"
SR, NFFT, HOP, MELS = 44100, 2048, 512, 128
FMIN, FMAX = 40.0, 16000.0


def mel_bank():
    # slaney 刻度（librosa 默认）
    F_SP = 200.0 / 3
    MIN_LOG_HZ = 1000.0
    MIN_LOG_MEL = MIN_LOG_HZ / F_SP
    LOGSTEP = np.log(6.4) / 27.0

    def hz2mel(f):
        f = np.asarray(f, dtype=np.float64)
        out = np.where(f < MIN_LOG_HZ, f / F_SP, MIN_LOG_MEL + np.log(np.maximum(f, 1e-9) / MIN_LOG_HZ) / LOGSTEP)
        return out

    def mel2hz(m):
        m = np.asarray(m, dtype=np.float64)
        return np.where(m < MIN_LOG_MEL, m * F_SP, MIN_LOG_HZ * np.exp(LOGSTEP * (m - MIN_LOG_MEL)))

    pts = mel2hz(np.linspace(hz2mel(FMIN), hz2mel(FMAX), MELS + 2))
    bins = NFFT // 2 + 1
    freqs = np.linspace(0, SR / 2, bins)
    fb = np.zeros((bins, MELS), dtype=np.float32)
    for m in range(MELS):
        a, b, c = pts[m], pts[m + 1], pts[m + 2]
        for k, f in enumerate(freqs):
            if a <= f <= b and b > a:
                fb[k, m] = (f - a) / (b - a)
            elif b < f <= c and c > b:
                fb[k, m] = (c - f) / (c - b)
    return fb


FB = mel_bank()


def analyze(x):
    win = np.hanning(NFFT + 1)[:-1].astype(np.float32)
    n = 1 + (len(x) - NFFT) // HOP
    frames = np.lib.stride_tricks.sliding_window_view(x, NFFT)[::HOP][:n] * win
    spec = np.abs(np.fft.rfft(frames, axis=1))
    mel = spec @ FB
    return np.log(np.maximum(mel, 1e-5))


def estimate_f0(x):
    hop = HOP
    n = len(x) // hop
    f0 = np.zeros(n, dtype=np.float32)
    W = 1024
    for i in range(n):
        s = x[i * hop:i * hop + W]
        if len(s) < W:
            break
        best, lag = 0.0, 0
        for L in range(SR // 1000, SR // 70):
            c = float(np.dot(s[:W - L], s[L:]))
            if c > best:
                best, lag = c, L
        f0[i] = SR / lag if lag else 0.0
    return f0


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x, sr = sf.read(src, dtype="float32")
    if x.ndim > 1:
        x = x.mean(1)
    if sr != SR:
        idx = np.linspace(0, len(x) - 1, int(len(x) * SR / sr))
        x = np.interp(idx, np.arange(len(x)), x).astype(np.float32)

    mel = analyze(x)
    f0 = estimate_f0(x)
    n = min(len(mel), len(f0))
    mel, f0 = mel[:n], f0[:n]

    sess = ort.InferenceSession(MODEL, providers=["CPUExecutionProvider"])
    wav = sess.run(None, {"mel": mel[None].astype(np.float32),
                          "f0": f0[None].astype(np.float32)})[0][0]

    wav = 0.9 * wav / max(1e-6, np.abs(wav).max())
    sf.write(dst, wav, SR)
    print(f"{dst} {len(wav)/SR:.2f}s  mel {mel.shape}")


if __name__ == "__main__":
    main()
