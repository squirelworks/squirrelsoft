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

## Multi-resolution spectrum

One FFT length is a trade: a long frame resolves bass notes a few hertz
apart but smears a short treble burst over the whole frame; a short frame
catches the burst but cannot tell the bass notes apart. `sqdsp_multires_plan`
and `sqdsp_multires_db` use up to four lengths at once on the same frame:
level k takes the newest n/4^k samples, so the treble comes from the most
recent few milliseconds and the bass from the whole frame.

- Level k (k > 0) takes over above q fs / m_k, where its bins are already q
  wide in relative terms (q = 32: about a quarter tone). Levels whose
  crossover would sit past Nyquist are dropped.
- The levels meet in a crossfade one octave wide (c/√2 to c√2), blended in
  power, so there is no step at a crossover.
- Each level is scaled to dBFS by its own window sum: a full-scale sine
  reads 0 dB on every level. The noise floor per bin rises 6 dB per level,
  since the bins are wider.
- The result is on the n-point grid (power linear between a level's bins),
  so it drops into anything that draws one FFT. `sqdsp_multires_spread`
  gives how many grid bins one level bin spans, for band power sums (the
  live view's octave bars divide each bin by it). Near a band edge, a
  coarse level spills a treble tone into the next band, by up to its own
  bin width.

`tests/test_sqdsp.c` checks the complex FFT against a naive DFT on random
input, plus impulse / tone / WAV round-trips.
