"""Import MPFB-built crew walkers (scripts/blender/rh_crew_build.py) over the old ones.

  RH_FACES=cmdr_vale,vet_kowalski RH_SRC=<abs Martians/gen/crew_mpfb> RH_REPORT=<txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_crew.py -unattended -nosound -stdout

Per face, in one commandlet run:
  1. M_RH_CharacterMasked exists (made once by duplicating M_RH_Character and
     switching it to BLEND_MASKED + two-sided with BaseTex.A -> OpacityMask):
     the new walkers' hair and brows are alpha cards, and M_RH_Character is
     opaque. Compile-free; the recompile's error list is reported.
  2. The OLD /Game/RedHope/Art/CrewAnim/RH_Walker_<face>/ folder is DELETED
     first (it is in git). A reimport-in-place would keep the old 14-bone
     skeleton - the 08-18 "slenderman" lesson - and the new walker is a
     53-bone MPFB game_engine rig, so the skeleton has to be new.
  3. The GLB imports into /Game/RedHope/Art/CrewAnim, which Interchange lays out
     as RH_Walker_<face>/{SkeletalMeshes,Materials,Textures}/ - the paths
     WalkerMeshPath()/WalkerAnimPath() load. Clips must come out named
     RH_Walker_<face><Clip>; any other naming is renamed to that and reported.
  4. MI_RH_Walker_<face> on M_RH_CharacterMasked: BaseTex = the RGBA atlas,
     MRTex = the MR atlas (TC_MASKS, sRGB off), UseMRTex 1, and - when the
     walker carries one (2026-10-14 face pass) - NormTex = the face normal map
     (TC_NORMALMAP, sRGB off, green flipped: Blender bakes OpenGL-style +Y, UE
     wants DirectX), UseNormTex 1; every material slot of the skeletal mesh
     points at it (the array is rebuilt - UE Python hands back copies).
Verify afterwards from a fresh -game boot (rh_capture.sh), not this process.
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
FACES = [f for f in os.environ["RH_FACES"].split(",") if f]
SRC = os.environ["RH_SRC"]
REPORT = os.environ.get("RH_REPORT", "/tmp/rh_import_crew.txt")
CLIPS = ["Walk", "Idle", "WorkBench", "WorkLab", "Dig", "Haul", "Charge", "Plant", "Repair", "Operate",
         "Sit", "SitWork", "SitEat", "Lie", "Talk"]  # alive pass 2 (2026-10-16)
MASTER = "/Game/RedHope/Art/M_RH_Character"
MASKED = "/Game/RedHope/Art/M_RH_CharacterMasked"
log = []


def masked_master():
    mat = unreal.load_asset(MASKED)
    if mat:
        log.append("M_RH_CharacterMasked: exists")
        return mat
    mat = EAL.duplicate_asset(MASTER, MASKED)
    if not mat:
        log.append("** could not duplicate M_RH_Character **")
        return None
    base = None
    for e in MEL.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D) and \
                str(e.get_editor_property("parameter_name")) == "BaseTex":
            base = e
    if base is None:
        log.append("** BaseTex sampler not found in the duplicate **")
        return None
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    MEL.connect_material_property(base, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    errs = MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log.append("M_RH_CharacterMasked: created (masked, two-sided, BaseTex.A -> OpacityMask), recompile: %s" % (errs or "ok"))
    return mat


def import_face(face, master):
    name = "RH_Walker_" + face
    folder = "/Game/RedHope/Art/CrewAnim/" + name
    glb = os.path.join(SRC, face, name + ".glb")
    if not os.path.isfile(glb):
        log.append("** %s: no GLB at %s **" % (face, glb))
        return
    if EAL.does_directory_exist(folder):
        EAL.delete_directory(folder)
        log.append("%s: deleted old %s" % (face, folder))
    task = unreal.AssetImportTask()
    task.filename = glb
    task.destination_path = "/Game/RedHope/Art/CrewAnim"
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    mesh = None
    anims, texs = [], []
    for a in EAL.list_assets(folder, recursive=True):
        o = unreal.load_asset(a)
        if isinstance(o, unreal.SkeletalMesh):
            mesh = o
        elif isinstance(o, unreal.AnimSequence):
            anims.append(o)
        elif isinstance(o, unreal.Texture2D):
            texs.append(o)
    if not mesh:
        log.append("** %s: no skeletal mesh imported **" % face)
        return
    mesh_path = mesh.get_path_name()
    want = "%s/SkeletalMeshes/%s.%s" % (folder, name, name)
    log.append("%s: mesh %s %s" % (face, mesh_path, "(contract path ok)" if mesh_path == want else "** NOT AT %s **" % want))
    skel = mesh.get_editor_property("skeleton")
    log.append("%s: skeleton %s" % (face, skel.get_name() if skel else None))
    dest = "%s/SkeletalMeshes" % folder
    got = {}
    for an in anims:
        n = an.get_name()
        clip = next((c for c in CLIPS if n == name + c or n.endswith(c) or n.endswith("_" + c)), None)
        if clip is None:
            log.append("%s: unmatched anim %s" % (face, n))
            continue
        target = "%s/%s%s" % (dest, name, clip)
        if an.get_path_name() != "%s.%s%s" % (target, name, clip):
            EAL.rename_asset(an.get_path_name(), target)
            log.append("%s: renamed %s -> %s%s" % (face, n, name, clip))
        got[clip] = True
    missing = [c for c in CLIPS if c not in got]
    log.append("%s: clips %d/%d%s" % (face, len(got), len(CLIPS), (" missing %s" % missing) if missing else ""))
    base = next((t for t in texs if "BaseColor" in t.get_name()), None)
    mr = next((t for t in texs if "MetallicRoughness" in t.get_name() or "metallic" in t.get_name().lower()), None)
    if mr:
        mr.set_editor_property("srgb", False)
        mr.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_loaded_asset(mr)
    nrm = next((t for t in texs if t.get_name().endswith("_Normal")), None)
    if nrm:
        was = (nrm.get_editor_property("compression_settings"), nrm.get_editor_property("srgb"),
               nrm.get_editor_property("flip_green_channel"))
        nrm.set_editor_property("srgb", False)
        nrm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        nrm.set_editor_property("flip_green_channel", True)
        EAL.save_loaded_asset(nrm)
        log.append("%s: normal %s (imported as %s, srgb %s, flip %s -> TC_NORMALMAP, srgb off, flip on)"
                   % (face, nrm.get_name(), was[0], was[1], was[2]))
    mi_name = "MI_" + name
    mi = AT.create_asset(mi_name, dest, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", master)
    if base:
        MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", base)
    if mr:
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0)
    if nrm:
        MEL.set_material_instance_texture_parameter_value(mi, "NormTex", nrm)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", 1.0)
    EAL.save_loaded_asset(mi)
    # read back, don't trust the setters: they return False even when the value
    # lands (2026-10-14 - a fresh-process readback showed all 20 set)
    got_n = MEL.get_material_instance_texture_parameter_value(mi, "NormTex")
    log.append("%s: readback NormTex=%s UseNormTex=%s" % (
        face, got_n.get_name() if got_n else None, MEL.get_material_instance_scalar_parameter_value(mi, "UseNormTex")))
    rebuilt = []
    for sm in mesh.get_editor_property("materials"):
        sm.set_editor_property("material_interface", mi)
        rebuilt.append(sm)
    mesh.set_editor_property("materials", rebuilt)
    EAL.save_loaded_asset(mesh)
    back = [m.get_editor_property("material_interface").get_name() for m in mesh.get_editor_property("materials")]
    log.append("%s: %s on %s (BaseTex=%s MRTex=%s); slots read back %s" % (
        face, mi_name, master.get_name(), base.get_name() if base else None, mr.get_name() if mr else None, back))


master = masked_master()
if master:
    for f in FACES:
        import_face(f, master)
with open(REPORT, "w") as fh:
    fh.write("\n".join(log) + "\n")
