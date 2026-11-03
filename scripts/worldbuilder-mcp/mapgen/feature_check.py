# Texturing step 2 check: how much the cell features say about which texture a reference map paints where.
# 1. Per feature alone: the share of the texture's uncertainty it explains (mutual information over entropy), median over the maps.
# 2. All features together: a random forest trained on one half of each map predicts the other half (halves share no cells);
#    against always guessing the most common texture.
# Usage: feature_check.py   Needs numpy and scikit-learn; reads work/refs/*/textures.npy and features.npz.
import json, os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "work", "refs")
FEATS = ["slope", "to_water", "depth", "rel_small", "rel_large", "convex", "to_cliff", "cliff_side", "flat_area", "height_rank",
         "tree_density", "rock_density", "to_road", "to_building", "to_tech", "to_prop", "to_edge"]


def entropy(c):
    p = c[c > 0] / c.sum()
    return -(p * np.log2(p)).sum()


def load(name):
    info = json.load(open(os.path.join(ROOT, name, "info.json")))
    b = info["border"]
    tex = np.load(os.path.join(ROOT, name, "textures.npy"))[b:-b, b:-b]
    f = np.load(os.path.join(ROOT, name, "features.npz"))
    return info, tex, {k: f[k][b:-b, b:-b] for k in FEATS}


def main():
    from sklearn.ensemble import RandomForestClassifier
    single = {k: [] for k in FEATS}
    rows, imp = [], np.zeros(len(FEATS))
    for name in sorted(os.listdir(ROOT)):
        info, tex, f = load(name)
        T = len(info["textures"])
        y = tex.ravel()
        ht = entropy(np.bincount(y, minlength=T).astype(float))
        for k in FEATS:
            v = f[k].ravel()
            if k == "cliff_side":
                bins = (v + 1).astype(int)
            else:
                qs = np.unique(np.quantile(v, np.linspace(0, 1, 13)))
                bins = np.clip(np.searchsorted(qs, v, "right") - 1, 0, len(qs) - 1)
            joint = np.zeros((bins.max() + 1, T))
            np.add.at(joint, (bins, y), 1)
            hc = sum(r.sum() / joint.sum() * entropy(r) for r in joint if r.sum())
            single[k].append((ht - hc) / ht if ht else 0)
        X = np.stack([f[k] for k in FEATS], -1)
        left = np.zeros(tex.shape, bool)
        left[:, : tex.shape[1] // 2] = True
        clf = RandomForestClassifier(n_estimators=40, min_samples_leaf=20, n_jobs=-1, random_state=1)
        clf.fit(X[left][::3], tex[left][::3])
        yte = tex[~left][::3]
        rows.append((name, T, float((yte == np.bincount(tex[left].ravel()).argmax()).mean()), float((clf.predict(X[~left][::3]) == yte).mean())))
        imp += clf.feature_importances_
    print("feature alone (share of texture uncertainty explained, median over maps):")
    for k in sorted(FEATS, key=lambda k: -np.median(single[k])):
        print(f"  {k:14s} {np.median(single[k]):.2f}")
    acc = np.array([r[3] for r in rows])
    base = np.array([r[2] for r in rows])
    print(f"all features, other half predicted: median {np.median(acc):.2f} (p25 {np.percentile(acc, 25):.2f}, p75 {np.percentile(acc, 75):.2f}); "
          f"most common texture: {np.median(base):.2f}")
    for n, t, b, a in sorted(rows, key=lambda r: r[3]):
        print(f"  {n:26s} textures {t:2d}  most common {b:.2f}  predicted {a:.2f}")
    print("importance:", ", ".join(f"{k} {v / len(rows):.2f}" for k, v in sorted(zip(FEATS, imp), key=lambda x: -x[1])))


if __name__ == "__main__":
    main()
