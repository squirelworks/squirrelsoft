# SquirrelSound numerics

The `sqnr` library is an **original** spectral toolkit. The *feature menu*
(FFT, windows, Welch PSD, pitch, THD) is inspired by the kind of chapter
set you would find in a Numerical Recipes–style DSP book. The **code** is
not taken from that book.

## Core transforms

- `sqnr_four1` — 0-based iterative radix-2 complex FFT with precomputed
  twiddle tables (Cooley–Tukey; clean-room)
- `sqnr_realft` — real input via even/odd split + half-length complex FFT,
  packed Hermitian output (`data[0]`=DC, `data[1]`=Nyquist, then Re/Im pairs)

## Windows / estimators

- Hann, Hamming, Blackman, Kaiser (I₀ from its power series / asymptotic),
  Flattop
- Periodogram and Welch PSD
- Autocorrelation via FFT
- Pitch, THD, SNR helpers
- Linear fit utility

`tests/test_sqnr.c` checks the complex FFT against a naive DFT on random
input, plus impulse / tone / WAV round-trips.
