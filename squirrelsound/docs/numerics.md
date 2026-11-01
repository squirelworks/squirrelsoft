# SquirrelSound numerics (`sqdsp`)

Original spectral DSP used by SquirrelSound — clean-room Cooley–Tukey FFT,
windows, Welch PSD, pitch, and THD helpers.

## Core transforms

- `sqdsp_fft` — 0-based iterative radix-2 complex FFT with precomputed twiddles
- `sqdsp_rfft` — real input via even/odd split + half-length complex FFT,
  packed Hermitian output (`data[0]`=DC, `data[1]`=Nyquist, then Re/Im pairs)

## Windows / estimators

- Hann, Hamming, Blackman, Kaiser (I₀ from its power series / asymptotic),
  Flattop
- Periodogram and Welch PSD
- Autocorrelation via FFT
- Pitch, THD, SNR helpers
- Linear fit utility

`tests/test_sqdsp.c` checks the complex FFT against a naive DFT on random
input, plus impulse / tone / WAV round-trips.
