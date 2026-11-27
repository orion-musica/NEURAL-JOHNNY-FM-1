#!/usr/bin/env python3
"""NEURAL JOHNNY - render the website's screen pictures (docs/assets/*.png)
with the firmware's own drawing code: builds the PC test, runs the scripted
player and saves the LCD at chosen moments, 2x, as PNG. Needs gcc, numpy, Pillow."""
import os, subprocess, tempfile
import numpy as np
from PIL import Image
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SHOTS = {500: 'boot', 9000: 'lead', 26000: 'kick', 38500: 'sound', 77400: 'filter', 91900: 'full', 111600: 'mutate'}
tmp = tempfile.mkdtemp()
exe = os.path.join(tmp, 'johnny_host')
src = [os.path.join(ROOT, 'johnny', 'host', 'johnny_host.c')] + [os.path.join(ROOT, 'app', 'johnny', 'johnny_%s.c' % n) for n in ('app', 'model', 'synth', 'screen', 'lights', 'drums', 'groove')]
subprocess.check_call(['gcc', '-O2', '-DFM1_HOST_TEST', '-I' + os.path.join(ROOT, 'app', 'johnny')] + src + ['-lm', '-o', exe])
env = dict(os.environ, SHOTS=','.join(str(t) for t in sorted(SHOTS)))
subprocess.check_call([exe, tmp, '64'], env=env)
for t, name in SHOTS.items():
    a = np.fromfile(os.path.join(tmp, 'screen_%d.raw' % t), dtype=np.uint16).reshape(240, 240)
    rgb = np.dstack([((a >> 11) & 31) << 3, ((a >> 5) & 63) << 2, (a & 31) << 3]).astype(np.uint8)
    out = os.path.join(ROOT, 'docs', 'assets', name + '.png')
    Image.fromarray(rgb).resize((480, 480), Image.NEAREST).save(out, optimize=True)
    print(out)
