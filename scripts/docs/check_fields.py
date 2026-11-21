"""Checks the INI names used on documentation pages against the source code.

Usage: python scripts/docs/check_fields.py [page.md ...]   (default: every page in docs/)

Collects every string literal in GeneralsMD/Code and Core (that is where FieldParse tables,
module factories and enum name tables keep the names the INI parser accepts) and reports
names written on a page as INI fields, modules or enum values that appear in no literal.
Such a name is misspelled, renamed or removed. A name that is found can still be
documented wrongly; this only catches the names.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONTENT = REPO / "docs" / "src" / "content" / "docs"
SOURCE_DIRS = [REPO / "GeneralsMD" / "Code", REPO / "Core"]
SKIP = {"Yes", "No", "None", "End", "Default", "True", "False", "Object", "Behavior", "Draw", "Body",
        "ArmorSet", "WeaponSet", "Weapon", "Armor", "Locomotor", "Upgrade", "Science", "SpecialPower"}

FIELD_IN_CODE = re.compile(r"^\s*([A-Z][A-Za-z0-9_]{2,})\s*=", re.M)          # Field = value
MODULE_IN_CODE = re.compile(r"^\s*(?:Behavior|Draw|Body|ClientUpdate|ClientBehavior)\s*=\s*([A-Za-z0-9_]+)", re.M)
FIELD_IN_TEXT = re.compile(r"`([A-Z][A-Za-z0-9_]{2,})\s*(?:=|`)")             # `Field = ...` or `Field`
ENUM_IN_TEXT = re.compile(r"`([A-Z][A-Z0-9_]{3,})`")                           # `SOME_ENUM`


def source_literals():
    names = set()
    lit = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"')
    for d in SOURCE_DIRS:
        for f in d.rglob("*"):
            if f.suffix.lower() in (".cpp", ".h", ".inl"):
                try:
                    text = f.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                names.update(lit.findall(text))
    return names


def page_names(text):
    body = text.split("\n---", 1)[-1]
    names = set()
    for block in re.findall(r"```[a-z]*\n(.*?)```", body, re.S):
        block = re.sub(r";.*", "", block)
        names.update(FIELD_IN_CODE.findall(block))
        names.update(MODULE_IN_CODE.findall(block))
    prose = re.sub(r"```.*?```", "", body, flags=re.S)
    names.update(FIELD_IN_TEXT.findall(prose))
    names.update(ENUM_IN_TEXT.findall(prose))
    return {n for n in names if n not in SKIP}


def main():
    pages = [Path(p).resolve() for p in sys.argv[1:]] or sorted(CONTENT.rglob("*.md"))
    known = source_literals()
    known_lower = {k.lower() for k in known}
    total = 0
    for page in pages:
        if "reference/generated" in page.as_posix():
            continue
        missing = sorted(n for n in page_names(page.read_text(encoding="utf-8")) if n not in known)
        if not missing:
            continue
        print(f"{page.relative_to(REPO).as_posix()}:")
        for n in missing:
            hint = " (exists with other case: case-insensitive lookup)" if n.lower() in known_lower else ""
            print(f"  {n}{hint}")
        total += len(missing)
    print(f"{total} unknown names")


if __name__ == "__main__":
    main()
