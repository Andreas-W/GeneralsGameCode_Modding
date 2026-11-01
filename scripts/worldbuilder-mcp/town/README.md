# Town generators

Generates towns from style packs measured on reference maps: place a town of a chosen style on any map, with any shape and size. The `town_generate` MCP tool does it on the map open in WorldBuilder; the scripts below it also build samples on flat scratch maps.

## The tool

`town_generate(polygon, style, preset, seed, jitter, theta, irregularity, theme, lots, outline, flatten, max_slope, reblend, clear_nature, dry_run)` (`town_tool.py`, its options are documented in `server.py`):

1. Reads heights, cliffs, water, objects and roads from the open map. Those, and ground steeper than `max_slope`, are blocked for streets and buildings (`town_site.py`).
2. Runs the generator for the style with that site. On real maps a block grid tries 90 positions, and other angles unless `theta` is given, and keeps the one with the most blocks.
3. Unless `dry_run`: smooths the ground under streets, levels a pad under each building, paints, places, adds roads, reblends the area. Water and cliffs stay as they are.

It does not touch what lies outside the plan, and saves nothing. It takes more steps than `edit_undo` holds, so the way back is reopening the map. With `dry_run=true`, look at `preview.png` in the plan folder first. `town_tool_test.py` tries it on a map through the second WorldBuilder.

## Setup

- Python with `numpy`, `scipy`, `pillow`.
- `GAME_DIR` as described in `CLAUDE.md`. To scan references, also `TOWN_REF_MAPS`: the folder holding `Maps\` and `maps_gens\` with the retail maps. Both go into the git-ignored `.claude/settings.local.json`.
- `WorldBuilderZH.exe` in the mod folder, built from this repo.
- Everything generated goes to `work\` next to the scripts (git-ignored; `TOWN_WORK` moves it). It can be rebuilt, except the Alpine scan described below.

```bash
python gamedata.py                                  # once, and after the INI files change
python start_wb.py                                  # second WorldBuilder on port 47810
python build_samples.py ArabicGeneric TownHighrise  # generate, build, save to the user maps; no names: all samples
python start_wb.py stop
```

The second WorldBuilder leaves the one Claude and the user work in alone, and scripts can talk to it directly, since the bridge serves one client. `TOWN_PLAN_ONLY=1` makes `build_samples.py` stop after the plans. The same seed gives the same town.

## Generators

| Script | Layout | Used for |
|---|---|---|
| `town_plan.py <seed> [no-outline] [lots=...]` | street-based, frontage prefabs | Alpine |
| `town_urban.py <pack> <seed> [jitter=] [theta=] [theme=] [lots=] [out=]` | blocks with sidewalk rings | urban, regular Arabic (`arabic_golden`, `arabic_baghdad`) |
| `town_organic.py arabic <preset> <seed> [irregularity=] [theme=] [lots=] [out=]` | main street, side streets branching in T junctions | irregular Arabic |

- `jitter`: scales the street node jitter of the pack. `0` is a perfect grid. `theta`: grid angle in degrees. Both 0: axis-aligned.
- `theme`: building mix from `packs/building_themes.json` (`highrise`, `asian`, `american`, `eastern`, `arabic`). Only `arabic` is counted from maps; the others are picked by name family.
- `lots=dock:1,site:2,hospital:1,repair:1,derrick:1,pad:1,radar:1,refinery:1`: see `town_lots.py`.
- `apply_plan.py plans/<folder> <map name> [x,y,zoom ...]` builds a plan, runs `terrain.reblend`, saves and takes pictures into the plan folder.
- Without `site=` the generators build the samples on a flat scratch map (their default polygon). With no site, the same seed must keep giving the same town: compare plans after changing a generator.

## Measuring references

```bash
python scan_refs.py cairo golden                    # objects, roads, textures into work/refs/<name>
python town_measure.py cairo DirtRoadTracks2,DirtRoad
python city_measure.py ha 1010,976,2213,2380        # needs a box around the town; also city_ground.py, city_joins.py
python city_catalog.py                              # buildings by family; where Tech buildings, docks and piles stand
```

`references.json` lists the known maps. Packs are still filled in by hand from these printouts, except the building mix in `make_arabic_pack.py`. `town_scan.py`, `town_plan_view.py`, `town_extract.py` and `road_ground.py` made `alpine_assault.json` from a manual dump and mask exports in `work/refs/alpine`; that data is not in the repo and these scripts have not been run from this folder.

## Rules, from the user unless marked as measured

**Roads**
- Different road types do not join. The smaller road ends with an end cap right beside the larger one, at its edge; the larger road keeps its connection. (Measured: `TwoLane` ends within 0.55 of `FourLane`'s width of its centre, always end-capped, 38 of 38 on Homeland Alliance.)
- Street corners that are not rectangular are `tight`, not sharp.
- No triangles of connecting streets.

**Sidewalks**
- Flush with the road, no gap. Checked on screen: offset 17 for `TwoLane` (flush at 16-18, gap at 22).
- At crossings they have plain angled corners, since the road has no curve there.
- On the outside of a road's curve they follow it in short mitred pieces; prefer the inside of a curve. Interrupting a sidewalk is the least wanted fix.
- Almost never end-capped (measured). End-capped sidewalks beside a road draw long stray strips, see Engine findings.

**Ground**
- Pick textures by measuring where references use them, never by name. `RocksType5` looks like cobblestone: right for Arabic towns (44-81% of the floor on Golden Oasis and Bridge Busters), wrong for Alpine.
- Road outlines are optional; many maps have none. When used, keep them thin: less than a full tile may show beside the road. Concrete in town, dirt outside with gaps.
- Blend everything at the end. The reference for how it should look is the Genesis map tools ("Remove Blend Tiles", "Blend All Tiles"), which `terrain_reblend` ports.

**Layout**
- Grid jitter and angle are parameters; a perfectly aligned city must be possible, not required.
- Buildings vary by theme. Tech buildings and supply docks must fit into city layouts, Alpine too. Supply piles go on a construction site lot.
- Props in clusters and not overused: street lights, bins.
- Fences as the WorldBuilder Fence tool places them: exact `FenceWidth`, whole pieces. Models need not match their box, so check new fence types on screen (`fence_test.py`) and add them to `FENCES` in `town_lots.py`. `ChainLinkFence01` starts at its position, `WoodWall01` is centred on it.

## What the styles measure (all measured)

**Alpine** (Alpine Assault): sidewalks along part of the street; cobble 41% within 20 of a building; no road outline; lamps 1.32 and houses 1.26 per 100 street. SwissMP puts `DirtMediumType12` and `DirtMediumType5` along country roads; `RocksType1` and `RocksType3` never come within 40 of a road.

**Urban** (Homeland Alliance, Bear Town Beatdown, Tournament City; Victory Valley is sparser and unused; Tournament Urban is too specialized, per the user):
- Sidewalks form closed rings around blocks, on both sides of 48-83% of street length.
- Block floor is `ConcreteType4` (60-65% on Homeland and Bear Town, far less on Tournament City), with grass islands (`GrassMediumType31`, `30`) and `AsphaltType3` deeper in.
- 4.6-6.7 buildings per 1000 street, 9-14 from the street edge, about 97% aligned to it, no preference for front or back.
- Street length 57-84 per 100x100.
- Tech buildings and docks stand 38-91 from a street edge, but docks and derricks mostly outside towns, so lot layouts are designed, not learned.

**Arabic** (no exact template, per the user, so a generalized style with presets):

| Map | User's description |
|---|---|
| Cairo Commandos | one small desert town, very uneven dirt roads; could be a sub-style |
| Golden Oasis | regular town, more trees and grass |
| Sand Serpent | irregular small desert towns, sidewalks as roads |
| El Scorcho | small scattered towns, dirt roads, regular and irregular |
| Bridge Busters | grassy, irregular, trees and cobblestone |
| USA01 | large Baghdad-themed regular city, wide roads with sidewalks. Use `maps_gens\USA01`: `MD_USA01` is a snowy Zero Hour mission |

Shared: Mogadishu buildings, 1-6° off the street's direction, median street gap -11 to 11. T junctions dominate. Irregular towns have 5-7 bends and 2-6 dead ends per 1000 street.

## Engine findings

- **End-capped road near another type**: `W3DRoadBuffer::findCrossTypeJoinVector` treats an end with `FLAG_ROAD_JOIN` closer than 0.55 of the other road's width to its centre line as a join, and draws a strip perpendicular to that road, as long as its segment. On a T junction that strip lies on the side road and is fine. Parallel, as with a sidewalk at offset 17 beside `TwoLane` (limit 19.25), it shows as a long line across the terrain.
- **Runs of one road type sharing an endpoint** draw similar strips, seen when old and new sidewalk runs overlapped. Inferred from one rebuild, not traced in code.
- **Road point ids change** after a save or manual edit: list them again before deleting.
- **Blending**: `autoBlendOut` blends both sides of a boundary and shows stair steps at 45°. `terrain_reblend` uses the corner method of Genesis, but keeps a cell's base texture where Genesis changes it (which would erode corners on every repeat, about 390 cells per run on a town map). `hard_edges` from `terrain_blend_report` does not prove a good look: the old method also reported 0.
- **3-way tiles**: an extra draw pass with a vertex buffer rebuilt each frame. Rough guess only: about 0.1-0.2 ms CPU per 1000 visible; `W3DDisplay.cpp` flags more than 2000 visible as a spike. Not measured.
- **Linking WorldBuilder** can fail with `LNK1201` while Visual Studio holds the PDB: move `WorldBuilderZH.pdb` aside.

## Open

- **Nothing has been tested in game.** Truck access to docks and piles is unchecked; this is the next thing to do. When tried, `generalszh.exe` quit at once with exit code 1 and no new debug log, on any map, also started by hand, so it is not about town maps. Cause not found; the single-instance mutex was held while a WorldBuilder ran, but this build may allow several instances (an `_Instance02` log exists).
- Slopes: only tried through `town_generate` on Alpenstadt, Bear Town Beatdown, Tournament Desert and Desert Coast, in WorldBuilder.
- **Block towns need open ground.** Existing objects and roads cut the grid: on Tournament Desert and Desert Coast (25-31% blocked) they came out with one or two blocks, even after trying 90 grid positions. Irregular towns fit in better. Streets do not connect to roads already on the map: they stop before them.
- Short grey stubs (about 40 units) can appear on open ground, far from any road point, on `TownAlpineLots`. Removing any of five unrelated sidewalk runs cleared one, two others did not, so it looks like a road buffer rendering quirk. Not understood; rings with `angled` corners have shown none so far.
- Arabic street shape is close but not matched: junctions 3.2 per 1000 street (measured 0.5-2.4), bends 3.3 (measured 5-7). Density is set by hand. El Scorcho's scattered towns are not reproduced.
- Stair-stepped masks at 45° were a blending problem, fixed by `terrain_reblend`; check again after any change to the masks.
- Alpine: only seed 33 was picked by eye; the 0.20 street drop rate probably needs lowering.
- Urban: only `Sidewalk` (8 wide) is used, Homeland also uses wider ones. Prop angles of `StopLight` and `StreetLamp03` are guesses.

## Next (proposed, the user chooses)

1. Get the game starting again, then test a town in game: `AlpenstadtTownTest` in the user maps has a dock, a construction site and a derrick.
2. Outposts. The user wants all kinds, as variants: military (guard towers, sandbags, fences, bunkers, like a checkpoint), civilian (a few scattered buildings such as a farm, gas station or depot) and gameplay (a defended Tech building or supply spot between bases). Needs reference maps from the user. Close to lots.
3. Other map aspects from references: cliffs, rivers.
4. More town styles; each should be mostly a new pack.
