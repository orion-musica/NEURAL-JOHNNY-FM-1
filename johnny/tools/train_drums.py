#!/usr/bin/env python3
"""NEURAL JOHNNY - train the tiny groove-continuation model for the drum pads.

For every 16th step of a looping groove it gives, for each of the 11 pad drums,
the chance that the drum hits. It sees the drums' hits one, two, four, eight
and sixteen steps back and half a loop back, which drums the player used at
all, the place in the bar and whether it is in the answer half / the last
beat. About 4.4k weights, stored as int8.

Training data: rule-built techno grooves: four-on-the-floor and broken kicks,
backbeats, off-beat and 16th hats, syncopated rims and cowbells, ride, shaker,
tom fills at the end of the phrase, each answered by a varied copy.

    python3 johnny/tools/train_drums.py        # writes app/johnny/johnny_drum_weights.h
"""
import os
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'app', 'johnny', 'johnny_drum_weights.h')
DRUMS = ['KICK', 'SNARE', 'CLAP', 'RIM', 'CH', 'OH', 'LTOM', 'HTOM', 'RIDE', 'SHAKER', 'COWBELL']
ND = len(DRUMS)
BACK = [1, 2, 4, 8, 16, 'half']
NIN = ND * len(BACK) + ND + 16 + 2
H1 = H2 = 32
rng = np.random.default_rng(606)


def p(s):
    return [c == 'x' for c in s]


def euclid(k, rot):
    b = [(i * k) % 16 < k for i in range(16)]
    return b[-rot:] + b[:-rot] if rot else b


POOL = {
    'KICK': ['x...x...x...x...'] * 4 + ['x..x..x.x...x...', 'x...x...x..x.x..', 'x.....x...x.....', 'x...x..x..x.x...'],
    'SNARE': ['....x.......x...'] * 3 + ['....x.......x.x.', '....x..x....x...', '.......x.......x'],
    'CLAP': ['....x.......x...'] * 3 + ['....x.......x..x', '....x...x...x...'],
    'RIM': ['..x..x....x..x..', '.x...x.x..x...x.', '...x..x...x..x..'],
    'CH': ['x.x.x.x.x.x.x.x.', 'xxxxxxxxxxxxxxxx', '.x.x.x.x.x.x.x.x', 'xx.xxx.xxx.xxx.x'],
    'OH': ['..x...x...x...x.'] * 3 + ['......x.......x.', '..x.......x.....'],
    'LTOM': ['......x.......x.', 'x.......x.......', '...........x..x.'],
    'HTOM': ['.....x.......x..', '..........x.x...', '.x.......x......'],
    'RIDE': ['x...x...x...x...', 'x.x.x.x.x.x.x.x.', '..x...x...x...x.'],
    'SHAKER': ['xxxxxxxxxxxxxxxx', 'x.xxx.xxx.xxx.xx', '.xxx.xxx.xxx.xxx'],
    'COWBELL': [],
}


def drum_bar(name):
    if name == 'COWBELL' or (name in ('RIM', 'LTOM', 'HTOM') and rng.random() < .5):
        return euclid(int(rng.choice([3, 5, 7] if name != 'LTOM' else [2, 3])), int(rng.integers(16)))
    return p(POOL[name][rng.integers(len(POOL[name]))])


def seed_bar():
    """1 to 6 drums, like a player tapping a few pads."""
    weights = np.array([.5, .45, .4, .35, .55, .4, .15, .15, .2, .25, .2])
    n = int(rng.choice([1, 2, 2, 3, 3, 3, 4, 4, 5, 6]))
    chosen = rng.choice(ND, size=n, replace=False, p=weights / weights.sum())
    bar = np.zeros((16, ND), np.uint8)
    for d in chosen:
        hits = drum_bar(DRUMS[d])
        if rng.random() < .25:   # the player missed or added a hit
            i = int(rng.integers(16)); hits[i] = not hits[i]
        bar[:, d] = hits
    if not bar.any():
        bar[0, 0] = 1
    return bar


def vary(bar, amount, fill):
    b = bar.copy()
    used = [d for d in range(ND) if b[:, d].any()]
    for _ in range(amount):
        d = int(rng.choice(used)); i = int(rng.integers(16))
        if d == 0 and i % 4 == 0:
            continue                                      # the four-on-the-floor stays
        b[i, d] ^= 1
    if fill and rng.random() < .55:                       # a fill on the last beat
        kind = rng.random()
        for i in range(12, 16):
            b[i, :] = 0 if kind < .3 else b[i, :]
        cand = [d for d in (1, 6, 7, 2, 3) if d in used] or [int(rng.choice([1, 6, 7]))]
        for i in range(12, 16):
            if rng.random() < .7:
                b[i, int(rng.choice(cand))] = 1
        if 0 in used and rng.random() < .6:
            b[12, 0] = 1
    if rng.random() < .3 and len(used) < 6:               # a new voice joins the answer
        d = int(rng.choice([4, 5, 9, 3, 10]))
        b[:, d] |= np.array(drum_bar(DRUMS[d]), np.uint8) & (rng.random(16) < .7)
    return b


def groove():
    a = seed_bar()
    if rng.random() < .7:
        return np.concatenate([a, vary(a, int(rng.integers(0, 3)), True)])
    b = vary(a, int(rng.integers(0, 3)), rng.random() < .4)
    return np.concatenate([a, b, vary(a, int(rng.integers(0, 3)), False), vary(b, int(rng.integers(1, 4)), True)])


def features(g, i):
    L = len(g)
    seed_used = g[:L // 2].any(0)
    parts = []
    for k in BACK:
        k = L // 2 if k == 'half' else k
        parts.append(g[(i - k) % L])
    pos = np.zeros(16); pos[i % 16] = 1
    return np.concatenate(parts + [seed_used, pos, [1.0 if i >= L // 2 else 0.0, 1.0 if i % 16 >= 12 else 0.0]]).astype(np.float32)


def dataset(n):
    X, Y = [], []
    for _ in range(n):
        g = groove().astype(np.float32)
        for i in range(len(g)):
            X.append(features(g, i)); Y.append(g[i])
    return np.array(X), np.array(Y, np.float32)


def init(shape, fan):
    return (rng.standard_normal(shape) * np.sqrt(2.0 / fan)).astype(np.float32)


def train():
    X, Y = dataset(16000)
    print(f'{len(X)} training steps, {NIN} inputs')
    perm = rng.permutation(len(X)); split = int(len(X) * .95)
    tr, va = perm[:split], perm[split:]
    P = {'W1': init((NIN, H1), NIN), 'b1': np.zeros(H1, np.float32), 'W2': init((H1, H2), H1),
         'b2': np.zeros(H2, np.float32), 'W3': init((H2, ND), H2), 'b3': np.full(ND, -1.5, np.float32)}
    M = {k: np.zeros_like(v) for k, v in P.items()}; V = {k: np.zeros_like(v) for k, v in P.items()}

    def fwd(x, P):
        z1 = x @ P['W1'] + P['b1']; h1 = np.maximum(z1, 0)
        z2 = h1 @ P['W2'] + P['b2']; h2 = np.maximum(z2, 0)
        return z1, h1, z2, h2, 1 / (1 + np.exp(-(h2 @ P['W3'] + P['b3'])))

    def ev(P, idx):
        pr = np.clip(fwd(X[idx], P)[-1], 1e-6, 1 - 1e-6); y = Y[idx]
        return float(-(y * np.log(pr) + (1 - y) * np.log(1 - pr)).mean()), float(((pr > .5) == (y > .5)).mean())

    lr, t, B = 2e-3, 0, 256
    for epoch in range(12):
        rng.shuffle(tr)
        for s in range(0, len(tr) - B + 1, B):
            idx = tr[s:s + B]; x = X[idx]
            z1, h1, z2, h2, pr = fwd(x, P)
            g = (pr - Y[idx]) / B
            G = {'W3': h2.T @ g, 'b3': g.sum(0)}
            d2 = (g @ P['W3'].T) * (z2 > 0); G['W2'] = h1.T @ d2; G['b2'] = d2.sum(0)
            d1 = (d2 @ P['W2'].T) * (z1 > 0); G['W1'] = x.T @ d1; G['b1'] = d1.sum(0)
            t += 1
            for k in P:
                M[k] = .9 * M[k] + .1 * G[k]; V[k] = .999 * V[k] + .001 * G[k] ** 2
                P[k] -= lr * (M[k] / (1 - .9 ** t)) / (np.sqrt(V[k] / (1 - .999 ** t)) + 1e-8)
        lr *= .8
        print(f'epoch {epoch + 1}: val loss {ev(P, va)[0]:.4f}, hit/miss right {ev(P, va)[1] * 100:.1f}%')
    return P, lambda Q: ev(Q, va)


def export(P, ev):
    lines = ['/* NEURAL JOHNNY - drum groove model weights. Generated by johnny/tools/train_drums.py; do not edit. */',
             '#pragma once', '#include <stdint.h>',
             f'enum {{ JD_DRUMS = {ND}, JD_BACK = {len(BACK)}, JD_NIN = {NIN}, JD_H1 = {H1}, JD_H2 = {H2} }};']
    Q, total = {}, 0
    for name in ['W1', 'W2', 'W3']:
        s = float(np.abs(P[name]).max()) / 127.0
        q = np.clip(np.round(P[name] / s), -127, 127).astype(np.int8)
        Q[name] = q.astype(np.float32) * s; total += q.size
        flat = q.T.flatten()
        body = ',\n'.join('  ' + ','.join(str(int(v)) for v in flat[i:i + 24]) for i in range(0, len(flat), 24))
        lines.append(f'static const int8_t jd_{name}[{flat.size}] = {{\n{body}\n}};')
        lines.append(f'static const float jd_{name}_scale = {s:.9g}f;')
    for name in ['b1', 'b2', 'b3']:
        Q[name] = P[name]
        lines.append(f'static const float jd_{name}[{P[name].size}] = {{' + ','.join(f'{v:.6g}f' for v in P[name]) + '};')
    lines.append(f'/* {total} int8 weights */')
    open(OUT, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('float', ev(P), 'int8', ev(Q), '->', OUT, total, 'weights')
    np.savez(os.path.join(ROOT, 'johnny', 'tools', 'drums_float.npz'), **P)


if __name__ == '__main__':
    P, ev = train()
    export(P, ev)
