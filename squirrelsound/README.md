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
  waterfall (QR scanning lives in `../squirrelqr/`). Like the desktop it has
  Octave and 1/3 octave bar views (bands from `libs/sqdsp` through JNI), a
  calibration offset per mic source (Advanced tab), and **Rec**, which saves
  what is captured as a 16-bit mono WAV in the app's Music folder and offers
  to share it.

## Build

```bash
make
make test
./build/squirrelsound --help

# Android APK (needs ANDROID_HOME / SDK 34)
make android
make android-test
```

`make test` also builds the Android JNI bridge (`android/app/src/main/cpp/sqdsp_jni.c`)
for this machine and drives each native entry point from a JVM
(`android/jni-smoke/`). It is skipped when there is no JDK.

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
./build/squirrelsound live --source mic --view third    # start on the 1/3-octave bars
./build/squirrelsound live --source mic --record take.wav   # record from the start (16-bit mono WAV)
./build/squirrelsound live --source mic --cal 4.5       # calibration offset for this run, in dB
./build/squirrelsound live --source mic --multires      # multi-resolution spectrum (3 levels; --multires 2..4)
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
keys from the earlier window still work (space, m, r, 1-5, 8/9/0, w, x, [ ],
arrows, q/Esc).

The top half can show four views (the segments in the toolbar, or `v`):

- **Spectrum**, with the loudest peaks labelled with frequency and level
  (**Peaks**, or `p`; with Hold on they follow the held trace).
- **Octave** and **1/3 Oct**: IEC 61260 base-10 band levels from the
  shared `libs/sqdsp` core, corrected for the window's noise bandwidth so a
  tone reads its own level. With Hold on, each bar keeps a hold tick.
- **Scope**: the last frame's samples, started on a rising zero crossing.

**MR** (or `x`) switches the spectrum to multi-resolution: the bass from
the whole frame and the treble from its newest quarter and sixteenth, so a
bass line keeps its fine resolution while a hi-hat shows up without being
smeared over the frame. Tones read the same level either way (see
`docs/numerics.md`). The LOFARgram colours only the rows that arrived
since the last frame and copies the rest, so a large window stays cheap.

**Rec** (or `R`) records what the source delivers, every sample once, to
`squirrelsound-YYYYMMDD-HHMMSS.wav` in the current directory (or the
`--record` file the first time). `-` and `=` move the calibration offset by
1 dB; it is saved per input (`cal.<source>=` in the prefs file below) and
loaded the next time the same input is used. `--cal` overrides it for one
run.

In the live window, `w` (or the Fountain button) switches between **Waterfall** (newest row at the top,
history scrolls down) and **Fountain** (newest row at the bottom, history
scrolls up). The choice is saved to `~/.config/squirrelsound/live.conf`
(`$XDG_CONFIG_HOME` is honoured; `%APPDATA%\squirrelsound\live.conf` on
Windows) and used next time; `--direction` overrides it for one run. The
Android app has the same setting under **Direction** and remembers it too.
