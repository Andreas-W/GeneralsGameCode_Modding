"""One-shot import of the old GitHub wiki into the Starlight site in docs/.

Usage: python scripts/docs/wiki_import.py <path to GeneralsGameCode_Modding.wiki clone>

Writes the pages into docs/src/content/docs/ and the map from old wiki URLs to new site
URLs into scripts/docs/wiki_anchor_map.json (used to replace the wiki pages with stubs).
The wiki is retired after the import; this script is kept so the conversion can be read
and repeated, not for ongoing syncing.
"""

import json

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONTENT = REPO / "docs" / "src" / "content" / "docs"
ANCHOR_MAP = Path(__file__).resolve().parent / "wiki_anchor_map.json"
WIKI_URL = "https://github.com/Andreas-W/GeneralsGameCode_Modding/wiki/"
SITE_URL = "https://andreas-w.github.io/GeneralsGameCode_Modding/"
# astro.config.mjs `base`. Changing it means rewriting the links in docs/ once.
BASE = "/GeneralsGameCode_Modding"

# Misspelled names on the wiki, checked against the code.
TYPOS = [
    ("CostModiferUpgrade", "CostModifierUpgrade"),
    ("ProductionTimeModiferUpgrade", "ProductionTimeModifierUpgrade"),
    ("DoneCarrierContain", "DroneCarrierContain"),
    ("LocomtorExtend", "LocomotorExtend"),
    ("ValueModifer", "ValueModifier"),
    ("collission", "collision"),
    ("Collission", "Collision"),
]

# Objects-&-Modules.md is split. Each entry is (h1 group, h2 section, None for the whole
# group, or PREAMBLE for the text before the group's first section). A whole group that is
# not the only part of its page is nested one level deeper.
PREAMBLE = "<preamble>"
OBJECTS_SPLIT = {
    "objects/object-definition": ("Object definition", "New fields on Object blocks and ObjectExtend.", [
        ("Object Improvements", None),
        ("Misc Improvements", None),
    ]),
    "modules/ai-update": ("AI update modules", "Changes to AIUpdateInterface and the other AIUpdate modules.", [
        ("Object Modules", "AIUpdateInterface (And all other AIUpdate types)"),
    ]),
    "modules/upgrades": ("Upgrade modules", "New and changed upgrade modules.", [
        ("Object Modules", "WeaponSetUpgrade"),
        ("Object Modules", "ArmorUpgrade"),
        ("Object Modules", "LocomotorSetUpgrade"),
        ("Object Modules", "MaxHealthUpgrade"),
        ("Object Modules", "CostModiferUpgrade"),
        ("Object Modules", "ProductionTimeModiferUpgrade (New)"),
        ("Object Modules", "ExperienceScalarUpgrade"),
        ("Object Modules", "UnitProductionBonusUpgrade (New)"),
        ("Object Modules", "PropagateUpgradeToContainedUpgrade (New)"),
        ("Object Modules", "StealthUpgrade"),
        ("Object Modules", "DelayedUpgradeBehavior (New)"),
    ]),
    "modules/projectiles-and-lasers": ("Projectile and laser modules", "Missiles, projectiles, scatter shots and lasers.", [
        ("Object Modules", "MissileAIUpdate"),
        ("Object Modules", "DumbProjectileBehavior"),
        ("Object Modules", "FreeFallProjectileBehavior"),
        ("Object Modules", "ScatterShotUpdate (New)"),
        ("Object Modules", "Laser Improvements"),
        ("Object Modules", "PointDefenseLaserUpdate"),
    ]),
    "modules/contain": ("Contain modules", "Changes to transports, garrisons and other containers.", [
        ("Object Modules", "Contain Modules"),
        ("Object Modules", "MultiAddOnContain (New)"),
        ("Multiple Portable Structures (Overlord contain)", None),
    ]),
    "modules/collide": ("Collide modules", "Crate collision, sticky bombs and AdvancedCollide.", [
        ("Object Modules", "CrateCollide"),
        ("Object Modules", "StickyBombCrateCollide"),
        ("Object Modules", "StickyBombUpdate"),
        ("Object Modules", "AdvancedCollide (New)"),
    ]),
    "modules/updates-and-behaviors": ("Update and behavior modules", "Other new and changed update and behavior modules.", [
        ("Object Modules", "ArmorDamageScalarUpdate (New)"),
        ("Object Modules", "LifeTimeUpdate"),
        ("Object Modules", "RadiusDecalBehavior (New)"),
        ("Object Modules", "ParkingPlaceBehavior"),
        ("Object Modules", "StealthDetectorUpdate"),
        ("Object Modules", "WeaponBonusUpdate"),
        ("Object Modules", "OCLUpdate"),
        ("Object Modules", "ChronoDeathBehavior (New)"),
        ("Object Modules", "DynamicGeometryClientUpdate (New)"),
        ("Object Modules", "ResetSpecialPowerTimerWhileAliveUpdate (New)"),
    ]),
    "modules/special-power-modules": ("Special power modules", "Special power modules and their update modules.", [
        ("Object Modules", "UpgradeSpecialPower (New)"),
        ("Object Modules", "OCLSpecialPower"),
        ("Object Modules", "TeleporterAIUpdate (New - EXPERIMENTAL)"),
    ]),
    "modules/draw": ("Draw modules", "W3DModelDraw and W3DDecalDraw.", [
        ("Object Modules", "W3DModelDraw"),
        ("Animation Blending", "W3DDecalDraw (New)"),
    ]),
    "modules/on-kill": ("On-kill modules", "Modules that act when the object kills something.", [
        ("On-Kill Modules", None),
    ]),
    "systems/battle-plans": ("Battle plans", "Battle plan bonuses for any object.", [
        ("Object Modules", "BattlePlanBonusBehavior (New)"),
        ("Object Modules", "AllBattlePlansUpdate (New)"),
    ]),
    "systems/energy-shields": ("Energy shields", "ShieldBody and EnergyShieldBehavior.", [
        ("Object Modules", "Energy Shields"),
    ]),
    "systems/drone-carriers": ("Drone carriers", "Carriers that launch and recover drones.", [
        ("Object Modules", "Drone Carrier system"),
    ]),
    "systems/naval-modules": ("Naval and water modules", "Modules for ships and objects on water.", [
        ("Naval / Water Modules", None),
        ("Water Genpower Support", None),
    ]),
    "systems/bridges": ("Bridges", "Destroyable road bridges and drawbridges.", [
        ("Bridges", None),
    ]),
    "systems/jumpjets": ("Jumpjets and special abilities", "Jumpjet infantry and special ability modules.", [
        ("Jumpjet / Special Ability Modules", None),
    ]),
    "systems/animation-blending": ("Animation blending", "Blending between model animations and steering animations.", [
        ("Animation Blending", PREAMBLE),
        ("Animation Blending", "W3DModelDraw (module level)"),
        ("Animation Blending", "ConditionState parameters"),
        ("Animation Blending", "AnimationSteeringUpdate"),
    ]),
    "systems/target-designator": ("Target designator", "SpecialPowerDesignatorUpdate.", [
        ("Target Designator", None),
    ]),
    "systems/crates-and-salvage": ("Crates and salvage", "Upgrades from crates and salvage.", [
        ("Crate / Salvage Upgrades", None),
    ]),
    "systems/veterancy": ("Veterancy", "Extra veterancy levels and rider changes.", [
        ("Veterancy", None),
    ]),
    "systems/superweapons": ("Superweapon modules", "Orbital beam, chronosphere, multi-location powers and the rift.", [
        ("Superweapon / Special Power Update Modules", None),
    ]),
    "systems/buffs": ("Buff system", "BuffTemplate.ini and BuffUpdate.", [
        ("Object Modules", "BuffUpdate"),
    ]),
}

# Other wiki pages: wiki page -> (site path, title, description). systems/buffs also gets the
# BuffUpdate section from Objects-&-Modules (appended after the page).
PAGES = {
    "Weapons": ("ini/weapon", "Weapon.ini", "New Weapon fields and WeaponExtend."),
    "Armors": ("ini/armor", "Armor.ini", "ArmorExtend."),
    "Locomotor": ("ini/locomotor", "Locomotor.ini", "Reverse movement, turning and LocomotorExtend."),
    "GameData": ("ini/gamedata", "GameData.ini", "New GameData and MiscAudio fields."),
    "MapData": ("ini/mapdata", "MapData", "Per-map settings."),
    "Object-Creation-List": ("ini/object-creation-list", "ObjectCreationList.ini", "New OCL nuggets and fields."),
    "FXList-&-ParticleSystems": ("ini/fxlist-and-particles", "FXList and ParticleSystem", "New FXList nugget fields and particle system changes."),
    "SpecialPowers": ("ini/special-power", "SpecialPower.ini", "New special power fields, Eva events and the Kodiak power."),
    "CommandButtons": ("ini/command-button", "CommandButton.ini", "New commands and command button options."),
    "Upgrades": ("ini/upgrade", "Upgrade.ini", "New Upgrade fields."),
    "Science": ("ini/science", "Science.ini", "New Science fields."),
    "Audio": ("ini/audio", "AudioEvent", "New audio event options."),
    "BuffSystem": ("systems/buffs", "Buff system", "BuffTemplate.ini and BuffUpdate."),
    "ChatCommands": ("systems/chat-commands", "Chat commands", "ChatCommands.ini and the singleplayer chat window."),
    "Turrets": ("systems/turrets", "Turrets", "Up to eight turrets per object."),
    "Naval-Support": ("systems/naval-overview", "Naval support overview", "Ships, water effects, naval weapons and carriers."),
    "New-Enum-Definitions": ("reference/enums", "New enum values", "New values for weapon slots, damage types, KindOfs and other enums."),
    "Changes-&-Bug-Fixes": ("changes", "Changes and bug fixes", "Raised limits, changed defaults and fixed engine bugs."),
}

NEW_RE = re.compile(r"\s*\((New(?:\s*-\s*EXPERIMENTAL)?)\)\s*$", re.I)
HEADING_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HTML_OK = {"br", "sup", "sub", "details", "summary", "img", "a", "b", "i", "u", "code", "kbd", "em", "strong", "p", "div", "span", "table", "tr", "td", "th"}


def slugify(text, seen):
    """github-slugger, as used by both GitHub and Astro."""
    s = re.sub(r"[^\w\- ]", "", text.strip().lower())
    s = s.replace(" ", "-")
    base, n = s, seen.get(s, 0)
    if n:
        s = f"{base}-{n}"
    seen[base] = n + 1
    return s


def split_fences(text):
    """Yields (is_code, chunk) so text outside code fences can be changed safely."""
    parts = re.split(r"(^```.*?^```[ \t]*$)", text, flags=re.M | re.S)
    for i, p in enumerate(parts):
        yield i % 2 == 1, p


def clean_heading(title):
    title = title.rstrip("#").strip()
    m = NEW_RE.search(title)
    note = None
    if m:
        note = "**New in this fork (experimental).**" if "EXPERIMENTAL" in m.group(1).upper() else "**New in this fork.**"
        title = title[: m.start()].strip()
    return title, note


def parse_sections(text):
    """Splits a page into [h1 group] -> preamble + [h2 sections]."""
    groups = []
    cur_group = None
    cur_sec = None
    for is_code, chunk in split_fences(text):
        for line in chunk.splitlines(keepends=True) if not is_code else [chunk]:
            m = None if is_code else HEADING_RE.match(line)
            if m and len(m.group(1)) == 1:
                cur_group = {"title": m.group(2), "pre": [], "sections": []}
                groups.append(cur_group)
                cur_sec = None
                continue
            if cur_group is None:
                cur_group = {"title": "", "pre": [], "sections": []}
                groups.append(cur_group)
            if m and len(m.group(1)) == 2:
                cur_sec = {"title": m.group(2), "lines": [line]}
                cur_group["sections"].append(cur_sec)
                continue
            (cur_sec["lines"] if cur_sec else cur_group["pre"]).append(line)
    return groups


def shift(text, by):
    """Changes heading levels outside code fences."""
    out = []
    for is_code, chunk in split_fences(text):
        if not is_code and by:
            chunk = re.sub(r"^(#{1,6})(?=\s)", lambda m: "#" * max(1, min(6, len(m.group(1)) + by)), chunk, flags=re.M)
        out.append(chunk)
    return "".join(out)


def tidy(text):
    """Heading cleanup, escaping of placeholder <tags>, code fence languages, typos."""
    out = []
    for is_code, chunk in split_fences(text):
        if is_code:
            if chunk.startswith("```\n") or chunk.startswith("```\r\n"):
                chunk = "```ini" + chunk[3:]
            out.append(chunk)
            continue
        lines = []
        for line in chunk.splitlines(keepends=True):
            m = HEADING_RE.match(line)
            if m:
                title, note = clean_heading(m.group(2))
                if not title:
                    continue  # empty headings such as a lone "###"
                lines.append(f"{m.group(1)} {title}\n")
                if note:
                    lines.append(f"\n{note}\n")
                continue
            # <placeholder> outside inline code would be swallowed as an HTML tag.
            segs = re.split(r"(`[^`]*`)", line)
            for i in range(0, len(segs), 2):
                segs[i] = re.sub(
                    r"<(/?)([^<>]*)>",
                    lambda mm: mm.group(0) if mm.group(2).split(" ")[0].lower() in HTML_OK else f"&lt;{mm.group(1)}{mm.group(2)}&gt;",
                    segs[i],
                )
            lines.append("".join(segs))
        out.append("".join(lines))
    text = "".join(out)
    for wrong, right in TYPOS:
        text = text.replace(wrong, right)
    return text


def frontmatter(title, description, order):
    return (
        "---\n"
        f"title: {json.dumps(title)}\n"
        f"description: {json.dumps(description)}\n"
        "status: unverified\n"
        f"sidebar:\n  order: {order}\n"
        "---\n\n"
    )


def heading_slugs(md):
    """Slugs of all headings of a rendered page body, in order, with their original text."""
    seen = {}
    result = []
    for is_code, chunk in split_fences(md):
        if is_code:
            continue
        for line in chunk.splitlines():
            m = HEADING_RE.match(line)
            if m:
                result.append((m.group(2), slugify(m.group(2), seen)))
    return result


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    wiki = Path(sys.argv[1])
    pages = {}  # site path -> [title, description, body]

    # Objects-&-Modules split
    om_text = (wiki / "Objects-&-Modules.md").read_text(encoding="utf-8")
    groups = parse_sections(om_text)
    used = set()
    for path, (title, desc, parts) in OBJECTS_SPLIT.items():
        body = []
        whole_only = len(parts) == 1 and parts[0][1] is None
        for g_title, s_title in parts:
            g = next((x for x in groups if x["title"] == g_title), None)
            if g is None:
                sys.exit(f"missing group {g_title!r}")
            if s_title is None:
                used.add((g_title, None))
                for s in g["sections"]:
                    used.add((g_title, s["title"]))
                if whole_only:
                    body.append("".join(g["pre"]) + "".join("".join(s["lines"]) for s in g["sections"]))
                else:
                    text = f"# {g_title}\n" + "".join(g["pre"]) + "".join("".join(s["lines"]) for s in g["sections"])
                    body.append(shift(text, 1))
            elif s_title == PREAMBLE:
                used.add((g_title, None))
                body.append("".join(g["pre"]))
            else:
                s = next((x for x in g["sections"] if x["title"] == s_title), None)
                if s is None:
                    sys.exit(f"missing section {g_title!r} / {s_title!r}")
                used.add((g_title, s_title))
                body.append("".join(s["lines"]))
        pages[path] = [title, desc, "\n".join(b.strip("\n") + "\n" for b in body)]
    for g in groups:
        for s in g["sections"]:
            if (g["title"], s["title"]) not in used:
                sys.exit(f"Objects-&-Modules section not assigned: {g['title']!r} / {s['title']!r}")
        if g["pre"] and "".join(g["pre"]).strip() and (g["title"], None) not in used and g["title"] != "Object Modules":
            sys.exit(f"Objects-&-Modules group preamble not assigned: {g['title']!r}")

    # Other pages
    for name, (path, title, desc) in PAGES.items():
        text = (wiki / f"{name}.md").read_text(encoding="utf-8")
        h1s = [g for g in parse_sections(text) if g["title"]]
        lines = text.splitlines(keepends=True)
        first = next((l for l in lines if l.strip()), "")
        if len(h1s) == 1 and HEADING_RE.match(first) and first.startswith("# "):
            body = "".join(lines[lines.index(first) + 1:])
        elif h1s:
            body = shift(text, 1)
        else:
            body = text
        if path in pages:  # systems/buffs: wiki page first, then BuffUpdate
            pages[path][2] = body.strip("\n") + "\n\n" + pages[path][2]
            pages[path][0], pages[path][1] = title, desc
        else:
            pages[path] = [title, desc, body]

    # docs/DirectX12.md moved into the site with the import (only once).
    dx_file = REPO / "docs" / "DirectX12.md"
    if dx_file.exists():
        dx_lines = dx_file.read_text(encoding="utf-8").splitlines(keepends=True)
        if dx_lines and dx_lines[0].startswith("# "):
            dx_lines = dx_lines[1:]
        pages["rendering/directx12"] = ["Direct3D 12 renderer", "Post-processing, shadow maps and HLSL materials with the Direct3D 12 renderer.", "".join(dx_lines)]

    # Tidy, then build the anchor map from the final bodies.
    for path in pages:
        pages[path][2] = tidy(pages[path][2])

    def find_new(heading_text):
        hits = []
        for path, (_, _, body) in pages.items():
            for t, slug in heading_slugs(body):
                if t == heading_text:
                    hits.append((path, slug))
        return hits

    anchor_map = {}
    link_map = {}  # (wiki page lower, old slug or "") -> "/path/#slug"
    sources = {"Objects-&-Modules": om_text}
    for name in PAGES:
        sources[name] = (wiki / f"{name}.md").read_text(encoding="utf-8")
    for name, text in sources.items():
        page_path = PAGES[name][0] if name in PAGES else None
        if page_path:
            link_map[(name.lower(), "")] = f"/{page_path}/"
            anchor_map[WIKI_URL + name] = SITE_URL + page_path + "/"
        for title, slug in heading_slugs(text):
            ct = clean_heading(title)[0]
            for wrong, right in TYPOS:
                ct = ct.replace(wrong, right)
            hits = find_new(ct)
            if page_path:
                hits = [h for h in hits if h[0] == page_path] or hits
            if hits:
                p, s = hits[0]
                link_map[(name.lower(), slug)] = f"/{p}/#{s}"
                anchor_map[f"{WIKI_URL}{name}#{slug}"] = f"{SITE_URL}{p}/#{s}"
    link_map[("objects-&-modules", "")] = "/objects/object-definition/"
    anchor_map[WIKI_URL + "Objects-&-Modules"] = SITE_URL + "objects/object-definition/"
    link_map[("home", "")] = "/"
    anchor_map[WIKI_URL + "Home"] = SITE_URL
    anchor_map[WIKI_URL.rstrip("/")] = SITE_URL

    # Rewrite links: full wiki URLs and relative Page#anchor links.
    unresolved = []

    def fix_link(m, page):
        target = m.group(2)
        t = target
        if t.startswith(WIKI_URL):
            t = t[len(WIKI_URL):]
        elif re.match(r"^[a-z]+:", t) or t.startswith("/"):
            return m.group(0)
        elif t.startswith("#"):
            # In-page anchor of the old page, which may now be on another page.
            wiki_page = "Objects-&-Modules" if page in OBJECTS_SPLIT else next((n for n, v in PAGES.items() if v[0] == page), None)
            if wiki_page is None:
                return m.group(0)
            t = wiki_page + t
        name, _, anchor = t.partition("#")
        key = (name.lower(), anchor.lower())
        new = link_map.get(key)
        if new is None and anchor:
            new = link_map.get((name.lower(), ""))
            unresolved.append(f"{page}: {target} (anchor not found, linked to page)")
        if new is None:
            unresolved.append(f"{page}: {target} (page not found)")
            return m.group(0)
        # Site links carry the base path; the links validator checks them against it.
        target_path, _, target_anchor = new.partition("#")
        if target_path.strip("/") == page and target_anchor:
            return f"{m.group(1)}(#{target_anchor})"
        return f"{m.group(1)}({BASE}{new})"

    for path in pages:
        out = []
        for is_code, chunk in split_fences(pages[path][2]):
            if not is_code:
                chunk = re.sub(r"(\[[^\]]*\])\(([^)\s]+)\)", lambda m: fix_link(m, path), chunk)
            out.append(chunk)
        pages[path][2] = "".join(out)

    # Write
    order = {p: i for i, p in enumerate(list(OBJECTS_SPLIT) + [v[0] for v in PAGES.values()])}
    for path, (title, desc, body) in pages.items():
        dest = CONTENT / f"{path}.md"
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists() and "\nstatus: unverified\n" not in dest.read_text(encoding="utf-8"):
            print(f"skipped {dest.relative_to(REPO)}: edited since the import")
            continue
        dest.write_text(frontmatter(title, desc, order.get(path, 0)) + body.strip("\n") + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {dest.relative_to(REPO)}")
    ANCHOR_MAP.write_text(json.dumps(anchor_map, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {ANCHOR_MAP.relative_to(REPO)} ({len(anchor_map)} entries)")
    for u in unresolved:
        print("UNRESOLVED", u)


if __name__ == "__main__":
    main()
