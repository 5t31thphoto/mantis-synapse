# Split RIG_SCYTHE into the femur (RIG_SCYTHE) and the hinged tibia/hook (RIG_CLAW) so the pincer can open and close.
# Hinge = the knee at top-right of the scythe sprite. Run once; rewrites src/mantis_rig.h in place.
import re, sys
path = sys.argv[1]
src = open(path).read()
m = re.search(r'static const uint16_t RIG_SCYTHE_PX\[(\d+)\] = \{(.*?)\};', src, re.S)
vals = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', m.group(2))]
W, H, KEY = 78, 69, 0xF81F
HX, HY = 54, 10                       # hinge (knee) in scythe sprite space
X0 = 49                               # claw sprite starts at this column
def in_claw(x, y): return x >= 54 or (x >= X0 and y < 12)
fem = [v if not in_claw(i % W, i // W) else KEY for i, v in enumerate(vals)]
CW = W - X0
claw = []
for y in range(H):
    for x in range(X0, W):
        v = vals[y * W + x]
        claw.append(v if in_claw(x, y) else KEY)
def arr(name, a):
    out = 'static const uint16_t %s[%d] = {\n' % (name, len(a))
    for i in range(0, len(a), 16): out += '  ' + ','.join('0x%04X' % v for v in a[i:i + 16]) + ',\n'
    return out + '};\n'
new_scy = arr('RIG_SCYTHE_PX', fem)
src = src[:m.start()] + new_scy[:-1] + src[m.end():]
tipx, tipy = 61.56, 66.91
claw_block = '\n// the hinged hook (tibia): pivot = the knee; open/close by rotating about it\n' + arr('RIG_CLAW_PX', claw) + \
  'static const RigPart RIG_CLAW = { RIG_CLAW_PX, %d, %d, %.2ff, %.2ff };\n' % (CW, H, HX - X0, HY) + \
  'static const float RIG_CLAW_TIP_X = %.2ff, RIG_CLAW_TIP_Y = %.2ff;\n' % (tipx - X0, tipy) + \
  'static const float RIG_SCYTHE_HINGE_X = %.2ff, RIG_SCYTHE_HINGE_Y = %.2ff;\n' % (HX, HY)
anchor = 'static const float RIG_SCYTHE_TIP_X'
k = src.index(anchor); k = src.index('\n', k) + 1
src = src[:k] + claw_block + src[k:]
open(path, 'w').write(src)
print('split ok: femur keeps pivot; claw', CW, 'x', H)
