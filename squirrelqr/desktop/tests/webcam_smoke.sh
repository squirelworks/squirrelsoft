#!/bin/sh
# CI smoke: --webcam decodes a synthetic frame (no real camera).
set -e
b="${1:?build dir}"
q="$b/squirrelqr"
"$q" --help | grep -q -- --webcam
"$q" --version | grep -q 'webcam:'
"$q" --encode "webcam-cli-synth" --out "$b/webcam_cli.png" 2>/dev/null
# @FILE path
[ "$("$q" --webcam --device "@$b/webcam_cli.png")" = "webcam-cli-synth" ] || {
	echo "webcam @file: wrong text"
	exit 1
}
# QR_WEBCAM_FRAME env (default device)
out=$(QR_WEBCAM_FRAME="$b/webcam_cli.png" "$q" --webcam)
[ "$out" = "webcam-cli-synth" ] || {
	echo "webcam env: wrong text ($out)"
	exit 1
}
"$q" --webcam --device "@$b/webcam_cli.png" --json | grep -q '"source":"webcam"'
"$q" devices >/dev/null
# blank synthetic frame -> exit 1
printf 'P5\n32 32\n255\n' >"$b/webcam_blank_cli.pgm"
head -c 1024 /dev/zero | tr '\0' '\377' >>"$b/webcam_blank_cli.pgm"
set +e
"$q" --webcam --device "@$b/webcam_blank_cli.pgm" 2>/dev/null
[ $? -eq 1 ] || {
	echo "webcam blank: wrong exit"
	exit 1
}
"$q" --webcam --device @ 2>/dev/null
[ $? -eq 2 ] || {
	echo "webcam bare @: wrong exit"
	exit 1
}
"$q" --device /dev/video0 2>/dev/null
[ $? -eq 2 ] || {
	echo "--device alone: wrong exit"
	exit 1
}
set -e
echo "squirrelqr webcam smoke ok (backend=$("$q" --version | sed -n 's/.*webcam: \([^)]*\).*/\1/p'))"
