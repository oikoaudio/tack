"""Summarises and plots solo voice renders from lxr_render.

usage: overview.py out.png name=file.wav [name=file.wav ...]

Prints peak level, time to fall 40 dB below peak, spectral centroid and spectral flatness
(near 0 = tonal, near 1 = white noise) for the left channel of each file, then plots
waveform and spectrogram per voice.
"""
import sys, wave
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

INK, MUTED, GRID, LINE = "#1f2328", "#6e7781", "#d0d7de", "#2f6fb0"


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    return x.reshape(-1, w.getnchannels())[:, 0], w.getframerate()


def stats(x, fs):
    on = np.argmax(np.abs(x) > 1e-3)
    x = x[on:]
    env = np.sqrt(np.convolve(x**2, np.ones(256) / 256, "same"))
    pk = env.max()
    after = np.nonzero(env[np.argmax(env):] > pk * 0.01)[0]
    decay_ms = 1000 * (after[-1] + np.argmax(env)) / fs if len(after) else 0
    seg = x[: int(0.1 * fs)] * np.hanning(int(0.1 * fs))
    mag = np.abs(np.fft.rfft(seg)) + 1e-12
    f = np.fft.rfftfreq(len(seg), 1 / fs)
    centroid = (f * mag).sum() / mag.sum()
    flat = np.exp(np.mean(np.log(mag**2))) / np.mean(mag**2)
    return on, 20 * np.log10(np.abs(x).max() + 1e-12), decay_ms, centroid, flat


def main():
    np.seterr(divide="ignore")
    out, items = sys.argv[1], [a.split("=", 1) for a in sys.argv[2:]]
    fig, ax = plt.subplots(len(items), 2, figsize=(11, 1.7 * len(items)), squeeze=False,
                           gridspec_kw={"width_ratios": [1, 1.3]})
    print(f"{'voice':<12}{'peak dBFS':>10}{'-40dB ms':>10}{'centroid Hz':>13}{'flatness':>10}")
    for row, (name, path) in enumerate(items):
        x, fs = load(path)
        on, pk, dec, cen, flat = stats(x, fs)
        print(f"{name:<12}{pk:10.1f}{dec:10.0f}{cen:13.0f}{flat:10.3f}")
        seg = x[on: on + int(0.8 * fs)]
        t = np.arange(len(seg)) / fs * 1000
        a = ax[row, 0]
        a.plot(t, seg, lw=0.5, color=LINE)
        a.set_ylim(-1, 1)
        a.set_ylabel(name, color=INK, rotation=0, ha="right", va="center")
        a.grid(color=GRID, lw=0.5)
        s = ax[row, 1]
        s.specgram(seg + 1e-9, NFFT=512, noverlap=384, Fs=fs, cmap="magma", vmin=-120, vmax=-20)
        s.set_ylim(0, 16000)
        s.set_yticks([0, 4000, 8000, 12000, 16000], ["0", "4k", "8k", "12k", "16k"])
        s.set_xticks([0, 0.2, 0.4, 0.6, 0.8], ["0", "200", "400", "600", "800"])
        for b in (a, s):
            b.tick_params(colors=MUTED, labelsize=7)
            for sp in b.spines.values():
                sp.set_color(GRID)
        if row < len(items) - 1:
            a.set_xticklabels([]); s.set_xticklabels([])
    ax[0, 0].set_title("waveform (L)", color=INK, fontsize=9)
    ax[0, 1].set_title("spectrogram, Hz", color=INK, fontsize=9)
    ax[-1, 0].set_xlabel("ms", color=MUTED); ax[-1, 1].set_xlabel("ms", color=MUTED)
    plt.tight_layout()
    plt.savefig(out, dpi=90)


if __name__ == "__main__":
    main()
