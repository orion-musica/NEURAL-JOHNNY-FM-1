# Neural Johnny

A tiny neural network inside the **M-VAVE FM-1**, by Orion-Musica.
Play 1–16 notes and it keeps your melody going as a hypnotic techno track: it answers,
changes a note or two on every repeat, brings in a new sound every 8 bars and lets them
breathe. The knobs steer it (PRESETS morphs the melody's sound through 64 steps, K1 filter,
K2 drive, K3 how much the model mutates, K4 how busy it plays, SELECT tempo, ALGORITHM note
length). Every sound is an oscillator line in square pixels on a two-tone 90s-phone LCD, and
all 41 key and button lights fire like a neural net in time with the music. The 11 panel
buttons FX..REC are drum pads: tap a groove and a second model continues it as an extra layer.
OCT- mutes sounds one by one (your drum groove first, the melody last), OCT+ brings them back, both held = one random sound alone.

[**Play in the browser**](https://orion-musica.github.io/NEURAL-JOHNNY-FM-1/play/) ·
[**Install on the FM-1**](https://orion-musica.github.io/NEURAL-JOHNNY-FM-1/install/) ·
[**Manual**](https://orion-musica.github.io/NEURAL-JOHNNY-FM-1/manual/) · [Player notes](JOHNNY_README.md) ·
[**Release 1.0.0**](https://github.com/orion-musica/NEURAL-JOHNNY-FM-1/releases/tag/v1.0.0)

> **Version 1.0.0, tested on hardware.** Tested on an original M-VAVE FM-1 (FCC ID: 2ARCP-FM1): the AI models run on
> the device itself, and surprisingly well. Back up your FM-1 projects before installing. See [CHANGELOG.md](CHANGELOG.md).

## Where things are

| Path | What |
| --- | --- |
| `app/johnny/johnny_app.c` | the app: start screen, melody capture, continuation, development, arrangement, LCD picture |
| `app/johnny/johnny_model.c` | TinyML inference (int8 MLP, about 8.4k weights) |
| `app/johnny/johnny_weights.h` | the trained weights (generated) |
| `app/johnny/johnny_drums.c` | TinyML groove model inference (int8 MLP, about 4.4k weights) |
| `app/johnny/johnny_drum_weights.h` | its trained weights (generated) |
| `app/johnny/johnny_groove.c` | drum pads: groove capture, the model's answer, development |
| `app/johnny/johnny_synth.c` | synth for the 8 layers and the 11 pad drums, 16th sequencer, runs in the audio interrupt |
| `app/johnny/johnny_screen.c` | 80×80 phone LCD → 240×240 RGB565 bands |
| `app/johnny/johnny_lights.c` | neural light waves over all 41 key and button LEDs, in time with the music |
| `app/johnny/johnny_fm1.c` | FM-1 adapter: panel, LEDs, screen, MASTER knob |
| `app/screen.c`, `app/audio_output.c` | two small hooks (`NEURAL_JOHNNY` in `app/johnny/johnny_fm1.h`) |
| `johnny/tools/train_drums.py` | builds the groove training set, trains the groove model |
| `johnny/tools/train_model.py` | builds the training set, trains the model, writes `johnny_weights.h` |
| `johnny/data/` | your own loops to train on (optional, see its README) |
| `johnny/tools/make_shots.py` | renders the website's screen pictures with the firmware's drawing code |
| `johnny/tools/build_wasm.sh` | builds the browser version `docs/play/index.html` from the same C |
| `johnny/host/` | PC test: a scripted player, writes a WAV, a bar log and screen dumps |
| `docs/` | GitHub Pages: start page, `manual/`, `play/`, `install/` web installer |

The rest of the tree is the FM1 Open SDK (V14) and the firmware base it ships with
(see `README.FM1-OPEN.md`, `THIRD_PARTY.md`).

## Build

Every push to `main` builds the firmware, the web installer and the site in GitHub Actions
(Windows runner, JieLi toolchain 2.5.3). The download is the `Motif-Max` artifact of the run;
with GitHub Pages set to "GitHub Actions" the site and installer go live too.

Locally on Windows: `INSTALL-JOHNNY.bat`.

Browser version and PC test (Linux/macOS, clang + gcc):

    sh johnny/tools/build_wasm.sh
    gcc -O2 -DFM1_HOST_TEST -Iapp/johnny johnny/host/johnny_host.c app/johnny/johnny_{app,model,synth,screen,lights,drums,groove}.c -lm -o johnny/host/johnny_host
    ./johnny/host/johnny_host johnny/host 84

Retrain the model (Python 3 + numpy, about 30 s):

    python3 johnny/tools/train_model.py

## License

GPL-3.0-only with the FM1 Open linking exception (`LICENSE.md`).
