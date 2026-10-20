"""Line-diff the sim lines of two RH battery log dirs (timestamps stripped, autosave byte counts masked)."""
import sys, os, re, difflib
pre, post = sys.argv[1], sys.argv[2]
SHOW = int(sys.argv[3]) if len(sys.argv) > 3 else 12
def sim_lines(p):
    out = []
    for l in open(p, errors='replace'):
        if 'LogRedHopeSim:' not in l:
            continue
        l = re.sub(r'^\[[^\]]*\]\[\s*\d+\]', '', l.rstrip('\n'))
        l = re.sub(r'\(\d+ bytes', '(N bytes', l)
        out.append(l)
    return out
total = 0
for name in sorted(os.listdir(post)):
    if not name.endswith('.log'):
        continue
    a = os.path.join(pre, name)
    b = sim_lines(os.path.join(post, name))
    if not os.path.exists(a):
        print(f"{name}: NEW SUITE ({len(b)} sim lines)")
        continue
    la = sim_lines(a)
    d = [x for x in difflib.unified_diff(la, b, lineterm='', n=0) if x[:1] in '+-' and not x.startswith(('+++', '---'))]
    total += len(d)
    print(f"{name}: {len(la)} -> {len(b)} sim lines, {len(d)} differing")
    for x in d[:SHOW]:
        print('    ' + x[:240])
print(f"TOTAL differing lines: {total}")
