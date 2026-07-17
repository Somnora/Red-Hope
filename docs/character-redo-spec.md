# Character Redo — working spec (2026-07-17)

Director brief (hand-play session, 2026-07-17): the crew walker models "look
weirdly unfinished" — they read as gray statues. Verdict from the screenshots:
the meshes are UNTEXTURED (the paint stage never ran on the character batch
before rigging; props got shape+paint, characters got shape only). The
director's call: **redo the character designs** — new sprites, full pipeline,
re-rig — queued as the next gate after the hand-play fix pass verifies.

## What exists (build on, not around)
- 20-face roster: `RH_Walker_<face>` skeletal meshes + Walk/Idle + per-job
  clips under `/Game/RedHope/Art/CrewAnim/`; `WalkerMeshPath()` wires them by
  crew Id. The redo swaps ASSETS in place — no code change if names hold.
- The rig loop from the animation phase (products in NFS `io/rigged/`, 21
  GLBs) — locate its script under `red_hope/scripts/` on Somnora-East when
  the box is up; it consumed cleaned character GLBs and emitted rigged+clipped
  walkers.
- The full generation stack on the A100: style-lock (SDXL + IP-Adapter),
  sprite-to-3d (Hunyuan3D shape), the PAINT stage (queue worker ran it for
  props: `queue/<batch>/out/*_textured.glb`), mesh-cleanup. All proven.
- `rh.WalkerYawOffsetDeg` (fix pass): mesh-forward correction is live-tunable,
  so the new meshes' facing convention is a console dial, not a re-export.

## The plan
1. **Sprites** — the director generates new character designs with Gemini
   (nano banana 2) OR asks for an SDXL batch; either way they land in
   `sprites/chars-redo/` on Somnora-East (the documented convention).
   Per-sprite requirements (Session-51 lessons, non-negotiable):
   - ONE figure, full body, FRONT view only (a turnaround meshes as two bodies)
   - A-pose or relaxed-neutral (rig-friendly), feet visible
   - genuinely FLAT solid background; negative-prompt the halo family:
     halo/vignette/glow/"circle behind"/orb/"radial gradient"
   - verify flatness numerically (corner-crop grayscale stddev < 6) before
     meshing; eyeball any flagged by a limb reaching a corner
2. **Style-lock** the approved sprites into one consistent sheet (keep the
   front-only seed per character).
3. **Shape → PAINT → cleanup** on the A100 (the paint stage is the whole
   point of the redo — budget its VRAM/time; A100 SXM4, not the A10).
   Preview contact sheets per character for a director pick BEFORE rigging.
4. **Re-rig** the picked, textured, cleaned meshes with the animation-phase
   rig loop (same skeleton + clip names so the existing Walk/Idle/job clips
   and `WalkerAnimPath()` keep working, or re-emit clips per walker as the
   loop did originally).
5. **Import** headless over the existing `RH_Walker_<face>` names
   (`-replaceexisting`), live boot, director look.

## Constraints
- Names are the contract: keep `RH_Walker_<face>` + clip suffixes so zero
  code changes ship with the art swap.
- The 20-face roster stays (crew identity is content, not art) - a redo
  changes their look, never their names/Ids.
- Robots keep their current walkers (director flagged CHARACTERS); revisit
  robots only if the character pass reads clearly better in-game.
- Cost hygiene: one A100 session, terminate on completion, outputs to NFS.

## Verification
- Contact sheets (textured, 3 angles) per character before rigging — director
  pick is the gate.
- Headless import receipts + a live boot with `RH.Demo`; the walker facing
  dial confirmed; the battery stays green (art-only, no sim surface).
