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
3. **The unwelded mesh was the animation-time defect. CONFIRMED 2026-08-17.**
   The suspect held, and the mechanism is worse than "some vertices diverge".

   With the mesh unwelded, `rig_colonist.py`'s heat solver fails on ALL of it and
   the nearest-bone backstop hard-assigns **53,885 of 53,885 vertices** — 100% —
   each pinned to one bone with zero blending anywhere. Every joint is a rigid
   boundary. Welding first (after import, BEFORE the bind) drops the mesh to
   9,002 real vertices, the heat solver succeeds completely, and the backstop
   assigns **0**.

   In motion the difference is not subtle: at Walk frame 1 a slab of hip
   geometry tears off the body and hangs in the air
   (`qa/2026-08-17/qa-weld-hip-tear.jpg`; four-frame comparison in
   `qa-weld-motion-ab.jpg`). That is the director's "sometimes you can see
   through parts of their body", reproduced deterministically and then removed.
   Silhouette differs by 1.6% of body pixels at the worst frame and 0.3% at the
   best — a defect that only appears at certain phases of the cycle, which is
   exactly why it read as intermittent.

   **The weld must happen inside the rigging script.** Doing it in an
   intermediate GLB does nothing: glTF cannot store per-loop UVs, so the
   exporter re-splits every vertex on the way out and the file arrives unwelded
   again (measured: a welded 9,002-vertex GLB came back as 53,751 at bind time).
   What matters is that each real vertex is ONE vertex *while weights are
   computed*; the exporter may duplicate freely afterwards, because the copies
   then carry identical weights and cannot travel apart.

   **All 20 crew are now fixed.** The 12 with local sources were re-rigged from
   those. The remaining 8 — the plan's "missing-8 faces", with no source mesh
   anywhere — were recovered by exporting their skeletal meshes back out of UE
   (`GLTFExporter`), stripping the old skin
   (`scripts/blender/rh_strip_rig.py`: delete armatures, drop armature
   modifiers, clear vertex groups, apply transforms — otherwise the fresh bind
   layers onto stale weights whose group names collide with the new rig's), then
   welding and re-rigging that.

   Three things that pass came out of doing the 8:

   - **Two of them (`fab_stone`, `vet_kowalski`) still fail the heat solver even
     welded**, because ~25% of their geometry sits in detached shells: 64 and 74
     connected components with the largest holding only ~76%, against a healthy
     mesh's 94%. Blender's heat weighting is all-or-nothing per object, so it
     fails for the whole mesh and every vertex lands in the backstop.
   - **So the backstop was rewritten to blend.** It used to assign 1.0 to the
     single nearest bone, which IS rigid binding and is what tears a mesh open.
     It now distributes over the 3 nearest bone segments by inverse distance: a
     detached pouch still rides its nearest bone almost rigidly because that
     bone dominates, while a vertex near a joint gets a real mix and deforms
     smoothly. The worst case degrades from "geometry tears off the body" to
     "slightly soft weighting" — verified in motion,
     `qa/2026-08-17/qa-soft-backstop-motion.jpg`.
   - **Reimporting an untextured GLB resets the material slot** to
     `WorldGridMaterial`. The 8 UE exports carry no embedded images, so all 8
     lost their binding on reimport and were restored by re-running
     `rh_wire_walkers.py` → `rh_wire_crew_mr.py` → `rh_wire_crew_normals.py`.
     Caught because the verification pass checks the material parent rather than
     assuming the reimport preserved it.

   **`v/t` in UE is not a fix indicator.** UE and glTF both re-split vertices, so
   a correctly welded-at-bind-time crew mesh still reads 2.99 verts/tri in UE
   (measured on the fixed `cmdr_vale`). The real indicator is the backstop count
   at rig time: 53,885 of 53,885 before, 0 after.

   **The density mismatch was left in place, deliberately, after testing the
   fix.** The 12 rebuilt from local sources are 18,000 tris where UE had them at
   9,000; the 8 rebuilt from UE exports are 9,000. I proposed normalising the 12
   down to match and the director approved it — then doing the work showed the
   normalisation makes the crew WORSE, so it was abandoned rather than shipped:

   - At 18,000 tris all 12 rig perfectly: backstop 0 on every one.
   - Decimated to 9,000, two of them (`med_haddad`, `quart_bello`) collapse to
     100% backstop — the heat solver fails again.
   - The reason, measured: `med_haddad` welds to **ONE connected component at
     18,000 tris**. Decimated to 9,000 it becomes **95 components, largest
     holding 38%**. Edge collapse severs thin connections and shatters the mesh,
     which is exactly the condition the heat solver cannot survive.

   Uniformity buys nothing here — 12 × 18k + 8 × 9k ≈ 288k triangles for the
   whole roster is trivial at this scale, and no perf argument favours 9k — while
   the cost is real rig quality on two characters. Different characters carrying
   different densities is ordinary and invisible at a camera that gets no closer
   than 29 m.

   **This also completes the root-cause story of the director's bug.** The
   original pipeline decimated before rigging. Decimation fragments the mesh; a
   fragmented mesh defeats Blender's heat weighting; the failure falls through to
   a backstop that hard-pinned every vertex to one nearest bone; rigid binding
   tears geometry apart at the joints during animation; and that is the "you can
   see through parts of their body" report. The correct order — **decimate never,
   or decimate then check connectivity; weld; then bind** — is now enforced by
   `rig_colonist.py` welding before the bind and by `rh_decimate.py` carrying the
   warning that it must run before rigging and can fragment what it touches.

## Outputs

Kept at `Martians/gen/crewtest/*.glb` (local) and `io/crewtest/out/` on
`red-hope-east`. They are usable if the stylization pilot later runs, so the
$1 is not spent twice.

---

## The splotch, finally: the crew's atlases in UE are confetti (2026-08-17)

The director looked at the re-rigged crew and reported them **worse**: "super
splotchy… just like a mesh of colors slapped on a model." He was right, and two
of the three causes were mine.

**What the splotch actually is.** The crew albedo atlases living in UE are
shattered confetti — hundreds of tiny disconnected paint fragments in hard
black/white (`qa/2026-08-17/qa-crew-confetti-atlas.jpg`, top row). The local
source GLBs for the same characters carry *coherent* paint: recognisable faces,
jackets, denim, gear (bottom row). Two completely different texture sets, and UE
had the ruined one.

This is the decimation story again, one layer further out. The original pipeline
decimated before rigging; decimation shatters the mesh into 95+ components; a
shattered mesh unwraps to confetti UV islands; paint baked onto confetti islands
IS the splotch. Every "splotchy" report going back weeks traces to that one
ordering mistake.

**What I made worse.** My re-rig replaced the 12 crew meshes with source-derived
geometry carrying the SOURCE's UVs, while leaving them bound to the OLD confetti
atlas keyed to the OLD UVs. Mesh and texture no longer agreed at all. The trap is
documented in `rh_import_textures.py`'s own docstring — *"reimport_inplace does
NOT bring the source file's textures with it… New geometry, old paint, and a
green log at every step"* — written after the crops hit it on 2026-08-16. I had
the tool, the warning, and the precedent, and still shipped the same bug.

**Fixed** by importing each source GLB's albedo and MR over the existing texture
assets (sRGB on / TC_DEFAULT for albedo, sRGB off / TC_MASKS for MR). Verified
by reading the textures back out of UE and diffing against the sources: mean
absolute difference **0.0** against source, ~74–104 against the old confetti.

**Not fixed for the 8** with no local source. They were reimported from their own
UE exports, so they are at least self-consistent — mesh and confetti atlas agree
— but they are still wearing confetti. Those need regenerating from sprites, and
that is now the strongest argument for the P2 character work: not because the
generator is bad, but because 8 characters' paint is unrecoverable.

## Foot sliding, measured and fixed

Also reported: "their steps don't match their strides." The Walk clip is 24
frames at 24 fps — exactly 1.0 s — and the foot bone swings 52.2 cm peak-to-peak
relative to the body, so the clip depicts ~104 cm of ground per cycle. The
visualizer translated the figure at 170 cm/s with `GlobalAnimRateScale` pinned to
1.0, so the feet slid forward by ~63%.

The rate is now the ratio of actual speed to depicted stride, with the stride
exposed as `rh.CrewStrideCm` (default 104.4) because it is a look value that must
follow any re-bake of the clip. Work and idle clips keep rate 1.0 — they are not
locomotion — and everything still freezes at `Pace == 0`.

**The robot walkers have the same bug** (`RHAgentVisualizerSubsystem.cpp` calls
`PlayAnimation` with no rate scaling) and are NOT fixed here; their clip's stride
has not been measured.

## Still open: crew walk through furniture

The third report — "no collision, they walk right through objects like desks and
tables" — is real and is not a bug in the sense the other two were. The crew
visualizer walks each figure in a straight line toward a wander point with no
knowledge of what is in the way; props are placed by a different subsystem and
nothing consults them. Fixing it properly means either prop-aware wander points
or real avoidance, which is a design decision about how much agency the crew
should appear to have, not a repair.
