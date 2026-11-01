# Compares generated cliffs with the reference map they follow, on the numbers of cliff_measure.py. Usage: cliff_compare.py [recipes]
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
m = json.load(open(os.path.join(HERE, "work", "measure.json")))


def row(r):
    w, s, a = r["wall"], r["run_spread"]["face_fine"], r.get("along", {})
    return [("rise50", w["rise_p50"]), ("rise90", w["rise_p90"]), ("width50", w["run_p50"]), ("slope", w["slope_avg_p50"]), ("peak", w["slope_max_p50"]),
            ("edgeT", w["round_top"]), ("edgeB", w["round_bottom"]), ("face10", s["p10"]), ("face50", s["p50"]), ("face90", s["p90"]),
            ("jag5", r["line"]["jag_5"]), ("bend12", r["line"]["bend_12"]), ("top300", a.get("top_std_300")), ("foot300", a.get("foot_std_300")),
            ("flat%", r["flat_pct"]), ("imp%", r["impassable_pct"]), ("ramps", r["ramps"]["count"]), ("rampSl", r["ramps"]["slope_p50"])]


names = sys.argv[1:] or [n[4:] for n in m if n.startswith("gen_")]
print(" " * 10 + " ".join(f"{k:>7}" for k, _ in row(m[names and "gen_" + names[0]])))
for n in names:
    for label, key in (("ref " + n, n), ("gen " + n, "gen_" + n)):
        print(f"{label[:14]:14}" + " ".join(f"{str(v):>7}" for _, v in row(m[key])))
    print("  levels ref", [(l["z"], l["pct"]) for l in m[n]["levels"]], "gen", [(l["z"], l["pct"]) for l in m["gen_" + n]["levels"]])
