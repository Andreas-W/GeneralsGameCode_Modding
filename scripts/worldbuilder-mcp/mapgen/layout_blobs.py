# Layout from a description instead of from mockup heights: when the user says the mockup's heightmap is only a rough guide, the plan's
# "layout" names the shapes (land blobs joined smoothly, plateaus, beaches, ramps) and this script draws an idealised mockup from them:
# flat levels, gentle slopes where beaches climb to the land, steep steps elsewhere, a seabed falling to the floor. terrain_from_mockup.py
# then turns that into the terrain as it does with a painted mockup (cliff style, ramps, beaches, connectivity).
# Rough symmetry: shapes listed once are repeated by "symmetry" (rotations about a centre), each copy with its own jitter and noise.
# Usage: layout_blobs.py <plan.json> <out dir>
#   Writes heights_new.npy (raw, vertex grid, row 0 south), passability.png (cells, nothing blocked), mock.json (kept objects from the plan,
#   plus one road segment across the step per ramp) and layout.png.
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

RAW, CELL = 0.625, 10.0


def fbm(shape, rng, scales):
    out = np.zeros(shape, np.float32)
    for s, a in scales:
        n = ndimage.gaussian_filter(rng.standard_normal(shape).astype(np.float32), s / CELL)
        out += a * n / (n.std() + 1e-9)
    return out


def radial(rng, amount, harmonics=(2, 3, 5)):
    # A blob's radius as a function of angle: a few random harmonics, so every copy of a shape has its own outline.
    terms = [(k, rng.uniform(0, 2 * math.pi), rng.uniform(0.5, 1.0) / k) for k in harmonics]
    norm = sum(a for _, _, a in terms)
    return lambda t: 1 + amount * sum(a * np.cos(k * t + p) for k, p, a in terms) / norm


def smax(a, b, k):
    # Smooth union of two signed distances (positive inside): a neck where two blobs meet instead of a sharp notch.
    h = np.clip(0.5 + 0.5 * (a - b) / k, 0, 1)
    return a * h + b * (1 - h) + k * h * (1 - h)


def copies(item, sym, rng):
    # The item itself and its rotated copies, each moved by a little jitter: rough symmetry, not mirrored.
    out = []
    n = sym.get("count", 1) if item.get("repeat", True) else 1
    cx, cy = sym.get("center", [0, 0])
    for i in range(n):
        a = math.radians(360.0 * i / n + rng.uniform(-1, 1) * sym.get("jitter_deg", 0))
        x, y = item["at"]
        dx, dy = x - cx, y - cy
        c = dict(item, at=[cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)], turn=math.degrees(a), copy=i)
        if "radius" in item:
            c["radius"] = item["radius"] * (1 + rng.uniform(-1, 1) * sym.get("jitter_size", 0))
            # Per copy scale, to even out what the jitter and noise did (e.g. equal building area for every player).
            c["radius"] *= item.get("scale", [1] * n)[i]
        if "angle" in item:
            c["angle"] = item["angle"] + math.degrees(a) + rng.uniform(-1, 1) * sym.get("jitter_deg", 0)
        out.append(c)
    return out


def main():
    plan = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    L = plan["layout"]
    rng = np.random.default_rng(plan.get("seed", 1))
    B = plan["border"]
    n = L["vertices"]
    shape = (n, n)
    yy, xx = np.indices(shape, dtype=np.float32)
    X, Y = (xx - B) * CELL, (yy - B) * CELL
    sym = L.get("symmetry", {})
    levels = [l["z"] for l in plan["levels"]]
    water = plan["water"]["height"]

    # Land: blobs joined smoothly; each blob's outline wobbles by its own harmonics.
    blobs = [c for b in L["blobs"] for c in copies(b, sym, rng)]
    land = None
    for b in blobs:
        dx, dy = X - b["at"][0], Y - b["at"][1]
        r = b["radius"] * radial(rng, b.get("wobble", 0.1))(np.arctan2(dy, dx))
        sd = r - np.hypot(dx, dy)
        land = sd if land is None else smax(land, sd, L.get("neck", 150))
    # Necks: a band of a given width from one point to another (rotated with the symmetry), so a peninsula joins the island on a
    # neck of known width instead of wherever two blobs happen to overlap.
    for nk in [c for k in L.get("necks", []) for c in copies(dict(k, at=k["from"]), sym, rng)]:
        a = np.radians(nk["turn"])
        cx, cy = sym.get("center", [0, 0])
        tx, ty = nk["to"][0] - cx, nk["to"][1] - cy
        bx, by = cx + tx * math.cos(a) - ty * math.sin(a), cy + tx * math.sin(a) + ty * math.cos(a)
        ax, ay = nk["at"]
        ux, uy = bx - ax, by - ay
        t = np.clip(((X - ax) * ux + (Y - ay) * uy) / (ux * ux + uy * uy), 0, 1)
        width = nk["width"] * (1 + rng.uniform(-1, 1) * sym.get("jitter_size", 0))
        sd = width / 2 - np.hypot(X - ax - t * ux, Y - ay - t * uy)
        land = smax(land, sd, L.get("neck", 150))
    land = land + L.get("coast_noise", 0) * fbm(shape, rng, [(260, 1.0), (110, 0.4)])
    is_land = land > 0

    # Levels: the main level everywhere on land, plateaus above it, beaches below it at the coast.
    main_z = levels[L.get("main_level", 1)]
    z = np.full(shape, main_z, np.float32)
    plateaus = [c for p in L.get("plateaus", []) for c in copies(p, sym, rng)]
    for p in plateaus:
        dx, dy = X - p["at"][0], Y - p["at"][1]
        r = p["radius"] * radial(rng, p.get("wobble", 0.1))(np.arctan2(dy, dx))
        top = levels[p["level"]]
        # A steep step (the cliff style comes later); the mockup only needs a clear outline.
        z = np.maximum(z, main_z + (top - main_z) * np.clip((r - np.hypot(dx, dy)) / 20 + 0.5, 0, 1))
    beaches = [c for b in L.get("beaches", []) for c in copies(b, sym, rng)]
    low = levels[0]
    coast_in = ndimage.distance_transform_edt(is_land) * CELL
    for b in beaches:
        dx, dy = X - b["at"][0], Y - b["at"][1]
        # Strength 1 inside the beach's radius, fading over its last third, so the beach ends in a slope along the coast too.
        along = np.clip((b["radius"] - np.hypot(dx, dy)) / (b["radius"] / 3), 0, 1)
        # Low ground for `depth` from the waterline, then a gentle climb to the main level over `climb`.
        t = np.clip((coast_in - b["depth"]) / b["climb"], 0, 1)
        t = t * t * (3 - 2 * t)
        zb = low + (main_z - low) * t
        z = np.where(is_land & (along > 0), z * (1 - along) + np.minimum(z, zb) * along, z)
    # Gentle hills on the main level, away from steps and the coast; stage A keeps them as painted.
    hills = L.get("hills", 0) * fbm(shape, rng, [(400, 1.0), (160, 0.4)])
    flat = (np.abs(z - main_z) < 0.5) & (coast_in > 150)
    z = z + hills * ndimage.gaussian_filter(flat.astype(np.float32), 6)
    # Sea: the floor falls from the coast to its depth within `shelf` (ships need deep water near the shore).
    sea_d = ndimage.distance_transform_edt(~is_land) * CELL
    floor = plan["water"]["floor"]
    z = np.where(is_land, z, floor + (water - 12 - floor) * np.clip(1 - sea_d / L["shelf"], 0, 1))

    raw = np.clip(np.round(z / RAW), 0, 255 * plan.get("height_scale", 1))
    np.save(os.path.join(out, "heights_new.npy"), raw.astype(np.float32))
    Image.fromarray(np.zeros((n - 1, n - 1), np.uint8)).save(os.path.join(out, "passability.png"))

    # Kept objects; ramps as one road segment each across the plateau's step (stage A puts a ramp where a road crosses a step).
    objs = [{"kind": k["kind"], "template": k["template"], "x": k["at"][0], "y": k["at"][1], "unique_id": k.get("name", k["template"])}
            for k in plan.get("keep", [])]
    ramps = []
    for p in plateaus:
        for rp in p.get("ramps", []):
            a = math.radians(rp + p.get("turn", 0) + rng.uniform(-1, 1) * L.get("ramp_jitter_deg", 0))
            r0, r1 = p["radius"] * 0.6, p["radius"] * 1.5
            seg = [(p["at"][0] + r * math.cos(a), p["at"][1] + r * math.sin(a)) for r in (r0, r1)]
            ramps.append(seg)
            objs += [{"kind": "road_point", "template": "ramp", "x": x, "y": y} for x, y in seg]
    json.dump({"objects": {"objects": objs}}, open(os.path.join(out, "mock.json"), "w"), indent=1)

    # Preview: levels, beaches, ramps, kept objects.
    zz = raw * RAW
    rgb = np.zeros(shape + (3,), np.float32)
    rgb[...] = (0.1, 0.25, 0.5)
    rgb[zz < water] = np.array([0.05, 0.15, 0.4]) + 0.3 * (zz[zz < water] / water)[:, None]
    t = np.clip((zz - low) / max(1, levels[-1] - low), 0, 1)
    landrgb = np.stack([0.85 - 0.45 * t, 0.8 - 0.2 * t, 0.55 - 0.3 * t], -1)
    rgb[zz >= water] = landrgb[zz >= water]
    k = 2
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1]).resize((n * k, n * k), Image.NEAREST)
    d = ImageDraw.Draw(im)
    P = lambda x, y: ((x / CELL + B) * k, (n - 1 - (y / CELL + B)) * k)
    for seg in ramps:
        d.line([P(*seg[0]), P(*seg[1])], fill=(255, 255, 0), width=4)
    for o in objs:
        if o["kind"] == "road_point":
            continue
        x, y = P(o["x"], o["y"])
        col = (255, 255, 255) if o["kind"] == "waypoint" else (255, 220, 0) if o["template"].startswith("Tech") else (255, 0, 255)
        d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=col)
    im.save(os.path.join(out, "layout.png"))
    print(json.dumps({"land_cells": int(is_land.sum()), "blobs": len(blobs), "plateaus": len(plateaus), "beaches": len(beaches), "ramps": len(ramps)}))


if __name__ == "__main__":
    main()
