#!/usr/bin/env python3
"""Render what a T-Deck shows, from the sketch's own screens.

    python3 tdeck_preview.py out ../docs/img/tdeck      (after `make sketch`)

The top 320x156 is each e-ink framebuffer that render_eink produced, pushed
through the same integer scaler TDeckPanel::update() runs on the device
(tdeck_hw.h) — taps, weights, the 3/2 gain and the 33-step colour ramp are
reproduced exactly, so these are the pixels the TFT gets. The status strip
underneath is drawn with the same Adafruit 5x7 font the firmware uses; its
text is a representative sample, since the strip's content is live state.
"""
import os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_W, SRC_H, W, H, GAME_H = 250, 122, 320, 240, 156

THEMES = {  # tdeck_hw.h TDECK_THEMES
    "phosphor": ((4, 10, 6), (60, 255, 120)),
    "amber":    ((10, 6, 0), (255, 176, 0)),
    "paper":    ((232, 230, 220), (16, 16, 16)),
    "ice":      ((2, 6, 14), (110, 200, 255)),
}

def rgb565(c):  # what the panel can actually show
    r, g, b = c
    return (r & 0xF8, g & 0xFC, b & 0xF8)

def ramp(theme):
    paper, ink = THEMES[theme]
    return [rgb565(tuple(paper[k] + (ink[k] - paper[k]) * i // 32 for k in range(3)))
            for i in range(33)]

def taps(S, D):
    out = []
    for d in range(D):
        a, b = d * S, (d + 1) * S
        s0 = a // D; bnd = (s0 + 1) * D
        w0 = 64 if b <= bnd else ((bnd - a) * 64 + S // 2) // S
        out.append((s0, min(s0 + 1, S - 1), w0))
    return out

def load_pbm(path):
    tok = open(path).read().split()
    w, h = int(tok[1]), int(tok[2]); bits = "".join(tok[3:])
    return [[bits[y * w + x] == "1" for x in range(w)] for y in range(h)]

def font():
    src = open(os.path.join(HERE, "stub", "heltec-eink-modules.h")).read()
    body = src[src.index("GFX_FONT5X7"):src.index("};")]
    nums = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    return [nums[i:i + 5] for i in range(0, len(nums), 5)]
FONT = font()

def text(img, x, y, s, col, size=1):
    for ch in s:
        o = ord(ch)
        g = FONT[o - 32] if 32 <= o <= 126 else FONT[ord("?") - 32]
        for cx in range(5):
            for cy in range(8):
                if g[cx] & (1 << cy):
                    for dx in range(size):
                        for dy in range(size):
                            px, py = x + cx * size + dx, y + cy * size + dy
                            if 0 <= px < W and 0 <= py < H:
                                img.putpixel((px, py), col)
        x += 6 * size

def render(pbm, strip, theme="phosphor"):
    fb, lut = load_pbm(pbm), ramp(theme)
    # TDeckPanel::paperFrame: a QR frame is always dark-on-light.
    game = ramp("paper") if "qr" in os.path.basename(pbm) else lut
    tx, ty = taps(SRC_W, W), taps(SRC_H, GAME_H)
    img = Image.new("RGB", (W, H), lut[0])
    for dy, (y0, y1, wy0) in enumerate(ty):
        wy1 = 64 - wy0
        for dx, (x0, x1, wx0) in enumerate(tx):
            wx1 = 64 - wx0
            v = (fb[y0][x0] * wx0 * wy0 + fb[y0][x1] * wx1 * wy0 +
                 fb[y1][x0] * wx0 * wy1 + fb[y1][x1] * wx1 * wy1)
            v = min(32, ((v >> 7) * 3) >> 1)
            img.putpixel((dx, dy), game[v])
    # tdeck_ui.h: rule at y=157, rows at 161/172/186/205/228, tones dim/mid/ink.
    tone = {0: lut[12], 1: lut[22], 2: lut[32]}
    for x in range(W): img.putpixel((x, GAME_H + 1), lut[12])
    rows = [(161, 1, 1), (172, 1, 1), (186, 2, 2), (205, 2, 2), (228, 1, 0)]
    for (y, size, t), s in zip(rows, strip):
        if s:
            if size == 2 and len(s) * 12 > W: size, y = 1, y + 4
            text(img, 4, y, s, tone[t], size)
    return img

NORMAL = ["LORA LONG 869.525   3 in range", "WIFI C32_B_GhostByte  > 192.168.4.1",
          "", "", "ball:page M:msg Q:QR B:beacon T:theme"]
SHOTS = [
    ("idle",         "eink-idle",         NORMAL, "phosphor"),
    ("census",       "eink-page-census",  NORMAL, "phosphor"),
    ("lastmsg",      "eink-page-lastmsg", NORMAL, "amber"),
    ("hack-win",     "eink-hack-win",     NORMAL[:2] + ["Beacon sent", ""] + NORMAL[4:], "phosphor"),
    ("compose",      "eink-idle",
     ["MSG TO  NULLBYTE   2/3", "ball=who  ENTER=send  BKSP=erase/close",
      "> meet at the fountain_", "", "22/32"], "phosphor"),
    ("setup",        "eink-setup-qr",
     ["LORA LONG 869.525   0 in range", "WIFI C32_B_HexByte  > 192.168.4.1",
      "", "", "Scan the QR to set up    T:theme"], "phosphor"),
]

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name, pbm, strip, theme in SHOTS:
        out = os.path.join(dst, "tdeck-%s.png" % name)
        render(os.path.join(src, pbm + ".pbm"), strip, theme) \
            .resize((W * 2, H * 2), Image.NEAREST).save(out)
        print("  %-10s -> %s" % (name, os.path.basename(out)))
