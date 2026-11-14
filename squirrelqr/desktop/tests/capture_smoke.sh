#!/bin/sh
# CI smoke: --screen / --select decode a synthetic frame (no real display).
set -e
b="${1:?build dir}"
q="$b/squirrelqr"
"$q" --help | grep -q -- --select
"$q" --version | grep -q 'capture:'
"$q" --encode "capture-cli-synth" --out "$b/capture_cli.png" 2>/dev/null
# QR_CAPTURE_FRAME drives --screen and --select
out=$(QR_CAPTURE_FRAME="$b/capture_cli.png" "$q" --screen)
[ "$out" = "capture-cli-synth" ] || {
	echo "capture --screen: wrong text ($out)"
	exit 1
}
out=$(QR_CAPTURE_FRAME="$b/capture_cli.png" "$q" --select)
[ "$out" = "capture-cli-synth" ] || {
	echo "capture --select: wrong text ($out)"
	exit 1
}
QR_CAPTURE_FRAME="$b/capture_cli.png" "$q" --screen --json | grep -q '"source":"screen"'
# blank synthetic frame -> exit 1
printf 'P5\n32 32\n255\n' >"$b/capture_blank_cli.pgm"
head -c 1024 /dev/zero | tr '\0' '\377' >>"$b/capture_blank_cli.pgm"
set +e
QR_CAPTURE_FRAME="$b/capture_blank_cli.pgm" "$q" --screen 2>/dev/null
[ $? -eq 1 ] || {
	echo "capture blank: wrong exit"
	exit 1
}
set -e
be=$("$q" --version | sed -n 's/.*capture: \([^,)]*\).*/\1/p')
echo "squirrelqr capture smoke ok (backend=$be)"
