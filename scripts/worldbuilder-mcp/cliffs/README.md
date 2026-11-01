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

Each feature (`region`, `ridge`, `frame`) gives a signed distance to its outline. Noise added to that distance moves the wall along its normal, which is the user's definition of variation. A soft profile then turns distance into height, with width 1.5 times the rise. On the map's ground the level stays flat; height changes fade with distance from the wall. In order of work:

1. Heights from the recipe.
2. Passability: impassable from slope 0.40 and in mountain frames, except the middle of each ramp. Reference maps block well beyond the steep middle: their first passable cells have a median slope of 0.13 to 0.50, usually about 0.25.
3. Ground texture, then cliff texture from a closed mask, since painting by slope leaves holes of ground texture in a bumpy face.
4. `terrain.reblend`, then `terrain.cliff_map` with `fit`.

**Ramps must be kept open on purpose.** A profile's steepest point is 1.5 times its average, so a ramp with an average of 0.23 peaks at 0.35, and wall noise then lifted parts over the 0.40 limit. In the game a USA Humvee drove at the wall and stopped: the levels of the Manic Aggression recipe were cut off from each other. Ramps now peak at 0.30, carry no noise and their middle is always passable. The generator prints `passable parts`: more than one large part means a level no ramp reaches. Small parts on the Tournament Desert recipe are ridge tops, unreachable by design.

Five recipes, each using the numbers of one reference. Layout is a rough stand-in by hand, not a copy.

| Reference / generated | Rise p50 | Width p50 | Slope | Peak slope | Face p10 / p50 / p90 | Jag | Bend |
|---|---|---|---|---|---|---|---|
| Twilight Flame | 77.5 | 110 | 0.73 | 1.31 | 0.8 / 1.4 / 3.0 | 0.009 | 0.026 |
| generated | 54.4 | 80 | 0.65 | 1.06 | 0.9 / 1.4 / 2.3 | 0.010 | 0.013 |
| Tournament Desert | 30 | 50 | 0.60 | 1.44 | 4.0 / 5.5 / 6.9 | 0.123 | 0.200 |
| generated | 45 | 60 | 0.72 | 1.62 | 3.9 / 6.0 / 7.7 | 0.145 | 0.308 |
| Overland | 55.6 | 70 | 0.76 | 1.31 | 1.8 / 2.1 / 3.7 | 0.023 | 0.034 |
| generated | 53.1 | 70 | 0.71 | 1.31 | 1.4 / 2.3 / 4.2 | 0.028 | 0.037 |
| Manic Aggression | 79.4 | 120 | 0.68 | 1.31 | 2.0 / 2.9 / 5.4 | 0.054 | 0.149 |
| generated | 74.4 | 100 | 0.76 | 1.62 | 1.4 / 4.4 / 4.6 | 0.075 | 0.141 |
| Flash Effect | 36.2 | 60 | 0.64 | 1.31 | 2.1 / 2.9 / 3.7 | 0.040 | 0.252 |
| generated | 34.4 | 50 | 0.66 | 1.31 | 2.2 / 2.5 / 2.7 | 0.029 | 0.047 |

The numbers of the wall itself follow the recipe without tuning per map, so the method holds; one scale, `DISPLACE_PER_FACE`, was calibrated once for all recipes.

What does not match yet:

- **Bend on Flash Effect** (0.046 against 0.252): large smooth island outlines lack the bays and inlets of the reference. This is layout, not wall shape.
- **Along-wall height changes**: low on Twilight Flame and Flash Effect, too high on Manic Aggression (25.7 against 12.5). The frame's mountain relief also counts there, and it raises Manic's typical face deviation (4.4).
- **Rise and width on Tournament Desert and Twilight Flame**: reflect how the rough layouts stack levels.
- **Ramp count** is simply what a recipe asks for; reference maps have more. Ramps have no measured width yet.
- **Overland's hills** are added as a property (`hills`) with an amount chosen by eye, as the measure does not capture them.
- **Not done**: symmetric layouts, rivers, inner masses, layout from anything but hand-written polygons, cliff dressing with objects.
- **Tested in the game only so far as this:** all five maps load and draw without black gaps or foreign texture. Pathfinding was tried once, on the Manic Aggression recipe: a USA Humvee took the ramp to the upper level. The other four are only checked by `passable parts`. No AI match has been played on them. Playable copies are saved as `Maps\CliffGen<Recipe>`.
- **Seen in the game:** Tournament Desert's mounds come out evenly spaced like a row of eggs, as the noise has one main size. Overland's wall reads as a flat band of busy texture rather than a cliff. Manic Aggression's walls end in blunt rounded ends beside ramps, one with a small dark slit (not looked into).
- **One crash** of the second WorldBuilder during the Flash Effect recipe, with no exception in the log. The same run then worked. Cause unknown.

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
