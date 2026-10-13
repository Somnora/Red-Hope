"""rh_hs - hard-surface kit for code-built Red Hope assets (Blender 5.2, headless).

Why this exists: the 2026-10-13 model triage split the buildings cleanly. The
ones the director likes (Lander, Habitat, Pylon, SolarArray) are clean primitive
silhouettes; the ones that read "weird" (BatteryBank, ComputeModule, AirFilter,
HumidityRegulator) are image-to-3D output - melted detail, shredded thin parts,
puffy panels. Hard-surface machinery is exactly what can be BUILT instead of
generated: every edge exact, every thin fin solid, every trim and light its
own material, so the masks the game needs come out perfect by construction.

The kit, in the order an asset script uses it:
  Kit(accent_hex)          the art-bible palette as role materials (RHCanon
                           neutrals + the building's TintFor accent as TRIM)
  box / bezel / pipe /     bmesh primitives: bevelled boxes with optional
  cyl / bolt / quad        chamfers, recessed rounded bezels, filleted pipes,
                           hex bolts, emissive cells
  finish(objs, name)       join, weighted normals, smart-UV + pack
  bake_and_export(...)     Cycles bakes (albedo, AO, edge, dust, metal, rough,
                           emissive) -> composed BaseColor + glTF MR + emissive
                           mask PNGs -> one-mesh, one-material GLB that
                           scripts/unreal/rh_import_model2.py ingests as-is

Rules this encodes (each is a past incident in this repo):
  - never shade_auto_smooth: the glTF exporter drops its normals. Normals are
    baked into the mesh (sharp-by-angle + an APPLIED Weighted Normal modifier).
  - accent is trim, not body (art bible: 5-15 % of the surface). It is painted
    into BaseColor here, and the MI's AccentAmount goes to 0 at import -
    M_RH_Master has no accent mask, so a non-zero AccentAmount tints the whole
    hull (the "toy" look on the 2026-10-13 triage page).
  - the GLB carries exactly two images named *_BaseColor and
    *_MetallicRoughness, so the import script identifies them by name.
  - glTF MR packing: G = roughness, B = metallic (what M_RH_Master reads).
"""
import math
import os

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector


def log(msg):
    print("[rh_hs] %s" % msg, flush=True)


def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_lin(h):
    h = h.lstrip("#")
    return tuple(srgb_to_lin(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


def lin_to_srgb_np(a):
    a = np.clip(a, 0.0, 1.0)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1.0 / 2.4) - 0.055)


# Art bible section 2 (RHCanon neutrals) plus the materials machinery needs.
# paint = gets edge chipping and rust; emit = lights up through EmissiveMask;
# dust = how much Mars dust the surface holds (0 on glass and lit cells, which
# read as wiped clean - a dusty charge window stops reading as a display).
# "metal" is a LIGHT, mid-metallic alloy on purpose: fully metallic surfaces
# under Mars' dim ambient reflect mostly darkness and read near-black.
ROLES = {
    "hull":     dict(albedo="#BFBCB3", metal=0.00, rough=0.55, paint=1, dust=1.0),  # BoneWhite
    "slate":    dict(albedo="#595D65", metal=0.30, rough=0.60, paint=1, dust=1.0),  # DarkSlate
    "metal":    dict(albedo="#B9BCBF", metal=0.55, rough=0.42, dust=0.5),          # alloy
    "pipe":     dict(albedo="#6E7277", metal=0.75, rough=0.40, dust=0.7),          # gunmetal
    "glass":    dict(albedo="#111518", metal=0.00, rough=0.08, dust=0.0),
    "cell_off": dict(albedo="#1D2725", metal=0.00, rough=0.30, dust=0.0),
    "cell_on":  dict(albedo="#A6F7EA", metal=0.00, rough=0.25, emit=1, dust=0.0),
    "rubber":   dict(albedo="#1C1D1F", metal=0.00, rough=0.85, dust=1.0),
    "hazard":   dict(albedo="#EDCE3F", metal=0.00, rough=0.50, paint=1, dust=0.8),  # HazYellow
    "rust":     dict(albedo="#B86F4B", metal=0.10, rough=0.75, dust=1.0),          # RustBeam
}
DUST_LIN = hex_lin("#B07A55")      # Mars dust, settles on tops and low surfaces
BARE_LIN = (0.40, 0.40, 0.41)      # what chipped paint reveals
RUST_LIN = hex_lin("#8E4A2C")      # rust streak (darker than RustBeam paint)


class Kit:
    def __init__(self, accent_hex):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.roles = dict(ROLES)
        self.roles["accent"] = dict(albedo=accent_hex, metal=0.15, rough=0.45, paint=1, dust=0.5)
        self.mats = {}
        for name, spec in self.roles.items():
            self.mats[name] = self._preview_material(name, spec)

    # ------------------------------------------------------------ materials
    def _preview_material(self, name, spec):
        m = bpy.data.materials.new("RH_%s" % name)
        m.use_nodes = True
        m["rh_role"] = name
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*hex_lin(spec["albedo"]), 1.0)
        b.inputs["Metallic"].default_value = spec["metal"]
        b.inputs["Roughness"].default_value = spec["rough"]
        if spec.get("emit"):
            b.inputs["Emission Color"].default_value = (*hex_lin("#29FFDB"), 1.0)
            b.inputs["Emission Strength"].default_value = 4.0
        return m

    def _obj(self, name, bm, roles):
        me = bpy.data.meshes.new(name)
        bm.to_mesh(me)
        bm.free()
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        for r in roles:
            ob.data.materials.append(self.mats[r])
        return ob

    # ----------------------------------------------------------- primitives
    def box(self, name, lo, hi, role, bevel=0.01, seg=2, chamfers=()):
        """Axis-aligned box lo..hi. chamfers: [(predicate(edge_dir, edge_mid), offset)]
        applied (1 segment) before the all-edge bevel."""
        lo, hi = Vector(lo), Vector(hi)
        size, ctr = hi - lo, (lo + hi) / 2
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co = Vector((v.co.x * size.x, v.co.y * size.y, v.co.z * size.z)) + ctr
        for pred, off in chamfers:
            bm.edges.ensure_lookup_table()
            sel = []
            for e in bm.edges:
                d = (e.verts[1].co - e.verts[0].co)
                if d.length < 1e-9:
                    continue
                if pred(d.normalized(), (e.verts[0].co + e.verts[1].co) / 2):
                    sel.append(e)
            if sel:
                bmesh.ops.bevel(bm, geom=sel, offset=off, offset_type="OFFSET",
                                segments=1, profile=0.5, affect="EDGES", clamp_overlap=True)
        if bevel > 0:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                            segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
        return self._obj(name, bm, [role])

    def bezel(self, name, center, w, h, proud, radius, rim, recess, rim_role, inner_role,
              facing=-1):
        """Rounded-rect frame standing `proud` metres off a face whose outward normal
        is facing*Y, with a recessed window `recess` deep. Returns (object, window
        plane y) so cells can sit just in front of the window."""
        cx, cy, cz = center
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co = Vector((v.co.x * w, v.co.y * proud, v.co.z * h))
        # round the 4 edges that run along Y (the depth axis)
        ys = [e for e in bm.edges
              if abs((e.verts[1].co - e.verts[0].co).normalized().y) > 0.99]
        bmesh.ops.bevel(bm, geom=ys, offset=radius, offset_type="OFFSET", segments=4,
                        profile=0.5, affect="EDGES", clamp_overlap=True)
        bm.faces.ensure_lookup_table()
        front = max(bm.faces, key=lambda f: (f.normal.y * facing, f.calc_area()))
        res = bmesh.ops.inset_individual(bm, faces=[front], thickness=rim, depth=0.0)
        ext = bmesh.ops.extrude_discrete_faces(bm, faces=[front])
        inner = ext["faces"][0]
        for v in inner.verts:
            v.co.y -= facing * recess
        # walls created by the extrude + the inner face become the window role
        walls = {f for v in inner.verts for f in v.link_faces}
        for f in bm.faces:
            f.material_index = 1 if f in walls else 0
        for v in bm.verts:
            v.co += Vector((cx, cy + facing * proud / 2.0, cz))
        window_y = cy + facing * (proud - recess)
        return self._obj(name, bm, [rim_role, inner_role]), window_y

    def quad(self, name, center, w, h, role, facing=-1):
        """Single quad facing facing*Y (cells, LED dashes)."""
        cx, cy, cz = center
        bm = bmesh.new()
        vs = [bm.verts.new((cx + dx * w / 2, cy, cz + dz * h / 2))
              for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        f = bm.faces.new(vs if facing < 0 else list(reversed(vs)))
        f.normal_update()
        return self._obj(name, bm, [role])

    def cyl(self, name, p0, p1, r, role, sides=12, caps=True):
        p0, p1 = Vector(p0), Vector(p1)
        axis = p1 - p0
        bm = bmesh.new()
        bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=False, segments=sides,
                              radius1=r, radius2=r, depth=axis.length)
        rot = Vector((0, 0, 1)).rotation_difference(axis.normalized()).to_matrix().to_4x4()
        bmesh.ops.transform(bm, matrix=Matrix.Translation((p0 + p1) / 2) @ rot, verts=bm.verts)
        return self._obj(name, bm, [role])

    def bolt(self, name, at, normal, role="metal", r=0.014, h=0.008):
        n = Vector(normal).normalized()
        return self.cyl(name, Vector(at), Vector(at) + n * h, r, role, sides=6)

    def pipe(self, name, pts, r, role, bend=0.1, sides=12, samples=6):
        path = _fillet([Vector(p) for p in pts], bend, samples)
        cu = bpy.data.curves.new(name + "_c", "CURVE")
        cu.dimensions = "3D"
        sp = cu.splines.new("POLY")
        sp.points.add(len(path) - 1)
        for pt, P in zip(sp.points, path):
            pt.co = (P.x, P.y, P.z, 1.0)
        cu.bevel_mode = "ROUND"
        cu.bevel_depth = r
        cu.bevel_resolution = max(0, sides // 4 - 1)
        cu.use_fill_caps = True
        tmp = bpy.data.objects.new(name + "_tmp", cu)
        bpy.context.scene.collection.objects.link(tmp)
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
        bpy.data.objects.remove(tmp)
        bpy.data.curves.remove(cu)
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        me.materials.clear()
        me.materials.append(self.mats[role])
        return ob

    # --------------------------------------------------------------- finish
    def finish(self, objs, name, sharp_deg=35.0, island_margin=0.002, pack_margin=0.003):
        """Join, shade (sharp-by-angle + applied weighted normals), unwrap."""
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        ob.name = name
        ob.data.name = name
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        # one material slot per role, deduplicated after the join
        _dedupe_material_slots(ob)
        me = ob.data
        for p in me.polygons:
            p.use_smooth = True
        me.set_sharp_from_angle(angle=math.radians(sharp_deg))
        wn = ob.modifiers.new("WN", "WEIGHTED_NORMAL")
        wn.keep_sharp = True
        wn.mode = "FACE_AREA"
        bpy.ops.object.modifier_apply(modifier=wn.name)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=island_margin,
                                 area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
        bpy.ops.uv.pack_islands(rotate=True, margin=pack_margin)
        bpy.ops.object.mode_set(mode="OBJECT")
        tris = sum(len(p.vertices) - 2 for p in me.polygons)
        dims = [round(d, 3) for d in ob.dimensions]
        log("finished %s: %d tris, %d verts, dims %s m, %d material roles"
            % (name, tris, len(me.vertices), dims, len(me.materials)))
        return ob

    # ------------------------------------------------------------- facing
    @staticmethod
    def face_camera(ob):
        """Turn a model built with its front at -Y so the front faces -X.

        Measured 2026-10-13 (rh_capture.sh, RH.Cam yaw 0): the strategy camera's
        default view meets the model's Blender -X face (the BatteryBank's two bar
        faces carry different charge levels; the in-game frame matched the face
        that had been built at +Y, which a +90 deg turn had moved to -X). So the
        identity face belongs at -X; build it at -Y as usual and call this.
        """
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        ob.rotation_euler.z = math.radians(-90.0)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        return ob

    # ----------------------------------------------------------------- bake
    def bake_and_export(self, ob, name, outdir, size=2048, wear=0.35, dust=0.30,
                        ao_samples=64, ao_distance=0.35):
        os.makedirs(outdir, exist_ok=True)
        sc = bpy.context.scene
        sc.render.engine = "CYCLES"
        sc.cycles.device = "CPU"
        sc.render.bake.margin = 8
        sc.render.bake.use_clear = True
        if not sc.world:
            sc.world = bpy.data.worlds.new("bakeworld")
        sc.world.light_settings.distance = ao_distance
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.context.view_layer.objects.active = ob
        originals = [s.material for s in ob.material_slots]
        roles = [m["rh_role"] for m in originals]

        def bake(pass_name, build, bake_type="EMIT", samples=1):
            img = bpy.data.images.new("%s_%s" % (name, pass_name), size, size,
                                      alpha=False, float_buffer=True)
            img.colorspace_settings.name = "Non-Color"
            sc.cycles.samples = samples
            for i, role in enumerate(roles):
                ob.material_slots[i].material = _bake_material(
                    "%s_%s_%s" % (name, pass_name, role), build(role, self.roles[role]), img)
            bpy.ops.object.bake(type=bake_type)
            a = np.empty(size * size * 4, dtype=np.float32)
            img.pixels.foreach_get(a)
            log("baked %-7s (%s, %d spp)" % (pass_name, bake_type, samples))
            return a.reshape(size, size, 4)

        albedo = bake("albedo", lambda r, s: ("albedo", hex_lin(s["albedo"]), not s.get("emit")))
        rough = bake("rough", lambda r, s: ("value", s["rough"], True))
        metal = bake("metal", lambda r, s: ("value", s["metal"], False))
        emit = bake("emit", lambda r, s: ("value", float(s.get("emit", 0)), False))
        paint = bake("paint", lambda r, s: ("value", float(s.get("paint", 0)), False))
        dustw = bake("dustw", lambda r, s: ("value", float(s.get("dust", 1.0)), False))
        edge = bake("edge", lambda r, s: ("edge", None, False), samples=16)
        dustm = bake("dust", lambda r, s: ("dust", None, False), samples=1)
        ao = bake("ao", lambda r, s: ("value", 1.0, False), bake_type="AO", samples=ao_samples)

        for i, m in enumerate(originals):
            ob.material_slots[i].material = m

        # ------------------------------------------------------- compose
        A = albedo[..., :3]
        E = emit[..., 0:1]
        P = paint[..., 0:1]
        EDG = np.clip((edge[..., 0:1] - 0.15) / 0.5, 0, 1)
        AO = ao[..., 0:1]
        D = dustm[..., 0:1]
        N1 = dustm[..., 1:2]                       # object-space noise A
        N2 = dustm[..., 2:3]                       # object-space noise B
        chip = P * EDG * np.clip(wear * 2.2 * (0.6 + 0.8 * N1), 0, 1)
        A = A * (1 - chip) + np.array(BARE_LIN) * chip
        rust = P * np.clip((1 - AO) * 1.6 - 0.25, 0, 1) * np.clip((N2 - 0.45) * 3.0, 0, 1) * wear
        A = A * (1 - rust) + np.array(RUST_LIN) * rust
        grime = 1.0 - 0.55 * (1.0 - AO)
        A = A * (grime * (1 - E) + E)
        dmask = np.clip(D * dustw[..., 0:1] * dust * (0.7 + 0.6 * N1), 0, 0.75) * (1 - E)
        A = A * (1 - dmask) + np.array(DUST_LIN) * dmask
        base = lin_to_srgb_np(A)
        R_ = np.clip(rough[..., 0:1] + 0.18 * dmask + 0.10 * rust - 0.06 * chip, 0.04, 1.0)
        M_ = np.clip(metal[..., 0:1] * (1 - dmask) + 0.55 * chip * (1 - metal[..., 0:1]), 0, 1)
        mr = np.concatenate([AO, R_, M_], axis=2)

        files = {
            "base": _save_png(base, os.path.join(outdir, "%s_BaseColor.png" % name)),
            "mr": _save_png(mr, os.path.join(outdir, "%s_MetallicRoughness.png" % name)),
            "emissive": _save_png(np.repeat(E, 3, axis=2),
                                  os.path.join(outdir, "%s_EmissiveMask.png" % name)),
            "normal": _save_png(np.tile(np.array([0.5, 0.5, 1.0]), (8, 8, 1)),
                                os.path.join(outdir, "%s_flat_normal.png" % name)),
        }
        cover = float((E > 0.5).mean())
        log("emissive coverage %.2f %% of texels" % (100 * cover))

        # ------------------------------------------------- export GLB
        final = bpy.data.materials.new(name)
        final.use_nodes = True
        nt = final.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        ib = nt.nodes.new("ShaderNodeTexImage")
        ib.image = bpy.data.images.load(files["base"])
        ib.image.name = "%s_BaseColor" % name
        ib.image.colorspace_settings.name = "sRGB"
        im = nt.nodes.new("ShaderNodeTexImage")
        im.image = bpy.data.images.load(files["mr"])
        im.image.name = "%s_MetallicRoughness" % name
        im.image.colorspace_settings.name = "Non-Color"
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(bsdf.inputs["Base Color"], ib.outputs["Color"])
        nt.links.new(sep.inputs["Color"], im.outputs["Color"])
        nt.links.new(bsdf.inputs["Roughness"], sep.outputs["Green"])
        nt.links.new(bsdf.inputs["Metallic"], sep.outputs["Blue"])
        ob.data.materials.clear()
        ob.data.materials.append(final)
        glb = os.path.join(outdir, "%s.glb" % name)
        bpy.ops.object.select_all(action="DESELECT")
        ob.select_set(True)
        bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", use_selection=True,
                                  export_apply=True)
        files["glb"] = glb
        log("exported %s" % glb)
        return files


# ------------------------------------------------------------------ helpers
def _fillet(pts, r, n):
    """Polyline with every interior corner replaced by a quadratic-Bezier arc."""
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        a, p, b = pts[i - 1], pts[i], pts[i + 1]
        d1, d2 = (p - a), (b - p)
        t = min(r, d1.length / 2, d2.length / 2)
        s, e = p - d1.normalized() * t, p + d2.normalized() * t
        for k in range(n + 1):
            u = k / n
            out.append((1 - u) ** 2 * s + 2 * (1 - u) * u * p + u ** 2 * e)
    out.append(pts[-1])
    return out


def _dedupe_material_slots(ob):
    me = ob.data
    first = {}
    remap = {}
    for i, m in enumerate(me.materials):
        key = m.name if m else None
        if key not in first:
            first[key] = len(first)
        remap[i] = first[key]
    keep = [None] * len(first)
    for i, m in enumerate(me.materials):
        keep[remap[i]] = m
    idx = np.empty(len(me.polygons), dtype=np.int32)
    me.polygons.foreach_get("material_index", idx)
    idx = np.vectorize(remap.get)(idx).astype(np.int32) if len(idx) else idx
    me.materials.clear()
    for m in keep:
        me.materials.append(m)
    me.polygons.foreach_set("material_index", idx)


def _bake_material(mname, spec, img):
    """Emission-only material for one bake pass. spec = (kind, value, vary)."""
    kind, value, vary = spec
    m = bpy.data.materials.new(mname)
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(out.inputs["Surface"], em.outputs["Emission"])
    tc = nt.nodes.new("ShaderNodeTexCoord")

    def noise(scale, detail=3.0):
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = scale
        nz.inputs["Detail"].default_value = detail
        nt.links.new(nz.inputs["Vector"], tc.outputs["Object"])
        return nz.outputs["Fac"]

    if kind == "albedo":
        col = nt.nodes.new("ShaderNodeRGB")
        col.outputs[0].default_value = (*value, 1.0)
        if vary:  # +-5 % panel-scale value variation, +-2 % grain
            mr = nt.nodes.new("ShaderNodeMapRange")
            mr.inputs["To Min"].default_value = 0.95
            mr.inputs["To Max"].default_value = 1.05
            nt.links.new(mr.inputs["Value"], noise(1.6))
            mul = nt.nodes.new("ShaderNodeMix")
            mul.data_type = "RGBA"
            mul.blend_type = "MULTIPLY"
            mul.inputs["Factor"].default_value = 1.0
            nt.links.new(mul.inputs["A"], col.outputs[0])
            gray = nt.nodes.new("ShaderNodeCombineColor")
            for ch in ("Red", "Green", "Blue"):
                nt.links.new(gray.inputs[ch], mr.outputs["Result"])
            nt.links.new(mul.inputs["B"], gray.outputs["Color"])
            nt.links.new(em.inputs["Color"], mul.outputs["Result"])
        else:
            nt.links.new(em.inputs["Color"], col.outputs[0])
    elif kind == "value":
        em.inputs["Color"].default_value = (value, value, value, 1.0)
        if vary:  # roughness breakup
            mr = nt.nodes.new("ShaderNodeMapRange")
            mr.inputs["To Min"].default_value = value - 0.06
            mr.inputs["To Max"].default_value = value + 0.06
            nt.links.new(mr.inputs["Value"], noise(3.0))
            gray = nt.nodes.new("ShaderNodeCombineColor")
            for ch in ("Red", "Green", "Blue"):
                nt.links.new(gray.inputs[ch], mr.outputs["Result"])
            nt.links.new(em.inputs["Color"], gray.outputs["Color"])
    elif kind == "edge":
        # Edge mask from the Bevel shader: where the rounded normal departs from
        # the true normal there is an edge, whatever the topology. Pointiness
        # (rh_bake.py's method) smears across big flat faces on low-poly boxes.
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        bev = nt.nodes.new("ShaderNodeBevel")
        bev.samples = 8
        bev.inputs["Radius"].default_value = 0.012
        dot = nt.nodes.new("ShaderNodeVectorMath")
        dot.operation = "DOT_PRODUCT"
        nt.links.new(dot.inputs[0], bev.outputs["Normal"])
        nt.links.new(dot.inputs[1], geo.outputs["True Normal"])
        inv = nt.nodes.new("ShaderNodeMath")
        inv.operation = "SUBTRACT"
        inv.inputs[0].default_value = 1.0
        nt.links.new(inv.inputs[1], dot.outputs["Value"])
        amp = nt.nodes.new("ShaderNodeMath")
        amp.operation = "MULTIPLY"
        amp.use_clamp = True
        amp.inputs[1].default_value = 6.0
        nt.links.new(amp.inputs[0], inv.outputs["Value"])
        nt.links.new(em.inputs["Color"], amp.outputs["Value"])
    elif kind == "dust":
        # R = dust mask (upward faces + the bottom 40 cm), G/B = two object-space
        # noises the compositor uses to break up chipping and rust.
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        sep_n = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(sep_n.inputs[0], geo.outputs["Normal"])
        up = nt.nodes.new("ShaderNodeMapRange")
        up.inputs["From Min"].default_value = 0.55
        up.inputs["From Max"].default_value = 0.95
        nt.links.new(up.inputs["Value"], sep_n.outputs["Z"])
        sep_p = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(sep_p.inputs[0], geo.outputs["Position"])
        low = nt.nodes.new("ShaderNodeMapRange")
        low.inputs["From Min"].default_value = 0.28
        low.inputs["From Max"].default_value = 0.0
        nt.links.new(low.inputs["Value"], sep_p.outputs["Z"])
        mx = nt.nodes.new("ShaderNodeMath")
        mx.operation = "MAXIMUM"
        nt.links.new(mx.inputs[0], up.outputs["Result"])
        nt.links.new(mx.inputs[1], low.outputs["Result"])
        comb = nt.nodes.new("ShaderNodeCombineColor")
        nt.links.new(comb.inputs["Red"], mx.outputs["Value"])
        nt.links.new(comb.inputs["Green"], noise(4.0, 4.0))
        nt.links.new(comb.inputs["Blue"], noise(7.0, 5.0))
        nt.links.new(em.inputs["Color"], comb.outputs["Color"])
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    for n in nt.nodes:
        n.select = False
    tex.select = True
    nt.nodes.active = tex
    return m


def _save_png(arr, path):
    """arr: HxWx3 floats in 0..1 (already display-encoded for colour maps)."""
    h, w = arr.shape[:2]
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=True)
    img.colorspace_settings.name = "Non-Color"   # write values verbatim
    rgba = np.concatenate([np.clip(arr, 0, 1), np.ones((h, w, 1))], axis=2).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return path
