#!/usr/bin/env python3
# kmap-compose.py: writes compose.kmap, what each dead key makes of the
# letters the layouts type, from unicode's own composition. it was run once,
# after kmap-vendor: python3 scripts/kmap/kmap-compose.py share/kmap
import os, sys, unicodedata

COMBINING = {
    "grave": 0x300, "acute": 0x301, "circumflex": 0x302, "tilde": 0x303,
    "macron": 0x304, "breve": 0x306, "abovedot": 0x307, "diaeresis": 0x308,
    "hook": 0x309, "abovering": 0x30A, "doubleacute": 0x30B, "caron": 0x30C,
    "doublegrave": 0x30F, "invertedbreve": 0x311, "abovecomma": 0x313,
    "abovereversedcomma": 0x314, "horn": 0x31B, "belowdot": 0x323,
    "belowcomma": 0x326, "cedilla": 0x327, "ogonek": 0x328,
    "belowmacron": 0x331, "iota": 0x345,
}
SPACING = {
    "grave": "`", "acute": "´", "circumflex": "^", "tilde": "~",
    "macron": "¯", "breve": "˘", "abovedot": "˙",
    "diaeresis": "¨", "abovering": "˚", "doubleacute": "˝",
    "caron": "ˇ", "cedilla": "¸", "ogonek": "˛", "iota": "ͺ",
}
# a stroke is no combining mark unicode composes with
STROKE = dict(zip("bdghiloOtzBDGHILTZ", "ƀđǥħɨłøØŧƶɃĐǤĦƗŁŦƵ"))

def word(c):
    if c == "'":
        return "''''"
    if c == "#":
        return "'#'"
    if c == " ":
        return "space"
    return c

root = sys.argv[1]
bases = set()
for d, _, files in os.walk(root):
    for f in files:
        if not f.endswith(".kmap") or f == "compose.kmap":
            continue
        for line in open(os.path.join(d, f), encoding="utf-8"):
            w = line.split()
            if w and w[0] == "key":
                for s in w[2:6]:
                    if len(s) == 1 and unicodedata.category(s).startswith("L"):
                        bases.add(s)
out = ["kmap 1", "id compose",
       "# what a dead key followed by a letter types, for every layout"]
for name in sorted(COMBINING):
    for b in sorted(bases):
        r = unicodedata.normalize("NFC", b + chr(COMBINING[name]))
        if len(r) == 1 and r != b:
            out.append("compose dead_%s %s %s" % (name, word(b), word(r)))
    if name in SPACING:
        out.append("compose dead_%s space %s" % (name, word(SPACING[name])))
        out.append("compose dead_%s dead_%s %s" % (name, name, word(SPACING[name])))
for b, r in STROKE.items():
    out.append("compose dead_stroke %s %s" % (b, r))
open(os.path.join(root, "compose.kmap"), "w", encoding="utf-8").write("\n".join(out) + "\n")
print(len(out) - 3, "compositions")
