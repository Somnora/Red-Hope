# Red Hope — Art Bible

**Gate G0.1 of `premium-asset-plan.md`. Dated 2026-08-14.**
The reference card: values, names, numbers. Strategy and phases live in the
plan; *this* is what you check while making an asset. Every value below is
derived from code already in the tree, not invented — `RHCanon` and `TintFor`
in `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp` are the source of
truth, and this file must be re-derived if they change.

---

## 1. The look, in one line

**A working colony, not a showroom:** bone-white pressure hulls and slate
machinery, weathered by rust and dust, lit by the machines' own function
glows against a cold Mars sky.

Three questions every asset must answer at gameplay zoom:

| Question | Answered by | Never by |
|---|---|---|
| **What is it?** | Silhouette | Texture detail |
| **What does it do?** | Function accent color + emissive | A label |
| **Is it working?** | Motion + PoweredState emissive | Nothing (today's failure) |

If an asset needs its hover label to be identifiable, the silhouette failed.

---

## 2. Palette (authoritative — converted from the linear values in code)

### 2.1 Surface neutrals — `RHCanon`

| Name | sRGB | Linear (code) | Used for |
|---|---|---|---|
| BoneWhite | `#BFBCB3` | 0.52, 0.50, 0.45 | Pressure hulls, habitat shells, lander bodies. Sits deliberately darker than sunlit regolith so white hulls separate from ground instead of washing into it |
| DarkSlate | `#595D65` | 0.10, 0.11, 0.13 | Machinery frames, trusses, undercarriage, shadowed structure |
| RustBeam | `#B86F4B` | 0.48, 0.16, 0.07 | Structural beams, weathering, oxidised edges — the family tie across the painted batch |
| HazYellow | `#EDCE3F` | 0.85, 0.62, 0.05 | Hazard striping, handrails, warning trim, lift edges |
| PVBlue | `#4561A0` | 0.06, 0.12, 0.35 | Photovoltaic glass, deep cold-side panels |

**Wear discipline:** rust and grime are *earned* — concentrated at edges
(curvature-driven) and cavities (AO-driven), never uniform noise. `WearAmount`
0 = newly landed, 1 = decade-old. Default 0.35 for surface structures, 0.15
for interiors, 0.55 for mining/forge equipment.

### 2.2 Emissive glows — HDR, `RHCanon` (values >1 drive bloom)

| Name | Hue | Intensity | Used for |
|---|---|---|---|
| FurnaceGlow | `#FF8D2C` | 6.0× | Forge interior, smelt ports, heat |
| TealGlow | `#29FFDB` | 4.5× | Life support, battery cells, "system nominal" |
| IceGlow | `#6FC5FF` | 5.0× | Water/ice processing, cold systems |
| AmberGlow | `#FFBA3B` | 4.5× | Power grid, charge state, caution |

**Emissive floor: 0.08 × base color** on every model material — the documented
reason it exists is that Mars ambient is dark enough that unlit model faces go
to mud. Do not remove it; tune it via the master material if it reads flat.

### 2.3 Function accents — `TintFor`, one per building

These are the machine identity hues. They already drive labels and glow
identity; under the new system they also drive the material's `AccentColor`
over the accent mask, so a building's function is legible from its paint.

| Building | Accent | Building | Accent |
|---|---|---|---|
| Lander | `#EDEDF3` | Electrolyzer | `#C4A0F3` |
| SolarArray | `#89B3F9` | Stockpile | `#EDC46C` |
| BatteryBank | `#59EDD3` | ComputeModule | `#F389C4` |
| Pylon | `#F9CB3F` | Habitat | `#F9F9F9` |
| ChargePad | `#F9E16C` | AirFilter | `#95EDE7` |
| Forge | `#F9A050` | Borer | `#F3C459` |
| IceDrill | `#BCEDF9` | *(fallback)* | `#BCBCBC` |
| WaterPlant | `#89C4F9` | | |

**Rule:** the accent is *trim*, not body — target 5–15 % of visible surface
(a stripe, a panel bezel, a housing, a light ring). A building painted head to
toe in its accent reads as a toy.

---

## 3. Material family (built at G0.2)

| Master | For | Key params |
|---|---|---|
| `M_RH_Master` | All statics | BaseTex, NormTex, **MRATex** (Metallic/Roughness/AO packed R/G/B), AccentColor, AccentMask, EmissiveMask, EmissiveColor, **PoweredState** (0/1), WearAmount, EmissiveFloor (0.08) |
| `M_RH_Character` | Crew + robots | as above minus PoweredState, plus SkinTint (subtle) |
| `M_RH_Motion` | Fans, radiators, holo-panels | as Master + PanSpeed / RotSpeed inputs |

**End state: every mesh in the game wears an `MI_<name>` instance of one of
these three.** One dial changes the whole world. Two lineages exist today
(hand-built `MI_<name>` of `M_ModelTex`, and auto-generated Interchange
`Materials/Material`); both converge here.

**Texture channels, per asset:** `T_<name>_BC` (BaseColor, sRGB),
`T_<name>_N` (Normal, linear), `T_<name>_MRA` (linear, R=Metallic
G=Roughness B=AO). Three textures per asset, no more.

---

## 4. Budgets (the tier table is the budget — exceeding it is a director call)

| | Tier H (hero) | Tier S (standard) | Tier B (bulk) |
|---|---|---|---|
| Members | Crew, Lander, Forge, Habitat, Elevator, Borer | Other buildings, vehicles, big props | Rocks, scatter, background |
| Tris | ≤ 35 k | ≤ 20 k | ≤ 8 k |
| Textures | 2K BC+N+MRA | 2K set | 1K or shared atlas |
| UV space used | ≥ 85 %, texel ±20 % | ≥ 80 % | auto |
| Finishing | C1–C6, hand-checked | C2–C6 scripted | C2 + C6 |
| Collision | Convex ≤ 64 verts or authored | Auto-convex | none/box |

**Characters additionally:** ≤ 25 k body+gear, ≤ 4 bone influences per vertex,
sockets `Prop_R` `Prop_L` `Back` `Helm`, and the 10-clip contract.

**Platform target: 8 GB unified memory, Metal.** No Nanite, no Lumen dependency
for readability, no MetaHumans, no marketplace packs.

> **"No Nanite" is enforced, not aspirational.** The project sets
> `r.Nanite.ProjectEnabled=False`, but UE 5.8's Interchange GLB import enables
> Nanite **per asset** by default — and a Nanite-enabled mesh in a Nanite-off
> project renders its reduced *fallback proxy*, not its real geometry. On
> 2026-08-14 that was true of **109 of 132** art meshes, with HabitatDome drawing
> 3,962 of its 18,000 triangles. Run `scripts/unreal/rh_fix_nanite.py` after
> **every** import; see `scripts/unreal/README.md`.

---

## 5. Naming contracts (breaking one means a code diff — don't)

| Thing | Contract | Consumed by |
|---|---|---|
| Crew mesh | `RH_Walker_<face>` | `WalkerMeshPath()` |
| Crew clips | `RH_Walker_<face><Clip>` where Clip ∈ Walk, Idle, Charge, Dig, Haul, Operate, Plant, Repair, WorkBench, WorkLab | `WalkerAnimPath()` |
| Building mesh | key in `RealModelPaths` = the sim's `DefName` | `HandleBuildingAdded` |
| Material instance | `MI_<name>` | `RHArtWire`, visualizer |
| Textures | `T_<name>_BC` / `_N` / `_MRA` | master material params |
| Interaction points | `WorkPost`, `VFX_<part>`, `Label` | `RHAssetPoints` |

**Skeleton:** one canonical `RH_Crew` skeleton; bone names must preserve the
clip contract above so existing animation paths keep resolving.

---

## 6. Per-asset acceptance checklist

An asset is done when **all** of these pass:

1. **Silhouette** — identifiable at 3 zooms (close, mid, strategic) in the
   three-zoom screenshot batch, without its label.
2. **Budget** — tris, texture count/size, UV utilisation and texel density
   within its tier (`mesh_qa.py` prints the numbers; no manual eyeballing).
3. **Material** — wears an `MI_<name>` of the family; accent covers 5–15 %;
   emissive mask exists where the machine has lights/displays; MRA packed.
4. **Pivot & scale** — ground-centred pivot, real-world metres, sits flush on
   terrain with no floating or sinking at its wired footprint.
5. **Collision** — per tier; no collision on decorative sub-parts.
6. **State** — where applicable, visibly differs powered vs unpowered.
7. **Receipts** — before/after render pair + QA table committed with the gate.
8. **Battery green** — art changes must never move a sim number; the pins
   (`power: gen 50 W load 20 W`, `deposit Regolith_A: 40645 kg`) stay
   byte-identical.

---

## 7. Standing prohibitions

- **Never rig an unfinished mesh.** Rigging is the last step, after shape,
  paint, cleanup and director pick. (This rule exists because we shipped 20
  walkers built from raw generator output.)
- **Never fuse a tool into a character mesh.** Job props attach to hand
  sockets.
- **Never `shade_auto_smooth`** in Blender — it adds a modifier whose normals
  the glTF exporter silently drops. Set `polygon.use_smooth` directly.
- **Never let generation output ship raw.** Tier decides finishing depth.
- **Never put art code in `RedHopeSim`.** The sim module must not depend on
  rendering, UI, Slate, or the RedHope module.
- **Gate-D standing rule:** all player-facing morale/sickness/evacuation
  wording and iconography stays abstract, prevention-framed, never graphic,
  pending the director's framing review.
