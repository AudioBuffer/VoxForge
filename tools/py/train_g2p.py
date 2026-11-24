#!/usr/bin/env python3
"""训练小型 G2P 网络：上下文汉字 -> 拼音类别 + 声调

结构：字符 hash 嵌入（64 维 × 5 窗口）+ 2 层 MLP -> 拼音 410 类 + 声调 5 类
纯 numpy，导出二进制给 C++ 推理用。
"""
import json, struct, sys
import numpy as np

EMB = 64        # 每字嵌入维度
WIN = 5         # 上下文窗口（前后各 2）
H1, H2 = 128, 128

def char_id(ch: str) -> int:
    """与 C++ 侧一致的 FNV-1a，绝不能用 Python 内置 hash（进程随机化）"""
    h = 2166136261
    for b in ch.encode('utf-8'):
        h ^= b
        h = (h * 16777619) & 0xffffffff
    return h % 65536

def embed(ctx, pos):
    v = np.zeros(EMB * WIN, np.float32)
    for k in range(WIN):
        i = pos + k - WIN // 2
        c = ctx[i] if 0 <= i < len(ctx) else (ctx[pos] if ctx else " ")
        h = char_id(c)
        for d in range(EMB):
            v[k * EMB + d] = 1.0 if ((h * (d + 1)) >> 7) & 1 else -1.0
    return v

def main(data_json, out_bin):
    rows = json.load(open(data_json))
    syls = sorted({r["syl"] for r in rows})
    syl2id = {s: i for i, s in enumerate(syls)}
    print(f"{len(rows)} 条, {len(syls)} 个音节")

    X = np.zeros((len(rows), EMB * WIN), np.float32)
    Ys = np.zeros(len(rows), np.int32)
    Yt = np.zeros(len(rows), np.int32)
    for i, r in enumerate(rows):
        ctx = r["ctx"]; pos = r.get("pos", len(ctx) // 2)
        X[i] = embed(ctx, pos)
        Ys[i] = syl2id[r["syl"]]
        Yt[i] = max(0, min(4, r["tone"] - 1))

    rng = np.random.default_rng(0)
    W1 = (rng.normal(0, np.sqrt(2 / X.shape[1]), (X.shape[1], H1))).astype(np.float32)
    b1 = np.zeros(H1, np.float32)
    W2 = (rng.normal(0, np.sqrt(2 / H1), (H1, H2))).astype(np.float32)
    b2 = np.zeros(H2, np.float32)
    Ws = (rng.normal(0, np.sqrt(1 / H2), (H2, len(syls))) * 0.01).astype(np.float32)
    bs = np.zeros(len(syls), np.float32)
    Wt = (rng.normal(0, np.sqrt(1 / H2), (H2, 5)) * 0.01).astype(np.float32)
    bt = np.zeros(5, np.float32)

    params = [W1, b1, W2, b2, Ws, bs, Wt, bt]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    lr, b1m, b2m, eps = 2e-3, 0.9, 0.999, 1e-8

    N = len(rows)
    B = 128
    for ep in range(12):
        perm = rng.permutation(N)
        tot = 0.0
        for i in range(0, N - B, B):
            idx = perm[i:i+B]
            x = X[idx]
            z1 = x @ W1 + b1; a1 = np.maximum(z1, 0)
            z2 = a1 @ W2 + b2; a2 = np.maximum(z2, 0)
            logits_s = a2 @ Ws + bs
            logits_t = a2 @ Wt + bt
            # softmax + CE
            ls = logits_s - logits_s.max(1, keepdims=True)
            ps = np.exp(ls); ps /= ps.sum(1, keepdims=True)
            lt = logits_t - logits_t.max(1, keepdims=True)
            pt = np.exp(lt); pt /= pt.sum(1, keepdims=True)
            ys, yt = Ys[idx], Yt[idx]
            loss = -np.log(ps[np.arange(B), ys] + 1e-9).mean() \
                   -np.log(pt[np.arange(B), yt] + 1e-9).mean()
            tot += loss

            ds = ps.copy(); ds[np.arange(B), ys] -= 1; ds /= B
            dt = pt.copy(); dt[np.arange(B), yt] -= 1; dt /= B
            gWs = a2.T @ ds; gbs = ds.sum(0)
            gWt = a2.T @ dt; gbt = dt.sum(0)
            da2 = ds @ Ws.T + dt @ Wt.T
            dz2 = da2 * (z2 > 0)
            gW2 = a1.T @ dz2; gb2 = dz2.sum(0)
            da1 = dz2 @ W2.T
            dz1 = a1 * 0 + da1 * (z1 > 0)
            gW1 = x.T @ dz1; gb1 = dz1.sum(0)

            grads = [gW1, gb1, gW2, gb2, gWs, gbs, gWt, gbt]
            for j, (p, g) in enumerate(zip(params, grads)):
                m[j] = b1m * m[j] + (1 - b1m) * g
                v[j] = b2m * v[j] + (1 - b2m) * (g * g)
                mh = m[j] / (1 - b1m ** (ep + 1))
                vh = v[j] / (1 - b2m ** (ep + 1))
                p -= lr * mh / (np.sqrt(vh) + eps)
        print(f"epoch {ep+1}: loss {tot / max(1, (N - B) // B):.3f}")

    # 导出：魔数 + 维度 + 音节表 + 权重
    with open(out_bin, "wb") as f:
        f.write(b"VXGP")
        f.write(struct.pack("<4i", EMB, WIN, H1, H2))
        f.write(struct.pack("<i", len(syls)))
        for s in syls:
            b = s.encode()
            f.write(struct.pack("<B", len(b))); f.write(b)
        for p in params:
            f.write(np.ascontiguousarray(p, dtype="<f4").tobytes())
    print("saved", out_bin)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
