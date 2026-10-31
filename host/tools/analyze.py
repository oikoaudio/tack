import sys, wave, numpy as np
w = wave.open(sys.argv[1]); fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
print(f"{len(x)/fs:.2f}s  peak {20*np.log10(np.abs(x).max()+1e-12):.1f} dBFS  nonzero from sample {np.argmax(np.abs(x)>0)}")
on = np.argmax(np.abs(x) > 1e-3)
for ms in [0, 5, 10, 20, 40, 80, 150, 300, 600, 1000]:
    a = on + int(ms*fs/1000); seg = x[a:a+int(0.01*fs)]
    if len(seg) < 64: break
    rms = np.sqrt(np.mean(seg**2))
    z = np.where(np.diff(np.signbit(seg)))[0]
    f0 = (len(z)-1)/2/((z[-1]-z[0])/fs) if len(z) > 2 else 0
    print(f"t={ms:5d}ms  rms {20*np.log10(rms+1e-12):6.1f} dB  ~f0 {f0:6.1f} Hz")
