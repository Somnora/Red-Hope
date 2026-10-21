#pragma once

#include "CoreMinimal.h"

class AActor;

// Procedural Mars scenery (graphics pass 3, director verdict on the primitive
// rig: "the rocks and terrain looked really bad"): a noise-displaced faceted
// ground skirt with carved craters, icosphere boulders, and ridged mountain
// ranges replace the flat apron disc, cube rocks, and smooth cones. Pure
// presentation - the sim never reads any of this. The height field is a pure
// deterministic function of position (fixed seeds), so every run and every
// reload grows the same Mars, and placement math can sample it freely.
namespace RHMarsTerrain
{
	// Logical ground elevation (cm) at a world XY (cm). Exactly 0 across the
	// buildable flat around the colony (the sim's plane), rolling terrain
	// beyond. Far-field presentation actors (rival markers, the trade rover,
	// deposit slabs, robots) sample this so they sit ON the ground the mesh
	// shows instead of hovering at the old z=0 plane.
	REDHOPE_API float GroundZCm(double XCm, double YCm);

	// The height of the scenery ground mesh AS DRAWN at a world XY (cm): the
	// coarse faceted lattice BuildScenery emits, which departs from GroundZCm
	// between its vertices (by up to a dune's height far out). Ground-hugging
	// overlays (deposit dig sites) sit on this, so they meet the terrain the
	// player sees instead of folding through it.
	REDHOPE_API float SceneryGroundZCm(double XCm, double YCm);

	// W1: the distance (cm) from a point to the nearest peak, massif or mesa base outline the scenery drew,
	// negative when the point is inside one; the largest value when none is built (before BuildScenery has
	// run). The height field does not contain these meshes, so a deposit site is checked against this.
	REDHOPE_API float SceneryClearanceCm(double XCm, double YCm);

	// Builds the scenery mesh components onto the rig actor: ground relief +
	// basalt patches, boulders + pebbles (with crater rim rubble), and the
	// three mountain bands + mesas + the hero massif's flat foothill bench
	// (the "habitat at the base of a mountain" build spot). Call once per rig.
	REDHOPE_API void BuildScenery(AActor& Rig);

	// One scenery boulder (the same crumpled icosphere, faceted) appended to
	// caller-owned mesh arrays - deposit dig sites dress their rims with it so
	// their rocks match the scenery's.
	REDHOPE_API void AppendRock(TArray<FVector>& V, TArray<int32>& I, TArray<FVector>& N, const FVector& CenterCm, const FVector& RadiiCm, float YawDeg, uint32 Seed);
}
