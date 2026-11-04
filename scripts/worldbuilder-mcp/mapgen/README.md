# Map generation from a mockup

The design, agreed with the user, is in `DESIGN.md`: read the mockup, write a plan, build into a new map, check. This file says what exists, how the first map was built with it, and what was learned on the way. Read both before working on map generation.

## Scripts

| Script | Stage | Does |
|---|---|---|
| `mockup_read.py <mock dir> <plan.json>` | read | Levels, steep and gentle edges, beaches, building clusters (town or outpost, families), tree clumps, kept objects; `plan_read.json` and `plan.png` |
| `style_scan.py <out json> <maps...>` | styles | Quick scan of reference maps: main textures on flat and steep ground, trees, civilian building families, water, time of day |
| `texture_roles.py <reference .map>` | styles | Which textures a reference uses per role (deep water, shallows, shore, main, high, slope, mountain, cliff) and the share each covers; JSON for the plan |
| `relief_measure.py [names] [--maps]` | relief | Relief of the references (or generated maps): height variation at three scales, gentle slopes, hills and pits, flat patches and soft slopes inside the levels, flatness around starts, Tech, supplies and buildings (starts and Tech from `MapCache.ini`); `work/relief.json` |
| `relief.py <plan> <heights> <objects.json> <out dir>` | A2 relief | After stage A: stage A's noise taken off the levels, flat patches at different heights with soft slopes, hills and pits, fine noise on slopes only; nothing near cliffs, ramps, beaches and water; starts levelled and kept flat, Tech and buildings kept flat; slopes softened, never steeper than before. Plan `relief` sets the numbers and extra kept areas |
| `layout_blobs.py <plan> <out dir>` | read | When the mockup's heights are only a rough guide: an idealised mockup drawn from the plan's `layout` (land blobs joined by necks, plateaus with ramps, beaches, gentle hills, the seabed), repeated by rotation with jitter for rough symmetry; output goes into `terrain_from_mockup.py` like a read mockup |
| `terrain_from_mockup.py <plan> <heights> <mock.json> <mockup passability> <out>` | A | Terrain shape: cliffs on the mockup's level outlines, slopes, ramps, beaches, mountain frame, hills; passability; connectivity check |
| `textures_from_plan.py <plan> <heights> <impassable> <out>` | B | One mask per texture layer, by role, in the reference's shares; imported in order, then `terrain_reblend` and `terrain_cliff_map` |
| `town_offline.py <map> <runs.json> [save]` | D | `town_tool.generate` in the second WorldBuilder, for dry runs and options the MCP server does not know yet |
| `bigfs.py` | data | Reads game files from the archives in place, in the engine's lookup order (loose files, then `.big` files under the game folder in sorted order with the first holding a file winning, then the Generals install's archives) |
| `texture_colors.py` | texturing 1 | Every `Terrain.ini` texture: file, class, tile size in cells (64 pixels per cell), mean colour, brightness, saturation, hue, variation; `work/textures.json` and a swatch sheet `work/textures.png` |
| `ref_scan.py <maps root> [names]` | texturing 1 | Per reference map in `work/refs/<name>/`: heights, base texture per cell, water and passability, trees, roads, bridges, buildings. Default: the 64 multiplayer maps of `MapCache.ini` in `MapsZH.big` (`work/mp_maps.json`) |
| `cell_features.py [names]` | texturing 2 | Features of every cell, the same for references and generated terrain: height rank in the map's land, height against the surroundings at two scales, convexity, slope, distances to water, land, cliff, roads, buildings, Tech and Supply, props and the map edge, the side of a nearby cliff (top or foot), size of the flat area, tree and rock density; `work/refs/<name>/features.npz` |
| `feature_check.py` | texturing 2 | How much the features say about where a reference paints which texture: each feature alone, and all together predicting one half of a map from the other |
| `texture_model.py [names]` | texturing 3 | Texture model per reference in `work/models/`: roles from what borders what (main, accent inside one host, transition between two, cliff, edge detail), zones (a main texture with its accents) merged into families where location does not tell similar zones apart, a classifier for zone by cell features, patch statistics (size, raggedness, elongation, direction), transition widths. `zone_probabilities(name, features)` applies a model to any map. Preview `<name>.png`: textures, zones, right half predicted. Needs scikit-learn |
| `painter.py <model> [terrain] [--seed N]` | texturing 4 | Paints a terrain with a reference's model: zones by location (smoothed, noisy borders), family variants and accents by warped noise at the reference's patch sizes (accents leaning to where the reference has them), transitions as bands where their sides meet and as strands through a zone, cliff textures by slope; no specks under 3 x 3, fewer three-texture corners; the border plain. Offline: `work/paint/<model>__<terrain>.png` (terrain's textures, painted) and the numbers of both. `terrain` is a reference or a map scanned into `work/maps/` |
| `paint_map.py <name> <model> <out dir> [--scan <map>]` | texturing 5 | Paints a map with a model and writes one mask per texture (largest first, `masks.json`) for `terrain_import_mask` (`fit=exact`); then `terrain_reblend`, `terrain_cliff_map`. Keeps cells of town ground and road surfaces (Concrete, Asphalt). `--scan` scans the map first through the second WorldBuilder (the map must lie in a folder of its name) |
| `paint_coast.py <name> <land model> <sea model> <out dir> --sand Main[,Accent] [--band Tex]` | texturing 5 | A coast from two references, when none has both land and sea: land from one model, seabed (and shore cliffs) from the other, beaches by rule (sand, then an optional band). Second map: Eastern Everglades, Seaside Mutiny, `--sand SandType2wavy,SandType2 --band DirtType1`; third: Cairo Commandos, Tournament Continent, `--sand SandType4,SandMediumType3 --sand-top 14` |
| `import_masks.py <map> <masks dir>` | texturing 5 | Imports a `masks.json` set through the second WorldBuilder, largest first, then reblends, maps the cliffs and saves |
| `examples/arab_coast_stage_c.py` | C | The third map's vegetation: palm groves by the water and scrub at the cliff foot, in the references' mixes and clump sizes, clear of the template's town areas, bases, roads and Tech |
| `examples/arab_coast_stage_d.py` | D | The third map's towns: one city over the highway (its stretch through the city gives way and is joined to the town's streets), a small island town, dirt tracks to the docks, kits around Tech and on open ground, rock groups |
| `symmetry.py` | any | Exact symmetry for square maps by folding (d4: mirror across both axes and both diagonals): heights, cell grids, object images |
| `examples/oasis_lake_stage_a.py` | A | The fourth map's terrain from a description only: one octant designed (lake lobes with bays, island, corner plateaus with ramps, side ridges), the same for every player, folded only on request |
| `examples/oasis_lake_stage_c.py` | C | The fourth map's vegetation by ground: each cell gets the style reference's plants per 1000 cells for its texture, and its mix there |
| `examples/oasis_lake_stage_d.py` | D | The fourth map's island: symmetric Tech and docks, kits, dirt roads to a plaza, Arabic houses in rings |
| `examples/island_cross_stage_c.py` | C | The second map's jungle: groves in a jungle reference's mix, bamboo stands, beach palms; base cores, neck lanes and the city area kept open; placed through the second WorldBuilder |
| `examples/tainted_paradise_stage_d.py` | D | The first map's stage D: flatten the city's levels, bridge ends onto cliff edges, both towns, roads to the bridges |
| `examples/tainted_paradise_island.py` | A to D | The first map's top-left island redone: rocky, cliffs on most sides, a soft beach toward the centre (eased into the water, no flat ground at the water level), rock outcrops, an Oil Refinery, supply and hut kits, palms |
| `examples/tainted_paradise_sea.py` | B | The usual blue seabed (SandMediumType9blue bands with noise) over the open sea only: lagoons keep theirs, rock textures stay near cliffs, tight bays get a short gradient |
| `examples/team_islands_stage_a.py` | A | The fifth map's terrain from a template: land averaged over four mirror images, rounded-rectangle player lobes, straight channels between them, the user's marked corners rounded, a steep seabed; folded for exact symmetry |
| `examples/team_islands_build.py` | A2 | The fifth map: relief, folded; new map, heights, water, symmetric Tech, `map.ini` (ships, light) |
| `examples/team_islands_stage_c.py` | C | The fifth map's plants after Tournament Island, clear of starts and Tech |
| `examples/team_islands_mirror_lobes.py` | D | Copies the user's hand-made lobes (huts, Tech, supplies, grass patches with their textures, heights and plants, the start) mirrored onto the other lobes |
| `examples/team_islands_ai_markers.py` | E | User areas to `NO_SHIPYARD` zones, `ai_skirmish_setup` with narrow flanks, three shipyards per player in open water, naval perimeters and paths; worked out for two players and mirrored |
| `map_lighting.py <map> [--blend FROM TO T [--dir D] [--brighter]]` | any | Reads a map's global lighting, or blends one time of day toward another (no bridge command sets lighting); writes the map uncompressed, WorldBuilder compresses it on its next save |

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
7. **B again** (texturing steps 1 to 5): repainted with `painter.py` and TournamentA's model after the towns, keeping their ground: three-way tiles 5299 to 895, hard edges 67 to 25 (`terrain_blend_report`). Backup before: `stage_b1`; masks in `mapgen_work\stageB2\`.

**Next: E** (naval markers: shipyards, naval perimeters, naval paths; AI paths with `ai_skirmish_setup`) **and F** (checks, an AI match). Backups in the map's folder: `before_towns`, `towns_v1`, `stage_b1` (before the repaint). The scripts' inputs and outputs (the mockup as read, stage A heights and impassable mask, stage B masks, reference scans) are in `mapgen_work\` there.

## Second map: IslandCrossGen

The user's template `TemplatesPIslandCross02.map`, generated into `IslandCrossGen\IslandCrossGen.map`. Prompt: four players, naval, one island with a peninsula per player, an Asian city over a plateau hill in the centre (two levels), tropical jungle; the template's heights only a rough guide, rough symmetry, not mirrored.

1. **Layout** (`layout_blobs.py`): main island, four peninsulas on necks, plateau with four ramps facing the peninsulas, beaches in the bays and at the peninsula tips; copies rotated with jitter. Then **A** with `terrain_from_mockup.py` unchanged. The user found one peninsula larger: copies are now scaled per copy (`scale`) until their building area (flat, passable, main level) matches within 1 %.
2. **B** (now `paint_coast.py`): no single reference fits; jungle maps paint grass under the sea, island maps bright sand inland. Land from Eastern Everglades, seabed from Seaside Mutiny, beaches by rule. Sand bands of 1 to 2 cells looked like nothing: most of a beach's low ground lies in the shallows, so the sand reaches 18 above the water.
3. **C**: 2051 plants (Everglades' mix: palms, dogwood, oak; bamboo stands; Bush08), beach palms. A first pass with large base cores and many clearings looked like edge belts only.

4. **D** (`examples/island_cross_stage_d.py`): levels snapped flat, a TwoLane road from every peninsula over its neck to the city, one Asian town over both levels (66 buildings, 42 on the plateau; hospital and repair bay on top, refinery below as lots, the construction yard placed first on the east side); four tropical hut kits at the necks. 2185 objects in all, no limit reached, the jungle kept as it was.

**Next: E** (naval and AI markers) **and F** (checks, a match). Backups in the map's folder: `stage_a`, `stage_b`, `stage_c`.

## Third map: ArabCoastGen

The user's template `TemplatesPArabCoast02.map` (two players north and south, a divided highway, a drawbridge to an island), generated into `ArabCoastGen\ArabCoastGen.map`. Prompt: desert / Arabic coast, naval; a big city along the existing road, a smaller town on the island, cliffs as the west border, patches of vegetation, desert details, existing roads integrated.

1. **A** from the template's heights (62 terraces, 87 main level, 142 west high ground) with Tournament Desert's cliff recipe. The user's corrections became plan `shape_edits`: island grown 30 and calmed, a channel carved for the drawbridge, calm cliffs at its ends; the beach ditch was a bug in the shore floor.
2. **B** with `paint_coast.py`: Cairo Commandos land, Tournament Continent seabed.
3. **C**: 26 patches (palm groves by the water, scrub at the cliff foot); the user then adjusted them by hand.
4. **D**: city over both sides of the highway (46 buildings with generic streets; 38 after the user asked for a more Arabic street style, Golden Oasis's; 33 with the irregular grid the user asked for next, jitter 2.5: 3.5 left blocks empty); island 8 buildings around the construction yard; small Arabic kits as villages at the template's two small areas; dirt tracks (`DirtRoadTracks2`, as Cairo Commandos) from the highway to the four docks; kits around every Tech building and dock and 6 on open ground; 22 rock groups.

**Next: E** (naval and AI markers) **and F**. Backups: `stage_a`, `stage_b`, `stage_c`.

## Fourth map: OasisLakeGen

From a description only (no template): 8 players placed as on Twilight Flame, high symmetry, a very large deep lake with an island, a beach close to every base, desert after Golden Oasis.

1. **A**: one octant designed and the same for every player. The first lake had narrow bumps toward the starts and became an eight-pointed star (a cosine over the octant gives round lobes); island beaches of 18 degrees became pits ringed by cliffs (40 degrees with a quick climb work); the desert cliff recipe was too jagged (face 1.8, bend 8). The user first saw an exact mirror (folded terrain and textures) and preferred rough symmetry: the same layout, each cliff and shore its own.
2. **B**: Golden Oasis land and its own wet-sand lake bottom (a pale seabed jumped against the warm sands).
3. **C**: the user's idea, grass more likely to carry trees than sand, is what Golden Oasis does: 28 to 70 plants per 1000 cells on its grasses, 0.4 on its main sand. 204 plants.
4. **D**: the island (some 700 across) with nine Tech objects fitted 0 to 4 buildings as a block town; a village of kits, dock roads and houses in rings instead.

5. **Relief** (`relief.py`, the user asked for soft hills and height changes): the check found the starts standing on their bay's beach slope (spread 6 to 9 within 300): starts moved out to 2250, start cores levelled. A quicker beach climb (150) meant to help made stage A turn 8 of 12 beach slopes into cliffs (the user: "soft slopes into cliffs"): back to 220. With the median reference's numbers the user saw little change: relief reached 31 % of the main level, kept-flat circles around eight bases covering most of it; now starts flat to 350 and full relief by 550, patch heights and hills doubled: 48 % of the main level changes by over 3, -26 to +29. `examples/oasis_lake_rebuild.py` rebuilds the map from the plan (stage A, relief, import, textures, plants).

**Next**: supplies for the bases and the corner plateaus, then E and F. Backups: `stage_a` (folded), `stage_b`, `stage_c`, `stage_d` (before the relief).

## Fifth map: TeamIslandsGen

The user's template `6pTournamentTeamIslands.map` (placeholder buildings for six starts and the centre), generated into `TeamIslandsGen\TeamIslandsGen.map`. Prompt: after Tournament Island, six players 3 vs 3, an island or peninsula per player with soft shores, symmetric layout with textures not mirrored, a larger centre for several Tech buildings, naval, 50 cells more on every side.

1. **A**: mirroring the template closed the notches between lobes, so straight channels are cut between them; the user then asked for slightly larger, rounded-rectangle lobes, four marked corners rounded and a steeper seabed.
2. **B, C**: Tournament Island's model and plants; the blue seabed of `tainted_paradise_sea.py`.
3. **D**: the user arranged two lobes by hand; `team_islands_mirror_lobes.py` copied them onto the other four.
4. **E**: no-shipyard zones from the user's areas, AI paths, naval markers; `ai_skirmish_check` clean. Lighting: Morning blended halfway toward Afternoon, never darker (`map_lighting.py`).

**Next: F** (an AI match). Backups in the map's folder.

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
- **A beach runs down to the real seabed beside it.** Falling toward a fixed depth (12 under the water) while the sea next to it kept the mockup's shallow floor left a ditch along the waterline with a dam in front (third map).
- **Shape edits** in the plan (`shape_edits`): `grow` pushes a land piece's whole profile outward (its height at the nearest edge only added a rim at the waterline), `calm` with `radius` scales the cliff and shore noise there, `carve` ([rx, ry]) makes an ellipse sea. The third map: the island grew 30 with calm 0.3 (60 was too much), a carved channel puts the cliff tops where the drawbridge ends (geometry in `town/work/gamedata/geo.json`: `IndustrialDrawBridge_New_Large` is 285 long), calm 0.15 at both ends.
- Every new WorldBuilder map has a "Default Water" area at height 7 (see `../cliffs/cliff_gen.py`).

### Textures

- One reference at a time, by role and measured share (`texture_roles.py`). Mixing references' textures looked like "desert and tropical".
- The user's rules for the next pass (larger strands, one family per layer with transitions, no strong contrast, edge detail) are in `DESIGN.md`.
- Cliff textures are left out of the water roles; underwater faces get theirs from cliff mapping.

### Relief

- **Reference relief is terraces, not noise**: 92 % of the ground inside the levels is flat; height changes are flat patches (some 600 across) at different heights joined by soft slopes (some 70 wide, slope 0.06). Plain noise tilts everything a little: 21 % flat.
- **Fine noise only on slopes**, **one height per patch** (scaled by how free the patch is): fading relief point by point tilted every patch reaching into the fade around a start, cliff or shore.
- **Starts are flat**: 0.03 within 300 on the references. Check bases against bays and ramps in the layout: a start 90 from its beach top cannot be flat.
- **Relief strength**: the median reference is nearly flat; a map asked for hills wants the hilly references' numbers (patch spread 12, hills to 20). Kept-flat areas must stay small (references: flat core 300, normal relief by 600 to 800), or eight bases leave no ground for it.
- **One client on the bridge**: a script that keeps its WorldBuilder connection open while starting another script that connects takes the second WorldBuilder down; close it first.
- **Never steeper than before**: slopes of beaches near the limit (0.35) became impassable when a softening step skipped what was already above its own limit.

### Trees

- `objects_scatter` with `cluster` noise can leave a small area empty when the noise is low across all of it; use it only for large areas.

### Towns and bridges

- **The town generator wants about 800 x 800.** Smaller or broken-up sites fail with "no street fits". New options in `town_urban.py`, `town_tool.py` and the `town_generate` tool (the tool shows them after Claude restarts): `grid_scale` (block size relative to the pack, 0.6 to 0.75 worked), `major` (road type of the major streets, e.g. TwoLane for a town without four-lane roads), `contain` (major streets stay inside the area instead of running on 340 units, for an island with cliff edges).
- **Mockup roads through a town area cut it apart**: the generator treats roads as blocked and builds only in the largest piece. The mockup's ring road around the city was a hint; without it the city got 27 buildings instead of 9.
- **Kits around Tech in a small area** may not fit at all ("nothing fits around ..."); urban kits are from Wasteland Warlords and Golden Oasis, not Asian.
- **A ring around a plateau is no town site.** At about 250 across, block grids and the organic layout found no room, neither per level nor per ring sector (1 to 6 buildings, or "no street fits"). One block grid over the whole hill puts blocks on both levels; its streets keep off ground steeper than 0.4 and climb only at the ramps and soft stretches of the edge.
- **Roads through a town site**: the generator builds only in the largest piece the roads leave, and strips beside a highway are too narrow (2 to 21 buildings each). Let the road's stretch through the town go, build one town (contained, or its main street runs on beside the old road) and join the stubs to its streets.
- **Street style**: the urban pack's streets (two-lane asphalt, sidewalks, concrete, green verges, stop lights) look western in an Arabic town. `street_style` (town_generate, town_urban.py) builds another block pack's streets, ground and props on the urban layout: Golden Oasis's old asphalt and rocky floor kept 38 of 46 buildings, Baghdad's narrow sidewalks only 15. On a small island the wider old roads left no building; there the generic layout is built and its types replaced afterwards (`map_replace`, sidewalks and stop lights deleted).
- **Small sites** (under some 500 across) get 1 to 8 buildings from the block grids, and the organic layouts find no street at all (they crashed on it; now a clear error): use kits for villages.
- **Kits in woodland**: `outpost_place` with `clear_nature` refused every spot in the jungle, since its buildings were tested against the trees that it would clear. With `clear_nature` the tree cells now count as free.
- **Bridges** (`WoodenSectional`, `roads.add` with two points) go from cliff edge to cliff edge: the first point along the bridge's axis where the land reaches its top, 20 units in. The island side of a bridge is the end nearer the island's middle (its ends lie outside the flat top's outline; testing against that outline sent island ends to mainland roads across the water).
- **The second WorldBuilder keeps its copy** when `map.open` names the path it has open, even with `discard_changes`: the jungle was placed on the map as it was before texturing and saved over the textures. Open another map (`map.new`) first. `ref_scan.scan` (and so `paint_map.py --scan`) does that now too: it had scanned a map's old terrain after a new heightmap.
- `clear_nature` is needed where kits or forests stand; the generators flatten pads and streets (up to 18 world units under kits).

## Open

- Texturing rules (`DESIGN.md`).
- Stages E and F for the first map; nothing has been tested in the game yet.
- The reader and the plan are still partly by hand (Claude wrote the first plan); the plan format in `DESIGN.md` is a sketch the scripts only partly read.
- Symmetry is not handled (the first mockup asked for none).
- Kits for Asian outposts, a small-site layout for towns that does not need streets.
