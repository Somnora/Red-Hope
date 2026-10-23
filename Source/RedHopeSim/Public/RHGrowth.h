#pragma once

#include "CoreMinimal.h"

// C5b (living city gate 5b, 2026-10-19): growth. Pure data, like RHDistricts: a growth
// PROJECT is the record of one thing the colony builds for itself inside a district - a new
// habitat module beside a rated surface network, new carved cells on a rated floor, or a
// service room zoned and furnished in a free cell or bay. It is a project RECORD, not an
// order and not a construction site: its whole bill is taken from the holdings the moment
// it starts, a linear RemainingSols countdown runs identically in the agent band and the
// era band, and at the end the sim applies the result directly (adds the building, carves
// the cells, zones and furnishes them, pre-fills the new air). No uplink lag, no robots,
// nothing for CanEnterEraMode to refuse.

enum class ERHGrowthKind : uint8
{
	SurfaceModule = 0, // Homes, surface: the next HabModule of at most 2 x 2 bays beside a rated network
	FloorCells = 1,    // Homes, underground: GrowthCellsPerProject new carved cells on a rated floor
	CivicRoom = 2,     // Civic: one free cell or bay zoned as the worst-covered CivicService room
	DeepCells = 3,     // D6b, Homes inside a floor SHAPE: up to GrowthCellsPerProject points of ONE floor of the shape, free carved cells zoned and
	                   // eligible rock carved first (Points names them, as lattice points in the underground frame)
};

struct REDHOPESIM_API FRHGrowthProject
{
	int32 DistrictId = 0;                 // one open project a district at most; the list is kept in ascending DistrictId
	ERHGrowthKind Kind = ERHGrowthKind::SurfaceModule;

	// --- Target ---
	int32 Level = 0;                      // 0 = surface; < 0 = the floor
	// SurfaceModule: the module's min corner lattice cell and its size in bays; HabId is the
	// ROOT of the network it extends (its lowest shell Id, the key the sim names a network by).
	FIntPoint Cell = FIntPoint::ZeroValue;
	FIntPoint Size = FIntPoint(1, 1);
	int32 HabId = 0;                      // SurfaceModule: the target network's root; CivicRoom on the surface: the shell holding the bay
	// FloorCells: Cells new cells go on at the end of the floor, FirstCell being the index of the first
	// (informational: the carve appends wherever the floor ends when the project finishes).
	int32 Cells = 0;
	int32 FirstCell = INDEX_NONE;
	// CivicRoom: the cell (floor) or bay (HabId's shell) to zone, or - bCarve - a cell to carve first at
	// the end of the floor; Room is the room row to zone.
	int32 Index = INDEX_NONE;
	bool bCarve = false;
	FName Room;
	// D6b: lattice points (region 0's underground frame, never ordinals) on Level, for a project inside a floor shape. DeepCells: the
	// points to turn into Living cells, the free carved cells (to zone) first, then the uncarved ones (to carve) from FirstCell on;
	// a Civic project inside a shape holds its one cell (carved already, or to be carved when bCarve). Empty for every other project.
	TArray<FIntPoint> Points;

	// --- Money and time ---
	TMap<FName, double> Bill;             // resource -> kg, taken whole at the start: Struct, Shielding, furniture, the O2 fill of new bays or cells
	double TotalSols = 0.0;
	double RemainingSols = 0.0;

	// 0 at the start, 1 at the end.
	double Progress() const { return TotalSols > 1.0e-9 ? FMath::Clamp(1.0 - RemainingSols / TotalSols, 0.0, 1.0) : 1.0; }
	// The Living sites (cells or bays) a Homes project adds; 0 for a Civic one.
	int32 HomeSites() const
	{
		return Kind == ERHGrowthKind::SurfaceModule ? FMath::Max(1, Size.X) * FMath::Max(1, Size.Y) : ((Kind == ERHGrowthKind::FloorCells || Kind == ERHGrowthKind::DeepCells) ? Cells : 0);
	}
};

// E1 (REVISION R8): what a completed project put back into holdings - the unplaced pieces credited to the Lander and the oxygen
// written into air. The bill less these is what the project used.
struct REDHOPESIM_API FRHGrowthApplied
{
	TMap<FName, double> Credited;
	double AirO2Kg = 0.0;
};
