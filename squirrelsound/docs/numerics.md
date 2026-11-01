# SquirrelSound numerics

The `sqnr` library is an original Numerical Recipes–**style** toolkit for
spectral audio analysis. It is not a copy of Numerical Recipes source.

Included methods:

- `sqnr_four1` — complex radix-2 FFT
- `sqnr_realft` — real packed FFT
- Windows: Hann, Hamming, Blackman, Kaiser, Flattop
- Periodogram and Welch PSD
- Autocorrelation via FFT
- Pitch, THD, SNR helpers
- Linear fit utility

These follow classic textbook spectral-analysis practice suitable for a
serious offline sound analyzer on host systems and, later, Squirrel-OS/2.
