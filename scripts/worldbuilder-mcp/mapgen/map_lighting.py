# Reads and changes a map's global lighting (the GlobalLighting chunk WorldBuilder's Global Light Options write), which no bridge
# command reaches yet. The .map is RefPack compressed ("EAR\0"); this decodes it, changes the chunk in place and writes the map
# uncompressed (the game and WorldBuilder read both; WorldBuilder compresses it again on its next save).
# Usage: map_lighting.py <map> [--blend FROM TO T [--dir D] [--brighter]]   (FROM, TO: morning, afternoon, evening, night)
#   Without --blend it prints the four lighting sets. --blend sets FROM's colours (ambient, diffuse of every terrain and object light)
#   to FROM + T * (TO - FROM), and its light directions D of the way toward TO's (default 0: FROM's sun angle stays); a light FROM
#   leaves dark (the fill lights) takes TO's direction. --brighter keeps every colour channel at least at FROM's ("morning, but
#   brighter": the afternoon's weaker ambient would otherwise darken it). Writes a .bak next to the map first.
import shutil, struct, sys

TIMES = ["morning", "afternoon", "evening", "night"]
LIGHTS = ["terrain0", "objects0", "objects1", "objects2", "terrain1", "terrain2"]   # order within one time of day (version 3)


def refpack_decode(src):
    flags = src[0]
    assert src[1] == 0xFB, "not RefPack"
    pos = 2
    n = 4 if flags & 0x80 else 3
    size = int.from_bytes(src[pos:pos + n], "big")
    pos += n
    if flags & 0x01:
        pos += n
    out = bytearray()
    while pos < len(src):
        b0 = src[pos]
        if b0 < 0x80:
            b1 = src[pos + 1]
            pos += 2
            plain, count, off = b0 & 3, ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = src[pos + 1], src[pos + 2]
            pos += 3
            plain, count, off = b1 >> 6, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = src[pos + 1], src[pos + 2], src[pos + 3]
            pos += 4
            plain, count, off = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            plain = ((b0 & 0x1F) << 2) + 4
            out += src[pos + 1:pos + 1 + plain]
            pos += 1 + plain
            continue
        else:
            plain = b0 & 3
            out += src[pos + 1:pos + 1 + plain]
            break
        out += src[pos:pos + plain]
        pos += plain
        for _ in range(count):
            out.append(out[-off])
    assert len(out) == size, (len(out), size)
    return bytes(out)


def load(path):
    data = open(path, "rb").read()
    if data[:4] == b"EAR\0":
        size = struct.unpack("<I", data[4:8])[0]
        data = refpack_decode(data[8:])
        assert len(data) == size
    assert data[:4] == b"CkMp", data[:4]
    return bytearray(data)


def lighting_chunk(data):
    # Name table, then top-level chunks: id (int), version (short), size (int), data.
    count = struct.unpack_from("<i", data, 4)[0]
    pos, names = 8, {}
    for _ in range(count):
        n = data[pos]
        name = data[pos + 1:pos + 1 + n].decode()
        names[struct.unpack_from("<I", data, pos + 1 + n)[0]] = name
        pos += 1 + n + 4
    while pos < len(data):
        cid, ver, size = struct.unpack_from("<IhI", data, pos)
        if names.get(cid) == "GlobalLighting":
            assert ver == 3, ver
            return pos + 10, size
        pos += 10 + size
    raise SystemExit("no GlobalLighting chunk")


def read(data, start):
    tod = struct.unpack_from("<i", data, start)[0]
    sets = {}
    for t, name in enumerate(TIMES):
        base = start + 4 + t * 54 * 4
        sets[name] = {l: list(struct.unpack_from("<9f", data, base + k * 36)) for k, l in enumerate(LIGHTS)}
    return tod, sets


def main():
    path = sys.argv[1]
    data = load(path)
    start, size = lighting_chunk(data)
    tod, sets = read(data, start)
    print("time of day", TIMES[tod - 1] if 1 <= tod <= 4 else tod)
    for name, lights in sets.items():
        print(name)
        for l, v in lights.items():
            print(f"  {l:9s} ambient {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}  diffuse {v[3]:.2f} {v[4]:.2f} {v[5]:.2f}  dir {v[6]:.2f} {v[7]:.2f} {v[8]:.2f}")
    if "--blend" not in sys.argv:
        return
    i = sys.argv.index("--blend")
    a, b, t = sys.argv[i + 1], sys.argv[i + 2], float(sys.argv[i + 3])
    dt = float(sys.argv[sys.argv.index("--dir") + 1]) if "--dir" in sys.argv else 0.0
    brighter = "--brighter" in sys.argv
    base = start + 4 + TIMES.index(a) * 54 * 4
    for k, l in enumerate(LIGHTS):
        v, w = sets[a][l], sets[b][l]
        col = [v[c] + t * (w[c] - v[c]) for c in range(6)]
        if brighter:
            col = [max(c, v[n]) for n, c in enumerate(col)]
        if not any(v[:6]):
            d = w[6:]
        else:
            d = [v[6 + c] + dt * (w[6 + c] - v[6 + c]) for c in range(3)]
            n = sum(x * x for x in d) ** 0.5 / (sum(x * x for x in v[6:]) ** 0.5 or 1)
            d = [x / (n or 1) for x in d]
        new = col + d
        struct.pack_into("<9f", data, base + k * 36, *new)
    shutil.copyfile(path, path + ".before_lighting.bak")
    open(path, "wb").write(data)
    _, sets2 = read(data, start)
    print(a, "now:")
    for l, v in sets2[a].items():
        print(f"  {l:9s} ambient {v[0]:.2f} {v[1]:.2f} {v[2]:.2f}  diffuse {v[3]:.2f} {v[4]:.2f} {v[5]:.2f}  dir {v[6]:.2f} {v[7]:.2f} {v[8]:.2f}")
    print("written uncompressed:", path)


if __name__ == "__main__":
    main()
