# Special building lots that a town generator can place along a street: supply docks, construction sites with supply piles, Tech buildings.
# A lot is laid out in its own frame: x along the street (0 = middle), y away from the street (0 = street side). The street side stays open for vehicles.
import math

STRUCT = "teamPlyrCivilian"

# FenceWidth from the INI, the spacing of WorldBuilder's Fence tool, and where the model sits relative to its position:
# 0 = it starts there and runs forward, 0.5 = it is centred there. Checked on screen, because models need not match their box.
FENCES = {"ChainLinkFence01": (27.35, 0.0), "WoodWall01": (29.0, 0.5)}

def fence_path(items, pts, template="ChainLinkFence01"):
    """Fence along a path, in whole pieces at exact spacing like the Fence tool. Each leg starts where the last one ended, so corners close."""
    w, anchor = FENCES[template]
    x, y = pts[0]
    for tx, ty in pts[1:]:
        L = math.dist((x, y), (tx, ty))
        n = max(1, round(L / w))
        dx, dy = (tx - x) / L, (ty - y) / L
        deg = math.degrees(math.atan2(dy, dx))
        for k in range(n):
            f = (k + anchor) * w
            items.append((template, x + dx * f, y + dy * f, deg, None))
        x, y = x + dx * n * w, y + dy * n * w

def fence_back(items, d, pieces, template="ChainLinkFence01"):
    half = pieces * FENCES[template][0] / 2
    fence_path(items, [(-half, d), (half, d)], template)

def fence_u(items, d, pieces, side, template="ChainLinkFence01"):
    """Back and part of both sides; the street side stays open. Corners close because the back is a whole number of pieces wide."""
    w = FENCES[template][0]
    half = pieces * w / 2
    front = d - side * w
    fence_path(items, [(-half, front), (-half, d), (half, d), (half, front)], template)

def layout(kind, rng):
    """Returns (width, depth, ground, items); ground 'dirt' or 'floor'; items are (template, x, y, angle, owner)."""
    it = []
    jitter = lambda v: v + rng.uniform(-4, 4)
    if kind == "dock":
        # the dock takes supply trucks from every side, so only the back gets a fence
        w, d, g = 170, 160, "floor"
        it.append(("SupplyDock", 0, 90, 0, STRUCT))
        fence_back(it, d - 4, 6)
        it.append(("ForkliftSmall", jitter(-72), jitter(24), rng.uniform(-40, 40), None))
        it.append(("ShippingCrate", jitter(70), jitter(30), rng.uniform(0, 90), None))
    elif kind == "site":
        # a building lot under construction: piles to gather from, building materials, fenced at the back and part of the sides
        w, d, g = 170, 130, "dirt"
        n = int(rng.integers(3, 6))
        spots = [(-44, 46), (0, 40), (44, 46), (-22, 84), (22, 84)]
        for k in rng.permutation(len(spots))[:n]:
            x, y = spots[k]
            it.append(("SupplyPileSmall", jitter(x), jitter(y), rng.uniform(0, 360), STRUCT))
        it.append(("Crane", 66 if rng.random() < 0.5 else -66, 92, 90, STRUCT))
        for t, x, y in (("WoodBeams02", -70, 30), ("Lumber", 70, 32), ("CementChunks01", -66, 62), ("ConstructionSupplyPile", 62, 58), ("Crate01", -34, 16), ("WoodBeams01", 30, 14)):
            it.append((t, jitter(x), jitter(y), rng.uniform(0, 360), None))
        it.append(("ConstructionBarricade", -80, 6, 0, None))
        it.append(("ConstructionBarricade", 80, 6, 0, None))
        fence_u(it, d - 4, 6, 3)
    elif kind == "hospital":
        # as on Homeland Alliance and Victory Valley: bib under it, ambulances out front, a row of cypress
        w, d, g = 124, 136, "floor"
        it.append(("TechHospitalBib", 0, 72, 0, STRUCT))
        it.append(("TechHospital", 0, 72, 0, STRUCT))
        it.append(("CarAmbulance", -46, 16, 0, None))
        it.append(("CarAmbulance", -24, 12, 0, None))
        for x in (-54, 54):
            for y in (44, 84, 124):
                it.append(("ArborVitae01" if rng.random() < 0.65 else "ArborVitae02", x, y, rng.uniform(0, 360), None))
    elif kind == "repair":
        w, d, g = 90, 84, "floor"
        it.append(("TechRepairbay", 0, 40, 0, STRUCT))
        it.append(("TirePile", 38, 14, rng.uniform(0, 360), None))
    elif kind == "derrick":
        w, d, g = 76, 70, "dirt"
        it.append(("TechOilDerrick", 0, 36, rng.choice([0, 90, 180, 270]), STRUCT))
        it.append(("TirePile", -30, 12, rng.uniform(0, 360), None))
        fence_back(it, d - 4, 2)
    elif kind == "pad":
        w, d, g = 100, 120, "floor"
        it.append(("TechReinforcementPad", 0, 60, 0, STRUCT))
    elif kind == "radar":
        w, d, g = 100, 100, "floor"
        it.append(("TechRadarBase", 0, 50, 0, STRUCT))
        fence_back(it, d - 4, 3)
    elif kind == "refinery":
        w, d, g = 110, 100, "dirt"
        it.append(("TechOilRefinery", 0, 50, 0, STRUCT))
    else:
        raise SystemExit(f"unknown lot kind {kind}; known: dock site hospital repair derrick pad radar refinery")
    return w, d, g, it

def to_world(item, ox, oy, tx, ty):
    """Item of a lot whose street side middle is at (ox, oy), with the street running along (tx, ty) and the lot on its left."""
    t, x, y, a, owner = item
    nx, ny = -ty, tx
    o = {"template": t, "x": round(ox + tx * x + nx * y), "y": round(oy + ty * x + ny * y), "angle_deg": round((math.degrees(math.atan2(ty, tx)) + a) % 360, 1)}
    if owner:
        o["owner"] = owner
    return o

def parse_spec(text):
    """'dock:2,site:1' -> [('dock', 2), ('site', 1)], largest lots first so that they find room."""
    out = []
    for part in text.split(","):
        if part:
            k, _, n = part.partition(":")
            out.append((k, int(n or 1)))
    order = ["dock", "site", "hospital", "pad", "refinery", "radar", "repair", "derrick"]
    return sorted(out, key=lambda kv: order.index(kv[0]) if kv[0] in order else 99)
