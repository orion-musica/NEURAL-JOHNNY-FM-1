# Changelog

## 1.0.0 — 2026-11-27

First stable release. Tested on an original M-VAVE FM-1 (FCC ID: 2ARCP-FM1).

- **Melody model:** a TinyML network (int8, about 8,400 weights) on the FM-1 continues the
  1–16 notes you play with an answer of the same length, in your key and range, and develops
  it a step or two on every repeat.
- **Arrangement:** after 8 bars one sound joins every 8 bars (kick, bass, hats, drone, clap,
  echo, perc); sounds take 1-, 2- and 4-bar breaths, with occasional breakdowns. A new melody
  can be played at any time.
- **Drum pads and groove model:** the 11 buttons FX–REC play kick, snare, clap, rim, closed and
  open hat, toms, ride, shaker and cowbell. A second TinyML network (about 4,400 weights)
  continues a tapped groove as an extra layer, GROOVE.
- **Knobs:** PRESETS morphs the melody's sound through 8 FM timbres in 64 steps; K1 filter,
  K2 drive, K3 MUTATE, K4 NOTES, SELECT tempo, ALGORITHM note length.
- **OCT− / OCT+:** mute sounds one by one (your groove first, the melody last) and bring them
  back; both held keep one random sound alone. A muted melody returns with a new one.
- **Screen:** one oscillator line per sound on an 80 × 80 two-tone phone LCD; knob labels;
  start screen with ORION-MUSICA and the version.
- **Lights:** neural light waves on all 41 key and button LEDs, in time with the music.
- Browser version, PC test, manual, landing page and web installer.

### Known limits

- The panel buttons have no velocity; every hit and note snaps to a sixteenth.
- Both models were trained on rule-built hypnotic-techno material, not on recordings;
  they can be retrained on your own loops (`johnny/tools/train_model.py`, `train_drums.py`).
- Neural Johnny plays its own small synth and saves nothing to the FM-1's memory.

## 0.1 beta — 2026-11-27

First builds: melody continuation, layers and breaths, knobs, neural lights, drum pads,
OCT mute and solo, then the first test on hardware.
