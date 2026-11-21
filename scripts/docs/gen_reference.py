"""Generates the INI field reference of the documentation site from the source code.

Usage:
  python scripts/docs/gen_reference.py                 # write docs/src/content/docs/reference/generated/
  python scripts/docs/gen_reference.py --baseline REF  # rewrite scripts/docs/upstream_fields.json from REF

Reads every FieldParse table in GeneralsMD/Code and Core (the tables the INI parser uses to
map field names to parse functions), groups them by the class that owns them, and writes
one page per module (named as it is registered in the module factories) and one per other
INI block owner. Fields that do not exist in the upstream baseline (the TheSuperHackers
code this fork is based on, stored in upstream_fields.json) are marked as new in this fork.

The output is not committed: CI runs this before building the site.
"""

import argparse
import io
import json
import os
import re
import subprocess
import sys
import tarfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUT = REPO / "docs" / "src" / "content" / "docs" / "reference" / "generated"
CONTENT = REPO / "docs" / "src" / "content" / "docs"
BASELINE = Path(__file__).resolve().parent / "upstream_fields.json"
SOURCE_ROOTS = ["GeneralsMD/Code", "Core"]
MODULE_FACTORIES = [
    "GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp",
    "Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp",
]
REPO_URL = "https://github.com/Andreas-W/GeneralsGameCode_Modding"
BASE = "/GeneralsGameCode_Modding"

# Parse function -> what a modder writes.
TYPES = {
    "INI::parseReal": "Real",
    "INI::parsePositiveNonZeroReal": "Real (> 0)",
    "INI::parseBool": "Bool (Yes / No)",
    "INI::parseInt": "Int",
    "INI::parseUnsignedInt": "Unsigned Int",
    "INI::parseUnsignedByte": "Unsigned Int (0..255)",
    "INI::parseUnsignedShort": "Unsigned Int",
    "INI::parseAsciiString": "String",
    "INI::parseAsciiStringVector": "String list",
    "INI::parseAsciiStringVectorAppend": "String list (repeatable, appends)",
    "INI::parseQuotedAsciiString": "Quoted string",
    "INI::parseDurationUnsignedInt": "Duration (ms)",
    "INI::parseDurationReal": "Duration (ms)",
    "INI::parseDurationUnsignedShort": "Duration (ms)",
    "INI::parseAngleReal": "Angle (degrees)",
    "INI::parseAngularVelocityReal": "Angular velocity (degrees / s)",
    "INI::parseVelocityReal": "Velocity (world units / s)",
    "INI::parseAccelerationReal": "Acceleration (world units / s²)",
    "INI::parsePercentToReal": "Percent (e.g. 50%)",
    "INI::parseFXList": "FXList name",
    "INI::parseObjectCreationList": "ObjectCreationList name",
    "INI::parseParticleSystemTemplate": "ParticleSystem name",
    "INI::parseWeaponTemplate": "Weapon name",
    "INI::parseArmorTemplate": "Armor name",
    "INI::parseUpgradeTemplate": "Upgrade name",
    "INI::parseSpecialPowerTemplate": "SpecialPower name",
    "INI::parseScience": "Science name",
    "INI::parseScienceVector": "Science list",
    "INI::parseAudioEventRTS": "AudioEvent name",
    "INI::parseDynamicAudioEventRTS": "AudioEvent name",
    "INI::parseColorInt": "Color (R:0 G:0 B:0)",
    "INI::parseRGBColor": "Color (R:0 G:0 B:0)",
    "INI::parseRGBAColorInt": "Color (R:0 G:0 B:0 A:0)",
    "INI::parseCoord2D": "Coord (X:0 Y:0)",
    "INI::parseCoord3D": "Coord (X:0 Y:0 Z:0)",
    "INI::parseCoord": "Coord (X:0 Y:0 Z:0)",
    "INI::parseIndexList": "One of a list",
    "INI::parseBitString8": "Flags",
    "INI::parseBitString32": "Flags",
    "INI::parseBitString": "Flags",
    "INI::parseLookupList": "One of a list",
    "INI::parseDamageTypeFlags": "DamageType flags",
    "INI::parseDeathTypeFlags": "DeathType flags",
    "INI::parseVeterancyLevelFlags": "Veterancy level flags",
    "INI::parseAndTranslateLabel": "String label (translated)",
    "INI::parseMappedImage": "Mapped image name",
    "INI::parseBitInInt32": "Bool (Yes / No)",
    "KindOfMaskType::parseFromINI": "KindOf flags",
    "ObjectStatusMaskType::parseFromINI": "Object status flags",
    "ModelConditionFlags::parseFromINI": "Model condition flags",
    "WeaponSetFlags::parseFromINI": "WeaponSet flags",
    "ArmorSetFlags::parseFromINI": "ArmorSet flags",
    "DisabledMaskType::parseFromINI": "Disabled flags",
    "TintStatusFlags::parseSingleBitFromINI": "TintStatus",
    "RadiusDecalTemplate::parseRadiusDecalTemplate": "Radius decal block",
}

FUNC_RE = re.compile(r"^[A-Za-z_][\w<>\s\*&:,]*?\b([A-Za-z_]\w*)::([A-Za-z_~]\w*)\s*\([^;{]*\)\s*(?:const\s*)?\{", re.M)
TABLE_RE = re.compile(r"(?:static\s+)?const\s+FieldParse\s+([A-Za-z_][\w:]*)\s*\[\s*\]\s*=\s*\{", re.M)
ENTRY_RE = re.compile(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*([^,{}]+?)\s*,\s*([^{}]*?)\s*,\s*([^{}]*?)\s*\}', re.S)
NAMES_RE = re.compile(r"(?:const\s+)?char\s*\*\s*(?:const\s+)?([A-Za-z_][\w:]*)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", re.S)
PARENT_RE = re.compile(r"\b([A-Za-z_]\w*)::buildFieldParse\s*\(\s*\w+\s*\)\s*;")
MODULE_DATA_RE = re.compile(r"MAKE_STANDARD_MODULE_(?:MACRO_WITH_MODULE_DATA|DATA_MACRO_ABC)\(\s*(\w+)\s*,\s*(\w+)\s*\)")
ADD_MODULE_RE = re.compile(r"addModule\s*\(\s*(\w+)\s*\)")


def source_files(read):
    """(path, text) of all C++ files, from the working tree or from a git ref."""
    out = []
    if read is None:
        for root in SOURCE_ROOTS:
            for f in (REPO / root).rglob("*"):
                if f.suffix.lower() in (".cpp", ".h", ".inl"):
                    out.append((f.relative_to(REPO).as_posix(), f.read_text(encoding="utf-8", errors="replace")))
    else:
        # One git archive instead of a git show per file.
        tar = subprocess.run(["git", "archive", "--format=tar", read, "--", *SOURCE_ROOTS],
                             cwd=REPO, capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(tar)) as t:
            for member in t.getmembers():
                if member.isfile() and member.name.lower().endswith((".cpp", ".h", ".inl")):
                    out.append((member.name, t.extractfile(member).read().decode("utf-8", errors="replace")))
    return out


def strip_comments(text):
    # Keeps line numbers: comments become spaces / newlines.
    def repl(m):
        s = m.group(0)
        if s.startswith("/"):
            return re.sub(r"[^\n]", " ", s)
        return s
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', repl, text, flags=re.S)


BFP_RE = re.compile(r"static\s+void\s+buildFieldParse\s*\([^)]*\)\s*\{")
CLASS_RE = re.compile(r"(?:class|struct)\s+(\w+)[^;{()]*\{")


def classes_of(text):
    return [(m.start(), matching_brace(text, m.end() - 1), m.group(1)) for m in CLASS_RE.finditer(text)]


def matching_brace(text, open_pos):
    depth = 0
    for i in range(open_pos, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def parse_tables(files):
    """Returns owners: name -> {file, line, fields: [(name, parse, user, line)], parents: [..]}."""
    owners = {}
    name_tables = {}
    for path, raw in files:
        text = strip_comments(raw)
        for m in NAMES_RE.finditer(text):
            vals = re.findall(r'"([^"]*)"', raw[m.start(2):m.end(2)])
            if vals:
                name_tables[m.group(1)] = vals
                name_tables.setdefault(m.group(1).split("::")[-1], vals)
        funcs = [(m.start(), matching_brace(text, m.end() - 1), m.group(1), m.group(2)) for m in FUNC_RE.finditer(text)]
        # Inline buildFieldParse bodies inside class definitions (headers).
        for m in BFP_RE.finditer(text):
            cls = next((c for c in reversed(classes_of(text)) if c[0] < m.start() < c[1]), None)
            if cls:
                funcs.append((m.start(), matching_brace(text, m.end() - 1), cls[2], "buildFieldParse"))
        for m in TABLE_RE.finditer(text):
            table = m.group(1)
            start = m.end() - 1
            end = matching_brace(text, start)
            body_raw = raw[start:end]
            body = text[start:end]
            func = next((f for f in funcs if f[0] < m.start() < f[1]), None)
            offs = re.findall(r"offsetof\s*\(\s*(\w+)\s*,", body)
            if "::" in table:
                owner = table.split("::")[0]
            elif func and func[3] == "buildFieldParse":
                owner = func[2]
            elif offs:
                owner = max(set(offs), key=offs.count)
            elif func:
                owner = func[2]
            else:
                owner = Path(path).stem + "." + table
            o = owners.setdefault(owner, {"file": path, "line": raw.count("\n", 0, m.start()) + 1, "fields": [], "parents": []})
            if func and func[3] == "buildFieldParse":
                for p in PARENT_RE.findall(text[func[0]:func[1]]):
                    if p != owner and p not in o["parents"]:
                        o["parents"].append(p)
            # Entries: match on the comment-free text, take names from the raw text.
            for e in ENTRY_RE.finditer(body):
                line = raw.count("\n", 0, start + e.start()) + 1
                name = re.search(r'"([A-Za-z0-9_]+)"', body_raw[e.start():e.end()])
                if not name:
                    continue
                parse = re.sub(r"\s+", "", e.group(2))
                user = re.sub(r"\s+", " ", e.group(3)).strip()
                if parse in ("0", "NULL", "nullptr"):
                    continue
                if any(f[0] == name.group(1) for f in o["fields"]):
                    continue
                o["fields"].append((name.group(1), parse, user, line))
    return owners, name_tables


def module_names(files):
    """Module data class -> registered module names; and module -> (factory file, line)."""
    data_of = {}
    for path, text in files:
        for cls, md in MODULE_DATA_RE.findall(text):
            data_of[cls] = md
    registered = {}
    for path, text in files:
        if path in MODULE_FACTORIES:
            for m in ADD_MODULE_RE.finditer(text):
                registered[m.group(1)] = (path, text.count("\n", 0, m.start()) + 1)
    modules_by_data = {}
    for mod in registered:
        md = data_of.get(mod, mod + "ModuleData")
        modules_by_data.setdefault(md, []).append(mod)
    return modules_by_data, registered


def describe_type(parse, user, name_tables):
    t = TYPES.get(parse)
    if t is None:
        t = parse.split("::")[-1]
        t = re.sub(r"^parse", "", t) or parse
        t = f"see source (`{parse}`)"
    values = None
    bits = re.match(r"^&?\s*(\w+)::getBitNames\s*\(\s*\)$", user.strip())
    if bits and f"{bits.group(1)}::s_bitNameList" in name_tables:
        return t, [v for v in name_tables[f"{bits.group(1)}::s_bitNameList"] if v]
    key = re.sub(r"[()&\s]", "", user).split("::")[-1]
    key = re.sub(r"getBitNames$", "", key)
    if key in name_tables:
        values = [v for v in name_tables[key] if v]
    elif user and user not in ("NULL", "0", "nullptr"):
        for cand in (key, key.replace("getBitNames", ""), "The" + key):
            if cand in name_tables:
                values = [v for v in name_tables[cand] if v]
                break
    return t, values


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def doc_links():
    """Heading text -> list of hand-written page URLs that document it."""
    links = {}
    for page in CONTENT.rglob("*.md"):
        rel = page.relative_to(CONTENT).as_posix()
        if rel.startswith("reference/generated/"):
            continue
        url = f"{BASE}/{rel[:-3]}/"
        seen = {}
        in_code = False
        for line in page.read_text(encoding="utf-8").splitlines():
            if line.startswith("```"):
                in_code = not in_code
                continue
            m = None if in_code else re.match(r"^(#{2,6})\s+(.*?)\s*$", line)
            if not m:
                continue
            text = m.group(2)
            s = re.sub(r"[^\w\- ]", "", text.strip().lower()).replace(" ", "-")
            n = seen.get(s, 0)
            seen[s] = n + 1
            anchor = s if n == 0 else f"{s}-{n}"
            for word in re.findall(r"[A-Za-z][A-Za-z0-9]+", text):
                links.setdefault(word, []).append(f"{url}#{anchor}")
    return links


def write_baseline(ref):
    files = source_files(ref)
    owners, _ = parse_tables(files)
    data = {"ref": subprocess.run(["git", "rev-parse", ref], cwd=REPO, capture_output=True, text=True, check=True).stdout.strip(),
            "owners": {k: sorted(f[0] for f in v["fields"]) for k, v in sorted(owners.items())}}
    BASELINE.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {BASELINE.relative_to(REPO)}: {len(data['owners'])} owners from {data['ref'][:10]}")


def generate():
    files = source_files(None)
    owners, name_tables = parse_tables(files)
    modules_by_data, registered = module_names(files)
    baseline = json.loads(BASELINE.read_text(encoding="utf-8")) if BASELINE.exists() else {"owners": {}, "ref": ""}
    base_owners = baseline["owners"]
    sha = os.environ.get("GITHUB_SHA") or subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout.strip() or "main"
    docs = doc_links()

    # Page per owner: modules get their registered name, other owners their class name.
    pages = {}
    for owner, o in owners.items():
        if not o["fields"] and not o["parents"]:
            continue
        mods = modules_by_data.get(owner)
        if mods:
            for mod in mods:
                pages[("modules", mod)] = (owner, o)
        else:
            pages[("blocks", owner)] = (owner, o)
    owner_page = {}
    for (kind, name), (owner, _) in pages.items():
        owner_page.setdefault(owner, f"{BASE}/reference/generated/{kind}/{slug(name)}/")

    if OUT.exists():
        for f in sorted(OUT.rglob("*"), reverse=True):
            f.unlink() if f.is_file() else f.rmdir()
    counts = {"modules": 0, "blocks": 0, "new_fields": 0}

    for (kind, name), (owner, o) in sorted(pages.items()):
        new_owner = owner not in base_owners
        base_fields = set(base_owners.get(owner, []))
        lines = []
        desc = f"INI fields of the {name} module." if kind == "modules" else f"INI fields parsed by {owner}."
        lines += ["---", f"title: {json.dumps(name)}", f"description: {json.dumps(desc)}", "status: generated",
                  "editUrl: false", "pagefind: true", "---", ""]
        lines.append(":::note[Generated]")
        lines.append(f"Generated from the source code. Field names and parse types only; see the linked documentation for meanings and defaults.")
        lines.append(":::")
        lines.append("")
        where = f"[{o['file']}:{o['line']}]({REPO_URL}/blob/{sha}/{o['file']}#L{o['line']})"
        if kind == "modules":
            fac = registered.get(name)
            lines.append(f"- Module data: `{owner}`, {where}")
            if fac:
                lines.append(f"- Registered in [{fac[0]}]({REPO_URL}/blob/{sha}/{fac[0]}#L{fac[1]})")
        else:
            lines.append(f"- Parsed by `{owner}`, {where}")
        if new_owner and baseline["ref"]:
            lines.append("- **New in this fork.**")
        for p in o["parents"]:
            link = owner_page.get(p)
            lines.append(f"- Also accepts the fields of {f'[`{p}`]({link})' if link else f'`{p}`'}")
        hits = [u for u in docs.get(name, [])][:5]
        if hits:
            lines.append("- Documentation: " + ", ".join(f"[{u.split('/')[-3]}/{u.split('/')[-2]}]({u})" for u in hits))
        lines.append("")
        if o["fields"]:
            lines.append("| Field | Type | New | Source |")
            lines.append("|---|---|---|---|")
            for fname, parse, user, line in o["fields"]:
                t, values = describe_type(parse, user, name_tables)
                if values:
                    shown = ", ".join(f"`{v}`" for v in values[:40])
                    if len(values) > 40:
                        shown += f", … ({len(values)} values)"
                    t = f"{t}: {shown}"
                is_new = baseline["ref"] and (new_owner or fname not in base_fields)
                counts["new_fields"] += 1 if is_new else 0
                lines.append(f"| `{fname}` | {t} | {'**new**' if is_new else ''} | [L{line}]({REPO_URL}/blob/{sha}/{o['file']}#L{line}) |")
        else:
            lines.append("No own fields.")
        dest = OUT / kind / f"{slug(name)}.md"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        counts[kind] += 1

    (OUT / "index.md").write_text("\n".join([
        "---", 'title: "INI field reference"', 'description: "All INI fields the engine parses, generated from the source code."',
        "status: generated", "editUrl: false", "sidebar:", "  order: 0", "---", "",
        f"Generated from the source code at [`{sha[:10]}`]({REPO_URL}/commit/{sha}). "
        f"{counts['modules']} modules and {counts['blocks']} other INI blocks.",
        "",
        f"Fields marked **new** do not exist in the upstream TheSuperHackers code this fork is based on"
        + (f" ([`{baseline['ref'][:10]}`](https://github.com/TheSuperHackers/GeneralsGameCode/commit/{baseline['ref']}))." if baseline["ref"] else "."),
        "",
        "- [Modules](#modules)", "- [Other INI blocks](#other-ini-blocks)", "",
        "## Modules", "",
        *[f"- [{n}]({BASE}/reference/generated/modules/{slug(n)}/)" for (k, n) in sorted(pages) if k == "modules"],
        "", "## Other INI blocks", "",
        *[f"- [{n}]({BASE}/reference/generated/blocks/{slug(n)}/)" for (k, n) in sorted(pages) if k == "blocks"],
        "",
    ]), encoding="utf-8", newline="\n")
    print(f"wrote {counts['modules']} module pages, {counts['blocks']} block pages, {counts['new_fields']} new fields")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", metavar="REF", help="rewrite the upstream baseline from this git ref")
    args = ap.parse_args()
    if args.baseline:
        write_baseline(args.baseline)
    else:
        generate()


if __name__ == "__main__":
    sys.exit(main())
