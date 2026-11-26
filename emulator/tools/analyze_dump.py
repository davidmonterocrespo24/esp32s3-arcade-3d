"""Analyse a headless --dump 1 log.

  car_game_headless out 4700 --race C --auto --dump 1 > log.txt
  python tools/analyze_dump.py log.txt [--verbose]

Checks:
  * overlaps : two car footprints (CAR_LEN x 2*CAR_HALF_W) intersect in the
               dumped (post-resolution) state; longest run of frames per pair
  * contacts : footprints touching (within 15 units / 0.01 half-widths) = bumps
  * lateral  : largest |x(f+1) - x(f)| per racer (teleport / jitter check)
  * jitter   : lateral direction reversals with |dx| > 0.004 in 3 consecutive frames
  * overtakes: order swaps between consecutive frames while racing
  * laps     : lap counter only steps by +1; finish order == classification
"""
import re, sys
from collections import defaultdict

CAR_LEN = 1100.0
W2 = 0.24

line_re = re.compile(r'^f=(\d+) (\S+) st=([\d.]+) t=([\d.]+) cyc=([\d.]+) \|(.*)$')
racer_re = re.compile(r' (\S+) P(\d) L(\d+) z=(-?\d+) x=(-?[\d.]+) v=(-?\d+)( FIN)?( CRASH)? \|')

def wrap(d, L):
    h = L / 2
    while d >= h: d -= L
    while d < -h: d += L
    return d

def main(path, verbose=False):
    frames = []
    track = None
    classification = []
    for ln in open(path):
        m = line_re.match(ln)
        if m:
            f = int(m.group(1)); state = m.group(2); t = float(m.group(4))
            rs = racer_re.findall(m.group(6))
            racers = [dict(name=r[0], place=int(r[1]), lap=int(r[2]), z=float(r[3]), x=float(r[4]),
                           v=int(r[5]), fin=bool(r[6]), crash=bool(r[7])) for r in rs]
            frames.append((f, state, t, racers))
            continue
        m = re.match(r'classification \((.*), (\d+) segments, lap (\d+) units\)', ln)
        if m: track = float(m.group(3))
        m = re.match(r'\s+(\d)\. (\S+)\s+(FIN|est)\s+([\d.]+) s', ln)
        if m: classification.append((int(m.group(1)), m.group(2), m.group(3), float(m.group(4))))
    if track is None:
        print('no classification line: pass the lap length'); return
    # de-duplicate frame numbers (the last dump repeats)
    seen = set(); fr = []
    for x in frames:
        if x[0] in seen: continue
        seen.add(x[0]); fr.append(x)
    frames = fr
    n = len(frames[0][3]); names = [r['name'] for r in frames[0][3]]

    overlapRun = defaultdict(int); overlapMax = defaultdict(int); overlapTot = defaultdict(int)
    contactTot = defaultdict(int); contactEvents = defaultdict(int); prevContact = set()
    contactFrames = defaultdict(list)
    latMax = [0.0] * n; latWhere = [0] * n; latBig = [0] * n
    jitter = [0] * n
    overtakes = []; placeChanges = 0
    lapErr = []
    hist = [[] for _ in range(n)]
    racing = 0
    for k, (f, state, t, rs) in enumerate(frames):
        live = state in ('RACE', 'FINISH', 'RESULTS', 'STANDINGS', 'DEMO') and t > 0
        if live: racing += 1
        cur = set()
        for a in range(n):
            for b in range(a + 1, n):
                A, B = rs[a], rs[b]
                dz = abs(wrap(B['z'] - A['z'], track)); dx = abs(B['x'] - A['x'])
                key = (names[a], names[b])
                if live and dz < CAR_LEN - 2 and dx < W2 - 0.011:
                    overlapRun[key] += 1; overlapTot[key] += 1
                    overlapMax[key] = max(overlapMax[key], overlapRun[key])
                else:
                    overlapRun[key] = 0
                if live and dz < CAR_LEN + 15 and dx < W2 + 0.011:
                    contactTot[key] += 1; cur.add(key)
                    if key not in prevContact:
                        contactEvents[key] += 1; contactFrames[key].append(f)
        prevContact = cur
        for i in range(n):
            hist[i].append(rs[i]['x'])
        if k > 0 and live:
            pf, pstate, pt, prs = frames[k - 1]
            for i in range(n):
                d = abs(rs[i]['x'] - prs[i]['x'])
                if d > latMax[i]: latMax[i] = d; latWhere[i] = f
                if d > 0.06: latBig[i] += 1
                if rs[i]['lap'] not in (prs[i]['lap'], prs[i]['lap'] + 1): lapErr.append((f, names[i]))
            po = sorted(range(n), key=lambda i: prs[i]['place']); no = sorted(range(n), key=lambda i: rs[i]['place'])
            if po != no:
                placeChanges += 1
                for i in range(n):
                    if rs[i]['place'] < prs[i]['place']:
                        overtakes.append((f, t, names[i], prs[i]['place'], rs[i]['place']))
        if k > 2 and live:
            for i in range(n):
                x0, x1, x2, x3 = hist[i][-4:]
                d1, d2, d3 = x1 - x0, x2 - x1, x3 - x2
                if abs(d1) > 0.004 and abs(d2) > 0.004 and abs(d3) > 0.004 and d1 * d2 < 0 and d2 * d3 < 0:
                    jitter[i] += 1

    print(f'frames {len(frames)} (racing {racing}), lap {track:.0f}')
    print('overlaps (post-resolution) : ' + (', '.join(f'{a}-{b}: {overlapTot[(a,b)]} frames (max run {overlapMax[(a,b)]})'
          for (a, b) in overlapTot) or 'none'))
    print('contacts (touching frames / events):')
    for key in sorted(contactTot, key=lambda k: -contactTot[k]):
        print(f'   {key[0]}-{key[1]}: {contactTot[key]} frames, {contactEvents[key]} events at f ' +
              ' '.join(str(x) for x in contactFrames[key][:12]))
    print('lateral step max per frame : ' + ', '.join(f'{names[i]} {latMax[i]:.3f}@{latWhere[i]}' for i in range(n)))
    print('frames with step > 0.06     : ' + ', '.join(f'{names[i]} {latBig[i]}' for i in range(n)))
    print('lateral jitter (3 reversals): ' + ', '.join(f'{names[i]} {jitter[i]}' for i in range(n)))
    print(f'place changes: {placeChanges} frames, {len(overtakes)} gains')
    pl = [o for o in overtakes if o[2] == names[0]]
    print(f'   {names[0]} gains: ' + ', '.join(f'f{o[0]} P{o[3]}->P{o[4]}' for o in pl[:20]))
    others = [o for o in overtakes if o[2] != names[0]]
    print('   rival gains: ' + ', '.join(f'f{o[0]} {o[2]} P{o[3]}->P{o[4]}' for o in others[:30]))
    print('lap counter errors: ' + (str(lapErr[:10]) if lapErr else 'none'))
    if classification:
        print('classification: ' + ', '.join(f'{p}.{nm} {kind} {tm:.2f}' for p, nm, kind, tm in classification))
        fins = [c for c in classification if c[2] == 'FIN']
        ok = all(fins[i][3] <= fins[i + 1][3] for i in range(len(fins) - 1))
        print('finish times ascending: ' + ('yes' if ok else 'NO'))
    if verbose:
        for f, state, t, rs in frames[::300]:
            print(f'  f{f} t={t:6.1f} ' + ' '.join(f"{r['name']}:P{r['place']}" for r in sorted(rs, key=lambda r: r['place'])))

if __name__ == '__main__':
    main(sys.argv[1], '--verbose' in sys.argv)
