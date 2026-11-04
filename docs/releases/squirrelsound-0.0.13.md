SquirrelSound 0.0.13 (versionCode 13), the first release since 0.0.8.

## What's new

- **Levels you can trust.** The spectrum reads true dBFS, so a full-scale sine shows 0 dBFS. The colour map starts at black for the floor. The default range is −120 to −20 dB, and Advanced has Min, Max and Auto range controls. The desktop live view matches.
- **50/50 split.** The spectrum and LOFARgram share the screen 50/50 by default, with 40/60 and 60/40 under Advanced. The waterfall draws one row per pixel, so it no longer smears.
- **The LOFARgram span setting works.** Changing the span now really changes how fast the waterfall scrolls.
- **Waterfall or fountain.** New rows can enter at the top or the bottom (Android, and the `w` key on desktop).
- **Mic source and gain.** Pick the microphone source (falls back automatically if a phone lacks one) and add digital gain.

## Downloads

- `SquirrelSound-0.0.13-android.apk`: signed Android build (Android 8.0+).
- `SquirrelSound-0.0.13-linux-x86_64.tar.gz`, `-windows-x86_64.zip`, `-macos-arm64.zip`: desktop analyzer (`squirrelsound synth|spectrum|pitch|thd|spectrogram|live`). See `RUNTIME.txt` in each archive for what the live view needs.
- `SHA256SUMS.txt`.

Play users get the same version through the store.
