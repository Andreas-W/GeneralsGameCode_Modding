# Map generation from a mockup

Design note, agreed with the user. Nothing here is built yet; the pieces it builds on are the town, outpost and cliff generators (`../town/README.md`, `../cliffs/README.md`).

## Goal

Semi-automated map generation from patterns, layouts and style elements of reference maps.

**First use case (MVP):** the user makes a rough mockup in WorldBuilder: a basic heightmap for layout and topology, paths, player starts, optionally Tech and Supply objects. A description goes with it, naming specific maps ("cliffs like Overland") or general styles (desert, alpine, urban, night, winter). For now the description is the user's prompt to Claude; a description file next to the mockup is optional, for when a mockup is generated again later. The plan records what was read from either. The generator turns that into a finished, playable map.

## Pipeline

1. **Read** the mockup: heights, passability, textures, objects, roads, waypoints, named areas, and the description.
2. **Plan**: turn every signal into a readable plan (below). Claude shows it to the user ("2 levels at 30 and 60, 4 ramps of which 3 from roads, a town at A: Arabic 70%, urban 30%, pine forest at B, terrain style: Tournament Desert 60%"). The user corrects it. This step is the "semi" in semi-automated.
3. **Build** from the plan into a **new map**, never over the mockup, so the user can change the mockup or try other seeds. The same plan and seed give the same map.
4. **Check** the result against the mockup's intent (below).

Claude interprets prose in the description into plan entries, using the style catalog. The tools themselves take explicit parameters and stay repeatable.

## The mockup

Everything in a mockup is evidence with a weight, not a command, unless locked.

| What the user places | What it signals | Weight and rules |
|---|---|---|
| Texture patches | Terrain style of that area; all patches together vote for the map's style | Weight by painted area. The largest texture (the base the map started with) gives no signal unless the description names it. A patch styles its surroundings, so one snow patch on a mountain does not make a winter map. |
| Heights | Levels, cliffs, ramps, hills, mountains | Heights are rounded into a few levels. A gentle slope joining two levels is a ramp; a steep step is a cliff. A hill stays as painted, unless it is very steep (then it is a small cliff) or very high and impassable (then it is a mountain). Brush outlines of levels are smoothed before a cliff style is applied. |
| Impassable painted in the editor | Rough or impassable ground: mountain, cliff or rough mass, whatever its texture | The strongest signal for rough ground. Also an explicit "keep impassable" for the checks. Impassable lines along the map edge become a mountain frame or border cliffs. |
| Civilian buildings | Town or outpost areas; their types put weight on a building family | A dense cluster is a town, one to three buildings an outpost. Building types vote for the family (`arabic`, `highrise`, `asian`, `american`, `eastern` in `building_themes.json`, outpost themes). Rotation can signal street direction. Hints by default: the generator fills the area with a coherent pack. |
| Tech and Supply objects | Fixed game locations | Locked by default, but may move a little (`may_move`, default 50 units) to fit the generated terrain: they must end on flat, buildable, reachable ground. Mirrored pairs move as mirrored pairs. The kind of object stays (supply dock or pile, which Tech building). Never replaced by other objects. |
| Trees | Forest areas and tree types | A clump marks a forest edge, to be filled; a lone tree means scatter this type around. Tree types also vote for the terrain style (pines: alpine). |
| Roads | A guaranteed open path, and a road style | See below. |
| Waypoint paths | An open lane without a road | Labeled `Center1`, `Flank1`, `Backdoor1` etc., they also become AI lanes for `ai_skirmish_setup`. |
| `Player_N_Start` waypoints | Start positions | Locked, and may move like Tech and Supply (`may_move`). |
| Named areas (polygon triggers) | Explicit intent for an area | See below. |

### Rough ground: height and passability decide, not texture

Textures named rock, rubble or even cliff are also used for decorative flat ground (cobblestone, rubble, yards). So:

- **Rough or impassable** needs evidence from height or passability: impassable painted in the editor (strongest), steep ground in the mockup heights. A rock or cliff texture there supports it and votes for which rock textures to use.
- **Rock, rubble or cobblestone texture on flat, passable ground** is decoration: a terrain style hint for that spot, never an obstacle.
- **Cliff textures** are a weak hint; on flat ground they count like any other style texture.

### Roads and paths

1. **A road is first a guaranteed path.** Its corridor stays free of cliffs, trees and buildings and stays passable. Where a road crosses a level step, that crossing becomes a ramp: roads also place ramps.
2. **Its road type is a style hint**, overridden by the description or the object style.
3. **A lane without a road** is a waypoint path. Road means paved and open, path means open only.

### Named areas

Polygon triggers with fixed names, easy to type and to read back: `Town`, `Outpost`, `Forest`, `Open` (keep clear), `Mountain`, `Water`, `Style <reference map>`, each optionally followed by more words (`Town Arabic`, `Style Overland`). A mockup has no other triggers, so no prefix is needed. When an existing map is edited, whose real triggers could be mistaken for these, the optional prefix `Mock` (`Mock Town Arabic`) marks the mockup's areas. The generator removes the areas it read as mockup areas from the finished map; AI areas (`InnerPerimeter1` ...) and water areas stay.

### Lock or hint

An object whose script name starts with `keep` is locked: it stays where it is, as it is. Defaults: starts, Tech and Supply are locked but may move a little; buildings, trees and roads are hints.

**How far locked objects may move** is a parameter, `may_move` in world units: 50 by default for starts, Tech and Supply, 0 for strict positions. It can be set for the whole map or per object kind, and an object named `keep...` never moves.

### When signals disagree

Named area > description > locked objects > other objects > textures > heights.

## The plan

A JSON file, one entry per decision, each with where it came from and how sure it is, so that Claude can explain it and the user can edit it. A sketch:

```json
{
  "seed": 1,
  "symmetry": {"kind": "rotate180", "from": "starts and Tech/Supply"},
  "styles": {
    "terrain": [{"ref": "Tournament Desert", "weight": 0.6, "from": "textures"}, {"ref": "Dust Devil", "weight": 0.4, "from": "description"}],
    "objects": [{"family": "arabic", "weight": 0.7, "from": "buildings"}],
    "cliff":   [{"ref": "Overland", "from": "description"}],
    "atmosphere": [{"ref": "Tournament Desert", "from": "terrain"}]
  },
  "levels": [{"z": 30, "share": 0.6}, {"z": 60, "share": 0.3}],
  "ramps": [{"at": [1200, 800], "from": "road crossing"}],
  "areas": [
    {"kind": "town", "polygon": [[...]], "family": "arabic", "from": "6 civilian buildings"},
    {"kind": "forest", "polygon": [[...]], "trees": ["TreePine"], "from": "tree clump"}
  ],
  "keep": [{"object": "TechOilDerrick", "at": [900, 400], "may_move": 50}, {"object": "Player_1_Start", "at": [400, 300], "may_move": 50}],
  "roads": [{"points": [[...]], "type": "TwoLane", "from": "mockup road"}]
}
```

## Styles

A style is extracted from a reference map and can be mixed with styles of other maps (Alpine terrain with Overland-shaped cliffs).

| Style | Holds | Extracted from |
|---|---|---|
| **Terrain** | Ground textures and how they blend, cliff and rock textures, trees, rocks and props with their densities | Texture and object scans |
| **Objects** | Building families, town and outpost packs, road types | Town packs, `outposts.json` kits |
| **Cliff** | Shape only: rise, width, face deviation and spread, bend, top and foot variation, ramps | `cliffs/cliff_measure.py` |
| **Water** (later) | Rivers, shores, banks | |
| **Atmosphere** | Lighting, time of day, weather | `map.ini` and the map's global settings |

Cliff textures belong to Terrain, not Cliff, so alpine terrain with Overland-shaped cliffs still gets alpine rock.

**Goal: Terrain and Object styles from every reference map**, not only the ones named so far (the maps folder holds 151, campaign maps included). Each map gets tags for climate and setting, mostly derived from texture and object names (Snow, Sand, Grass, Arabic ...) and checked by the user. The tags let a description such as "a desert town at night" find its references.

## Checks against intent

- Every start reaches every other start.
- Every Tech and Supply object is reachable from a start.
- Roads and waypoint paths stay passable from end to end.
- Areas painted impassable stay impassable.
- If the mockup is symmetric, the result is too.
- AI: `ai_skirmish_check`, and optionally an AI match (`ai_match_run`).

The passable-parts check in `cliff_gen.py` and the AI tools exist; they need to be pointed at the mockup's objects.

## What exists and what is missing

| Step | Exists | Missing |
|---|---|---|
| Read mockup | Bridge reads heights, passability, textures (`terrain.sample`), objects, roads, waypoints, polygons | Reader that turns them into signals |
| Plan | | Plan format, signal weighting, symmetry detection |
| Cliffs | `cliff_gen.py`: signed distance, wall noise, profile, ramps, recipes from 11 references | Outlines from mockup levels instead of hand-written polygons |
| Terrain style | Cliff recipes borrow each reference's main textures | Terrain style extraction, painting and blending by style, tree and rock scattering by style |
| Towns, outposts | `town_generate`, `outpost_place`, `outpost_populate`, packs | Picking pack and family from the plan |
| Roads | `roads_route`, `roads_add` | Corridor clearing, ramps at level crossings |
| Atmosphere | `map_set_info` | Extraction from references |
| Symmetry | `map_symmetrize` | Detection from the mockup |
| AI and checks | `ai_skirmish_setup`, `ai_skirmish_check`, `ai_match_run` | Checks aimed at the mockup's objects |

## First slice

**Status:** under way on the user's TaintedParadise mockup, stages A to D done (terrain shape, textures, trees, towns and outposts), E and F (naval and AI markers, checks) next. Scripts, how each stage went and what was learned: `README.md`.

One real mockup from the user to a playable map, end to end, even if each step is crude. It shows which step matters most; the guess is ground texturing and scattering rather than cliff fidelity.

1. The user makes a mockup and a short description.
2. Reader and plan: levels, ramps from roads, starts, Tech and Supply, one terrain style by texture vote, town and forest areas.
3. Cliffs from the mockup's levels with one cliff recipe.
4. Ground texture from one reference's main textures (as the cliff recipes do now); trees scattered in forest areas.
5. Roads, then towns and outposts on the marked areas, `outpost_populate` with `decorate_tech`.
6. AI setup and the checks.

Then: style extraction for all reference maps, symmetry, and better cliff fidelity (mountains as ridges, bridges).

## Texturing rules (from the user, for a later pass)

The first slice paints each role with one reference's textures in its measured shares, split by noise (`textures_from_plan.py`, roles measured by `texture_roles.py`). The user judged that a step up, but painting needs more rules:

- **Larger noise:** textures meet as intersecting strands and bands, not single cells or dots. The per-cell scatter of the first slice is too fine.
- **Mostly one texture family per layer:** the beach mostly sand, the main level mostly grass, with different grasses only as detail within the grass. A large slope between two layers gets a transition from one to the other, not a mix of both.
- **No strong contrast side by side:** e.g. dark grass next to bright sand. Textures mixed within a layer, and those meeting at a transition, should be close in brightness and colour.
- **Edge detail:** the edge of a layer may carry a detail texture, e.g. dirt or rocks between grass and a cliff.

Measuring this on references means more than shares per role: which textures border which, how wide transitions are, and what lines a layer's edge.

## Texturing: the plan

Agreed with the user. Texturing is learned from reference maps, one reference at a time, and painted by location with texture sets.

**Locations are measured, not named.** Every cell gets features: height relative to its level, slope, convexity (ridge or hollow), distance to water, to a cliff foot and to a cliff top, size of the flat area it belongs to, and what is near it (trees, roads, buildings). Named locations (beach, hill top, connected plain) are combinations of these. The same code measures references and generated terrain.

**Texture sets instead of single textures.** A location is painted with a set: a main texture, accents (e.g. a darker grass), a soft contrast (e.g. dirt or leaves), transitions to neighbouring locations (e.g. grassy sand between grass and beach, snowy dirt between dirt and snow), and edge detail (e.g. rocks between grass and a cliff). Roles in a set are measured: the main texture covers most of a location, accents lie inside its patches, transitions and edge detail along its borders.

**What a texture looks like** is needed for "no strong contrast": mean colour, brightness and variation of each texture, and its tile size (a patch smaller than one tile of its texture looks cut out). Read from the game's terrain art: `Terrain.ini` names the file of each texture class, the files are TGAs in `Terrain.big` / `TerrainZH.big`, read in place with `bigfs.py` (the engine's lookup order; terrain textures rarely change, so reading again on demand is enough).

**Painting methods**, all with a minimum patch size (no speckles: patches and holes below about 3 x 3 cells are removed):
- fractal noise with a minimum feature size, and warped noise for interlocking, flowing shapes;
- gradients between locations (grass to sand), dithered at a large scale;
- outlines (a grass patch in a desert has a rim of grassy sand), from distance to the patch;
- streaks and bands: along the contour on slopes (strata), down the slope (erosion), or along a map-wide wind direction on flat ground;
- context: forest floor under tree clumps, worn ground around buildings, Tech and roads.

**Constraints:** a cell blends only two textures, and where three meet the edge is hard (fine scattering raised hard edges from 37 to 201 on the first map), so tiny three-way corners are avoided; the number of texture classes a map can hold is limited (`can_fit` in `terrain_list_textures`); textures side by side stay close in brightness and colour.

**Steps:**
1. **Data:** per-cell texture maps (base texture per cell) of all multiplayer reference maps, with heights, water, passability, trees, roads and buildings; every texture's mean colour, brightness, variation and tile size. Which maps are multiplayer: `Maps\MapCache.ini` in `MapsZH.big` (`isMultiplayer`): 64 maps, 62 of them in `MapsZH.big`, 2 from Generals' `maps.big`.
2. **Features** per cell, on references and generated terrain alike.
3. **Model per reference:** texture by location (as probabilities: painters are not consistent), texture sets and their roles from what appears together and what borders what, transitions and their widths, patch statistics per texture (area, raggedness, elongation and direction, mixing).
4. **Painter:** per location the set's main texture, then accents and soft contrast by warped noise, then outlines and transitions at location borders, then context; under the constraints above.
5. **Evaluate** on the same numbers as the reference, plus an offline top-down preview in each texture's mean colour (fast iterations without WorldBuilder), then repaint stage B of the first map for the user.

Mixing references comes later: the first map's mix of four references looked like "desert and tropical".

**Done so far:**
- Step 1 (`texture_colors.py`, `ref_scan.py`): 235 textures measured; all 64 multiplayer maps scanned (8 to 28 textures each, 219 in all, every cell with a texture). The numbers confirm the user's contrast complaint on the first map: `GrassMediumType21` brightness 0.47 next to `SandLargeType2` 0.80.
- Step 2 (`cell_features.py`, `feature_check.py`): 17 features per cell on all 64 maps. Alone, each explains 10 to 21 % of where a map's textures go (height rank most, then distance to a cliff, height against the surroundings, flat area size; rocks, water depth and cliff side little). Together, one half of a map predicts the other half's texture on 60 % of cells (median; 26 to 89 %), against 37 % for always the most common texture. Weakest are maps with many textures in mixes the features cannot see (Flash Fire, Tournament Urban, Leipzig Lowlands, Silent River): accents and urban ground, for step 3's patch statistics. Distance to the map edge ranks high, partly because mirrored layouts let position give the answer away; to be watched.
- Step 3 (`texture_model.py`): a model per reference. Roles from borders: an accent has at least 60 % of its border on one larger host (or covers under 0.5 % of the map), a transition borders two larger textures that meet each other less than they meet it (never next to a cliff), cliff textures lie on steep faces, edge detail within 3 cells of one. On average over the maps: main 65 % of cells, accents 17 %, cliff 9 %, transitions 9 %. Zones are main textures with their accents; zones that border, differ in brightness by at most 0.15 and that location does not tell apart (the classifier mixes them up 30 % of the time or more) merge into a family whose variants are mixed by noise: 56 families, median 3.5 variants (Leipzig Lowlands: eight grasses, one family). Maps have 1 to 12 zones (median 4); on 12 maps one family covers over 90 %, which is the user's "mostly one family per layer". One half of a map predicts the other's zones on 90 % of cells (median; 79 % before families; most common zone 72 %). Absolute height and the edge distance are left out. Weak where a map has regions no feature describes (Mountain Guns: snow, grass and desert apart): on a mockup the user's painted textures give the regions. Patches of accents and variants: median area about 40 cells (middle half 16 to 110), raggedness 1.35, elongation 2; transitions about 10 cells wide (middle half 6 to 20).
- Step 4 (`painter.py`): paints offline from a model in about 5 seconds. Order: zones (the model's probabilities smoothed over 1.5 cells, noise on their borders, steep faces always cliff), family variants, transitions (bands of the measured width where their two sides meet, plus the share the reference has away from zone borders as strands through the zone), accents (inside their host, by noise plus where the reference has them by location: a second classifier per texture), cliff textures by slope rank, then no patches under 3 x 3 and at three-texture corners the weakest cell takes the texture around it. The border is the nearest zone's main texture, held the same over 41 cells along the edge. Painting a reference with its own model gives its shares and patch sizes roughly back, with 1 to 2 times its three-texture corners (2 to 7 before the corner pass) and less fine detail. On the first map's terrain (scanned into `work/maps/`) four tropical and grass references cut its three-texture corners from 4717 (stage B) to 750 to 1000 and the brightness step across texture borders (90th percentile) from 0.33 to 0.15 to 0.29.
- Step 5, first run (`paint_map.py`): the first map repainted in WorldBuilder with TournamentA's model (the user's choice), its town ground kept (Concrete, Asphalt: 5024 cells). 13 textures. WorldBuilder counts three-way tiles 5299 before and 895 after, hard edges 67 and 25. The user judged it good in WorldBuilder.

## Relief: the plan

The user's request: soft hills and height changes, general noise, soft plateaus, pits and hills, start areas, towns and outposts kept mostly flat. Stage A so far gives flat levels joined by cliffs, ramps and beaches, plus a little noise on the levels.

**Measured on the 64 multiplayer maps** (`relief_measure.py`, inside the levels: 300 or more from cliffs and water):
- The ground is mostly flat: 92 % of it has no slope (median; 83 to 97 %). Relief is **flat patches at different heights**, about 2.3 per km^2 and some 4000 cells each (630 x 630), **joined by soft slopes** some 70 wide (45 to 100), of slope 0.06 (90th percentile 0.14). The heights of the patches spread by 1 (median) to 18 (75th percentile), up to 30 on Golden Oasis and Twilight Flame: some maps are flat, others terraced.
- **Hills and pits** on top: 1.2 hills per km^2 (some 430 cells, 15 high), 0.7 pits per km^2 (larger, 1200 cells, 12 deep), a few with a flat top.
- **Fine noise** is small: 0.7 (standard deviation below some 80 units).
- **Starts are flat**: height spread 0.03 within 300 of a start, 0.2 to 450, 1.5 to 600, normal relief (8) only from 600 to 800.
- Our generated maps roll slightly everywhere instead (slope 0.03 over most of the ground, patches of 500 cells): plain noise.

**The relief step** (`relief.py`, after stage A): on each level's interior, away from cliffs, ramps, beaches and water (anything not on a level fades it out):
1. **Patches**: warped cells of some 600 units, each lifted or lowered by a random amount (the plan's spread), smoothed so that the borders become soft slopes of the measured width; soft plateaus and soft pits are patches that stand out.
2. **Hills and pits**: rounded bumps and hollows at the measured rate, size and height, some with a flat top.
3. **Fine noise**, small.
4. **Kept flat**: starts (flat to 450, back to full relief by 650), towns, outposts, Tech and supplies (flat to their radius); the relief fades in around them.
5. **Checks**: no slope above the plan's limit (softened where it is), no new impassable ground, land stays above the water; the same numbers as the references to compare.

**Settled on the fourth map** (OasisLakeGen): the user approved the result as the general height variation. `relief.py`'s defaults hold it: patches of 600 lifted or lowered by about 12, hills 8 to 20, pits 6 to 14, starts flat to 350 and levelled to 240, full relief by 550, nothing within 40 to 150 of cliffs, ramps and beaches. About half of a level then changes by over 3, soft slopes are as on the references (0.06, 90th percentile 0.15).

## Settled

- **Named areas** need no prefix: a mockup has no other triggers. `Mock` is an optional prefix for editing an existing map.
- **Description**: the user's prompt to Claude for now; a description file next to the mockup is optional.
- **Hills** stay as painted, unless very steep (small cliff) or very high and impassable (mountain).
- **Movement of starts, Tech and Supply**: `may_move`, default 50 units, 0 for strict positions.
