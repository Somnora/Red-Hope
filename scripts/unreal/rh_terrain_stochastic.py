"""Break the regolith repeat at the LOOKUP, not at the final colour. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_terrain_stochastic.py -unattended -nosound -stdout

WHY THIS EXISTS, AND WHY THE LAST ATTEMPT FAILED
------------------------------------------------
`M_MarsSurface` is a correct triplanar that samples `SurfTex` at exactly ONE
scale (TileCm, 900 cm). At the 215 m default camera register that is ~20 repeats
across a frame the ground mostly fills, so the planet reads as wallpaper - the
single most visible surface in the game and the last unfixed root cause from the
2026-08-14 audit.

`rh_terrain_macro.py` multiplied the FINISHED colour by low-frequency noise. It
was reverted because it measured as doing nothing (repeat peak 0.439 -> 0.484,
inside the sun-angle confound). The reason is structural: a smooth multiply does
not change whether the rock at p matches the rock at p + one period. It only
tints both of them the same way. The finding's own conclusion was that the fix
"has to disturb the texture LOOKUP", and that is what this does.

THE FIX
-------
Per triplanar plane, sample SurfTex a SECOND time on a decorrelated UV and blend:

    uv_macro = uv_detail / MacroRatio * (1, -1.13) + (0.37, 0.61)
    mixed    = lerp(detail, macro, MacroBlend)

Three things decorrelate the second tap, all cheap:
  - a different period (MacroRatio, default 5.3 - deliberately not an integer, so
    the two periods do not come back into phase anywhere near screen scale),
  - an axis flip and a slight anisotropy (1, -1.13), which kills streak alignment
    without needing to know which channels the plane's ComponentMask uses,
  - a non-zero offset, so the two taps do not coincide at the world origin.

CONTRAST, AND WHY THE MEAN COMES FROM A MIP
-------------------------------------------
Blending two decorrelated samples costs variance: stddev falls by
sqrt(b^2 + (1-b)^2), i.e. 26% at b=0.35, which reads as a flat, hazy ground. So
the blend is followed by a variance restore about the texture mean:

    corrected = Mean + (blended - Mean) * MacroContrast

`Mean` is SurfTex sampled at a very high explicit mip level. The top mip of a
mipped texture IS its average colour, so the GPU hands us the mean for the price
of a 1x1 lookup - no CPU texture read, no hand-entered constant that would rot
the moment the texture is re-authored. If the texture turns out to have no mip
chain, that assumption is false, so the script says so and leaves the restore at
1.0 rather than silently subtracting a full-detail sample.

MacroContrast defaults to exactly 1/sqrt(b^2 + (1-b)^2) for the chosen blend.

WHAT IT COSTS, HONESTLY
-----------------------
Albedo goes from 3 texture samples to 6, plus one 1x1 mean sample: 6 -> 10 for
the material. The NORMAL path is deliberately left alone. At the distance where
tiling is most objectionable the normal map has mipped to near-flat and
contributes almost nothing to the repeat, while albedo's large-scale blobs
survive mipping - which is precisely what the eye locks onto. Fixing what does
not cause the problem is how you pay twice.

RUN IT TWICE
------------
RH_MACRO_BLEND=0 is a mathematically exact no-op (lerp collapses to the detail
tap, contrast collapses to 1.0). Run it at 0 first and diff the frames: that
separates "did I break the graph" from "did the fix help", the same discipline
that proved the EmissiveMask was a no-op before any mask was assigned.

Re-runnable: the first run performs the graph surgery and marks every node it
creates; later runs find the marker and only retune the scalars, so the graph
never accumulates duplicates.

Env knobs:
  RH_MACRO_RATIO     period ratio of the second tap, default 5.3
  RH_MACRO_BLEND     weight of the second tap, default 0.35 (0 = exact no-op)
  RH_MACRO_CONTRAST  variance restore; default = 1/sqrt(b^2+(1-b)^2)
  RH_REPORT          report path
"""
import math
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_terrain_stochastic.txt")
PATH = "/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"
MARK = "RH_STOCHASTIC"

RATIO = float(os.environ.get("RH_MACRO_RATIO", "5.3"))
BLEND = float(os.environ.get("RH_MACRO_BLEND", "0.35"))
_auto_contrast = 1.0 / math.sqrt(BLEND * BLEND + (1.0 - BLEND) * (1.0 - BLEND))
CONTRAST = float(os.environ.get("RH_MACRO_CONTRAST", "%f" % _auto_contrast))

log = []
fails = []


def rec(s):
    log.append(str(s))


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    try:
        e.set_editor_property("desc", MARK)
    except Exception as ex:
        rec("  ! could not mark %s: %s" % (cls.__name__, ex))
    for k, v in props.items():
        try:
            e.set_editor_property(k, v)
        except Exception as ex:
            fails.append("set %s on %s: %s" % (k, cls.__name__, ex))
    return e


def link(a, out_name, b, in_name):
    ok = MEL.connect_material_expressions(a, out_name, b, in_name)
    if not ok:
        fails.append("connect %s.%s -> %s.%s" % (
            type(a).__name__, out_name or "<default>", type(b).__name__, in_name))
    return ok


def ins(mat, e):
    try:
        return list(MEL.get_inputs_for_material_expression(mat, e))
    except Exception:
        return []


def param_named(exprs, cls, name):
    for e in exprs:
        if isinstance(e, cls):
            try:
                if str(e.get_editor_property("parameter_name")) == name:
                    return e
            except Exception:
                pass
    return None


def main():
    mat = unreal.load_asset(PATH)
    if not mat:
        rec("MISSING %s" % PATH)
        return

    exprs = list(MEL.get_material_expressions(mat))
    rec("loaded %s (%d expressions)" % (PATH, len(exprs)))

    # ---- retune-only path -------------------------------------------------
    existing = {}
    for e in exprs:
        try:
            d = str(e.get_editor_property("desc"))
        except Exception:
            continue
        if d == MARK and isinstance(e, unreal.MaterialExpressionScalarParameter):
            existing[str(e.get_editor_property("parameter_name"))] = e
    if existing:
        rec("marker found - RETUNE ONLY, no graph surgery")
        for pname, value in (("MacroRatio", RATIO), ("MacroBlend", BLEND),
                             ("MacroContrast", CONTRAST)):
            e = existing.get(pname)
            if e:
                e.set_editor_property("default_value", value)
                rec("  %s = %.4f" % (pname, value))
            else:
                fails.append("retune: %s not found among marked scalars" % pname)
        finish(mat)
        return

    # ---- locate the albedo lookup structurally ----------------------------
    surf = param_named(exprs, unreal.MaterialExpressionTextureObjectParameter, "SurfTex")
    tint = param_named(exprs, unreal.MaterialExpressionVectorParameter, "Tint")
    if not surf or not tint:
        rec("ABORT: SurfTex=%s Tint=%s" % (bool(surf), bool(tint)))
        return

    # detail albedo taps = TextureSamples fed by SurfTex; their UV is input 0
    taps = []
    for e in exprs:
        if not isinstance(e, unreal.MaterialExpressionTextureSample):
            continue
        up = ins(mat, e)
        if surf in up:
            uv = up[0] if up and up[0] is not surf else None
            taps.append((e, uv))
    rec("albedo taps found: %d" % len(taps))
    if len(taps) != 3:
        rec("ABORT: expected 3 SurfTex samples (triplanar), got %d" % len(taps))
        return

    # each tap feeds a Multiply (tap * planar weight); that Multiply is where
    # the blended value must be spliced in
    weight_mul = {}
    for e in exprs:
        if not isinstance(e, unreal.MaterialExpressionMultiply):
            continue
        up = ins(mat, e)
        for tap, _uv in taps:
            if up and up[0] is tap:
                weight_mul[tap] = e
    if len(weight_mul) != 3:
        rec("ABORT: could not match all 3 taps to their weight Multiply (got %d)"
            % len(weight_mul))
        return

    # the Tint multiply is the tail of the albedo chain
    tint_mul = None
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionMultiply) and tint in ins(mat, e):
            tint_mul = e
    if not tint_mul:
        rec("ABORT: no Multiply consumes Tint")
        return
    blended = [u for u in ins(mat, tint_mul) if u is not tint]
    if len(blended) != 1:
        rec("ABORT: Tint multiply has %d non-Tint inputs" % len(blended))
        return
    blended = blended[0]
    rec("splice points: 3 weight multiplies + tail %s" % type(blended).__name__)

    # ---- does SurfTex actually have a mip chain? --------------------------
    mipped = True
    try:
        tex = surf.get_editor_property("texture")
        mgs = tex.get_editor_property("mip_gen_settings")
        mipped = str(mgs) != "TextureMipGenSettings.TMGS_NO_MIPMAPS"
        rec("SurfTex mip_gen_settings=%s -> mean-from-mip %s"
            % (mgs, "OK" if mipped else "UNAVAILABLE"))
    except Exception as exc:
        rec("  ! could not read mip settings (%s) - assuming mipped" % exc)

    # ---- build ------------------------------------------------------------
    ratio = node(mat, unreal.MaterialExpressionScalarParameter, -1500, 1500,
                 parameter_name="MacroRatio", default_value=RATIO,
                 group="Terrain Macro")
    blend = node(mat, unreal.MaterialExpressionScalarParameter, -1500, 1600,
                 parameter_name="MacroBlend", default_value=BLEND,
                 group="Terrain Macro")
    contrast = node(mat, unreal.MaterialExpressionScalarParameter, -1500, 1700,
                    parameter_name="MacroContrast",
                    default_value=CONTRAST if mipped else 1.0,
                    group="Terrain Macro")
    flip = node(mat, unreal.MaterialExpressionConstant2Vector, -1500, 1800,
                r=1.0, g=-1.13)
    offset = node(mat, unreal.MaterialExpressionConstant2Vector, -1500, 1900,
                  r=0.37, g=0.61)

    y = 1500
    for tap, uv in taps:
        if uv is None:
            fails.append("tap has no UV source; skipped")
            continue
        scaled = node(mat, unreal.MaterialExpressionDivide, -1200, y)
        link(uv, "", scaled, "A")
        link(ratio, "", scaled, "B")
        flipped = node(mat, unreal.MaterialExpressionMultiply, -1050, y)
        link(scaled, "", flipped, "A")
        link(flip, "", flipped, "B")
        shifted = node(mat, unreal.MaterialExpressionAdd, -900, y)
        link(flipped, "", shifted, "A")
        link(offset, "", shifted, "B")

        macro = node(mat, unreal.MaterialExpressionTextureSample, -740, y)
        link(shifted, "", macro, "UVs")
        link(surf, "", macro, "Tex")

        mix = node(mat, unreal.MaterialExpressionLinearInterpolate, -560, y)
        link(tap, "", mix, "A")
        link(macro, "", mix, "B")
        link(blend, "", mix, "Alpha")

        # splice: the weight multiply now reads the BLENDED value
        link(mix, "", weight_mul[tap], "A")
        y += 220

    # variance restore about the texture mean (top mip = average colour)
    mean = node(mat, unreal.MaterialExpressionTextureSample, -560, y + 60)
    link(surf, "", mean, "Tex")
    try:
        mean.set_editor_property("mip_value_mode",
                                 unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
        mean.set_editor_property("const_mip_value", 15.0)
    except Exception as exc:
        fails.append("mip-level mean unavailable: %s" % exc)
        mipped = False

    sub = node(mat, unreal.MaterialExpressionSubtract, -400, y + 60)
    link(blended, "", sub, "A")
    link(mean, "", sub, "B")
    scale = node(mat, unreal.MaterialExpressionMultiply, -260, y + 60)
    link(sub, "", scale, "A")
    link(contrast, "", scale, "B")
    restored = node(mat, unreal.MaterialExpressionAdd, -120, y + 60)
    link(mean, "", restored, "A")
    link(scale, "", restored, "B")
    link(restored, "", tint_mul, "A")

    # ---- read back: a believed write is not a write -----------------------
    rec("")
    rec("--- read-back ---")
    for tap, _uv in taps:
        up = ins(mat, weight_mul[tap])
        ok = bool(up) and isinstance(up[0], unreal.MaterialExpressionLinearInterpolate)
        rec("  weight multiply in[0] = %s  %s" % (
            type(up[0]).__name__ if up else "<none>", "OK" if ok else "FAIL"))
        if not ok:
            fails.append("splice did not stick on a weight multiply")
    up = ins(mat, tint_mul)
    ok = bool(up) and isinstance(up[0], unreal.MaterialExpressionAdd)
    rec("  tint multiply in[0]     = %s  %s" % (
        type(up[0]).__name__ if up else "<none>", "OK" if ok else "FAIL"))
    if not ok:
        fails.append("contrast restore did not stick")

    finish(mat)


def finish(mat):
    errors = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    rec("")
    rec("expressions: %d" % MEL.get_num_material_expressions(mat))
    rec("compile errors: %d" % len(errors))
    for e in errors:
        rec("  ERROR %s" % e)
    rec("settings: ratio=%.3f blend=%.3f contrast=%.3f" % (RATIO, BLEND, CONTRAST))
    rec("link/set failures: %d" % len(fails))
    for f in fails:
        rec("  FAIL %s" % f)
    rec("RESULT: %s" % ("OK" if not fails and not errors else "PROBLEMS - read above"))


main()
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
