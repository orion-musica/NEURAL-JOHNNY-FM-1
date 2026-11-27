# Neural Johnny

A TinyML melody-continuation app for the **M-VAVE FM-1**, by BULØKE.
Play 1–16 notes; a tiny on-device model carries your melody on as a hypnotic
techno track, developing it a little on every repeat and adding one sound every
8 bars. Knobs shape it live: PRESETS morphs the lead's sound, K1 filter, K2 drive,
K3 how much the model mutates, K4 how busy it plays, SELECT tempo, ALGORITHM note length.
Each sound is a waving oscillator line in square pixels on a two-tone
90s-phone LCD.

[**Play in the browser**](https://orion-musica.github.io/NEURAL-JOHNNY-FM-1/play/) ·
[**Install on the FM-1**](https://orion-musica.github.io/NEURAL-JOHNNY-FM-1/install/) ·
[Player notes](JOHNNY_README.md)

> **Experimental.** First builds for device testing. Back up your FM-1 projects before installing.

## Where things are

| Path | What |
| --- | --- |
| `app/johnny/johnny_app.c` | the app: start screen, melody capture, continuation, development, arrangement, LCD picture |
| `app/johnny/johnny_model.c` | TinyML inference (int8 MLP, about 8.4k weights) |
| `app/johnny/johnny_weights.h` | the trained weights (generated) |
| `app/johnny/johnny_synth.c` | 8-layer synth and 16th sequencer, runs in the audio interrupt |
| `app/johnny/johnny_screen.c` | 80×80 phone LCD → 240×240 RGB565 bands |
| `app/johnny/johnny_fm1.c` | FM-1 adapter: panel, LEDs, screen, MASTER knob |
| `app/screen.c`, `app/audio_output.c` | two small hooks (`NEURAL_JOHNNY` in `app/johnny/johnny_fm1.h`) |
| `johnny/tools/train_model.py` | builds the training set, trains the model, writes `johnny_weights.h` |
| `johnny/data/` | your own loops to train on (optional, see its README) |
| `johnny/tools/build_wasm.sh` | builds the browser version `docs/play/index.html` from the same C |
| `johnny/host/` | PC test: a scripted player, writes a WAV, a bar log and screen dumps |
| `docs/` | GitHub Pages: start page, `play/`, `install/` web installer |

The rest of the tree is the FM1 Open SDK (V14) and the firmware base it ships with
(see `README.FM1-OPEN.md`, `THIRD_PARTY.md`).

## Build

Every push to `main` builds the firmware, the web installer and the site in GitHub Actions
(Windows runner, JieLi toolchain 2.5.3). The download is the `Motif-Max` artifact of the run;
with GitHub Pages set to "GitHub Actions" the site and installer go live too.

Locally on Windows: `INSTALL-JOHNNY.bat`.

Browser version and PC test (Linux/macOS, clang + gcc):

    sh johnny/tools/build_wasm.sh
    gcc -O2 -DFM1_HOST_TEST -Iapp/johnny johnny/host/johnny_host.c app/johnny/johnny_{app,model,synth,screen}.c -lm -o johnny/host/johnny_host
    ./johnny/host/johnny_host johnny/host 84

Retrain the model (Python 3 + numpy, about 30 s):

    python3 johnny/tools/train_model.py

## License

GPL-3.0-only with the FM1 Open linking exception (`LICENSE.md`).
