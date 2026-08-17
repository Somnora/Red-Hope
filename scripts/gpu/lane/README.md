# The GPU lane's own tooling — rescued from the NFS 2026-08-17

Everything in this directory ran on Lambda boxes and, until this commit, existed
in **exactly one place: the persistent filesystem**. Not in git, not on the Mac.

## Why it is here

Twice in one week that arrangement cost real work:

- The emissive-mask cutter ran in a session scratchpad on 2026-08-14 and died
  with it. Only its outputs and its recipes (preserved by accident, in commit
  `3bf72f3`'s message) survived, and rebuilding it on 2026-08-17 took an
  archaeology dig through a commit log.
- `docs/character-redo-spec.md` still told a future session to "locate its
  script under `red_hope/scripts/` on Somnora-East" — a filesystem that no
  longer exists under that name after the 2026-08-17 migration. The instruction
  was already stale, and the script it pointed at (`rig_colonist.py`) is the
  hard prerequisite for re-rigging any new crew mesh onto the existing 21
  animation clips.

The whole tree is 380 KB across 71 files. There was never a reason to leave it
exposed, and cherry-picking "the important one" is precisely the judgement that
failed before — so the entire directory came across, not a selection.

## What is in it

Nothing here is a supported entry point; treat it as the lane's working history
that must not be lost, alongside the maintained scripts one level up in
`scripts/gpu/`. The pieces that matter most:

- **`rig_colonist.py`** — the animation-phase rig loop. Blender headless: fits a
  13-bone armature procedurally from bounds proportions, automatic weights with
  envelope fallback, bakes Walk (24f) and Idle (48f), exports a skinned GLB that
  UE Interchange imports as a SkeletalMesh with AnimSequences. This is what
  produced the 21 walkers, and what any crew redo must re-run.
- `mesh_cleanup.py`, `mesh_cleanup_batch.py`, `mesh_qa.py`, `optimize_glb.py` —
  the cleanup/decimation stage between generation and import.
- `rh_stylelock.py`, `rh_chars*.py`, `gensprite_run.sh` — sprite generation and
  style-locking, the front of the character pipeline.
- `analyze_plate.py`, `rh_flatten.py` — early plinth/plate analysis, ancestors of
  `scripts/blender/rh_cut_plate.py`.
- `glb2fbx.py`, `obj2glb.py`, `make_contact*.py`, `render_preview.py` — format
  conversion and the contact sheets every batch was judged on.
- `bootstrap.sh`, `env.sh`, `fix_trellis2_env.sh` — the pre-TRELLIS.2 environment
  setup, kept because the Hunyuan path still has to be reproducible while any
  Hunyuan-derived asset remains in the game.

Four files (`bootstrap_trellis2.sh`, `rh_trellis2.py`, `rh_tiers_redo2.sh`,
`rh_permissive_rebake.sh`) were already backed into `scripts/gpu/trellis2/`
earlier this session and are NOT duplicated here — verified byte-identical
against the NFS copies before being dropped, which also independently confirmed
that the migration's path repointing produced exactly the same files on both
sides.

## The rule

Anything that runs on a rented box and is not reproducible from this repo is one
`terminate` away from gone. Persistent filesystems are a cache, not an archive.
