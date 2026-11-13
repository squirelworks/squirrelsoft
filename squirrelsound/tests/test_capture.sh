#!/bin/sh
# Live capture without audio hardware.
#  mic: SDL's disk audio driver feeds a 1 kHz tone file to --source mic.
#  loopback: a PulseAudio null sink plays a 1.5 kHz tone and --source loopback
#    must hear it through the sink's monitor. Skipped when pulseaudio/paplay
#    are missing, unless REQUIRE_LOOPBACK=1 (CI).
# Usage: sh tests/test_capture.sh BUILD
set -e
b="${1:?build dir}"
q="$b/squirrelsound"
if ! "$q" devices >/dev/null 2>&1 && ! SDL_AUDIODRIVER=disk "$q" devices >/dev/null 2>&1; then
    echo "skipping capture tests (built without SDL2)"
    exit 0
fi
tmp=$(mktemp -d)
trap 'kill $play_pid $pa_pid 2>/dev/null || true; rm -rf "$tmp"' EXIT
pa_pid=
play_pid=

# mic through the disk driver: raw float32 mono at the 44.1 kHz the source asks for
python3 -I -c "
import math, struct, sys
sys.stdout.buffer.write(b''.join(struct.pack('=f', 0.5 * math.sin(2 * math.pi * 1000 * i / 44100)) for i in range(44100 * 4)))
" > "$tmp/tone.raw"
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILEIN="$tmp/tone.raw" SDL_DISKAUDIOFILE=/dev/null \
    "$q" live --source mic --headless --frames 10 2>/dev/null > "$tmp/mic.txt"
grep -q 'peak=\(99[0-9]\|100[0-9]\)\.[0-9]* Hz.*src=mic' "$tmp/mic.txt" || { echo "mic: wrong peak"; cat "$tmp/mic.txt"; exit 1; }
echo "squirrelsound mic capture ok"

# a bad source name is an error
if "$q" live --source nope --headless --frames 1 2>/dev/null; then echo "bad source accepted"; exit 1; fi

if ! command -v pulseaudio >/dev/null || ! command -v paplay >/dev/null; then
    [ "${REQUIRE_LOOPBACK:-0}" = 1 ] && { echo "loopback test required but pulseaudio/paplay are missing"; exit 1; }
    echo "skipping the loopback test (needs pulseaudio and paplay)"
    exit 0
fi
export XDG_RUNTIME_DIR="$tmp/run" HOME="$tmp"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
pulseaudio -n --daemonize=no --exit-idle-time=-1 --log-target=file:"$tmp/pa.log" \
    --load=module-native-protocol-unix \
    --load="module-null-sink sink_name=sqtest sink_properties=device.description=SquirrelTestSink" &
pa_pid=$!
i=0
until pactl info >/dev/null 2>&1; do
    i=$((i + 1)); [ $i -gt 50 ] && { echo "pulseaudio did not start"; cat "$tmp/pa.log"; exit 1; }
    sleep 0.1
done
pactl set-default-sink sqtest
SDL_AUDIODRIVER=pulseaudio "$q" devices | grep -q '^monitor *Monitor of SquirrelTestSink' || { echo "loopback: no monitor listed"; exit 1; }
"$q" synth "$tmp/t.wav" --freq 1500 --seconds 10 --rate 44100 >/dev/null
paplay "$tmp/t.wav" &
play_pid=$!
sleep 0.5
SDL_AUDIODRIVER=pulseaudio "$q" live --source loopback --headless --frames 10 > "$tmp/lb.txt" 2> "$tmp/lb.err"
grep -q 'Monitor of SquirrelTestSink' "$tmp/lb.err" || { echo "loopback: wrong device"; cat "$tmp/lb.err"; exit 1; }
grep -q 'peak=1\(49[0-9]\|50[0-9]\)\.[0-9]* Hz.*src=loopback' "$tmp/lb.txt" || { echo "loopback: wrong peak"; cat "$tmp/lb.txt"; exit 1; }
kill $play_pid 2>/dev/null || true
wait $play_pid 2>/dev/null || true
play_pid=
echo "squirrelsound loopback capture ok"
