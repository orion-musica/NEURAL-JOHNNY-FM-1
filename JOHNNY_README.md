NEURAL JOHNNY - player notes
========================

START
  The start screen shows the name and "PLAY ME 1-16 NOTES. I WILL CARRY YOUR MELODY ON."
  Press any key: that note is the first 16th of bar 1. A silent 132 BPM clock starts.

YOUR MELODY
  Play 1 to 16 notes, then stop. The notes snap to 16ths.
  The melody is finished when
    - you leave 4 silent 16ths before a bar line, or
    - it ran to the end of its bar and nothing follows in the next three 16ths, or
    - you played 16 notes, or 2 full bars.
  A melody of 1 bar gets a 1-bar answer (2-bar loop); 2 bars get a 2-bar answer (4-bar loop).

THE MODEL
  A small neural network (int8, about 8,400 weights) writes the answer one 16th at a time,
  inside the key and range of your melody. Your notes stay as you played them; on every
  repeat the model changes one or two steps of its answer, keeps it about as sparse as your
  melody, and now and then drifts back towards its first answer.

LAYERS
  After 8 bars the first sound joins, then one more every 8 bars:
    KICK, BASS, HATS, DRONE, CLAP, ECHO, PERC
  Sounds rest for 1, 2 or 4 bars, always coming back on the 8-bar line. Sometimes kick,
  bass and hats drop out together for a 4-bar breakdown, and sometimes the melody itself
  takes a breath.

A NEW MELODY
  Play at any time. Everything keeps playing while the model's melody waits; when you stop,
  the model continues your new motif. Sounds not yet in arrive at random 8-bar lines.

SCREEN
  One oscillator line per sound, in 80 x 80 square pixels on a two-tone green LCD.
  A resting sound's line disappears. The key under the melody note lights up.

CONTROLS
  Keys       play
  PRESETS    the melody's sound evolves: 8 FM timbres (SOFT, HOLLOW, BELL, GLASS,
             METAL, ACID, REED, VAPOUR), 8 small morph steps between each, 64 in all,
             going round. Every detent changes the sound a little.
  K1         FILTER: left = low-pass (darker), right = high-pass (thinner), middle = off
  K2         DRIVE: saturation of the whole mix
  K3         MUTATE 0..10: how much the model changes on every repeat.
             0 = frozen loop, 1-3 calm (safe notes), 4-7 alive, 8-10 wild
  K4         NOTES -5..+5: sparser or busier answers
  SELECT     TEMPO 118..140 BPM
  ALGORITHM  LENGTH: note length of the melody, staccato to legato
  MASTER     volume
  HOME       back to the start screen (stops the music, knobs back to default)
  While a knob turns, its name and value show at the top of the screen for a second.

STORAGE
  Neural Johnny saves nothing in the FM-1's Flash.
