# Map generation from a mockup

The design, agreed with the user, is in `DESIGN.md`: read the mockup, write a plan, build into a new map, check. This file says what exists, how the first map was built with it, and what was learned on the way. Read both before working on map generation.

## Scripts

| Script | Stage | Does |
|---|---|---|
| `mockup_read.py <mock dir> <plan.json>` | read | Levels, steep and gentle edges, beaches, building clusters (town or outpost, families), tree clumps, kept objects; `plan_read.json` and `plan.png` |
| `style_scan.py <out json> <maps...>` | styles | Quick scan of reference maps: main textures on flat and steep ground, trees, civilian building families, water, time of day |
| `texture_roles.py <reference .map>` | styles | Which textures a reference uses per role (deep water, shallows, shore, main, high, slope, mountain, cliff) and the share each covers; JSON for the plan |
| `terrain_from_mockup.py <plan> <heights> <mock.json> <mockup passability> <out>` | A | Terrain shape: cliffs on the mockup's level outlines, slopes, ramps, beaches, mountain frame, hills; passability; connectivity check |
| `textures_from_plan.py <plan> <heights> <impassable> <out>` | B | One mask per texture layer, by role, in the reference's shares; imported in order, then `terrain_reblend` and `terrain_cliff_map` |
| `town_offline.py <map> <runs.json> [save]` | D | `town_tool.generate` in the second WorldBuilder, for dry runs and options the MCP server does not know yet |
| `bigfs.py` | data | Reads game files from the archives in place, in the engine's lookup order (loose files, then `.big` files under the game folder in sorted order with the first holding a file winning, then the Generals install's archives) |
| `texture_colors.py` | texturing 1 | Every `Terrain.ini` texture: file, class, tile size in cells (64 pixels per cell), mean colour, brightness, saturation, hue, variation; `work/textures.json` and a swatch sheet `work/textures.png` |
| `ref_scan.py <maps root> [names]` | texturing 1 | Per reference map in `work/refs/<name>/`: heights, base texture per cell, water and passability, trees, roads, bridges, buildings. Default: the 64 multiplayer maps of `MapCache.ini` in `MapsZH.big` (`work/mp_maps.json`) |
| `cell_features.py [names]` | texturing 2 | Features of every cell, the same for references and generated terrain: height rank in the map's land, height against the surroundings at two scales, convexity, slope, distances to water, land, cliff, roads, buildings, Tech and Supply, props and the map edge, the side of a nearby cliff (top or foot), size of the flat area, tree and rock density; `work/refs/<name>/features.npz` |
| `feature_check.py` | texturing 2 | How much the features say about where a reference paints which texture: each feature alone, and all together predicting one half of a map from the other |
| `texture_model.py [names]` | texturing 3 | Texture model per reference in `work/models/`: roles from what borders what (main, accent inside one host, transition between two, cliff, edge detail), zones (a main texture with its accents) merged into families where location does not tell similar zones apart, a classifier for zone by cell features, patch statistics (size, raggedness, elongation, direction), transition widths. `zone_probabilities(name, features)` applies a model to any map. Preview `<name>.png`: textures, zones, right half predicted. Needs scikit-learn |
| `examples/tainted_paradise_stage_d.py` | D | The first map's stage D: flatten the city's levels, bridge ends onto cliff edges, both towns, roads to the bridges |

Everything else is existing tools: `objects_scatter` (forests), `objects_place_many`, `outpost_place` / `outpost_populate`, `town_generate`. Scripts that change a map work through the second WorldBuilder (`town/start_wb.py`, port 47810) or write files that Claude imports with the MCP tools. `uv run --with numpy --with scipy --with pillow python <script>`.

The plan (`mapgen_plan.json`) lives next to the generated map, in its own folder, since a `map.ini` applies to every map in its folder. It records each decision with where it came from, plus the parameters the scripts read: `border`, `shift`, `water`, `levels`, `mountain_above`, `mountains`, `styles.cliff_params`, `styles.terrain_textures`, `height_scale`, `playable_max`.

## First slice: TaintedParadise

The user's mockup `TaintedParadise002.map` (MapWork), generated into `TaintedParadiseGen\TaintedParadiseGen.map`. Prompt: tropical / Asian, mainly grass with tropical forests, a large city in the lower right and on the centre island, irregular natural cliffs, sandy beaches, naval, rough diagonal symmetry. Stages so far, each shown to the user before the next:

1. **Read**: no textures painted (the style came from the prompt); levels 0/6 (seabed), 53, 89, 116, 159; two starts, 14 Tech, 11 Supply, 4 wooden bridges, 12 Asian buildings, 7 grass huts, 54 trees in 6 clumps, TwoLane and DirtRoad4 roads.
2. **User's initial changes**: border 50 to 20 (`map_resize` growing the playable area while shrinking the border keeps the 400 vertex grid, objects shift by +300); land lifted by 10, water 50 to 60, sea at least 60 deep after 20 cells (navy skill's gradient); `EnableShips` and underwater lighting in `map.ini`.
3. **A, terrain shape** (`terrain_from_mockup.py`), five rounds of user feedback, see below. Ends at `HeightMapScale = 1.2`.
4. **B, textures**: first a mix of four references (rejected: "a mix of desert and tropical, cliffs look sandy"), then Tournament Continent only, then Tournament A only with fuzzier patches (accepted as better; texturing rules recorded in `DESIGN.md` for a later pass).
5. **C, trees**: mockup trees deleted as hints; 6 forests with each clump's own mix (252), beach palms in groups (90), sparse scatter (42).
6. **D, towns**: hut outposts at the mockup's hut groups, kits around Tech and Supply, an Asian city in the lower right (26 buildings) and a small town on the centre island (9), bridges moved onto the cliff edges and joined by roads.

**Next: E** (naval markers: shipyards, naval perimeters, naval paths; AI paths with `ai_skirmish_setup`) **and F** (checks, an AI match). Backups in the map's folder: `before_towns`, `towns_v1`. The scripts' inputs and outputs (the mockup as read, stage A heights and impassable mask, stage B masks, reference scans) are in `mapgen_work\` there.

## Learned (rules now in the scripts)

### Terrain shape

- **A mockup's passability says nothing about intent.** The game blocks ground only from a slope of about 0.7 to 1, so nearly every brushed step counts as passable. Gentle stretches are found by how steep the mockup climbs across a step: rise over the distance between the lower and the upper level.
- **Cliff, slope or ramp** (`GENTLE_AVG`, `SLOPE_ALONG`, `RAMP_ALONG`): a stretch of a step that climbs at 0.3 or less on average is gentle. Running 200 units or more along the step it stays a slope (the mockup's shape, smoothed to at most 0.3); 40 to 200 a ramp; the rest is cliff. Most of the first mockup's steps were steep (71 % above 0.5), 8 to 9 % gentle.
- **Ramps are geometry, not a passability flag.** Widening the cliff profile near a point left noise in the ramp, slopes up to 1.9, and an "opened" flag hid it from the check. A ramp is now a straight corridor across the step: 120 wide, climbing at 0.2 on average (steepest under 0.3), no noise or painted hill inside, blending over 60. Its length follows the heights it joins (a double step gets a longer ramp); one at the sea moves up the slope until it starts on land; one that ends on a shelf continues down a further step within 300 (chained); one that ends near the water meets it as a beach.
- **The check is the game's view:** slope from 0.4 plus mountains, nothing opened by hand. Every piece of 400 cells or more that the mockup connects (bridges count) must stay connected; smaller pockets and ground behind the mountain frame that runs into the map border may stay cut off. With a minimum of 60 cells, ramps were added to pockets and "led nowhere".
- **A thin middle level is no level:** where the mockup climbs from 63 to 126 through a strip of 99 narrower than 80, it joins the level above; two cliffs with their own noise made a ledge in front of the cliff.
- **One noise for all steps**, calmed around every ramp and slope whichever step it belongs to; otherwise cliffs lying together drift apart and a ramp keeps the other cliff's noise.
- **The border is loose in a mockup**: mountains are a generated frame over the high ground it marks, rising a fixed amount above the ground they stand on (a fixed crest height stood only 5 to 20 above the upper level), at least covering what was painted. Height scale 1 tops out at 159; with land at 126 that left no room, so `HeightMapScale = 1.2` (the user's maximum).
- **Hills stay as painted** unless very steep (softened to 0.3) or high, large and impassable (mountain); a small high hill painted passable is a hill.
- **Beaches**: the waterline stays on the mockup's shore (a wider profile first put a coastal gun under water).
- Every new WorldBuilder map has a "Default Water" area at height 7 (see `../cliffs/cliff_gen.py`).

### Textures

- One reference at a time, by role and measured share (`texture_roles.py`). Mixing references' textures looked like "desert and tropical".
- The user's rules for the next pass (larger strands, one family per layer with transitions, no strong contrast, edge detail) are in `DESIGN.md`.
- Cliff textures are left out of the water roles; underwater faces get theirs from cliff mapping.

### Trees

- `objects_scatter` with `cluster` noise can leave a small area empty when the noise is low across all of it; use it only for large areas.

### Towns and bridges

- **The town generator wants about 800 x 800.** Smaller or broken-up sites fail with "no street fits". New options in `town_urban.py`, `town_tool.py` and the `town_generate` tool (the tool shows them after Claude restarts): `grid_scale` (block size relative to the pack, 0.6 to 0.75 worked), `major` (road type of the major streets, e.g. TwoLane for a town without four-lane roads), `contain` (major streets stay inside the area instead of running on 340 units, for an island with cliff edges).
- **Mockup roads through a town area cut it apart**: the generator treats roads as blocked and builds only in the largest piece. The mockup's ring road around the city was a hint; without it the city got 27 buildings instead of 9.
- **Kits around Tech in a small area** may not fit at all ("nothing fits around ..."); urban kits are from Wasteland Warlords and Golden Oasis, not Asian.
- **Bridges** (`WoodenSectional`, `roads.add` with two points) go from cliff edge to cliff edge: the first point along the bridge's axis where the land reaches its top, 20 units in. The island side of a bridge is the end nearer the island's middle (its ends lie outside the flat top's outline; testing against that outline sent island ends to mainland roads across the water).
- `clear_nature` is needed where kits or forests stand; the generators flatten pads and streets (up to 18 world units under kits).

## Open

- Texturing rules (`DESIGN.md`).
- Stages E and F for the first map; nothing has been tested in the game yet.
- The reader and the plan are still partly by hand (Claude wrote the first plan); the plan format in `DESIGN.md` is a sketch the scripts only partly read.
- Symmetry is not handled (the first mockup asked for none).
- Kits for Asian outposts, a small-site layout for towns that does not need streets.
