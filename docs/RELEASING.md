# Releasing SquirrelSoft apps

Only Squirrel tags, publishes or uploads to Play. Claude prepares everything up
to a draft. AAB files never go to GitHub, not as commits, Release assets or
Actions artifacts. The rules come from the kit's release-artifacts addendum.

## When an app is ready

An app reaches a release point when the roadmap fixes queued for its version
are merged and CI is green on main. Claude then opens a **release-prep PR**:

- `versionCode` raised and `versionName` set in `<app>/android/app/build.gradle.kts`
  (`versionCode` only ever goes up, and `applicationId` never changes);
- `docs/releases/<app>-<version>.md`, which becomes the Release text.

After that PR is merged, Claude asks Squirrel for the go-ahead with the exact
steps below, and keeps working on the next roadmap item meanwhile.

Desktop-only programs (Station, CAD, GNSS, Calc, Paint, Games, Debug) take
their version from `<app>/VERSION`. Bump that file in the same PR that ships
behaviour changes you want in the next desktop suite draft.

## The button (Squirrel)

One-time setup: add four repository secrets under Settings > Secrets and
variables > Actions. They must be the upload key registered in the Play Console:

| Secret | Value |
|---|---|
| `SQUIRREL_UPLOAD_KEYSTORE_BASE64` | `base64 -w0 upload.jks` (macOS: `base64 -i upload.jks`) |
| `SQUIRREL_UPLOAD_STORE_PASSWORD` | keystore password |
| `SQUIRREL_UPLOAD_KEY_ALIAS` | key alias |
| `SQUIRREL_UPLOAD_KEY_PASSWORD` | key password |

Each release:

1. Actions > **Release** > Run workflow (branch `main`) > pick the app > Run.
   Or: `gh workflow run release.yml -f app=<app>`
   Use app `desktop` to draft a suite Release with every C desktop program's
   packages (no APK).
2. The run checks that the release notes exist and the tag is free, runs
   `make test` on Ubuntu / Windows / macOS, builds and verifies the signed APK
   (Android apps), builds the desktop packages, writes `SHA256SUMS.txt`, and
   creates a **draft** Release named `<AppName> <version>`.
3. Open the draft (Releases page, or the link in the run summary), check it, and
   press **Publish release**. Publishing creates the tag `<app>-v<version>` on
   the commit the workflow built.
4. For Play: build the AAB on your machine and upload it in the Play Console.

   ```bash
   cd <app>/android
   SQUIRREL_UPLOAD_STORE_FILE=/path/upload.jks \
   SQUIRREL_UPLOAD_STORE_PASSWORD=... SQUIRREL_UPLOAD_KEY_ALIAS=... \
   SQUIRREL_UPLOAD_KEY_PASSWORD=... ./gradlew :app:bundleRelease
   # -> app/build/outputs/bundle/release/app-release.aab
   ```

   Or with `scripts/play_upload_aab.py` (internal track by default). Keep the AAB
   under `release-out/`, which is git-ignored.

## What each release contains

| App | Tag | Android | Desktop |
|---|---|---|---|
| SquirrelSound | `squirrelsound-v<ver>` | `SquirrelSound-<ver>-android.apk` | Windows zip/MSI, macOS `.dmg` (`.app`), Linux AppImage + `.deb` |
| SquirrelSuica | `squirrelsuica-v<ver>` | `SquirrelSuica-<ver>-android.apk` | same C packages (PC/SC / PaSoRi / GUI) |
| SquirrelSuicaAnalysis | `squirrelsuica-analysis-v<ver>` | `SquirrelSuicaAnalysis-<ver>-android.apk` | `-python.zip` |
| SquirrelQR | `squirrelqr-v<ver>` | `SquirrelQR-<ver>-android.apk` | Windows zip/MSI, macOS `.dmg`, Linux AppImage + `.deb` |
| Desktop suite | `desktop-vYYYY.MM.DD` | none | packages for every C desktop program |

Every release also has `SHA256SUMS.txt` covering its files. Packages are
unsigned to start; see [docs/packaging.md](packaging.md).

## Dry runs

Pull requests that change `release.yml`, `ci.yml` or `scripts/release/` run
`make test` on all three OSes and every desktop package build, unsigned, and
upload nothing. Locally:

```bash
scripts/release/app_meta.sh squirrelsound
scripts/release/package_desktop.sh squirrelsound /tmp/dist linux-x86_64
scripts/release/package_desktop.sh squirrelgnss /tmp/dist linux-x86_64
```
