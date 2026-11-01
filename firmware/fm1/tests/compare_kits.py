# SPDX-License-Identifier: GPL-3.0-only
"""compare_kits.sh's renders: distributions over every kit and voice."""
import sys, wave
import numpy as np


def load(p, seconds=1.5):
    w = wave.open(p)
    fs = w.getframerate()
    n = min(w.getnframes(), int(seconds * fs))
    return np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, 2).astype(np.float64).mean(axis=1), fs


def db(x):
    return 20 * np.log10(max(x, 1e-12))


def spec(x, fs):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    return s, np.fft.rfftfreq(len(x), 1 / fs)


def f0(x, fs):
    s, f = spec(x, fs)
    s[f < 25] = 0
    i = int(np.argmax(s))
    if i < 1 or i + 1 >= len(s):
        return float(f[i])
    a, b, c = np.log(s[i - 1:i + 2] + 1e-9)
    return float((i + 0.5 * (a - c) / (a - 2 * b + c)) * f[1])


def bands(x, fs):
    s, f = spec(x, fs)
    return np.array([db(np.sqrt((s[(f >= lo) & (f < hi)] ** 2).sum()))
                     for lo, hi in ((20, 200), (200, 2000), (2000, 8000), (8000, 16000), (16000, 20000))])


def decay(x, fs):
    w = int(fs * 0.005)
    n = len(x) // w
    e = np.sqrt((x[: n * w].reshape(n, w) ** 2).mean(axis=1))
    return float((np.nonzero(e > e.max() * 0.01)[0][-1] + 1) * w / fs * 1000)


def transient(x, fs):
    n = int(fs * 0.005)
    return float(np.abs(x[: n]).max())


def dist(name, v, unit):
    v = np.array(v)
    print(f"  {name:40} median {np.median(v):8.2f}  p95 {np.percentile(v, 95):8.2f}  worst {v.max():8.2f} {unit}")


def metrics(a, fa, c, fc, v):
    n = int(fa * 0.15)
    out = {}
    if v < 3:                                          # the pitched voices; the others have no single pitch
        f1, f2 = f0(c[:n], fc), f0(a[:n], fa)
        if f1 > 0 and f2 > 0:                          # (a slow attack: nothing to measure yet)
            out["cents"] = abs(1200 * np.log2(f1 / f2))
    out["decay"] = abs(decay(c, fc) - decay(a, fa))
    ba, bc = bands(a, fa), bands(c, fc)
    out["band"] = np.abs(ba - bc)[:4].max()
    out["hi"] = abs(ba[4] - bc[4])
    out["trans"] = abs(db(transient(c, fc) + 1) - db(transient(a, fa) + 1))
    return out


LABEL = {"cents": ("strongest partial, drums 1-3", "cents"), "decay": ("decay to -40 dB", "ms"),
         "band": ("worst band, 20 Hz-16 kHz", "dB"), "hi": ("band 16-20 kHz (aliasing)", "dB"),
         "trans": ("transient, first 5 ms", "dB")}


def table(name, rows):
    print(name)
    for k, (label, unit) in LABEL.items():
        v = np.array([r[k] for r in rows if k in r])
        print(f"  {label:32} median {np.median(v):8.2f}  p95 {np.percentile(v, 95):8.2f}  p99 {np.percentile(v, 99):8.2f}"
              f"  worst {v.max():8.2f} {unit}")


def main():
    d, kits = sys.argv[1], int(sys.argv[2])
    nulls, peaks, worst, cr, rr, br = [], [], [], [], [], []
    for k in range(kits):
        for v in range(7):
            a, fa = load(f"{d}/{k}_{v}_A.wav")
            b, _ = load(f"{d}/{k}_{v}_B.wav")
            c, fc = load(f"{d}/{k}_{v}_C.wav")
            r2, _ = load(f"{d}/{k}_{v}_R.wav")
            r = np.sqrt((a ** 2).mean())
            if r < 3:
                continue
            nul = db(np.sqrt(((a - b) ** 2).mean()) / r)
            nulls.append(nul)
            peaks.append(abs(a - b).max())
            worst.append((round(float(nul), 1), k, v))
            cr.append(metrics(a, fa, c, fc, v))
            rr.append(metrics(a, fa, r2, fa, v))
            br.append(metrics(a, fa, b, fa, v))
    nv = np.array(nulls)
    print(f"{len(nulls)} sounding voices in {kits} kits")
    print(f"FM-1 arithmetic against desktop, sample by sample: median null {np.median(nv):.1f} dB, "
          f"p95 {np.percentile(nv, 95):.1f}, {int((nv > -60).sum())} voices over -60 dB, {int((nv > -40).sum())} over -40")
    print("  largest:", sorted(worst, reverse=True)[:8])
    print(f"  largest sample difference: median {np.median(peaks):.1f}, p95 {np.percentile(peaks, 95):.1f}, of 32768")
    table("yardstick: desktop against desktop with another noise seed", rr)
    table("FM-1 arithmetic against desktop, 44140 Hz", br)
    table("the FM-1's REAL_FS 44100 against desktop 44140", cr)


main()
