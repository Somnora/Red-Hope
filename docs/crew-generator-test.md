# Crew generator test — 2026-08-17

**Question:** the premium plan's long pole is P2 Characters (16–25 h), justified
by the director's verdict that the crew look "incomplete or blotchy". Every
TRELLIS.2 asset so far has been hard-surface; humanoids are the hardest case for
single-image 3D and lumpy humanoids are the reason the Hunyuan crew were
rejected. **So: does TRELLIS.2 do humanoids materially better than Hunyuan?**

**Method:** the three existing character front-cutouts (`cmdr_vale`, `eng_ruiz`,
`geo_okafor`) — the *same sprites* the shipped meshes were built from — through
TRELLIS.2 at 12k tris, rendered against the shipped Hunyuan meshes in one fixed
Blender light rig. One variable. ~14 min on an A100, ~$1.

**Answer: no, and the plan should change because of it.**

| char | source | tris | verts | v/t |
|---|---|---|---|---|
| cmdr_vale | Hunyuan (ships now) | 18,000 | 53,885 | 2.99 |
| cmdr_vale | TRELLIS.2 | 11,213 | 9,619 | 0.86 |
| eng_ruiz | Hunyuan (ships now) | 18,000 | 53,864 | 2.99 |
| eng_ruiz | TRELLIS.2 | 11,295 | 10,040 | 0.89 |
| geo_okafor | Hunyuan (ships now) | 18,000 | 53,853 | 2.99 |
| geo_okafor | TRELLIS.2 | 11,294 | 10,515 | 0.93 |

Sheet: `qa/2026-08-17/qa-crew-generator-test.jpg`.

## What the renders actually show

**The Hunyuan meshes are not lumpy.** Clean silhouettes, correct proportions,
separated fingers, real boots, faces with structure. This contradicts the premise
the whole character redo was built on, and it contradicts my own first read: I
looked at the TRELLIS.2 output alone, called it "decisively better", and was
wrong — I had judged without the control in frame, which is the exact error this
session has been cataloguing. With both rendered side by side they are
comparable, and on GEAR DETAIL the Hunyuan meshes are arguably *better*:
eng_ruiz's shoulder harness and geo_okafor's vest pockets read crisply in the old
row and are smoothed away in the new one.

**Where TRELLIS.2 genuinely wins is topology, not looks.** Every Hunyuan mesh is
exactly 18,000 tris carrying ~53,860 vertices — **2.99 verts per triangle**,
which means the mesh is completely unwelded: every triangle owns its own three
vertices and shares none. TRELLIS.2 comes out at 0.86–0.93, properly welded, with
40% fewer triangles.

## Consequences

1. **Do not re-bake the crew through TRELLIS.2 for quality.** It would cost the
   plan's largest budget line, lose gear detail, and not address the complaint.
   The 3×3 stylization pilot is deferred, not cancelled — it answers a *design*
   question (realistic / lightly stylized / chunky), and that question is
   independent of which generator executes it.
2. **The crew source art is not the defect.** Whatever the director sees is
   downstream of the mesh: shading, rig, scale, or viewing distance. Two shading
   causes have already been found and fixed since that verdict was given — the
   crew were rendering through a SUBSURFACE PROFILE skin shader (light bleeding
   through thin geometry, which is also the "see through parts of their body"
   report), and `M_RH_Character` had no normal input and no MR input at all, so
   every walker's roughness map sat inert. **The verdict predates both fixes and
   should be re-taken before any rebuild is authorised.**
3. **The unwelded mesh is a live suspect for the animation-time symptom.** 53,860
   split vertices going through `rig_colonist.py`'s automatic weights is exactly
   the setup where adjacent triangles receive different weights and seams pull
   apart as the mesh deforms — which would read as gaps opening in a body while
   it walks, and would not show in a static render. NOT yet demonstrated; the
   test is to weld one mesh, re-rig it, and compare the two in motion. If it
   holds, the fix is a weld + re-rig of the existing meshes — hours, not the
   16–25 h rebuild, and it keeps the gear detail.

## Outputs

Kept at `Martians/gen/crewtest/*.glb` (local) and `io/crewtest/out/` on
`red-hope-east`. They are usable if the stylization pilot later runs, so the
$1 is not spent twice.
