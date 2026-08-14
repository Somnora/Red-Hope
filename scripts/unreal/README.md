# Headless Unreal tooling

UE runs Python headlessly with the editor closed, which turns out to be the
cheapest inspection-and-repair surface in the project — no compile gate:

```bash
UnrealEditor-Cmd <proj> -run=pythonscript -script=<abs path>.py \
  -unattended -nosound -stdout -nosourcecontrol
```

**Gotcha:** neither `print()` nor `unreal.log()` reliably reaches stdout from a
commandlet run. Have the script **write a report file** and read that instead.
Every script here does. Override the destination with `RH_REPORT=<path>`.

## What this lane can do (all without a compile)

| script | does |
|---|---|
| `rh_audit_nanite.py` | reports Nanite state + LOD0 counts for every art mesh |
| `rh_fix_nanite.py` | disables Nanite on every art mesh that has it |
| `rh_reimport_inplace.py` | re-imports meshes **or textures** over their existing asset path |
| `rh_author_master.py` | authors `M_RH_Master`'s whole graph |
| `rh_tune_motion.py` | sets the per-machine ambient-pulse parameters |
| `rh_terrain_macro.py` | adds/retunes the terrain's macro albedo variation |
| `rh_capture.sh` | boots the game for real and harvests screenshots |
| `rh_tune_emissive.py` | blunt knob: sets EmissiveAmount on every MI |
| `rh_import_masks.py` | imports grayscale mask PNGs as linear TC_Grayscale |
| `rh_assign_masks.py` | per-model emissive mask + post-mask strength table |

**Materials are compile-free.** `UMaterialEditingLibrary` is a
`UBlueprintFunctionLibrary`, so its whole graph API is exposed to Python:
`create_material_expression`, `connect_material_expressions`,
`connect_material_property`, `recompile_material` (which *returns the compile
errors*, so a script can verify itself), plus the full instance-parameter setter
suite. `rh_author_master.py` is therefore the canonical place to edit the master
material now; `RHArtWireCommandlet::AuthorMaster` remains as the C++ bootstrap.
Rebuilding the graph is safe for existing instances — `MI_<name>` overrides bind
by parameter **name** and survive, which was verified after the rebuild.

**Pin-name gotcha:** an expression with a single input reports that pin as
`"None"` (see `get_material_expression_input_names`); pass `""` to connect it.
Multi-input nodes use real names (`A`/`B`, `A`/`B`/`Alpha`).

**The graph can be READ, not just written** (5.8, found 2026-08-14). This is what
makes *surgical* edits possible — you no longer have to rebuild a material from
scratch just to change one thing:

| call | gives you |
|---|---|
| `get_material_expressions(mat)` | every node in the graph |
| `get_material_property_input_node(mat, MaterialProperty.MP_BASE_COLOR)` | what currently drives an output |
| `get_material_property_input_node_output_name(...)` | which of its outputs |
| `get_inputs_for_material_expression(mat, expr)` | a node's upstream inputs |
| `get_material_expression_input_names/output_names(expr)` | its pin names |
| `duplicate_material_expression`, `delete_material_expression`, `disconnect_material_property` | edit in place |

Note `mat.get_editor_property("expressions")` does **not** work in 5.8 (the
property moved); go through `MaterialEditingLibrary` instead. `rh_terrain_macro.py`
is the worked example: it reads whatever drives BaseColor, wraps it in one
multiply, and leaves the other 58 nodes of the triplanar untouched.

## In-place reimport — the only way to re-cut a FLAT-layout asset

Three layouts exist in this project: **FLAT** (`Art/<Grp>/<n>/<n>`, 23 assets,
FBX era), **single+SM** (`Art/<Grp>/<n>/StaticMeshes/<n>`, 75), and **DOUBLE**
(`Art/<Grp>/<n>/<n>/StaticMeshes/<n>`, 34).

The `ImportAssets` commandlet's `-dest` is fed through Interchange's SubPath
machinery — `bSceneNameSubFolder` and `bAssetTypeSubFolders`, both **true in the
glTF pipeline and false in the FBX/OBJ one**, which is exactly why the two
lineages have different shapes. So a `-dest` re-import of a FLAT asset creates a
*new* asset at a double-folder path and silently orphans the one the game
references.

Setting `ImportAssetParameters.reimport_asset` bypasses SubPath entirely and
writes to the existing package verbatim — flat, single and double alike, and it
works on the legacy FBX assets too (a GLB with exactly one mesh node takes the
single-factory-node short-circuit). That is what `rh_reimport_inplace.py` does:

```bash
RH_MANIFEST=pairs.json RH_REPORT=/tmp/r.txt UnrealEditor-Cmd <proj> \
  -run=pythonscript -script=$PWD/scripts/unreal/rh_reimport_inplace.py \
  -unattended -nosound -stdout
# pairs.json: [{"asset": "/Game/...", "source": "/abs/mesh.glb"}, ...]
```

Preconditions: the GLB must contain exactly **one** mesh node (`rh_finish.py`
joins), and internal mesh/image names must stay stable.

---

## Seeing the game: scripted capture (`rh_capture.sh`)

Every art verification before 2026-08-14 was a *receipt* — triangle counts,
warning counts, battery pins. None of it was a pixel. `rh_capture.sh` closes
that: it boots the real game with a real Metal RHI, drives it with console
commands, and harvests the PNGs, so the art can be reviewed as images.

```bash
bash scripts/unreal/rh_capture.sh surface \
  "r.setres 1600x900w, RH.Demo, RH.ActivateCrop all, RH.Floor 0, RH.Snapshot 35, RH.Snapshot 70" \
  2 400
```

Three things cost a run each to learn, so they are worth stating plainly:

1. **`-ExecCmds` must carry NO literal quotes, and must come last.** The form the
   docs imply, `-ExecCmds="a, b arg"`, makes the engine run *nothing at all* —
   silently, with no warning. Pass it as one shell-quoted argument instead
   (`"-ExecCmds=$CMDS"`). Because `ParseExecCmdsFromCommandLine` parses with
   `bShouldStopOnSeparator = false`, the value runs to end of line, which is
   both why the unquoted form tolerates spaces *and* why nothing may follow it.
2. **`-resx`/`-resy` do not set the screenshot size** — the saved
   `GameUserSettings` resolution wins. Put `r.setres <W>x<H>w` first in the
   command list.
3. **Delay the snapshot past the load.** `RH.Snapshot <seconds>` schedules on a
   world timer; the first ~30 s of a cold-DDC boot is texture building, and a
   shot taken then catches an unfinished scene. 35 s and up is safe.

`RH.Demo` rides the elevator down, so add `RH.Floor 0` for surface shots.

---

## The Nanite trap (found 2026-08-14 — re-run the fixer after every import)

`Config/DefaultEngine.ini` sets **`r.Nanite.ProjectEnabled=False`**, but UE 5.8's
Interchange GLB import turns Nanite **on** at the asset level by default. When
the project has Nanite off, such a mesh does not draw its real geometry — it
draws its reduced Nanite **fallback proxy**. Measured on the shipped library:

| mesh | drawn before | actual |
|---|---|---|
| HabitatDome | 3,962 tris | 18,000 |
| HeavyForge | 5,766 | 18,000 |
| SolarPanel | 8,532 | 18,000 |
| CommandModule | 5,786 | 17,999 |
| RH_AirFilter2 | 2,357 | 7,000 |
| humidity | 3,482 | 5,000 |

**109 of 132 art static meshes** were in this state — every Interchange-imported
asset, including all 20 crew colonists and all the crops. HabitatDome was
rendering at 22 % of its triangles. Only the older FBX-imported models
(`battery`, `ice`, `lander2`, `extractor2`, `stockpile`) were unaffected, because
that path never set the flag.

This is a strong candidate for a large part of the "models look weirdly
unfinished" verdict: much of the library was literally drawing a low-poly proxy.

```bash
# audit  (writes: how many meshes have Nanite enabled)
RH_REPORT=/tmp/audit.txt UnrealEditor-Cmd <proj> -run=pythonscript \
  -script=$PWD/scripts/unreal/rh_audit_nanite.py -unattended -nosound -stdout

# fix    (disables Nanite on every art static mesh that has it, saves in place)
RH_REPORT=/tmp/fix.txt   UnrealEditor-Cmd <proj> -run=pythonscript \
  -script=$PWD/scripts/unreal/rh_fix_nanite.py  -unattended -nosound -stdout
```

**Any new GLB import re-introduces the flag**, so the order for any asset pass is:

1. finish the mesh (`scripts/blender/rh_finish.py`)
2. import (`-run=ImportAssets … -replaceexisting`)
3. **`rh_fix_nanite.py`** — restores full geometry
4. `-run=RHArtWire -wire` — restores the `MI_<name>` material assignment, which
   the import also resets

Verify with the geometry receipt `RHArtWire` logs per mesh: a welded, un-Nanited
asset reads ~0.8–1.3 verts/tri at its true triangle count. An unwelded one reads
3.00 verts/tri; a Nanite-fallback one reads far fewer triangles than its source.
