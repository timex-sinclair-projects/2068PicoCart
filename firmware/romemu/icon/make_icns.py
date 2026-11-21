#!/usr/bin/env python3
"""Build VolumeIcon.icns for the PICOCART drive from picocart_1024.png.

  make_icns.py picocart_1024.png out.icns

Every size is stored as a palette PNG (iconutil would store the small sizes as raw
ARGB, which is bigger), so the whole icon is about 25 KB: it lives in the firmware
and on the 1.4 MB drive. picocart_1024.png is picocart.svg rendered at 1024 px.
"""
import io, struct, sys
from PIL import Image

TYPES = [("icp4", 16), ("icp5", 32), ("ic07", 128), ("ic08", 256), ("ic09", 512),
         ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)]
src = Image.open(sys.argv[1]).convert("RGBA")
cache, chunks = {}, b""
for t, s in TYPES:
    if s not in cache:
        buf = io.BytesIO()
        src.resize((s, s), Image.LANCZOS).quantize(128, method=Image.Quantize.FASTOCTREE).save(buf, "PNG", optimize=True)
        cache[s] = buf.getvalue()
    chunks += t.encode() + struct.pack(">I", 8 + len(cache[s])) + cache[s]
open(sys.argv[2], "wb").write(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)
print(f"wrote {sys.argv[2]}: {8 + len(chunks)} bytes")
