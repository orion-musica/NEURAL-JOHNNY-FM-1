#!/bin/sh
# NEURAL JOHNNY - build the browser version (johnny/prototype/index.html and
# docs/play/index.html) from the very same C sources as the firmware.
set -e
cd "$(dirname "$0")/../.."
M=app/johnny
clang --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin-memset -DFM1_HOST_TEST \
  -Ijohnny/prototype/wasm -I$M -Wall -Wno-unused-function \
  -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-memory -Wl,--strip-all \
  johnny/prototype/wasm/johnny_wasm.c $M/johnny_app.c $M/johnny_model.c $M/johnny_synth.c $M/johnny_screen.c \
  -o johnny/prototype/johnny.wasm
ls -l johnny/prototype/johnny.wasm
python3 - <<'PY'
import base64, os
t = open('johnny/prototype/canvas.template.html', encoding='utf-8').read()
w = base64.b64encode(open('johnny/prototype/johnny.wasm', 'rb').read()).decode()
page = t.replace('%%WASM%%', w)
for out in ('johnny/prototype/index.html', 'docs/play/index.html'):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, 'w', encoding='utf-8').write(page)
    print(out, 'written')
PY
