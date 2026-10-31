import sys, wave, numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
w = wave.open(sys.argv[1]); fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
on = np.argmax(np.abs(x) > 1e-3); x = x[on:on+int(0.4*fs)]
# pitch track from successive positive-going zero crossings
z = np.where((x[:-1] <= 0) & (x[1:] > 0))[0]
for a, b in zip(z[:12], z[1:13]):
    print(f"t={1000*a/fs:6.1f}ms  f={fs/(b-a):6.1f} Hz")
t = np.arange(len(x)) / fs * 1000
fig, ax = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
ax[0].plot(t, x, lw=0.6); ax[0].set_ylabel("amplitude"); ax[0].set_title("LXR init kit, drum 1 (note 60, vel 127)")
ax[1].plot(1000*z[1:]/fs, fs/np.diff(z), ".-"); ax[1].set_ylabel("cycle freq (Hz)"); ax[1].set_xlabel("ms")
plt.tight_layout(); plt.savefig(sys.argv[2], dpi=90)
