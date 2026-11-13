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
- **Live mode**: demo / WAV loop / mic / system audio (loopback) / ALSA → SDL spectrum + waterfall
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

# live spectrum
./build/squirrelsound live --source demo --freq 440
./build/squirrelsound live --source mic                 # default input (SDL: PulseAudio/PipeWire, ALSA, WASAPI, CoreAudio)
./build/squirrelsound live --source mic --device USB    # first input whose name contains "USB"
./build/squirrelsound live --source loopback            # what the computer is playing
./build/squirrelsound devices                           # inputs and monitor sources
./build/squirrelsound live --source demo --headless --frames 30
./build/squirrelsound live --source demo --direction fountain
./build/squirrelsound live --source demo --gui-snapshot live.ppm --frames 30   # the window, drawn to a file
```

`--source loopback` listens to the computer's own output through the
monitor source of the default PulseAudio/PipeWire sink (`--device` picks
another one by name). Windows (WASAPI loopback) and macOS come later in
roadmap S3.2. `--source alsa` keeps the direct ALSA reader for systems
without SDL.

The live window (drawn with `libs/sqgui`) has the Android Live tab's split
view: a toolbar with Start/Stop, Hold, Reset and Fountain, then the range
presets (Human, Music, Bass, Speech, Full) and the scroll speed (Slow,
Normal, Fast); the spectrum fills the top half and the LOFARgram the bottom
half. The mouse wheel over either plot zooms around the pointer, and the
keys from the earlier window still work (space, m, r, 1-5, 8/9/0, w, [ ],
arrows, q/Esc).

In the live window, `w` (or the Fountain button) switches between **Waterfall** (newest row at the top,
history scrolls down) and **Fountain** (newest row at the bottom, history
scrolls up). The choice is saved to `~/.config/squirrelsound/live.conf`
(`$XDG_CONFIG_HOME` is honoured; `%APPDATA%\squirrelsound\live.conf` on
Windows) and used next time; `--direction` overrides it for one run. The
Android app has the same setting under **Direction** and remembers it too.
