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

	// Builds the scenery mesh components onto the rig actor: ground relief +
	// basalt patches, boulders + pebbles (with crater rim rubble), and the
	// three mountain bands + mesas + the hero massif's flat foothill bench
	// (the "habitat at the base of a mountain" build spot). Call once per rig.
	REDHOPE_API void BuildScenery(AActor& Rig);
}
