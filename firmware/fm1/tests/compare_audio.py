# SPDX-License-Identifier: GPL-3.0-only
"""Compares compare_audio.sh's renders: per voice and for the kit, against the desktop reference A."""
import sys, wave
import numpy as np

STEP = 1200 * 64
NAMES = ["drum1", "drum2", "drum3", "snare", "cymbal", "cl hat", "op hat", "all", "tail"]


def load(p):
    w = wave.open(p)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64)
    return a, w.getframerate()


def db(x):
    return 20 * np.log10(max(x, 1e-12))


def spectrum(x, fs):
    n = len(x)
    s = np.abs(np.fft.rfft(x * np.hanning(n)))
    return s, np.fft.rfftfreq(n, 1 / fs)


def peak_freq(x, fs):
    """strongest partial, parabolic interpolation on the log magnitude"""
    s, f = spectrum(x, fs)
    s[f < 25] = 0
    i = int(np.argmax(s))
    if i < 1 or i + 1 >= len(s):
        return float(f[i])
    a, b, c = np.log(s[i - 1:i + 2] + 1e-9)
    return float((i + 0.5 * (a - c) / (a - 2 * b + c)) * (f[1] - f[0]))


def centroid(x, fs):
    s, f = spectrum(x, fs)
    return float((s * f).sum() / max(s.sum(), 1e-9))


def decay_ms(x, fs):
    """time until the 5 ms RMS envelope stays under -40 dB of its peak"""
    w = max(int(fs * 0.005), 1)
    n = len(x) // w
    if n == 0:
        return 0.0
    e = np.sqrt((x[: n * w].reshape(n, w) ** 2).mean(axis=1))
    if e.max() <= 0:
        return 0.0
    above = np.nonzero(e > e.max() * 0.01)[0]
    return float((above[-1] + 1) * w / fs * 1000)


def bands(x, fs):
    s, f = spectrum(x, fs)
    e = []
    for lo, hi in ((20, 200), (200, 2000), (2000, 8000), (8000, 20000)):
        m = (f >= lo) & (f < hi)
        e.append(db(np.sqrt((s[m] ** 2).sum()) + 1e-9))
    return np.array(e)


def main():
    out, kits = sys.argv[1], int(sys.argv[2])
    worst = {"null": 999.0, "peak": 0.0, "cents_c": 0.0, "cents_d": 0.0, "decay_ms": 0.0, "band": 0.0}
    for k in range(kits):
        A, fa = load(f"{out}/k{k}_A.wav")
        B, _ = load(f"{out}/k{k}_B.wav")
        C, fc = load(f"{out}/k{k}_C.wav")
        D, fd = load(f"{out}/k{k}_D.wav")
        print(f"\nkit {k}")
        print(f"  {'voice':7} {'rms dB':>7} {'B null':>7} {'B peak':>7} {'f0 Hz':>9} {'C cents':>8} {'D cents':>8} "
              f"{'decay ms':>9} {'C dec ms':>8} {'C bandmax':>9} {'centroid':>9}")
        for v in range(8):
            a, b = A[v * STEP:(v + 1) * STEP], B[v * STEP:(v + 1) * STEP]
            c, d = C[v * STEP:(v + 1) * STEP], D[v * STEP:(v + 1) * STEP]
            r = np.sqrt((a ** 2).mean())
            if r < 1:
                continue
            nul = db(np.sqrt(((a - b) ** 2).mean()) / r)
            peak = abs(a - b).max()
            # strongest partial over the first 150 ms; the noise-driven voices' peaks are not a pitch
            n = int(fa * 0.15)
            f_a, f_c, f_d = (peak_freq(x[:n].mean(axis=1), fs) for x, fs in ((a, fa), (c, fc), (d, fd)))
            ca = 1200 * np.log2(f_c / f_a)
            da = 1200 * np.log2(f_d / f_a)
            dec_a, dec_c = decay_ms(a.mean(axis=1), fa), decay_ms(c.mean(axis=1), fc)
            band = np.abs(bands(a[: int(fa)].mean(axis=1), fa) - bands(c[: int(fa)].mean(axis=1), fc)).max()
            print(f"  {NAMES[v]:7} {db(r):7.1f} {nul:7.1f} {peak:7.0f} {f_a:9.2f} {ca:8.2f} {da:8.2f} {dec_a:9.1f} "
                  f"{dec_c - dec_a:8.1f} {band:9.2f} {centroid(a[:int(fa)].mean(axis=1), fa):9.0f}")
            worst["null"] = min(worst["null"], -nul)
            worst["peak"] = max(worst["peak"], peak)
            if v in (0, 1, 2):                            # the pitched voices
                worst["cents_c"] = max(worst["cents_c"], abs(ca))
                worst["cents_d"] = max(worst["cents_d"], abs(da))
            worst["decay_ms"] = max(worst["decay_ms"], abs(dec_c - dec_a))
            worst["band"] = max(worst["band"], band)
    print("\nworst over all kits and voices")
    print(f"  B null against A (depth, dB)          {worst['null']:.1f}")
    print(f"  B largest sample difference (of 32768) {worst['peak']:.0f}")
    print(f"  C pitched-voice tuning error (cents)  {worst['cents_c']:.2f}")
    print(f"  D pitched-voice tuning error (cents)  {worst['cents_d']:.2f}")
    print(f"  C decay difference (ms, 5 ms windows)  {worst['decay_ms']:.1f}")
    print(f"  C largest band level difference (dB)  {worst['band']:.2f}")


main()
