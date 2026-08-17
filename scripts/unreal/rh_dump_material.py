"""Dump a material's node graph: expressions, params, and what drives each
output. Read-only. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_dump_material.py -unattended -nosound -stdout

Env:
  RH_MAT     asset path, default /Game/RedHope/Art/M_MarsSurface.M_MarsSurface
  RH_REPORT  report path

In 5.8 `mat.get_editor_property("expressions")` does NOT work; the readable
surface is MaterialEditingLibrary's get_material_expressions /
get_material_property_input_node / get_inputs_for_material_expression. This
script exists because guessing at a graph and authoring against the guess is
how the macro-variation pass got built and then reverted.
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_dump_material.txt")
PATH = os.environ.get("RH_MAT", "/Game/RedHope/Art/M_MarsSurface.M_MarsSurface")

log = []


def get(e, name, default="?"):
    try:
        return e.get_editor_property(name)
    except Exception:
        return default


def label(e):
    if e is None:
        return "<none>"
    cls = type(e).__name__.replace("MaterialExpression", "")
    pname = get(e, "parameter_name", None)
    desc = get(e, "desc", "")
    bits = [cls]
    if pname:
        bits.append("'%s'" % pname)
    dv = get(e, "default_value", None)
    if dv is not None and dv != "?":
        bits.append("default=%s" % (dv,))
    if desc:
        bits.append("desc=%s" % desc)
    return " ".join(str(b) for b in bits)


mat = unreal.load_asset(PATH)
if not mat:
    log.append("MISSING %s" % PATH)
else:
    log.append("=== %s ===" % PATH)
    for attr in ("get_scalar_parameter_names", "get_vector_parameter_names",
                 "get_texture_parameter_names", "get_static_switch_parameter_names"):
        try:
            names = [str(n) for n in getattr(MEL, attr)(mat)]
            log.append("%s: %s" % (attr.replace("get_", "").replace("_names", ""), names))
        except Exception as exc:
            log.append("%s: unavailable (%s)" % (attr, str(exc)[:60]))

    log.append("")
    log.append("--- material properties -> driver ---")
    props = [
        ("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
        ("Metallic", unreal.MaterialProperty.MP_METALLIC),
        ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
        ("Normal", unreal.MaterialProperty.MP_NORMAL),
        ("EmissiveColor", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
        ("Opacity", unreal.MaterialProperty.MP_OPACITY),
        ("AmbientOcclusion", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),
    ]
    for name, mp in props:
        try:
            src = MEL.get_material_property_input_node(mat, mp)
        except Exception as exc:
            src = None
            log.append("  %-16s <error %s>" % (name, str(exc)[:50]))
            continue
        log.append("  %-16s <- %s" % (name, label(src)))

    log.append("")
    exprs = list(MEL.get_material_expressions(mat))
    log.append("--- %d expressions ---" % len(exprs))
    index = {}
    for i, e in enumerate(exprs):
        index[e] = i
    for i, e in enumerate(exprs):
        line = "[%3d] %s" % (i, label(e))
        extra = []
        for k in ("texture", "scale", "levels", "output_min", "output_max",
                  "const_a", "const_b", "material_expression_editor_x",
                  "material_expression_editor_y"):
            v = get(e, k, None)
            if v is not None and v != "?":
                if k == "texture" and v is not None:
                    try:
                        extra.append("texture=%s" % v.get_name())
                    except Exception:
                        pass
                elif k.startswith("material_expression_editor"):
                    continue
                else:
                    extra.append("%s=%s" % (k, v))
        if extra:
            line += "  {%s}" % ", ".join(extra)
        log.append(line)
        # get_inputs_for_material_expression returns the upstream EXPRESSIONS
        # themselves, in input order - NOT input names. The first version of
        # this script assumed names and called MEL.get_input_node (which does
        # not exist in 5.8); the AttributeError was swallowed by a bare except,
        # so the graph silently dumped with no connections at all. Never let an
        # inspection tool fail quietly - a blank section reads as "nothing
        # connected" rather than "the reader is broken".
        try:
            ups = list(MEL.get_inputs_for_material_expression(mat, e))
            for slot, tgt in enumerate(ups):
                if tgt is not None:
                    log.append("        in[%d] <- [%s] %s" % (
                        slot, index.get(tgt, "?"), label(tgt)))
        except Exception as exc:
            log.append("        <inputs UNAVAILABLE: %r>" % (exc,))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
