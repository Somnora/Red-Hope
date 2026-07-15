@/Users/jamesmcshane/Vaults/Main/Projects/red_hope/claude-context.md

## Repo location
Actual UE project root is `/Volumes/Unreal/red_hope/red_hope` (one level below the same-named volume folder). External volume `/Volumes/Unreal`. This IS a git repo on `main`.

## Build / drive
- The editor is driven by the Unreal MCP HTTP server declared in `.mcp.json` (`http://127.0.0.1:8000/mcp`); it must be running. Editor-side asset/DataTable work goes through MCP, not by hand.
- C++ changes require a director-driven editor compile (agents write structs/source; the human compiles). There is no MCP tool to create DataTable row structs.
- Headless self-tests: `UnrealEditor-Cmd <proj> -run=RHSim -sols=N [-crew|-habitat|-vault|-borer|-rooms|-garden|-luxury]`. Run these + a live smoke before considering an increment done; the multi-sol regression baseline must stay identical.

## Hard rules
- Never make `RedHopeSim` depend on rendering, UI, Slate, or the `RedHope` module — that boundary is the core architecture rule.
- Do not enable MassAI dynamic StateTree processors; keep `ai.mass.DynamicSTProcessorsEnabled=0`. All robot tree ticking is owned by `URHRobotBrainProcessor` on the fixed sub-step (determinism).
- CSV in `docs/data/RH_*.csv` and the in-editor DataTable rows must match; live-sync DT after CSV edits — "pure-data" verifiers fail on drift.
- All player-facing morale/sickness/evacuation wording and iconography is PLACEHOLDER pending the director's Gate-D framing review; keep it abstract, prevention-framed, never graphic.
- Working cadence: commit each gate separately with a detailed message; stop for director review at gate boundaries.

## Secrets
`Config/DefaultEngine.ini` contains an AndroidFileServer `SecurityToken` — do not echo or propagate it.
