# Cliffs

Measurements of cliff shape on reference maps, and a prototype that generates cliffs by mixing independent properties. Textures are not studied here: they belong to a map's theme and will be extracted with its tileset. The prototype only borrows each reference's main textures to be comparable.

- `cliff_gen.py <recipe> [seed]`: the prototype, on a blank map through the second WorldBuilder. Saves to `work/gen_<recipe>/`. `cliff_measure.py gen_<recipe>` measures the result, `cliff_compare.py` sets it against the reference, `cliff_views.py` takes pictures of inner walls.
- `references.json`: the maps, with the user's description of each.
- `cliff_scan.py`: reads heights, passability, water and cliff mapping through the second WorldBuilder (`town/start_wb.py`), and takes views. Needs `TOWN_REF_MAPS`.
- `cliff_measure.py`: computes everything below from those files, without WorldBuilder. `work/relief.jpg` shows all maps: red impassable, yellow steep but passable, blue water.
- `uv run --with numpy --with scipy --with pillow python <script>`. Results go to `work/` (git-ignored).

Heights are in world units. A wall is any cell with slope of at least 0.75, where the cliff tool also starts mapping. A run is one connected piece of wall.

## Properties measured

| Map | Rise p50 / p90 | Width p50 | Face deviation per run p10 / p50 / p90 | Bend (12 cells) | Top / foot variation over 300 | Flat levels (height: share) | Near edge impassable % | Inner masses | Ramps | Mapped cells |
|---|---|---|---|---|---|---|---|---|---|---|
| Tournament Desert | 30 / 50 | 50 | 4.0 / 5.5 / 6.9 | 0.200 | 8.7 / 11.2 | 42:61 62:28 | 35 | 4 | 12 | 0 |
| Tournament Continent | 40 / 76 | 60 | 2.0 / 3.2 / 4.9 | 0.273 | 9.1 / 11.7 | 22:80 62:10 | 71 (sea) | 7 | 9 | 2670 |
| Tournament Lake | 55 / 69 | 80 | 1.2 / 2.2 / 6.3 | 0.082 | 7.8 / 10.1 | 7:91 | 0 | 9 | 2 | 120 |
| Homeland Alliance | 39 / 76 | 70 | 2.4 / 5.2 / 7.5 | 0.449 | 14.5 / 7.6 | 37:88 | 12 | 10 | 21 | 0 |
| Overland | 56 / 67 | 70 | 1.8 / 2.1 / 3.7 | 0.034 | 8.0 / 4.8 | 32:63 82:36 | 25 | 12 | 37 | 0 |
| Twilight Flame | 78 / 106 | 110 | 0.8 / 1.4 / 3.0 | 0.026 | 4.4 / 4.4 | 97:63 152:18 | 7 | 17 | 49 | 0 |
| Manic Aggression | 79 / 129 | 120 | 2.0 / 2.9 / 5.4 | 0.149 | 12.5 / 11.8 | 12:45 92:41 | 60 | 4 | 12 | 0 |
| Killing Fields | 70 / 115 | 100 | 4.3 / 5.1 / 8.1 | 0.478 | 24.8 / 21.8 | 82:91 | 22 | 3 | 10 | 0 |
| Hostile Dawn | 54 / 98 | 90 | 2.1 / 3.9 / 5.0 | 0.324 | 12.8 / 18.3 | 12:33 52:67 | 17 | 10 | 14 | 0 |
| Flash Effect | 36 / 43 | 60 | 2.1 / 2.9 / 3.7 | 0.252 | 4.3 / 1.3 | 12:42 37:55 | 93 (sea) | 0 | 11 | 0 |
| Dust Devil | 35 / 66 | 60 | 0.8 / 3.0 / 4.8 | 0.151 | 13.1 / 10.6 | 32:64 62:27 | 19 | 2 | 17 | 11 |

**Face deviation** is the user's definition of variation on a cliff: treat the wall as a steep plane and measure how far it leaves that plane along the plane's normal (rms over a plane fitted within 6 cells; `face_fine`). **Bend**: how much the direction a wall faces varies within 12 cells, 0 = straight, 1 = no common direction. Mapped cells include flat cells beside walls.

## What holds across these maps

- **Slope hardly depends on height.** Average slope stays 0.60 to 0.76 from 30 to 130 tall, with a steepest step of 1.1 to 1.7. A taller wall is wider, not steeper: about 1.5 units of width per unit of rise.
- **Walls have soft edges.** Top and foot each take 0.30 to 0.40 of the width for a quarter of the rise (an even ramp would be 0.25).
- **Two levels, or one level plus relief.** Playable flat ground gathers on at most two heights, with 45 to 81% of each map flat. The step between levels sets the wall height: from 20 (Tournament Desert) to 80 (Manic Aggression).
- **Ramps are one slope.** Median 0.22 to 0.31 everywhere, so about 0.23 is a safe default; only their number varies, 2 to 49.
- **Layered maps have a calm foot.** Where a level ends in a wall, the foot barely changes over short distances (0.7 to 2.2 over 100 units on Flash Effect, Overland, Twilight Flame, Tournament Desert, Dust Devil, Homeland Alliance) while the top varies more. Mountain maps (Killing Fields, Hostile Dawn) vary at both.

## Face deviation is the main detail control

- **It is one property, seen two ways.** The jag of a wall's line from above (direction change within 5 cells) ranks the maps almost the same as face deviation: Spearman 0.96 over the 11 maps. A wall that bulges along its normal has a wobbling line when seen from above. So jag and deviation should be one control, not two, and displacing the wall along its normal produces both.
- **It belongs to each cliff, not to the map.** Within a map, the p90 run deviates 1.7 to 6 times as much as the p10 run. Twilight Flame has the calmest median (1.4) but runs from 0.73 to 6.33. Seen in WorldBuilder: its 0.73 and 0.82 runs are flat planes, its 3.02 and 3.97 runs bulge and recess into gullies. This is what the user saw: high variation on some cliffs, very little on others. A median per map hides it.
- **Maps differ in how much they mix.** Tournament Desert and Flash Effect (1.7 to 1.8 times) keep their cliffs alike. Dust Devil and Tournament Lake (5 to 6 times) mix calm and rough ones. So a recipe needs a typical amount and a spread.

## Other results for the user's descriptions

- **"Rounded"** is large bends with little deviation: Tournament Continent and Flash Effect.
- **"Straight"** is low bend: Twilight Flame and Overland (about 0.03).
- **"Jagged, fuzzy"** is high deviation: Tournament Desert (5.5), Killing Fields (5.1), Hostile Dawn (3.9).
- **Cliff mapping** was used on Tournament Continent as the user suspected, slightly on Tournament Lake and Dust Devil, and not on the other eight. With mapping almost nothing runs out of the texture: 1 to 2 cells on each.

## Corrected or unsure

- An earlier version of this file used "noise on / beside the wall" (difference of two smoothings), and concluded that noise and jag were independent and that noise is local to cliffs. Those numbers mostly measured the wall's own S-shaped profile, so these conclusions are withdrawn. The values remain in `measure.json` under `rough`, and should not be used.
- Hills beside walls, with wall cells kept out of the fit (`hills`), come out small on every map: 0.2 to 0.75 above or below walls, 0 to 0.32 on open ground. Ground beside walls is a bit rougher than open ground, but this measure is weak: a median misses sparse hills and the fit absorbs large ones. Overland's small hills at the upper edge (user's description) do not stand out (0.33), so that property is not captured yet.
- A short run with a rounded end counts as high deviation: Twilight Flame's 6.33 is the end of a stub, not a rough face. Maximums are not to be trusted, and short runs should be filtered before taking values for generating.
- Homeland Alliance has high deviation (5.2), which the user did not mention. Its many short pieces along river banks may raise it.
- Wall views in `work/` show the densest wall area, which is not always typical: on Tournament Continent it is a decorative islet.
- Eleven hand-picked maps of one game. These ranges describe them, not all maps.

## The prototype

Each feature (`region`, `ridge`, `frame`) gives a signed distance to its outline; `parts` joins several shapes into one outline, so that overlapping ones leave no walls where they meet. Noise added to that distance moves the wall along its normal, which is the user's definition of variation. A soft profile then turns distance into height, with width 1.5 times the rise. On the map's ground the level stays flat; height changes fade with distance from the wall. In order of work:

1. Heights from the recipe.
2. Passability: impassable from slope 0.40, under water and in mountains, except the middle of each ramp. Reference maps block well beyond the steep middle: their first passable cells have a median slope of 0.13 to 0.50, usually about 0.25.
3. Ground texture, then cliff texture from a closed mask, since painting by slope leaves holes of ground texture in a bumpy face.
4. `terrain.reblend`, then `terrain.cliff_map` with `fit`.

**Ramps must be kept open on purpose.** A profile's steepest point is 1.5 times its average, so a ramp with an average of 0.23 peaks at 0.35, and wall noise then lifted parts over the 0.40 limit. In the game a USA Humvee drove at the wall and stopped: the levels of the Manic Aggression recipe were cut off from each other. Ramps now peak at 0.30, carry no noise and their middle is always passable. The generator prints `passable parts`: more than one large part means a level no ramp reaches. Small parts on the Tournament Desert recipe are ridge tops, unreachable by design.

Features, all built on those three kinds:

- **Plateau or pit**: a `region` with positive or negative rise. `level=True` names an absolute height instead of stacking.
- **River or channel**: a `ridge` with negative rise, and `water` in the recipe. River pieces get `bend=0`: they are already curved, and bend moved narrow channels off their line.
- **Mountain**: any feature with `relief`, which also makes it impassable. `mass()` is a lone rock or mountain, a round region with relief.
- **Gentle slope**: `wall_width` in world units, e.g. the flank of Tournament Lake's mountain.
- **Water** below the recipe's height, everywhere or only inside `water_area` (a crater lake high up). The generator deletes the "Default Water" area at height 7 that every new WorldBuilder map has: it flooded Tournament Lake's ground at 6.
- **Ramps**: `ramps` at fixed points, or `ramp_toward` (one or several) on the nearest point of the final outline, edges included.
- `img()` takes points as read off a relief picture (north up, y running down); `circle()` and `corner()` make shapes.

Eleven recipes, one per reference. Layout is a rough stand-in by hand, not a copy; Killing Fields is square here, the reference is tall.

| Reference / generated | Rise p50 | Width p50 | Slope | Peak slope | Face p10 / p50 / p90 | Jag | Bend | Levels (height: share) |
|---|---|---|---|---|---|---|---|---|
| Twilight Flame | 77.5 | 110 | 0.73 | 1.31 | 0.8 / 1.4 / 3.0 | 0.009 | 0.026 | 97:63 152:18 |
| generated | 55 | 80 | 0.67 | 1.06 | 0.6 / 1.2 / 2.8 | 0.004 | 0.004 | |
| Tournament Desert | 30 | 50 | 0.60 | 1.44 | 4.0 / 5.5 / 6.9 | 0.123 | 0.200 | 42:61 62:28 |
| generated | 50 | 60 | 0.75 | 1.75 | 4.7 / 6.4 / 7.2 | 0.186 | 0.362 | |
| Overland | 55.6 | 70 | 0.76 | 1.31 | 1.8 / 2.1 / 3.7 | 0.023 | 0.034 | 32:63 82:36 |
| generated | 52.5 | 70 | 0.71 | 1.25 | 1.5 / 2.2 / 3.2 | 0.024 | 0.033 | |
| Manic Aggression | 79.4 | 120 | 0.68 | 1.31 | 2.0 / 2.9 / 5.4 | 0.054 | 0.149 | 12:45 92:41 |
| generated | 74.4 | 100 | 0.77 | 1.62 | 3.5 / 4.3 / 5.7 | 0.077 | 0.134 | |
| Flash Effect | 36.2 | 60 | 0.64 | 1.31 | 2.1 / 2.9 / 3.7 | 0.040 | 0.252 | 12:42 37:55 |
| generated | 35 | 50 | 0.67 | 1.31 | 2.0 / 2.8 / 4.0 | 0.031 | 0.053 | |
| Dust Devil | 35 | 60 | 0.60 | 1.12 | 0.8 / 3.0 / 4.8 | 0.066 | 0.151 | 32:64 62:27 |
| generated | 56.2 | 70 | 0.76 | 1.62 | 2.4 / 3.3 / 5.8 | 0.056 | 0.098 | 32:66 62:31 |
| Tournament Continent | 40 | 60 | 0.71 | 1.62 | 2.0 / 3.2 / 4.9 | 0.047 | 0.273 | 22:80 62:10 |
| generated | 31.2 | 40 | 0.70 | 1.50 | 2.5 / 3.5 / 6.1 | 0.084 | 0.218 | 22:87 62:11 |
| Hostile Dawn | 53.8 | 90 | 0.66 | 1.31 | 2.1 / 3.9 / 5.0 | 0.082 | 0.324 | 12:33 52:67 |
| generated | 52.5 | 70 | 0.73 | 1.75 | 2.8 / 4.7 / 6.6 | 0.100 | 0.239 | 12:28 52:68 |
| Killing Fields | 70 | 100 | 0.74 | 1.62 | 4.3 / 5.1 / 8.1 | 0.111 | 0.478 | 82:91 |
| generated | 71.9 | 90 | 0.76 | 2.12 | 5.8 / 7.3 / 10.3 | 0.171 | 0.276 | 82:92 |
| Homeland Alliance | 39.4 | 70 | 0.66 | 1.62 | 2.4 / 5.2 / 7.5 | 0.227 | 0.449 | 37:88 |
| generated | 36.2 | 50 | 0.69 | 1.69 | 3.5 / 5.6 / 8.7 | 0.197 | 0.629 | 37:90 |
| Tournament Lake | 55 | 80 | 0.68 | 1.69 | 1.2 / 2.2 / 6.3 | 0.034 | 0.082 | 7:91 |
| generated | 45.6 | 90 | 0.53 | 1.31 | 1.5 / 2.2 / 3.4 | 0.024 | 0.043 | 7:94 |

Generated rows are seed 1. The numbers of the wall itself follow the recipe without tuning per map, so the method holds; one scale, `DISPLACE_PER_FACE`, was calibrated once for all recipes. Face p50 is within 20% of the reference on 8 of 11 maps.

**Mountains overshoot.** The maps with mountain masses come out rougher than their references: Killing Fields 7.3 against 5.1, Manic Aggression 4.3 against 2.9, Hostile Dawn 4.7 against 3.9. Relief explains part of it (Hostile Dawn without relief: 4.3). Generated mountains also merge with nearby walls into a few long runs (Manic Aggression: 5 runs against 31), so their roughness counts for the walls too. The reference mountains are longer ridges along walls; the generated ones are round masses.

**The per-cliff amount has a mean of 1, not a median of 1.** With a median of 1, recipes with a wide spread came out too rough (Dust Devil 3.6 against 3.0, Twilight Flame 2.1 against 1.4, over three seeds). With a mean of 1: Dust Devil 3.0, Twilight Flame 1.7.

**One seed is not enough to calibrate.** Twilight Flame's face p50 is 1.4 on seed 1 and about 2.0 averaged over seeds 1 to 3, with the old noise and the new alike. Calibrate on the average of several seeds. Face and jag need only heights, so this can be done without WorldBuilder: write each seed's heights from `build()` into `work/<name>/` as `heights.npy` with a `scan.json` (border, empty cliff report) and blank masks, and run `cliff_measure.py` on them.

**Wall noise** (`wall_noise`): one bump size made evenly spaced mounds, a row of eggs on the Tournament Desert recipe. The reference walls instead have calm straight stretches broken by clusters of sharp jogs. So the noise mixes five sizes, 1.5 to 28 cells, larger ones weaker (taking weight from the small ones flattened walls and lost the jag), and its strength changes along the wall over 12 to 30 cells (`LOCAL_SPREAD`), with a median of 1 since face deviation is a median per run. That local change also widens the spread between runs, so the per-cliff spread from `face_spread` gives up a share (`LOCAL_SHARE`); without that, face p90 came out about twice the reference. With 0.7 instead of 0.5, rough clusters broke off loose blobs. Over seeds 1 to 3, face p50 and p90 stay as close to the references as with the old noise, and `DISPLACE_PER_FACE` went from 3.8 to 4.0.

What does not match yet:

- **Bend on Flash Effect** (0.053 against 0.252): large smooth island outlines lack the bays and inlets of the reference. This is layout, not wall shape.
- **Along-wall height changes**: low on Twilight Flame and Flash Effect, too high on Manic Aggression (23.5 against 12.5). The frame's mountain relief also counts there, and it raises Manic's typical face deviation (4.3).
- **Rise and width on Tournament Desert, Twilight Flame and Dust Devil**: reflect how the rough layouts stack levels; on Dust Devil the edge mountains also count as tall walls.
- **Tournament Continent's shore walls** are 20 high, while the reference's rise p50 is 40: its shores or rocks are taller than drawn here.
- **Small unreachable parts**: on Homeland Alliance (seed 1), a 519-cell shelf on a plateau and a 203-cell pocket enclosed by river pieces. Killing Fields falls into three parts like its reference, whose river is crossed by bridges; the generator places none.
- **Ramp count** is simply what a recipe asks for; reference maps have more. Ramps have no measured width yet.
- **Overland's hills** are added as a property (`hills`) with an amount chosen by eye, as the measure does not capture them.
- **Not done**: symmetric layouts, layout from anything but hand-written polygons, bridges and fords, cliff dressing with objects, Homeland Alliance's gentle basins beside its river pieces.
- **Tested in the game only so far as this:** the first five maps load and draw without black gaps or foreign texture. Pathfinding was tried once, on the Manic Aggression recipe: a USA Humvee took the ramp to the upper level. The other recipes are only checked by `passable parts`; the six newer ones were looked at as relief pictures only. No AI match has been played on them. Playable copies are saved as `Maps\CliffGen<Recipe>`.
- **Seen in WorldBuilder, old against new wall noise under the same cameras:** on Tournament Desert the new noise gives some calm stretches and lumps of mixed length, but narrow ridge walls still read as a string of beads. The user's verdict: a bit more jagged and fuzzy than the reference, but acceptable as an example of the extreme end. The noise moves the wall about as far as the wall is wide, so each bump becomes a lump of its own; mixing sizes does not change that. Overland looks the same with both: a thin band of cliff texture between grass slopes. That is texture and profile, not noise: cliff texture covers only the steep middle, the soft top and foot stay grass.
- **Seen in the game** (before the new wall noise): Tournament Desert's mounds came out evenly spaced like a row of eggs. Overland's wall reads as a flat band of busy texture rather than a cliff. Manic Aggression's walls end in blunt rounded ends beside ramps, one with a small dark slit (not looked into).
- **Crashes** of the second WorldBuilder, three times so far (Flash Effect, Tournament Desert, Hostile Dawn recipes), always after several maps in a row, with no exception in the log. Restarting it and running again worked. Cause unknown.

## Next steps

In this order:

1. **Overland's band** (low priority; the beads on Tournament Desert are accepted): see above. Not solved by noise spacing; needs a look at how far cliff texture reaches. Narrow walls could limit displacement to less than their width if beads matter again.
2. **Automatic layouts**: generate outlines instead of hand-written polygons, starting with level steps, then plateaus and frames.
3. **Symmetry**: build one part and repeat it with `map_symmetrize`.
4. **Missing features**: mountains as ridges along walls instead of round masses, bridges or fords, ramp width (not measured yet), bays and inlets for Flash Effect.
5. **AI test**: `ai_skirmish_setup` and an AI match on a generated map, to show that it plays.
6. **MCP tool**: a `cliff_generate` tool once recipes hold up, as with `town_generate`.

On a new machine: `work/` is git-ignored, so run `cliff_scan.py` and `cliff_measure.py` again (second WorldBuilder running, `TOWN_REF_MAPS` set to the folder holding `Maps\`). `town/start_wb.py` first needs `town/gamedata.py` run once. `WorldBuilderZH.exe` in the mod folder must be a build with the `terrain.cliff_*` commands. Close every WorldBuilder before testing in the game.

## Kinds of layout (from the relief pictures, by eye)

Not measured. Each is a way of laying out walls, independent of the wall's shape:

- **Level step along a boundary**: Twilight Flame, Dust Devil, Hostile Dawn, Manic Aggression.
- **Thin spurs that make lanes**, with level changes between: Tournament Desert.
- **Inner plateau or ring**: Overland (rim plus centre), Tournament Lake (crater with broken ring on a cone), Twilight Flame (centre).
- **Shore cliffs around land**: Flash Effect, Tournament Continent.
- **River banks**: Homeland Alliance, Killing Fields.
- **Mountain frame on the map edge**: Manic Aggression, parts of Tournament Desert and Dust Devil.
- **Lone masses as obstacles or decoration**: Killing Fields, Tournament Continent, Hostile Dawn.

Almost all are symmetric, so one part can be built and repeated with `map_symmetrize`.
