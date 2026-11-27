import sys, glob, os
from PIL import Image
# usage: sheet.py out.png scale img1.ppm img2.ppm ...
out, scale, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
ims = [Image.open(f).convert('RGB') for f in files]
w, h = ims[0].size
cols = 2 if len(ims) > 1 else 1
rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w * scale + (cols - 1) * 4, rows * h * scale + (rows - 1) * 4), (255, 255, 255))
for i, im in enumerate(ims):
    im = im.resize((w * scale, h * scale), Image.NEAREST)
    sheet.paste(im, ((i % cols) * (w * scale + 4), (i // cols) * (h * scale + 4)))
sheet.save(out)
