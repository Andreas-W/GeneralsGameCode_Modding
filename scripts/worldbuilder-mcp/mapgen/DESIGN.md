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

One real mockup from the user to a playable map, end to end, even if each step is crude. It shows which step matters most; the guess is ground texturing and scattering rather than cliff fidelity.

1. The user makes a mockup and a short description.
2. Reader and plan: levels, ramps from roads, starts, Tech and Supply, one terrain style by texture vote, town and forest areas.
3. Cliffs from the mockup's levels with one cliff recipe.
4. Ground texture from one reference's main textures (as the cliff recipes do now); trees scattered in forest areas.
5. Roads, then towns and outposts on the marked areas, `outpost_populate` with `decorate_tech`.
6. AI setup and the checks.

Then: style extraction for all reference maps, symmetry, and better cliff fidelity (mountains as ridges, bridges).

## Settled

- **Named areas** need no prefix: a mockup has no other triggers. `Mock` is an optional prefix for editing an existing map.
- **Description**: the user's prompt to Claude for now; a description file next to the mockup is optional.
- **Hills** stay as painted, unless very steep (small cliff) or very high and impassable (mountain).
- **Movement of starts, Tech and Supply**: `may_move`, default 50 units, 0 for strict positions.
