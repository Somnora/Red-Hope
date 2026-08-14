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
