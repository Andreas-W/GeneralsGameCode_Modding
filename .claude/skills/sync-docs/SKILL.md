---
name: sync-docs
description: Catch the modding documentation site (docs/, Starlight) up with modding-relevant changes in this repo. Reads the last synced commit from docs/last-sync.txt, diffs up to the tip of origin/main (or a ref the user names), documents new INI fields / modules / enums and behavior-changing fixes on the right pages in docs/src/content/docs/, rechecks the touched pages, builds the site, and commits on a branch. Use when asked to "update the docs", "sync the docs", or document changes since the last docs update.
argument-hint: "[target ref or tag, default: origin/main]"
---

# Sync the documentation site with this repo

Normally a change that adds or changes something modders write or see updates `docs/` in the same
PR (see `CLAUDE.md`). This skill catches up on what slipped through.

Site: `docs/` (Starlight). Pages: `docs/src/content/docs/`. `docs/README.md` explains the layout and
frontmatter. The generated INI field reference (`reference/generated/`) is rebuilt from the code by
`scripts/docs/gen_reference.py` and is never edited by hand. Only Zero Hour matters: `GeneralsMD/`
and `Core/`. Ignore `Generals/`.

## 1. Find the range

1. The working tree must be clean for `docs/` (`git status -- docs`). If not, stop and ask.
2. Last synced commit: `docs/last-sync.txt` (one full SHA).
3. Target: by default the tip of `origin/main` (`git fetch origin --tags`, `git rev-parse origin/main`).
   A tag or ref named by the user (`$ARGUMENTS`) replaces it. The site is public, so only pushed code
   is documented. Mention unpushed commits on local `main` that touch `GeneralsMD`/`Core` in the
   report, don't document them.
4. If target == last synced, report "already in sync" and stop. `git merge-base --is-ancestor <last> <target>`
   must succeed.
5. Never select commits by date (upstream merges keep old author dates). Always use `<last>..<target>`.

## 2. Gather changes

```bash
git log --first-parent --format='%H %ad %s' --date=short <last>..<target>
git diff --stat <last>..<target> -- GeneralsMD Core
git diff --stat <last>..<target> -- docs/src/content/docs   # already documented in the PRs
```

- **The diff is the source of truth. Commit messages only explain why.**
- Skip what a commit in the range already documented under `docs/`.
- `--first-parent` shows SuperHackers merges as one entry. Inspect them with
  `git log <merge>^1..<merge>^2 --oneline` and document only upstream commits that change
  modder-visible behavior (mostly `bugfix(...)`, `feat(...)`). Skip `refactor`, `perf`, `chore`,
  `test`, `unify`, build/CI and rendering internals with no INI effect. After an upstream merge, also
  refresh the "new in this fork" baseline: `python scripts/docs/gen_reference.py --baseline $(git merge-base <target> TheSuperHackers/main)`.

| Signal | How to find |
|---|---|
| New/changed INI fields | `FieldParse` tables: `git diff <r> -- GeneralsMD Core \| grep -E '^\+.*\{ *"[A-Za-z]+", *(INI::\|parse\|&)'`, plus `buildFieldParse` bodies |
| New modules | `addModule(` in `ModuleFactory.cpp` and `Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp`; new files under `GameLogic/Object/{Behavior,Update,Contain,Die,Collide,Create,Damage,Upgrade,SpecialPower,Body}` or `Drawable/Draw` |
| New enum values | Name tables (`*Names[] =`, `s_bitNameList`) in `KindOf.cpp`, `Damage.h`/`Damage.cpp`, `Weapon.h`, `ThingTemplate.h`, `Locomotor.h`, `ParticleSys.h`, `ControlBar.h`, `SpecialPower.cpp`, `Eva.cpp`, `BodyModule.h`, `AIUpdate.h`, `GameType.cpp`, `GameCommon.cpp`; model-condition, weapon-bonus and tint-status tables |
| Changed defaults / limits | changed initializers in `*ModuleData` constructors, `GlobalData` defaults, `MAX_*` / array sizes |
| Removed / renamed fields | `^-` lines in `FieldParse` tables, `DEBUG_CRASH`/"no longer supported" messages |
| Behavior fixes | fork commits named "fix ..." and upstream `bugfix(...)` that change in-game behavior modders would notice |

Read code **at the target commit** (`git show <target>:<path>`), not the working tree. For real usage
examples, the mod's INIs are at `$GAME_DIR\Data\INI` (read only).

## 3. Map to pages (in `docs/src/content/docs/`)

| Source area | Page |
|---|---|
| Object-level fields (`ThingTemplate`, `WeaponSet`, `ArmorSet`, `ObjectExtend`) | `objects/object-definition.md` |
| AI update modules | `modules/ai-update.md` |
| Upgrade modules | `modules/upgrades.md` |
| Projectile, missile, laser modules | `modules/projectiles-and-lasers.md` |
| Contain / collide / draw / on-kill / special power modules | `modules/contain.md`, `collide.md`, `draw.md`, `on-kill.md`, `special-power-modules.md` |
| Other update / behavior modules | `modules/updates-and-behaviors.md` |
| Systems spanning several modules (shields, drone carriers, naval, bridges, jumpjets, animation blending, battle plans, buffs, veterancy, superweapons, turrets, chat commands, crates) | the matching page in `systems/` |
| `Weapon.cpp/h`, `Armor*`, `Locomotor*`, `GlobalData`, `MapData`, `ObjectCreationList.cpp`, `FXList.cpp`/`ParticleSys*`, `SpecialPower.cpp`/`Eva.cpp`, `ControlBar*`/`INICommandButton.cpp`, `Upgrade.cpp`, `Science.cpp`, audio | the matching page in `ini/` |
| Direct3D 12 renderer, post-processing, materials, shadow maps | `rendering/directx12.md` |
| Any new enum value | `reference/enums.md` (plus a mention on the feature page) |
| Behavior-changing bugfixes, raised limits, removed support | `changes.md` |

No fitting page: add a section to the closest page. A new page only for a large self-contained system
(tell the user). Every new page needs the frontmatter from `docs/README.md`.

## 4. Write

- Match the page style: `##`/`###` headings by INI block/module, bullets `` `Field = default` - (meaning) ``,
  INI examples in ` ```ini ` blocks. New modules get a "**New in this fork.**" line under the heading.
- Edit the existing section instead of appending duplicates. Keep anchors stable; links between pages
  are written as `/GeneralsGameCode_Modding/<page>/#<anchor>`.
- Document what modders write and see, not C++ internals. Don't invent defaults: write "see source"
  and list it as an open question.
- Removed or no longer supported: say so on the feature page and in `changes.md`.
- Every touched page: check it against the code at the target, then set `status: verified` and
  `verifiedAt: <target short SHA>`, and extend `sources:`.

## 5. Check and commit

```bash
python scripts/docs/check_fields.py <touched pages>   # names that exist nowhere in the code
cd docs && npm run build                               # regenerates the reference, fails on broken links
```

Write the target SHA into `docs/last-sync.txt`. Commit on a branch (`docs/sync-<short SHA>`), never on
`main` directly:

- `docs: Document modding changes up to <ref>` (fields, modules, enums), and if there are any
- `docs: Document behavior changes and fixes up to <ref>` (`changes.md` and related notes).

`<ref>` is `main@<short SHA>` or the tag/ref the user named. Don't push without asking.

## 6. Report

Range (`<last short>..<target short>`), pages touched, items documented, items skipped as not
modder-visible (one line each), open questions.
