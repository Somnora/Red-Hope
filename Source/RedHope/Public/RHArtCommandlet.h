#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RHArtCommandlet.generated.h"

// Authors /Game/RedHope/Art/M_MarsSurface in code and saves the package:
// a triplanar (world-aligned) texture material so meshes with no UVs at all
// (the procedural terrain, crumpled boulders) and wildly stretched ISM cubes
// (pit walls) can wear the director's real tiling textures. MCP cannot run
// with the editor closed and there is no editor GUI in this loop - same
// author-assets-in-code precedent as RH.BuildRobotStateTree.
//
//   UnrealEditor-Cmd <proj> -run=RHArt
//
// Parameters on the material (all MID-settable at runtime):
//   SurfTex (texture) - the tiling surface texture
//   TileCm  (scalar)  - world size of one texture repeat, in cm
//   Tint    (vector)  - multiplied over the texture; white = texture as-is
//   Rough   (scalar)  - roughness (rock ~0.95, deck plate ~0.45)
UCLASS()
class URHArtCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
