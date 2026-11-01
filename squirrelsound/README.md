# SquirrelSound

Spectral sound analyzer built with an original **Numerical Recipes–style**
numerics core (`sqnr/`): radix-2 FFT, real FFT packing, windowed periodograms,
Welch PSD, autocorrelation, peak picking, THD, and SNR estimates.

> Important: this does **not** ship copyrighted *Numerical Recipes* book source.
> Algorithms are standard spectral methods reimplemented from first principles,
> organized the way an NR-flavored DSP toolkit would be.

## Features

- WAV PCM16/float mono-stereo reader (uses first channel)
- Hann / Hamming / Blackman / Kaiser / Flattop windows
- Magnitude spectrum, one-sided PSD (periodogram & Welch)
- STFT spectrogram → SVG heatmap
- Fundamental frequency via autocorrelation + spectral refine
- Harmonic THD and band-limited SNR helpers
- Synthetic tone / chirp / noise generators for self-tests
- **Live mode**: demo / WAV loop / ALSA mic → SDL spectrum + waterfall
  (pinch/wheel zoom, max-hold, peak marker) or `--headless` for CI
- Phone mic UI: see `../squirrelfield/web/spectrum.html`

## Build

```bash
make
make test
./build/squirrelsound --help
```

## Examples

```bash
# synthesize a 440 Hz tone + harmonics, then analyze
./build/squirrelsound synth /tmp/a440.wav --freq 440 --seconds 1.5 --harmonics 3
./build/squirrelsound spectrum /tmp/a440.wav --fft 4096 --window hann
./build/squirrelsound pitch /tmp/a440.wav
./build/squirrelsound welch /tmp/a440.wav --seg 2048 --overlap 0.5
./build/squirrelsound spectrogram /tmp/a440.wav /tmp/a440.svg --fft 1024
./build/squirrelsound thd /tmp/a440.wav --fund 440

# live spectrum (demo source; use --source alsa for mic)
./build/squirrelsound live --source demo --freq 440
./build/squirrelsound live --source demo --headless --frames 30
```
