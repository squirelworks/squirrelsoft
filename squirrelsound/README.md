# SquirrelSound

Spectral sound analyzer with an original DSP core (`sqdsp`, in `../libs/sqdsp`): radix-2 FFT,
packed real FFT, windowed periodograms, Welch PSD, autocorrelation, peak
picking, THD, and SNR estimates.

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
- **Android phone app** under `android/` (`works.squirrel.sound`) — mic FFT +
  waterfall (QR scanning lives in `../squirrelqr/`)

## Build

```bash
make
make test
./build/squirrelsound --help

# Android APK (needs ANDROID_HOME / SDK 34)
make android
make android-test
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
./build/squirrelsound live --source demo --direction fountain
```

In the live window, `w` switches between **Waterfall** (newest row at the top,
history scrolls down) and **Fountain** (newest row at the bottom, history
scrolls up). The choice is saved to `~/.config/squirrelsound/live.conf`
(`$XDG_CONFIG_HOME` is honoured; `%APPDATA%\squirrelsound\live.conf` on
Windows) and used next time; `--direction` overrides it for one run. The
Android app has the same setting under **Direction** and remembers it too.
