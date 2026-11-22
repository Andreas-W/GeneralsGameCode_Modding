"""Replaces every page of the old GitHub wiki with a stub that links to the documentation site.

Usage: python scripts/docs/wiki_stubs.py <path to GeneralsGameCode_Modding.wiki clone>

Uses wiki_anchor_map.json (written by wiki_import.py) to list each old section with a link to
where it lives now. Checks that every target still exists in docs/ (pages and anchors can move
after the import). Only writes files in the clone: committing and pushing the wiki is left to
the user, since the wiki is public.
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


def page_anchors():
    """Site path -> set of heading anchors, from the current pages."""
    result = {}
    for page in CONTENT.rglob("*.md*"):
        rel = page.relative_to(CONTENT).as_posix().rsplit(".", 1)[0]
        path = "" if rel == "index" else rel + "/"
        seen, anchors, in_code = {}, set(), False
        for line in page.read_text(encoding="utf-8").splitlines():
            if line.startswith("```"):
                in_code = not in_code
                continue
            m = None if in_code else re.match(r"^#{2,6}\s+(.*?)\s*$", line)
            if m:
                s = re.sub(r"[^\w\- ]", "", m.group(1).strip().lower()).replace(" ", "-")
                n = seen.get(s, 0)
                seen[s] = n + 1
                anchors.add(s if n == 0 else f"{s}-{n}")
        result[path] = anchors
    return result


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    wiki = Path(sys.argv[1])
    amap = json.loads(ANCHOR_MAP.read_text(encoding="utf-8"))
    anchors = page_anchors()
    problems = []

    def check(url):
        path, _, anchor = url[len(SITE_URL):].partition("#")
        if path not in anchors:
            problems.append(f"missing page: {url}")
        elif anchor and anchor not in anchors[path]:
            problems.append(f"missing anchor: {url}")
        return url

    for page in sorted(wiki.glob("*.md")):
        name = page.stem
        if page.read_text(encoding="utf-8").startswith("# This wiki has moved"):
            sys.exit(f"{page.name} is already a stub: run this on an unchanged wiki checkout")
        target = amap.get(WIKI_URL + name)
        if target is None:
            problems.append(f"no target for wiki page {name}")
            continue
        sections = []
        for title, line in [(m.group(2), m.group(0)) for m in re.finditer(r"^(#{1,3})\s+(.*?)\s*#*\s*$", page.read_text(encoding="utf-8"), re.M)]:
            slug = re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")
            url = amap.get(f"{WIKI_URL}{name}#{slug}")
            if url and (title, url) not in sections:
                sections.append((title, check(url)))
        lines = [
            "# This wiki has moved",
            "",
            f"The modding documentation is now at **{SITE_URL}**.",
            "",
            f"This page is now: **[{target}]({check(target)})**",
            "",
            "This wiki is no longer updated.",
        ]
        if sections:
            lines += ["", "## Where the sections went", ""]
            lines += [f"- [{t}]({u})" for t, u in sections]
        page.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        print(f"stubbed {page.name} ({len(sections)} sections)")

    if problems:
        print("\n".join(problems))
        sys.exit(1)


if __name__ == "__main__":
    main()
