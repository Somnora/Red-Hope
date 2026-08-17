"""Break the regolith tiling with large-scale albedo variation. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_terrain_macro.py -unattended -nosound -stdout

WHY: M_MarsSurface is a correct triplanar, but it samples SurfTex at exactly one
scale (TileCm, 900 cm for the ground sections). At the default 215 m camera
register that is roughly twenty repeats across the screen, and the ground fills
most of the frame, so the planet reads as tiled wallpaper - the single most
visible surface in the game.

The fix is the standard one: multiply the finished base colour by a very
low-frequency noise centred on 1.0, so broad patches sit lighter and darker than
their neighbours and the eye stops locking onto the repeat. The texture, the UVs
and the triplanar blend are all untouched; this only wraps the final colour.

    BaseColor = <existing chain> * (1 + (Noise - 0.5) * MacroStrength)

Re-runnable: the first run builds the chain, later runs only retune the
constants, so the graph never accumulates duplicates.

Env knobs:
  RH_MACRO_STRENGTH  peak-to-peak variation, default 0.55 (about +/-27%)
  RH_MACRO_SIZE_CM   feature size in cm, default 9000 (90 m patches)
  RH_REPORT          report path
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_terrain_macro.txt")
STRENGTH = float(os.environ.get("RH_MACRO_STRENGTH", "0.55"))
SIZE_CM = float(os.environ.get("RH_MACRO_SIZE_CM", "9000"))
PATH = "/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"
MARK = "RH_MACRO"  # desc marker so a re-run can find what it built last time

log = []


def prop(obj, name, value):
    """Set an editor property, recording rather than raising when a build
    spells it differently."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:
        log.append("    ! %s=%s failed: %s" % (name, value, str(exc)[:60]))
        return False


def node(mat, cls, x, y, desc=None):
    e = MEL.create_material_expression(mat, cls, x, y)
    if desc is not None:
        prop(e, "desc", desc)
    return e


def main():
    mat = unreal.load_asset(PATH)
    if not mat:
        log.append("MISSING %s" % PATH)
        return

    existing = [str(n) for n in MEL.get_scalar_parameter_names(mat)]
    log.append("material: %s" % PATH)
    log.append("scalars before: %s" % existing)

    if "MacroStrength" in existing:
        # Already wired: only retune, so repeated runs cannot duplicate nodes.
        tuned = 0
        for e in MEL.get_material_expressions(mat):
            try:
                if e.get_editor_property("parameter_name") == "MacroStrength":
                    prop(e, "default_value", STRENGTH)
                    tuned += 1
                    continue
            except Exception:
                pass
            try:
                if e.get_editor_property("desc") == MARK + "_NOISE":
                    prop(e, "scale", 1.0 / SIZE_CM)
                    tuned += 1
            except Exception:
                pass
        log.append("already wired: retuned %d node(s) -> strength %.2f, size %.0f cm"
                   % (tuned, STRENGTH, SIZE_CM))
    else:
        src = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
        if not src:
            log.append("ABORT: nothing currently drives BaseColor")
            return
        log.append("wrapping BaseColor source: %s" % type(src).__name__)

        strength = node(mat, unreal.MaterialExpressionScalarParameter, -900, 1500, MARK)
        prop(strength, "parameter_name", "MacroStrength")
        prop(strength, "default_value", STRENGTH)
        prop(strength, "group", "Macro variation")

        # Noise leaves Position unconnected on purpose: it then defaults to
        # absolute world position, which is exactly the domain we want, and it
        # keeps the ground pad and every scenery section in the same noise field
        # instead of each one restarting the pattern at its own origin.
        noise = node(mat, unreal.MaterialExpressionNoise, -900, 1650, MARK + "_NOISE")
        prop(noise, "scale", 1.0 / SIZE_CM)
        prop(noise, "output_min", 0.0)
        prop(noise, "output_max", 1.0)
        prop(noise, "levels", 2)
        prop(noise, "turbulence", False)
        prop(noise, "quality", 1)
        try:
            prop(noise, "noise_function", unreal.NoiseFunction.NOISEFUNCTION_SIMPLEX_TEX)
        except Exception as exc:
            log.append("    ! noise_function left at default: %s" % str(exc)[:60])

        centred = node(mat, unreal.MaterialExpressionSubtract, -600, 1650, MARK)
        prop(centred, "const_b", 0.5)
        MEL.connect_material_expressions(noise, "", centred, "A")

        scaled = node(mat, unreal.MaterialExpressionMultiply, -430, 1600, MARK)
        MEL.connect_material_expressions(centred, "", scaled, "A")
        MEL.connect_material_expressions(strength, "", scaled, "B")

        around_one = node(mat, unreal.MaterialExpressionAdd, -260, 1600, MARK)
        prop(around_one, "const_b", 1.0)
        MEL.connect_material_expressions(scaled, "", around_one, "A")

        tinted = node(mat, unreal.MaterialExpressionMultiply, -90, 1450, MARK)
        MEL.connect_material_expressions(src, "", tinted, "A")
        MEL.connect_material_expressions(around_one, "", tinted, "B")
        MEL.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
        log.append("built macro chain: strength %.2f, feature size %.0f cm" % (STRENGTH, SIZE_CM))

    errors = MEL.recompile_material(mat)
    log.append("compile errors: %s" % (errors if errors else 0))
    log.append("scalars after: %s" % [str(n) for n in MEL.get_scalar_parameter_names(mat)])
    if not errors:
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        log.append("saved %s" % PATH)
    else:
        log.append("NOT SAVED - compile errors above")


main()
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
