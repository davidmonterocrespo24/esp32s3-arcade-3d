# validate_circuits.py dump0 dump1 dump2 : independent check of the circuit rules
#   (dumps from circuit_dump; tools/check_circuits.sh builds and runs both)
#   layout : segment count 1500..2200, closed plan (< 1 segment), total turn 360,
#            flat & straight start/finish (grid area), level start = 0
#   props  : nothing in a tunnel or at its exit, nothing on the road / rumble,
#            nothing through a facade, nothing inside a rail, land props off
#            the water, sea props on the water, nothing near a lamp at the kerb,
#            start gantry area clear, one sprite type in range
#   flags  : no lamps / rails / crosswalks in tunnels, no crosswalk near start,
#            start flag only on segment 0
import sys, math
MAT_SIDEWALK, MAT_SAND, MAT_WATER = 1, 2, 3
SAILBOAT = 28
bad_total = 0
for path in sys.argv[1:]:
    segs, H, name = [], {}, ''
    for line in open(path):
        p = line.split()
        if not p: continue
        if p[0] == 'CIRCUIT': name = ' '.join(p[2:-1])
        elif p[0] == 'S':
            segs.append(dict(i=int(p[1]), c=float(p[2]), y=float(p[3]), f=int(p[4]), g=int(p[5]),
                             b=(int(p[6]), int(p[8])), spr=(int(p[10]), int(p[12])), off=(int(p[11]) / 16.0, int(p[13]) / 16.0)))
        elif p[0] == 'H': H[int(p[1])] = (float(p[2]), float(p[3]))
    N = len(segs)
    errs = []
    def err(i, msg):
        if len(errs) < 25: errs.append('  seg %d: %s' % (i, msg))
        else: errs.append(None)
    # layout
    if not (1500 <= N <= 2200): err(-1, 'segment count %d' % N)
    h = x = y = 0.0
    for s in segs:
        h += s['c'] / 200.0
        x += math.sin(h); y -= math.cos(h)
    if abs(math.degrees(h) - 360) > 0.5: err(-1, 'total turn %.2f' % math.degrees(h))
    if math.hypot(x, y) > 1.0: err(-1, 'plan does not close: %.2f %.2f' % (x, y))
    for i in list(range(N - 30, N)) + list(range(0, 8)):
        s = segs[i % N]
        if abs(s['c']) > 1e-6 or abs(s['y']) > 1e-3: err(i % N, 'grid area not flat/straight (c %.2f y %.1f)' % (s['c'], s['y']))
    for i, s in enumerate(segs):
        tun = s['f'] & 1
        prev_tun = segs[i - 1]['f'] & 1
        if tun and s['f'] & (0x02 | 0x04 | 0x08 | 0x20 | 0x40): err(i, 'lamp/crosswalk/rail flag in a tunnel')
        if (s['f'] & 0x10) and i != 0: err(i, 'start flag off segment 0')
        if (s['f'] & 0x08) and (i < 6 or i > N - 60): err(i, 'crosswalk near the start')
        if tun and (s['b'][0] or s['b'][1]): err(i, 'building in a tunnel')
        for side in (0, 1):
            t = s['spr'][side]
            if t < 0: continue
            hit, fw = H[t]
            d = s['off'][side]
            inner, outer = d - fw / 2, d + fw / 2
            mat = (s['g'] >> 4) if side else (s['g'] & 15)
            rail = s['f'] & (0x40 if side else 0x20)
            lamp = s['f'] & (0x04 if side else 0x02)
            if tun or prev_tun: err(i, 'sprite %d in a tunnel / at its exit' % t)
            if inner < 1.12 - 1e-3: err(i, 'sprite %d on the road (inner %.2f)' % (t, inner))
            if s['b'][side] and outer > 1.49 + 1e-3: err(i, 'sprite %d through a facade (outer %.2f)' % (t, outer))
            if rail and inner < 1.30 - 1e-3: err(i, 'sprite %d inside the rail (inner %.2f)' % (t, inner))
            if lamp and inner < 1.62 - 1e-3: err(i, 'sprite %d at a lamp (inner %.2f)' % (t, inner))
            if (i <= 1 or i >= N - 2) and inner < 1.62 - 1e-3: err(i, 'sprite %d at the start gantry' % t)
            if t == SAILBOAT:
                if mat not in (MAT_WATER, MAT_SAND) or (mat == MAT_SAND and inner < 2.9) or (mat == MAT_WATER and inner < 1.5):
                    err(i, 'sailboat on land (mat %d inner %.2f)' % (mat, inner))
            else:
                if mat == MAT_WATER: err(i, 'land prop %d on the water' % t)
                if mat == MAT_SAND and outer > 2.6: err(i, 'land prop %d in the sea (outer %.2f)' % (t, outer))
    counts = {}
    for s in segs:
        for t in s['spr']:
            if t >= 0: counts[t] = counts.get(t, 0) + 1
    NAMES = ['pine', 'tree', 'bush', 'rock', 'post', 'palm', 'pineSnow', 'cypress', 'flowers', 'grass', 'boulder',
             'fence', 'chevL', 'chevR', 'board3', 'board2', 'board1', 'bbA', 'bbB', 'bbC', 'lighthouse', 'umbrella',
             'trafficLight', 'busStop', 'planter', 'streetTree', 'neonPink', 'neonCyan', 'sailboat', 'snowPole',
             'chalet', 'flag', 'umbrellaB']
    print('  sprites: ' + ', '.join('%s %d' % (NAMES[t] if t < len(NAMES) else t, n) for t, n in sorted(counts.items())))
    n_err = sum(1 for e in errs)
    bad_total += n_err
    print('%-13s %4d segments: %s' % (name, N, 'OK' if not n_err else '%d problems' % n_err))
    for e in errs:
        if e: print(e)
sys.exit(1 if bad_total else 0)
