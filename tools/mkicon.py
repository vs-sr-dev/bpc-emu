# res/bpc-emu.png and res/bpc-emu.ico (the Windows icon built into bpc-emu.exe): a handheld
# drawn on a 32x32 pixel grid -- the case, the LCD with a line of text and a cursor, the pad,
# the Circle button -- scaled up without smoothing. Needs Pillow. After MobiGoEmu's, same author.
import os
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

CASE, EDGE, SHADE = (0xE8, 0xEC, 0xF0, 255), (0x40, 0x4C, 0x60, 255), (0xB8, 0xC0, 0xCC, 255)
LCD, INK = (0xB8, 0xC8, 0xA8, 255), (0x34, 0x40, 0x34, 255)
PAD, BUTTON = (0x30, 0x38, 0x48, 255), (0xE0, 0x50, 0x40, 255)

img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle((1, 6, 30, 25), radius=4, fill=CASE, outline=EDGE)
d.line((3, 24, 28, 24), fill=SHADE)
# the LCD: a title bar, two lines of "text", the answer's square
d.rectangle((9, 9, 22, 21), fill=EDGE)
d.rectangle((10, 10, 21, 20), fill=LCD)
d.rectangle((10, 10, 21, 11), fill=INK)
for y in (14, 17):
    for x in range(13, 21, 2):
        d.point((x, y), fill=INK)
d.rectangle((11, 13, 11, 14), fill=INK)
d.rectangle((11, 16, 12, 17), fill=INK)
# the pad and the Circle button
d.rectangle((3, 14, 7, 16), fill=PAD)
d.rectangle((4, 13, 6, 17), fill=PAD)
d.point((5, 15), fill=SHADE)
d.ellipse((24, 13, 28, 17), fill=BUTTON, outline=EDGE)
d.point((26, 10), fill=PAD)
d.point((26, 20), fill=PAD)

big = img.resize((256, 256), Image.NEAREST)
os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
big.save(os.path.join(ROOT, "res", "bpc-emu.png"))
big.save(os.path.join(ROOT, "res", "bpc-emu.ico"),
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
