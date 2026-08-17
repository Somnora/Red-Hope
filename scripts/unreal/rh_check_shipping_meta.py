"""Verify the shipping metadata and the notices staging entry. No editor needed.

  python3 scripts/unreal/rh_check_shipping_meta.py

WHY STATIC RATHER THAN THROUGH THE EDITOR
-----------------------------------------
The obvious check is to boot the editor and read the settings CDOs back. Tried
on 2026-08-17 and it is not worth it: `unreal.GeneralProjectSettings` and
`unreal.ProjectPackagingSettings` are not Python-bound in this build, and while
`unreal.find_object(None, "/Script/EngineSettings.Default__GeneralProjectSettings")`
does find the CDO, `get_editor_property` throws on most of its properties. One
editor boot per probe to learn that is a poor trade for a config file check.

The four facts that actually needed establishing were settled by reading the
engine, and they are recorded here so nobody re-derives them:

  1. UProjectPackagingSettings is `config=Game`, so DefaultGame.ini is the
     right file    (Engine/Source/Developer/DeveloperToolSettings/Classes/
                    Settings/ProjectPackagingSettings.h:178)
  2. `DirectoriesToAlwaysStageAsNonUFS` is the correct property name    (:599)
  3. The class MOVED from UnrealEd to DeveloperToolSettings, but the config
     SECTION name did not: Epic's own BaseGame.ini:87 still writes
     `[/Script/UnrealEd.ProjectPackagingSettings]`, and BaseEngine.ini:960
     carries a ClassRedirects entry for it. So the old section name is correct
     and a "corrected" one would be the broken variant.
  4. GeneralProjectSettings.Description read back live from the CDO after the
     edit, which is what proves the file parses at all.

WHAT THIS GUARDS
----------------
`ProjectName` shipped as "Top Down BP Game Template" until 2026-08-17 - the UE
template's own name, in the packaged build's window title and About box - and
the rest of GeneralProjectSettings was absent. The staging entry is what carries
Notices/NOTICES.txt into a package; without it the licence attributions never
leave the source repo, which satisfies neither DINOv3 ("Built with DINOv3" on
product documentation) nor Tencent (notice must accompany distribution).

Exits non-zero on any failure so it can gate a release step.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME_INI = os.path.join(ROOT, "Config", "DefaultGame.ini")
NOTICES = os.path.join(ROOT, "Notices", "NOTICES.txt")

REQUIRED_META = {
    "ProjectName": lambda v: v and "Template" not in v,
    "ProjectVersion": lambda v: bool(v),
    "CompanyName": lambda v: bool(v),
    "CopyrightNotice": lambda v: bool(v),
    "Description": lambda v: bool(v),
}
# Every attribution the audit says is owed on distribution.
REQUIRED_NOTICE_TEXT = [
    "Built with DINOv3",
    "Tencent Hunyuan 3D 2.1 Community License Agreement",
    "Copyright (c) Microsoft Corporation",
    "Xintao Wang",
]


def sections(text):
    out, cur = {}, None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1]
            out.setdefault(cur, [])
        elif cur is not None:
            out[cur].append(line)
    return out


def main():
    fails = []
    if not os.path.exists(GAME_INI):
        print("FAIL  %s missing" % GAME_INI)
        return 1
    sec = sections(open(GAME_INI).read())

    gps = "\n".join(sec.get("/Script/EngineSettings.GeneralProjectSettings", []))
    for key, ok in REQUIRED_META.items():
        m = re.search(r"^%s=(.*)$" % re.escape(key), gps, re.M)
        val = m.group(1).strip() if m else ""
        good = bool(m) and ok(val)
        print("%-6s %-16s %s" % ("ok" if good else "FAIL", key, val or "<missing>"))
        if not good:
            fails.append(key)

    pkg_sec = "/Script/UnrealEd.ProjectPackagingSettings"   # see note 3 above
    pkg = "\n".join(sec.get(pkg_sec, []))
    staged = re.findall(r"\+DirectoriesToAlwaysStageAsNonUFS=\(Path=\"([^\"]+)\"\)", pkg)
    good = "Notices" in staged
    print("%-6s %-16s %s" % ("ok" if good else "FAIL", "staged non-UFS", staged or "<none>"))
    if not good:
        fails.append("staging")

    if os.path.exists(NOTICES):
        # Collapse whitespace before matching: these are required VERBATIM
        # strings, but they are long enough to wrap in a 79-column text file,
        # so a raw substring test fails on correct text. Caught exactly that
        # way on the Tencent line the first time this ran.
        body = re.sub(r"\s+", " ", open(NOTICES).read())
        missing = [t for t in REQUIRED_NOTICE_TEXT
                   if re.sub(r"\s+", " ", t) not in body]
        good = not missing
        print("%-6s %-16s %d bytes%s"
              % ("ok" if good else "FAIL", "NOTICES.txt", len(body),
                 "" if good else "  MISSING: %s" % ", ".join(missing)))
        if not good:
            fails.append("notices-content")
    else:
        print("FAIL   %-16s not on disk" % "NOTICES.txt")
        fails.append("notices-file")

    print()
    print("PASS - shipping metadata and notices staging are in place" if not fails
          else "FAILED: %s" % ", ".join(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
