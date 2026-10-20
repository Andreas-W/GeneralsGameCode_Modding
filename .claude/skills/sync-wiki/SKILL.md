---
name: sync-wiki
description: Sync the GeneralsGameCode_Modding GitHub wiki with modding-relevant changes in this repo. Reads the last synced source commit from the wiki's git trailer, diffs this repo up to the newest weekly release tag (or a given ref), documents new INI fields / modules / enums and behavior-changing fixes on the right wiki pages, and commits to the wiki with a Source-Commit trailer. Use when asked to "update the wiki", "sync the wiki", or document changes since the last wiki update.
argument-hint: "[target ref, default: newest weekly-* tag]"
---

# Sync wiki with this repo

Source repo: this repo (`GeneralsGameCode_Modding`, branch `main`, remote `origin` = Andreas-W/GeneralsGameCode_Modding).
Wiki repo: sibling clone `../GeneralsGameCode_Modding.wiki` (branch `master`). It has to be added to the session (`--add-dir ../GeneralsGameCode_Modding.wiki`, or added as a folder in the desktop app) so its files can be edited. If it's missing, stop and ask the user to add it, or to clone `https://github.com/Andreas-W/GeneralsGameCode_Modding.wiki.git` there.

Below, `$WIKI` = `../GeneralsGameCode_Modding.wiki`. Run wiki git commands as `git -C $WIKI ...`. Only Zero Hour matters: `GeneralsMD/` and `Core/`. Ignore `Generals/`.

## 1. Find the range

1. The wiki tree must be clean (`git -C $WIKI status`). If not, stop and ask. Run `git -C $WIKI pull` so web edits aren't overwritten.
2. Last synced commit: the newest `Source-Commit` trailer in wiki history.
   ```bash
   git -C $WIKI log --format='%(trailers:key=Source-Commit,valueonly)' | grep -m1 .
   ```
   No trailer found → use the bootstrap value `78949f91c0a9593aa35056550eb994bc2562cca8` (covered by wiki commit `7efd799`, 2026-09-01).
3. Target: `$ARGUMENTS` if given, else the newest weekly tag:
   ```bash
   git fetch origin --tags
   git tag -l 'weekly-*' --sort=-creatordate | head -1
   ```
   Resolve it to a full SHA (`git rev-parse <tag>^{commit}`). If target == last synced, report "already in sync" and stop.
   Sanity check: `git merge-base --is-ancestor <last> <target>` must succeed.
4. Never select commits by date. Upstream merges keep old author dates, so date ranges skip or duplicate commits. Always use `<last>..<target>`.

## 2. Gather changes

```bash
git log --first-parent --format='%H %ad %s' --date=short <last>..<target> > ../commits_range.txt
git diff --stat <last>..<target> -- GeneralsMD Core
```

- **The diff is the source of truth. Commit messages only explain why.** Messages like "misc fixes" hide new fields.
- `--first-parent` lists fork-level commits. SuperHackers merges (`Merge remote-tracking branch 'SuperHackers/main'` / `tsh_merge_*`) show up as one entry. Inspect them with `git log <merge>^1..<merge>^2 --oneline` and only document upstream commits that change modder-visible behavior (mostly `bugfix(...)`, `feat(...)`). Skip `refactor`, `perf`, `chore`, `test`, `unify`, build/CI, and rendering internals with no INI effect.

Look for:

| Signal | How to find |
|---|---|
| New/changed INI fields | `FieldParse` tables: `git diff <r> -- GeneralsMD Core \| grep -E '^\+.*\{ *"[A-Za-z]+", *(INI::\|parse\|&)'`, plus `buildFieldParse` bodies |
| New modules | `addModule(` / `addModuleInternal` in `ModuleFactory.cpp` and `W3DModuleFactory.cpp`; new files under `GameLogic/Object/{Behavior,Update,Contain,Die,Collide,Create,Damage,Upgrade,SpecialPower,Body}` or `Drawable/Draw` |
| New enum values | Name tables (`*Names[] =`, `s_bitNameList`) in `KindOf.cpp`, `Damage.h`/`Damage.cpp`, `Weapon.h`, `ThingTemplate.h`, `Locomotor.h`, `ParticleSys.h`, `ControlBar.h`, `SpecialPower.cpp`, `Eva.cpp`, `BodyModule.h`, `AIUpdate.h`, `GameType.cpp`, `GameCommon.cpp`; model-condition, weapon-bonus and tint-status tables |
| Changed defaults / limits | changed initializers in `*ModuleData` constructors, `GlobalData` defaults, `MAX_*` / array sizes |
| Removed / renamed fields | `^-` lines in `FieldParse` tables, `DEBUG_CRASH`/"no longer supported" messages |
| Behavior fixes | fork commits named "fix ..." and upstream `bugfix(...)` that change in-game behavior modders would notice |

Read the surrounding code for every hit so you can document type, default and semantics correctly. For real usage examples, the mod's INIs are at `F:\Operation-Firestorm-2.0\Data\INI` (read-only).

## 3. Map to wiki pages (in `$WIKI`)

| Source area | Page |
|---|---|
| Object modules (Behavior/Update/Contain/Die/Body/Draw...), `ThingTemplate`, `WeaponSet`, `ArmorSet` | `Objects-&-Modules.md` |
| Turret AI / `AIUpdate` turret fields | `Turrets.md` |
| Ships, water, naval death, carriers (overview) | `Naval-Support.md` (link to detailed sections elsewhere) |
| `Weapon.cpp`/`Weapon.h`, `INIWeapon.cpp` | `Weapons.md` |
| `Armor*.cpp/h` | `Armors.md` |
| `Locomotor.cpp/h` | `Locomotor.md` |
| `GlobalData.cpp/h`, `INIGameData.cpp` | `GameData.md` |
| `MapData.cpp/h`, `INIMapData.cpp` | `MapData.md` |
| `ObjectCreationList.cpp` | `Object-Creation-List.md` |
| `FXList.cpp`, `ParticleSys*` | `FXList-&-ParticleSystems.md` |
| `SpecialPower.cpp`, `INISpecialPower.cpp`, `Eva.cpp`, SpecialPower modules | `SpecialPowers.md` |
| `ControlBar*`, `INICommandButton.cpp`, `INICommandSet.cpp`, GUI commands | `CommandButtons.md` |
| `Upgrade.cpp`, `INIUpgrade.cpp` | `Upgrades.md` |
| `Science.cpp` | `Science.md` |
| `INIAudioEventInfo.cpp`, audio | `Audio.md` |
| Buff templates / modifiers | `BuffSystem.md` |
| `ChatCommand*` | `ChatCommands.md` |
| Any new enum value | `New-Enum-Definitions.md` (plus a mention on the feature page) |
| Behavior-changing bugfixes, raised limits, removed support | `Changes-&-Bug-Fixes.md` |

No fitting page → add a section to the closest page. Create a new page only for a large self-contained system, and tell the user. Never put non-page files in `$WIKI`: GitHub renders every `.md` there, subfolders included, as a public page.

## 4. Write

- Match the existing page style: `#`/`##` headings by INI block/module, a parameter list or table (name, type, default, meaning), and an INI code example in a fenced block when useful.
- Edit the existing section for a module instead of appending duplicates. Keep page anchors stable. Other pages link with `[[Page Name]]` / `[text](Page-Name#anchor)`.
- Document what modders write and see, not C++ internals.
- When something is removed or no longer supported, say so explicitly on the feature page and in `Changes-&-Bug-Fixes.md`.
- Don't invent defaults. If the code doesn't make one clear, write "see source" and list it as an open question in the report.

## 5. Commit (in `$WIKI`)

Make two commits if both kinds of change exist:
1. `update wiki: new modding features up to <tag>`: fields, modules, enums.
2. `update wiki: behavior changes and fixes up to <tag>`: `Changes-&-Bug-Fixes.md` and related notes.

Each commit body has a short summary of what was documented, then the trailers:

```
Source-Commit: <full target SHA>
Source-Ref: <tag or ref>
Co-Authored-By: ...
```

Both commits get the same `Source-Commit`. **Don't push.** The wiki is public. Show the summary and ask the user before `git -C $WIKI push`. Never commit anything to this source repo as part of a sync.

## 6. Report

Short report: range (`<last short>..<target short>`, tag), pages touched, items documented, items skipped as not modder-visible (one line each), open questions.
