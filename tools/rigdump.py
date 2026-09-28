# dump a rig sprite from mantis_rig.h to PNG (magnified, with a grid) so we can pick hinge points
import re, sys
from PIL import Image, ImageDraw
src = open(sys.argv[1]).read()
def part(name):
    arr = re.search(r'static const uint16_t %s_PX\[(\d+)\] = \{(.*?)\};' % name, src, re.S)
    vals = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', arr.group(2))]
    meta = re.search(r'static const RigPart %s = \{ %s_PX, (\d+), (\d+), ([\d.]+)f, ([\d.]+)f \};' % (name, name), src)
    w, h = int(meta.group(1)), int(meta.group(2))
    return vals, w, h, float(meta.group(3)), float(meta.group(4))
def to_img(vals, w, h, S=6):
    im = Image.new('RGB', (w * S, h * S), (40, 40, 40)); d = ImageDraw.Draw(im)
    for i, v in enumerate(vals):
        if v == 0xF81F: continue
        x, y = i % w, i // w
        r, g, b = ((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3
        d.rectangle([x * S, y * S, x * S + S - 1, y * S + S - 1], fill=(r, g, b))
    for x in range(0, w, 10): d.line([x * S, 0, x * S, h * S], fill=(90, 90, 90))
    for y in range(0, h, 10): d.line([0, y * S, w * S, y * S], fill=(90, 90, 90))
    return im
for name in sys.argv[2:]:
    vals, w, h, px, py = part(name)
    im = to_img(vals, w, h); d = ImageDraw.Draw(im)
    d.ellipse([px * 6 - 5, py * 6 - 5, px * 6 + 5, py * 6 + 5], outline=(255, 255, 0), width=2)
    im.save('/tmp/%s.png' % name); print(name, w, h, px, py)
