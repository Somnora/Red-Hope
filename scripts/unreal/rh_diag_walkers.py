"""Diagnose the crew walkers: which material is actually on the slot, its blend
mode and two-sidedness, and whether the albedo carries an alpha channel.

  RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_diag_walkers.py -unattended -nosound -stdout

WHY
---
Director, 2026-08-17: "a lot of the character models still look splotchy,
sometimes you can see through parts of their body."

See-through is almost never a texture problem. The candidates, in order of how
cheap they are to check:

  1. BLEND MODE. Interchange's glTF pipeline reads the GLB's `alphaMode` and
     will happily produce a MASKED or TRANSLUCENT material. If the base colour
     texture then carries a noisy alpha channel, that alpha punches holes in
     the body - which reads as BOTH "splotchy" and "see-through", from one
     cause.
  2. TWO-SIDED off + inverted normals: back faces cull and you see the inside.
  3. Genuine holes in the mesh (open boundary edges).

Each walker has TWO materials on disk - Materials/Material (Interchange's
auto-generated one) and SkeletalMeshes/MI_RH_Walker_<n>. Which one is on the
slot matters: the Props/Props2 false-done came from assuming rather than
reading the slot back.

Reads the slot rather than trusting a name, because UE Python's
static/skeletal material getters return COPIES.
"""
import os

import unreal

OUT = os.environ.get("RH_REPORT", "/tmp/rh_diag_walkers.txt")
ROOT = "/Game/RedHope/Art/CrewAnim"

BLEND = {0: "OPAQUE", 1: "MASKED", 2: "TRANSLUCENT", 3: "ADDITIVE",
         4: "MODULATE", 5: "ALPHACOMPOSITE", 6: "ALPHAHOLDOUT"}

reg = unreal.AssetRegistryHelpers.get_asset_registry()
names = []
for a in reg.get_assets_by_path(ROOT, recursive=True):
    p = str(a.package_name)
    if "/SkeletalMeshes/RH_Walker_" in p and not any(
            p.endswith(s) for s in ("_Skeleton", "_PhysicsAsset")):
        leaf = p.rsplit("/", 1)[1]
        if leaf.startswith("RH_Walker_") and leaf == p.split("/")[-3]:
            names.append(p)
names = sorted(set(names))

log = []
issues = {"masked_or_translucent": [], "one_sided": [], "alpha_in_albedo": [],
          "wrong_material": []}

for pkg in names:
    mesh = unreal.load_asset("%s.%s" % (pkg, pkg.rsplit("/", 1)[1]))
    if not isinstance(mesh, unreal.SkeletalMesh):
        continue
    short = pkg.rsplit("/", 1)[1]
    log.append(short)

    mats = mesh.get_editor_property("materials")
    for i, sm in enumerate(mats):
        mi = sm.material_interface
        if not mi:
            log.append("    slot %d: <none>" % i)
            continue
        path = mi.get_path_name()
        base = mi.get_base_material() if hasattr(mi, "get_base_material") else None
        try:
            blend = mi.get_editor_property("blend_mode")
            blend_n = BLEND.get(int(blend), str(blend))
        except Exception:
            blend_n = "?"
        try:
            two = bool(mi.get_editor_property("two_sided"))
        except Exception:
            two = None
        log.append("    slot %d: %s" % (i, path))
        log.append("             blend=%s  two_sided=%s  parent=%s"
                   % (blend_n, two, base.get_name() if base else "-"))
        if blend_n in ("MASKED", "TRANSLUCENT"):
            issues["masked_or_translucent"].append("%s slot%d" % (short, i))
        if two is False:
            issues["one_sided"].append("%s slot%d" % (short, i))
        if "/Materials/Material" in path:
            issues["wrong_material"].append("%s slot%d" % (short, i))

    # Does the albedo carry alpha at all? If the material is masked, this is
    # what would be punching the holes.
    stem = short.replace("RH_Walker_", "")
    tex = unreal.load_asset("%s/Textures/%s_textured.%s_textured"
                            % (pkg.rsplit("/", 2)[0], stem, stem))
    if tex:
        try:
            src = tex.get_editor_property("source")
            fmt = str(src.get_format()) if src else "?"
        except Exception:
            fmt = "?"
        comp = str(tex.get_editor_property("compression_settings"))
        log.append("             albedo %dx%d fmt=%s comp=%s"
                   % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                      fmt, comp.split(".")[-1]))
        if "BGRA8" in fmt or "RGBA" in fmt:
            issues["alpha_in_albedo"].append(short)
    else:
        log.append("             albedo <not found>")

log.append("")
log.append("=== SUMMARY over %d walkers ===" % len(names))
for k, v in issues.items():
    log.append("  %-22s %d  %s" % (k, len(v), ", ".join(v[:6]) + (" ..." if len(v) > 6 else "")))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log[-14:]))
