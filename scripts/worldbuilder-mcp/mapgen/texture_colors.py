# What each terrain texture looks like: for every Terrain.ini entry its file, class, size and tile size in cells (64 pixels per cell,
# TILE_PIXEL_EXTENT), mean colour, brightness (Rec. 709 luma), saturation, hue, and variation (luma spread within it, how busy it looks).
# Read from the game's archives in place (bigfs.py). Terrain textures rarely change: run again when they do.
# Usage: texture_colors.py [out dir, default work/]   Writes textures.json and textures.png (a swatch per texture, sorted by class).
import colorsys, io, json, os, re, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bigfs

CELL_PIXELS = 64


def terrain_entries(text):
    out, cur = [], None
    for line in text.splitlines():
        s = line.split(";", 1)[0].strip()
        m = re.match(r"Terrain\s+(\S+)", s)
        if m:
            cur = {"name": m.group(1)}
        elif s.lower() == "end" and cur is not None:
            out.append(cur)
            cur = None
        elif cur is not None and "=" in s:
            k, v = s.split("=", 1)
            cur[k.strip().lower()] = v.strip()
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "work")
    os.makedirs(out, exist_ok=True)
    fs = bigfs.BigFS()
    entries = terrain_entries(fs.read(r"data\ini\terrain.ini").decode("latin-1"))
    result, missing = {}, []
    for e in entries:
        f = e.get("texture", "")
        name = "art\\terrain\\" + f.lower()
        if not fs.exists(name):
            missing.append((e["name"], f))
            continue
        img = Image.open(io.BytesIO(fs.read(name))).convert("RGB")
        a = np.asarray(img, dtype=np.float32) / 255
        mean = a.reshape(-1, 3).mean(0)
        luma = a @ np.array([0.2126, 0.7152, 0.0722])
        h, l, s = colorsys.rgb_to_hls(*[float(v) for v in mean])
        result[e["name"]] = {
            "file": f, "class": e.get("class", ""), "blend_edges": e.get("blendedges", "").lower() == "yes",
            "size": list(img.size), "tile_cells": [img.size[0] / CELL_PIXELS, img.size[1] / CELL_PIXELS],
            "rgb": [round(float(v) * 255) for v in mean], "brightness": round(float(luma.mean()), 3),
            "saturation": round(s, 3), "hue": round(h * 360), "variation": round(float(luma.std()), 3),
            "where": os.path.basename(fs.where(name) or ""),
        }
    json.dump(result, open(os.path.join(out, "textures.json"), "w"), indent=1)
    # Swatch sheet: mean colour with a small crop of the texture, grouped by class.
    names = sorted(result, key=lambda n: (result[n]["class"], result[n]["brightness"]))
    cols, sw = 12, 96
    sheet = Image.new("RGB", (cols * sw, ((len(names) + cols - 1) // cols) * (sw + 14)), "white")
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        r = result[n]
        x, y = (i % cols) * sw, (i // cols) * (sw + 14)
        d.rectangle([x, y, x + sw - 2, y + sw // 2], fill=tuple(r["rgb"]))
        tex = Image.open(io.BytesIO(fs.read("art\\terrain\\" + r["file"].lower()))).convert("RGB").resize((sw - 2, sw // 2))
        sheet.paste(tex, (x, y + sw // 2))
        d.text((x + 2, y + sw + 1), n[:16], fill=(0, 0, 0))
    sheet.save(os.path.join(out, "textures.png"))
    print("textures", len(result), "missing files", len(missing), missing[:5])


if __name__ == "__main__":
    main()
