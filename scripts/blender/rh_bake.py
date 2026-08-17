"""rh_bake — bake AO and curvature for one finished mesh, and export its albedo.

  blender --background --python scripts/blender/rh_bake.py -- \
      --in <finished.glb> --outdir <dir> [--size 2048] [--samples 32]

Writes <outdir>/<name>_albedo.png, _ao.png, _curv.png. Compositing into a final
BaseColor happens outside Blender (rh_compose.py) so it can be re-tuned in
seconds without re-baking.

Why no normal map: there is no high-poly source anywhere in this pipeline - the
meshes ARE the generator's output - so a baked normal has nothing to capture that
the geometry does not already carry. AO (contact shadowing) and curvature
(edge wear) add depth that the flat generator albedo genuinely lacks.

Bakes onto the mesh's EXISTING UV layout, so the result drops straight onto the
current texture asset with no re-wiring.
"""

import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_lib as R  # noqa: E402


def parse(argv):
    o = {"in": None, "outdir": None, "size": 2048, "samples": 32}
    i = 0
    while i < len(argv):
        if argv[i] == "--in":
            o["in"] = argv[i + 1]; i += 2
        elif argv[i] == "--outdir":
            o["outdir"] = argv[i + 1]; i += 2
        elif argv[i] == "--size":
            o["size"] = int(argv[i + 1]); i += 2
        elif argv[i] == "--samples":
            o["samples"] = int(argv[i + 1]); i += 2
        else:
            i += 1
    if not o["in"] or not o["outdir"]:
        raise SystemExit("[rh] usage: --in <glb> --outdir <dir> [--size N] [--samples N]")
    return o


def enable_cycles(samples):
    import addon_utils
    addon_utils.enable("cycles", default_set=True, persistent=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.render.bake.margin = 8
    sc.render.bake.use_clear = True
    try:
        sc.render.bake.use_selected_to_active = False
    except Exception:
        pass


def save_albedo(ob, out):
    """Write the mesh's existing baseColor image straight out of the GLB."""
    for slot in ob.material_slots:
        mat = slot.material
        if not mat or not mat.use_nodes:
            continue
        for n in mat.node_tree.nodes:
            if n.type == "TEX_IMAGE" and n.image:
                img = n.image
                # the metallic/roughness map is the other one; pick the one wired
                # to Base Color if we can tell, else the first non-MR image
                if "metallic" in img.name.lower() or "roughness" in img.name.lower():
                    continue
                img.filepath_raw = out
                img.file_format = "PNG"
                img.save()
                R.log(f"albedo: {img.name} {img.size[0]}x{img.size[1]} -> {os.path.basename(out)}")
                return True
    R.log("albedo: NONE FOUND (mesh has no base-colour image)")
    return False


def bake_to(ob, image, bake_type):
    """Every material on the object needs the target image as its active node."""
    for slot in ob.material_slots:
        mat = slot.material
        if not mat:
            continue
        mat.use_nodes = True
        nt = mat.node_tree
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = image
        nt.nodes.active = tex
        for n in nt.nodes:
            n.select = False
        tex.select = True
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.bake(type=bake_type)


def curvature_material(ob):
    """Replace materials with an emission of remapped Pointiness.

    Pointiness sits around 0.5 on flat surfaces, >0.5 on convex edges and <0.5
    in cavities. Expanding around 0.5 turns it into a usable wear mask.
    """
    mat = bpy.data.materials.new("RH_Curv")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sub = nt.nodes.new("ShaderNodeMath"); sub.operation = "SUBTRACT"
    mul = nt.nodes.new("ShaderNodeMath"); mul.operation = "MULTIPLY"
    add = nt.nodes.new("ShaderNodeMath"); add.operation = "ADD"
    clamp = nt.nodes.new("ShaderNodeClamp")
    em = nt.nodes.new("ShaderNodeEmission")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(sub.inputs[0], geo.outputs["Pointiness"])
    sub.inputs[1].default_value = 0.5
    nt.links.new(mul.inputs[0], sub.outputs[0])
    mul.inputs[1].default_value = 6.0          # expand the narrow pointiness band
    nt.links.new(add.inputs[0], mul.outputs[0])
    add.inputs[1].default_value = 0.5          # recentre on mid-grey
    nt.links.new(clamp.inputs["Value"], add.outputs[0])
    nt.links.new(em.inputs["Color"], clamp.outputs[0])
    nt.links.new(out.inputs["Surface"], em.outputs["Emission"])
    ob.data.materials.clear()
    ob.data.materials.append(mat)


def main():
    o = parse(R.argv_after_ddash())
    name = os.path.splitext(os.path.basename(o["in"]))[0]
    os.makedirs(o["outdir"], exist_ok=True)
    R.log(f"=== baking {name} @ {o['size']} ===")

    ob = R.load_mesh(o["in"])
    # bake onto the ORIGINAL UVs - the layout the shipped texture already uses
    uv = ob.data.uv_layers[0]
    ob.data.uv_layers.active = uv
    uv.active_render = True
    R.log(f"uv layout: '{uv.name}'")

    save_albedo(ob, os.path.join(o["outdir"], f"{name}_albedo.png"))
    enable_cycles(o["samples"])

    ao = bpy.data.images.new(f"{name}_ao", o["size"], o["size"], alpha=False)
    bake_to(ob, ao, "AO")
    ao.filepath_raw = os.path.join(o["outdir"], f"{name}_ao.png")
    ao.file_format = "PNG"
    ao.save()
    R.log(f"ao -> {name}_ao.png")

    curvature_material(ob)
    cv = bpy.data.images.new(f"{name}_curv", o["size"], o["size"], alpha=False)
    bake_to(ob, cv, "EMIT")
    cv.filepath_raw = os.path.join(o["outdir"], f"{name}_curv.png")
    cv.file_format = "PNG"
    cv.save()
    R.log(f"curvature -> {name}_curv.png")

    print("RH_BAKE_OK", name, flush=True)


main()
