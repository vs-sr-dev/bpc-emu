"""A deterministic button script for bpc-cli --keys and the oracle: presses of 3-12 frames,
gaps of 5-40, now and then a long one (Esc held 3 s is a different command).

    python tools/monkey.py OUT.keys [--frames N] [--seed S] [--no-left]
"""
import random
import sys


def main():
    args = sys.argv[1:]
    out = args[0]
    frames = int(args[args.index('--frames') + 1]) if '--frames' in args else 6000
    seed = int(args[args.index('--seed') + 1]) if '--seed' in args else 1
    names = ['up', 'down', 'left', 'right', 'circle', 'clear', 'pass', 'view', 'esc']
    if '--no-left' in args:
        names.remove('left')
    weights = [6 if n in ('up', 'down', 'circle') else 2 for n in names]
    r = random.Random(seed)
    f = 120
    with open(out, 'w') as o:
        o.write('0 none\n')
        while f < frames:
            b = r.choices(names, weights)[0]
            hold = r.randint(3, 12) if r.random() > 0.03 else r.randint(160, 260)
            o.write(f'{f} {b}\n{f + hold} none\n')
            f += hold + r.randint(5, 40)


if __name__ == '__main__':
    main()
