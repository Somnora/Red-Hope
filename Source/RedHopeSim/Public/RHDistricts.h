#pragma once

#include "CoreMinimal.h"

// C5a (living city gate 5a, 2026-10-19): districts. Pure data and pure
// functions - no UObject, no world - so the same code answers the paint ghost's
// preflight, the uplink's execution, the City panel and the headless -districts
// suite. A district is the player's zoning of ground or of a whole floor into a
// KIND (what the city is meant to do there); it carries a NAME so it can later
// have interests and a voice (the politics ruling), and a region id so several
// regions (step 4) change nothing. Nothing here acts on the colony: a district
// is identity, membership and a demand readout until the growth gate (C5b).

// The fixed set of kinds. Spelled once, here; every comparison goes through
// RHDistricts::ResolveKind so a stray spelling can never found a sixth kind.
namespace RHDistricts
{
	// Homes | Industry | Farming | Research | Civic.
	REDHOPESIM_API const TArray<FName>& GetKinds();
	// The canonical FName for a kind, or NAME_None if Kind is not one of the five.
	REDHOPESIM_API FName ResolveKind(FName Kind);

	// The per-order cap on a surface paint rectangle (cells a side). The coverage
	// check scans buildings once per cell and the ghost runs every frame.
	constexpr int32 MaxPaintSide = 32;
	// A district's name is capped at this many characters (after trimming).
	constexpr int32 MaxNameLength = 32;
	// The generated names: about forty real Martian place names, walked by district
	// Id. After the list runs out the walk starts again with the suffix "II", then
	// "III", and so on.
	REDHOPESIM_API int32 GetNameCount();
	REDHOPESIM_API FString NameForId(int32 DistrictId);
	// The roman numeral for 2..3999 (the suffix); empty below 2.
	REDHOPESIM_API FString RomanSuffix(int32 N);
	// What the sim stores for a typed name: control characters dropped, trimmed,
	// capped at MaxNameLength characters and trimmed again. False (the sim refuses
	// the order) if nothing is left. Names are FStrings and compare case-sensitively:
	// "Gale" and "gale" are two names (FName would fold them into one).
	REDHOPESIM_API bool NormalizeName(const FString& Typed, FString& OutName);
}

// A district. Surface districts are sets of 10 m LATTICE cells (the world 10 m
// grid the habitat bays ride: a cell spans [10k - 5, 10k + 5) m a side, centred on
// multiples of 10 m); an underground district is a WHOLE FLOOR (a floor is a
// carved-cell count, not a shape; finer shapes come with going deep).
struct REDHOPESIM_API FRHDistrict
{
	int32 Id = 0;               // never reused
	FName Kind;                 // one of RHDistricts::GetKinds()
	FString Name;               // generated, renamable (an FString: names are case-sensitive)
	int32 RegionId = 0;         // several regions are step 4; the field rides along from the start
	int32 Level = 0;            // 0 = surface; < 0 = the whole of that underground floor
	TArray<FIntPoint> Lattice;  // surface only, sorted by (X, Y), no duplicates
	// C5b: the player's growth switch. On (the default), a Homes or Civic district is built up
	// by the colony itself when it has demand and material; off, the colony leaves it alone (a
	// project already open still finishes). Saved with the district (v38).
	bool bGrowth = true;

	bool IsFloor() const { return Level < 0; }
};

// One district's counts (GetDistrictCounts), in the order of GetDistricts().
struct FRHDistrictCounts
{
	int32 DistrictId = 0;
	int32 Cells = 0;       // surface: lattice cells painted; floor: carved cells
	int32 Buildings = 0;   // buildings (sites included) that belong to it
	int32 Bays = 0;        // bays of the built habitat shells that belong to it
	int32 Residents = 0;   // colonists whose home belongs to it
	int32 Beds = 0;        // certified beds (the GetHousingCapacity rule: rated homes only)
	int32 Seats = 0;       // work seats at the stations in it (rated homes only)
};

// What a PaintDistrict order would do, for the ghost.
struct FRHDistrictPaintPreview
{
	FIntPoint Min = FIntPoint::ZeroValue; // the rectangle in lattice cells, inclusive
	FIntPoint Max = FIntPoint::ZeroValue;
	int32 CellsInRect = 0;
	int32 CellsNew = 0;        // cells the order would add (rect cells no district holds yet)
	int32 CellsOutside = 0;    // rect cells outside grid coverage
	int32 JoinId = 0;          // the district it would join (keeps its Id); 0 = founds a new one
	bool bFloor = false;       // a whole-floor order
};

// One service room function's coverage gap (the C2a coverage rule): a function's
// rooms cover min(1, rooms x RoomServesResidents / population) of the colony.
struct FRHServiceGap
{
	FName Function;
	int32 Rooms = 0;
	double Weight = 0.0;    // the function's MoraleWeight
	double Coverage = 0.0;  // 0..1
	double Gap = 1.0;       // 1 - Coverage
};

// The city's demand, colony-wide (C5a): a pure function of state. The deck
// reads it and the sim does not act on it yet. Each bar is 0 (nothing missing)
// to 1 (all of it missing).
struct FRHCityDemand
{
	int32 Population = 0;

	// --- Housing (the arrivals' own limits; see GetImmigrationCount) ---
	// Settlers' room is min(FreeBeds - ReserveBeds, ComfortCapacity - Population);
	// the Big Arrival's gate is FreeBeds >= BigArrivalFreeBeds. FreeBeds are CERTIFIED
	// beds (floors that rate, and rated surface habitats once the vault is certified);
	// ComfortCapacity counts every live Living bed on rated ground, surface or not -
	// so before the vault is certified surface beds lift the comfort term but not the
	// free-bed one, and the binding term is FreeBeds.
	int32 FreeBeds = 0;
	int32 ComfortCapacity = 0;
	bool bVaultCertified = false;
	bool bSettlersDraw = false;     // smoothed Hope reaches ImmigrationHopeMin: the want is the next ship's settlers
	int32 SettlersWanted = 0;       // the next ship's settlers if room allowed (bSettlersDraw), else one Crew Pod
	int32 SettlerRoom = 0;          // what the arrivals' room terms allow
	int32 SettlerBedsShort = 0;     // beds missing for them: max over the free-bed and comfort terms
	bool bBigArrivalPending = false; // the streak is running and the wave has not landed
	int32 BigArrivalBedsShort = 0;  // BigArrivalFreeBeds - FreeBeds, floored at 0
	int32 HousingBedsShort = 0;     // the headline: the larger of the two shortfalls
	int32 HousingBedsWanted = 0;    // the bed count that shortfall is measured against
	FName HousingLimit;             // None | FreeBeds | Comfort | BigArrival: the term that sets the headline
	double Housing = 0.0;           // HousingBedsShort / HousingBedsWanted, 0..1

	// --- Jobs ---
	int32 FilledSeats = 0;          // colonists at a work post
	int32 TotalSeats = 0;           // seats at every working station
	int32 Unemployed = 0;           // max(0, Population - FilledSeats)
	int32 SeatsShort = 0;           // max(0, Population - TotalSeats): seats the city lacks
	double Jobs = 0.0;              // Unemployed / Population

	// --- Services ---
	// Every room function with MoraleWeight > 0 (Living is housing, counted above),
	// worst gap first (ties: heavier weight, then name). The bar is the share of the
	// Rooms bonus the city leaves on the table: 1 - sum(weight x coverage) / sum(weight).
	TArray<FRHServiceGap> ServiceGaps;
	double Services = 0.0;

	// --- Air ---
	// Homes on short rations (the C2a ration under 1, whatever its scarcest leg) and
	// the oxygen margin in sols per home: the home's O2 fill over what its residents
	// breathe in a sol.
	int32 Homes = 0;
	int32 HomesShort = 0;
	int32 HomesShortOnAir = 0;
	int32 ResidentsShort = 0;       // residents of the short homes
	FName ScarcestLeg;              // the leg short in the home with the lowest ration (Oxygen, Water, Food), else None
	double MinOxygenMarginSols = -1.0; // the tightest home's margin; -1 when there is no home with residents
	double Air = 0.0;               // ResidentsShort / Population
};
