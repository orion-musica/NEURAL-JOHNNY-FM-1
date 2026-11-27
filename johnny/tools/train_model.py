#!/usr/bin/env python3
"""NEURAL JOHNNY - train the tiny melody-continuation model and export it as C.

The model is a small MLP (about 8.5k weights, stored as int8) that predicts the
next 16th step of a looping melody: a rest, or a note -12..+24 semitones from
the tonic. It sees the last four steps, the steps one half-bar, one bar and one
half-phrase back, the last note played, the position in the bar and whether it
is in the continuation half / the cadence.

Training data: a rule-built corpus of hypnotic-techno motifs (minor modes,
pedal tones, arpeggios, short cells repeated with small changes, answers that
resolve to the tonic or the fifth). Add your own loops as plain-text files in
johnny/data/*.txt to teach it your taste (format: see johnny/data/README.md).

    python3 johnny/tools/train_model.py            # writes app/johnny/johnny_weights.h
"""
import glob, os, sys
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'app', 'johnny', 'johnny_weights.h')
VOCAB = 38            # 0 = rest, 1..37 = -12..+24 semitones from the tonic
EMB = 8
CTX = 8               # token features per sample
H1 = H2 = 48
NPOS = 16
NIN = CTX * EMB + NPOS + 2

SCALES = {
    'aeolian':  [0, 2, 3, 5, 7, 8, 10],
    'phrygian': [0, 1, 3, 5, 7, 8, 10],
    'dorian':   [0, 2, 3, 5, 7, 9, 10],
    'minpent':  [0, 3, 5, 7, 10],
    'harmmin':  [0, 2, 3, 5, 7, 8, 11],
    'mixo':     [0, 2, 4, 5, 7, 9, 10],
}
SCALE_W = {'aeolian': .33, 'phrygian': .25, 'dorian': .2, 'minpent': .12, 'harmmin': .05, 'mixo': .05}

rng = np.random.default_rng(1977)


def tok(off):
    return 0 if off is None else int(np.clip(off, -12, 24)) + 13


def euclid(k, n=16, rot=0):
    pat = [(i * k) % n < k for i in range(n)]
    return pat[-rot:] + pat[:-rot] if rot else pat


RHYTHMS = [
    'x..x..x...x..x..', 'x.x..x.x..x.x...', '..x...x...x...x.', 'x..x..x..x..x...',
    'x...x.x.x...x.x.', 'x.xx..x.x.xx..x.', '.x.x.x.x.x.x.x.x', 'x..xx..xx..x.x..',
    'x.......x..x....', 'x..x....x..x..x.', 'xx..x.x.xx..x.x.', '..x..x..x..x..x.',
    'x.x.x.x.x.x.x.x.', 'x...x...x...x...', 'x..x..x.x..x..x.', '.xx..xx..xx..xx.',
]


def rhythm():
    r = rng.random()
    if r < .45:
        return [c == 'x' for c in RHYTHMS[rng.integers(len(RHYTHMS))]]
    if r < .85:
        return euclid(int(rng.integers(3, 10)), 16, int(rng.integers(0, 16)))
    p = [rng.random() < (.55 if i % 2 else .35) for i in range(16)]
    p[0] = p[0] or rng.random() < .6
    return p


def degree_pitch(scale, d, base=0):
    n = len(scale)
    return base + scale[d % n] + 12 * (d // n)


def johnny_bar(scale):
    """One bar (16 offsets or None) in one of five hypnotic styles."""
    hits = rhythm()
    if sum(hits) < 2:
        hits[0] = hits[8] = True
    style = rng.choice(['pedal', 'arp', 'walk', 'cell', 'octave'], p=[.28, .2, .22, .18, .12])
    reg = int(rng.choice([0, 0, 0, 12, -12 if style != 'octave' else 0]))
    n = len(scale)
    out = [None] * 16
    if style == 'pedal':
        others = [degree_pitch(scale, d) for d in (2, 4, 6, 1, 7)]
        ped = 0
        for i in range(16):
            if hits[i]:
                out[i] = ped if rng.random() < .6 else int(rng.choice(others))
    elif style == 'arp':
        third = 3 if 3 in scale else 4
        chord = [0, third, 7, 12, 10 if 10 in scale else 7, 15 if third == 3 else 16]
        k = int(rng.integers(3, 6)); seq = chord[:k]
        if rng.random() < .5:
            seq = seq + seq[-2:0:-1]
        j = 0
        for i in range(16):
            if hits[i]:
                out[i] = seq[j % len(seq)]; j += 1
    elif style == 'walk':
        d = int(rng.choice([0, 0, 2, 4]))
        for i in range(16):
            if hits[i]:
                out[i] = degree_pitch(scale, d)
                d += int(rng.choice([-2, -1, -1, 0, 1, 1, 2]))
                d = int(np.clip(d, -n, 2 * n - 1))
    elif style == 'cell':
        cl = int(rng.integers(2, 4))
        cell = [degree_pitch(scale, int(rng.integers(-1, 6))) for _ in range(cl)]
        cell[0] = int(rng.choice([0, 0, 7, 12]))
        j = 0
        for i in range(16):
            if hits[i]:
                shift = 0 if i < 8 else int(rng.choice([0, 0, 0, degree_pitch(scale, 3), degree_pitch(scale, 4)]))
                out[i] = cell[j % cl] + (shift if i >= 8 and j % cl == 0 else 0); j += 1
    else:  # octave bounce
        for i in range(16):
            if hits[i]:
                out[i] = int(rng.choice([0, 12, 0, 12, 7, degree_pitch(scale, 2) + 12]))
    return [None if o is None else o + reg for o in out]


def nearest_in_scale(scale, off, direction):
    for k in range(1, 4):
        c = off + direction * k
        if c % 12 in scale:
            return c
    return off


def vary(bar, scale, amount, cadence=True):
    """The bar again with a few small changes and (often) an answer at the end."""
    b = list(bar)
    for _ in range(amount):
        notes = [i for i, o in enumerate(b) if o is not None]
        r = rng.random()
        if r < .45 and notes:
            i = int(rng.choice(notes))
            b[i] = nearest_in_scale(scale, b[i], int(rng.choice([-1, 1])))
        elif r < .65:
            i = int(rng.integers(16))
            if b[i] is None:
                prev = [o for o in b[:i] if o is not None]
                b[i] = prev[-1] if prev and rng.random() < .5 else int(rng.choice([0, 7, 12, degree_pitch(scale, 2)]))
            elif len(notes) > 2:
                b[i] = None
        elif r < .8 and notes:
            i = int(rng.choice(notes))
            b[i] = b[i] + (12 if b[i] < 6 else -12)
        else:  # transpose the second half by the 4th/5th degree (sequence)
            t = int(rng.choice([degree_pitch(scale, 3), degree_pitch(scale, 4), -degree_pitch(scale, 1) if len(scale) > 1 else 0]))
            for i in range(8, 16):
                if b[i] is not None:
                    c = b[i] + t
                    b[i] = c if c % 12 in scale else nearest_in_scale(scale, c, 1)
    if cadence and rng.random() < .65:
        last = [i for i in range(10, 16) if b[i] is not None]
        if last:
            b[last[-1]] = int(rng.choice([0, 0, 7, 12, -5 if -5 % 12 in scale else 0]))
    return b


def phrase():
    name = rng.choice(list(SCALES), p=[SCALE_W[k] for k in SCALES])
    scale = SCALES[name]
    a = johnny_bar(scale)
    if rng.random() < .7:      # 2 bars: seed + continuation
        return a + vary(a, scale, int(rng.integers(0, 4)))
    b = vary(a, scale, int(rng.integers(0, 3)), cadence=False) if rng.random() < .6 else johnny_bar(scale)
    seed = a + b
    cont = vary(a, scale, int(rng.integers(0, 3)), cadence=False) + vary(b, scale, int(rng.integers(1, 4)))
    return seed + cont


def read_user_loops():
    """johnny/data/*.txt: one loop per line, 32 or 64 space-separated steps,
    '.' = rest, otherwise semitones from the tonic (-12..24)."""
    loops = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'johnny', 'data', '*.txt'))):
        for line in open(path, encoding='utf-8'):
            line = line.split('#')[0].split()
            if len(line) in (32, 64):
                loops.append([None if s == '.' else int(s) for s in line])
    return loops


def features(toks, i):
    """Same feature set as johnny_model.c: 8 token ids, position, 2 flags."""
    L = len(toks)
    prev = lambda k: toks[(i - k) % L]
    last = 0
    for k in range(1, L):
        t = toks[(i - k) % L]
        if t:
            last = t; break
    ids = [prev(1), prev(2), prev(3), prev(4), prev(8), prev(16), prev(L // 2), last]
    return ids, i % 16, [1.0 if i >= L // 2 else 0.0, 1.0 if L - i <= 4 else 0.0]


def build_dataset(phrases):
    X_ids, X_pos, X_fl, Y = [], [], [], []
    for p in phrases:
        toks = [tok(o) for o in p]
        for i in range(len(toks)):
            ids, pos, fl = features(toks, i)
            X_ids.append(ids); X_pos.append(pos); X_fl.append(fl); Y.append(toks[i])
    return (np.array(X_ids, np.int32), np.array(X_pos, np.int32), np.array(X_fl, np.float32), np.array(Y, np.int32))


def init(shape, fan):
    return (rng.standard_normal(shape) * np.sqrt(2.0 / fan)).astype(np.float32)


def train():
    user = read_user_loops()
    phrases = [phrase() for _ in range(24000)] + user * 20
    ids, pos, fl, y = build_dataset(phrases)
    n = len(y)
    print(f'{len(phrases)} loops ({len(user)} of your own), {n} training steps')
    perm = rng.permutation(n); split = int(n * .95)
    tr, va = perm[:split], perm[split:]

    P = {
        'E': (rng.standard_normal((VOCAB, EMB)) * .3).astype(np.float32),
        'W1': init((NIN, H1), NIN), 'b1': np.zeros(H1, np.float32),
        'W2': init((H1, H2), H1), 'b2': np.zeros(H2, np.float32),
        'W3': init((H2, VOCAB), H2), 'b3': np.zeros(VOCAB, np.float32),
    }
    M = {k: np.zeros_like(v) for k, v in P.items()}
    V = {k: np.zeros_like(v) for k, v in P.items()}

    def forward(idx, P):
        B = len(idx)
        e = P['E'][ids[idx]].reshape(B, CTX * EMB)
        onehot = np.zeros((B, NPOS), np.float32); onehot[np.arange(B), pos[idx]] = 1
        x = np.concatenate([e, onehot, fl[idx]], 1)
        z1 = x @ P['W1'] + P['b1']; h1 = np.maximum(z1, 0)
        z2 = h1 @ P['W2'] + P['b2']; h2 = np.maximum(z2, 0)
        lo = h2 @ P['W3'] + P['b3']
        return x, z1, h1, z2, h2, lo

    def evaluate(P, idx):
        lo = forward(idx, P)[-1]
        lo -= lo.max(1, keepdims=True)
        lp = lo - np.log(np.exp(lo).sum(1, keepdims=True))
        return float(-lp[np.arange(len(idx)), y[idx]].mean()), float((lo.argmax(1) == y[idx]).mean())

    lr, t, B = 2e-3, 0, 512
    for epoch in range(10):
        rng.shuffle(tr)
        for s in range(0, len(tr) - B + 1, B):
            idx = tr[s:s + B]
            x, z1, h1, z2, h2, lo = forward(idx, P)
            lo -= lo.max(1, keepdims=True)
            pr = np.exp(lo); pr /= pr.sum(1, keepdims=True)
            g = pr; g[np.arange(B), y[idx]] -= 1; g /= B
            G = {'W3': h2.T @ g, 'b3': g.sum(0)}
            d2 = (g @ P['W3'].T) * (z2 > 0)
            G['W2'] = h1.T @ d2; G['b2'] = d2.sum(0)
            d1 = (d2 @ P['W2'].T) * (z1 > 0)
            G['W1'] = x.T @ d1; G['b1'] = d1.sum(0)
            dx = (d1 @ P['W1'].T)[:, :CTX * EMB].reshape(B, CTX, EMB)
            GE = np.zeros_like(P['E']); np.add.at(GE, ids[idx], dx); G['E'] = GE
            t += 1
            for k in P:
                M[k] = .9 * M[k] + .1 * G[k]; V[k] = .999 * V[k] + .001 * G[k] ** 2
                P[k] -= lr * (M[k] / (1 - .9 ** t)) / (np.sqrt(V[k] / (1 - .999 ** t)) + 1e-8)
        lr *= .8
        print(f'epoch {epoch + 1}: val loss {evaluate(P, va)[0]:.3f}, accuracy {evaluate(P, va)[1] * 100:.1f}%')
    return P, (lambda Q: evaluate(Q, va))


def quantize(a):
    s = float(np.abs(a).max()) / 127.0 or 1.0
    return np.clip(np.round(a / s), -127, 127).astype(np.int8), s


def export(P, evaluate):
    Q = {}
    lines = ['/* NEURAL JOHNNY - melody model weights. Generated by johnny/tools/train_model.py; do not edit. */',
             '#pragma once', '#include <stdint.h>',
             f'enum {{ MM_VOCAB = {VOCAB}, MM_EMB = {EMB}, MM_CTX = {CTX}, MM_NPOS = {NPOS}, MM_NIN = {NIN}, MM_H1 = {H1}, MM_H2 = {H2} }};']
    total = 0
    for name in ['E', 'W1', 'W2', 'W3']:
        q, s = quantize(P[name]); Q[name] = q.astype(np.float32) * s; total += q.size
        flat = q.T.flatten() if name != 'E' else q.flatten()  # W stored [out][in] for C
        body = ',\n'.join('  ' + ','.join(str(int(v)) for v in flat[i:i + 24]) for i in range(0, len(flat), 24))
        lines.append(f'static const int8_t mm_{name}[{flat.size}] = {{\n{body}\n}};')
        lines.append(f'static const float mm_{name}_scale = {s:.9g}f;')
    for name in ['b1', 'b2', 'b3']:
        Q[name] = P[name]
        lines.append(f'static const float mm_{name}[{P[name].size}] = {{' + ','.join(f'{v:.6g}f' for v in P[name]) + '};')
    lines.append(f'/* {total} int8 weights + {sum(P[b].size for b in ("b1", "b2", "b3"))} float biases */')
    open(OUT, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    lf, af = evaluate(P); lq, aq = evaluate(Q)
    print(f'float  model: loss {lf:.3f} acc {af * 100:.1f}%')
    print(f'int8   model: loss {lq:.3f} acc {aq * 100:.1f}%  -> {OUT} ({total} bytes of weights)')
    np.savez(os.path.join(ROOT, 'johnny', 'tools', 'model_float.npz'), **P)


if __name__ == '__main__':
    P, ev = train()
    export(P, ev)
