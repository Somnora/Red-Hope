#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RHArtCommandlet.generated.h"

// Authors the project's code-built art assets and saves their packages (the
// RH.BuildRobotStateTree precedent: no editor GUI in this loop, MCP needs a
// live editor - a commandlet needs neither). Idempotent; re-run any time.
//
//   UnrealEditor-Cmd <proj> -run=RHArt
//
// What it does:
//   1. Configures imported *_Normal textures (TC_Normalmap, sRGB off).
//   2. Authors M_MarsSurface - triplanar (world-aligned) tiling texture with
//      matching triplanar normal mapping, for meshes with no UVs at all (the
//      procedural terrain, crumpled boulders) and stretched ISM cubes (pit
//      walls). MID params: SurfTex, NormTex, TileCm (world cm per repeat),
//      Tint, Rough, NormalStrength (0 disables the bump).
//   3. Authors M_VertexColor - shows the COLOR_0 baked vertex colors that
//      image-to-3D GLB exports carry (their importer material ignores them).
//      MID params: Tint, Rough.
//   4. Enforces bUsedWithInstancedStaticMeshes on M_Graybox (without it,
//      -game runs render ISMs with the engine DEFAULT material).
//   5. Logs receipts for imported GLB models (tris, verts, color verts,
//      bounds) - the evidence the runtime wiring scales against.
UCLASS()
class URHArtCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
