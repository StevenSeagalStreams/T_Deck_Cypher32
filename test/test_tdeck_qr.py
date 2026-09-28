#!/usr/bin/env python3
"""The join QR must still scan after the T-Deck's 1.28x scaler, in every theme.

The scaler anti-aliases, so QR modules land on the TFT 3 or 4 pixels wide with
grey edges, and the dark themes would show it light-on-dark — which most phone
cameras will not read. TDeckPanel forces QR frames to dark-on-light; this
checks the result actually decodes. Needs `make sketch` first (for out/).
"""
import os, sys
try:
    import cv2, numpy as np
except ImportError:
    print("opencv not installed — skipping T-Deck QR scan"); sys.exit(0)
import tdeck_preview as T

bad = 0
want = None
for theme in T.THEMES:
    img = T.render("out/eink-setup-qr.pbm", [""] * 5, theme)
    got = cv2.QRCodeDetector().detectAndDecode(
        cv2.cvtColor(np.array(img), cv2.COLOR_RGB2BGR))[0]
    ok = got.startswith("WIFI:")
    print("  %-9s %s" % (theme, got if ok else "DID NOT SCAN"))
    bad += not ok
print("T-Deck QR: %d themes, %d failures" % (len(T.THEMES), bad))
sys.exit(1 if bad else 0)
