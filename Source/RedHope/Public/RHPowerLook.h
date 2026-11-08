// G1 (2026-11-04), "nothing flashes, day or night": the one place presentation asks how the colony's power should LOOK.
//
// While the bank scrapes empty the sim's shed walk cuts and restores loads on alternate steps, so every reader of the per-step
// shed flag (bPowered, ShedCount, bDeficit, the live GenW / LoadW pair, IsFloorCirculated) strobed. Canon: "photosensitivity:
// nothing may flash". The machines really do run on the steps they are served, so a rationed machine must read as steady and dim,
// never as off. The sim's shed observer classifies each building over a hold (URHSimWorldSubsystem::GetShedHold: Powered,
// Rationed, Unserved); this subsystem adds the display exemptions, Switched off, the site look, a real-time floor (no shown state
// changes twice inside FloorRealS, at any speed) and the fades, and every reader asks it instead of the shed flag.
//
// Presentation only: it reads the sim and never writes to it. It allocates nothing per frame (its maps grow only when a building
// Id or a latch key first appears; pruning removes; per-level state is fixed arrays) and draws no random number.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHPowerLook.generated.h"

struct FRHBuildingInstance;
struct FRHBuildingRow;
class URHSimWorldSubsystem;

// The shown look of a building's power. Site and Off come from the building's own flags; the rest from the sim's held state.
enum class ERHPowerLook : uint8 { Powered = 0, Rationed = 1, Unserved = 2, Off = 3, Site = 4 };

UCLASS()
class REDHOPE_API URHPowerLookSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double FloorRealS = 3.2;          // placeholder: no element changes its shown state twice inside this (the design's 2 s, raised)
	static constexpr float FadeS = 1.0f;               // placeholder: "about 1 s"
	static constexpr float RationedGlow = 0.45f;       // placeholder: "steady dim, never dark"
	static constexpr float RationedRoomLevel = 0.30f;  // today's brownout room-light level, kept
	static constexpr int32 MaxLevels = 64;             // floors 0..-63 hold per-level state in fixed arrays

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHPowerLookSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Lander, BatteryBank, Pylon, or a def that draws nothing at all: always reads Powered (generation and storage never read
	// bPowered, so showing them shed is false; a 0 W building has nothing to ration).
	static bool IsExempt(FName DefName, const FRHBuildingRow* Def);
	// The shown (latched) look; Powered for an Id it has not seen yet.
	ERHPowerLook GetLook(int32 BuildingId) const;
	// 0..1, faded over FadeS: Powered 1, Rationed RationedGlow, else 0. 1 for an Id it has not seen yet.
	float GetGlow(int32 BuildingId) const;
	// The latched held circulation of a floor (URHSimWorldSubsystem::IsFloorCirculatedHeld): the floor's latched light kind is not dark.
	bool IsFloorCirculatedShown(int32 Level) const;
	// 0..1 faded toward the floor's latched light kind: dark (not circulated), rationed (RationedRoomLevel) or full. The kind is latched
	// as ONE value from the held circulation and the colony's rationing, so it never changes twice inside the floor because the two
	// inputs moved at different times (G1 r1).
	float GetFloorLight(int32 Level) const;
	// A hash of every floor's faded light (quantized to 1/1000), for the room lights' change key.
	uint32 GetFloorLightKey() const { return FloorLightKey; }
	// The latched URHSimWorldSubsystem::IsColonyRationing (a load that draws was cut inside its hold).
	bool IsRationingShown() const { return bRationShown != 0; }
	// The sim rations now (unlatched) or rationing is still shown: while this holds, the per-step GenW / LoadW pair alternates with the
	// shed flag, so no number on screen may be built from it (G1 r1). It picks numbers only, never a shown state's kind.
	bool IsRationingNow() const;
	// Net watts as shown, worked out once a frame in Tick (G1 r2): one latched take of the source's KIND, its value and the generation
	// of the same instant, at most once per FloorRealS. The kind: while IsRationingNow the observer's running shortfall, negated and
	// rounded up to the next whole watt (so a colony that rations never reads 0 W short: its last real shed was short, and the mean stays
	// above 0 for the whole hold), else the live GenW - LoadW. A change of kind brings the new source's value with it (after rationing
	// ends the tile reads the live net, never the last rationing figure as a red 'short'), and within a kind the value moves at most once
	// per floor (G1 r1: the per-step wanted load moved its last digits back and forth at the step rate). Every reader of the net (the
	// tile, the fold header, the spare-watts line, the vault hint) reads this value.
	double GetNetW() const;
	// The kind of the shown net: true while it is the rationing shortfall (the tile's and the fold header's rationing state).
	bool IsNetMeanShown() const { return bNetShownSet ? NetKindShown != 0 : IsRationingNow(); }
	// The generation taken at the same instant as the shown net, so a line built from both (the fold header's MAKING and USING) never
	// drifts between two takes of the net (G1 r2: USING = live GenW - a held net climbed with the morning sun and snapped back).
	double GetNetGenW() const;
	// The colony's running served share of the wanted load (0..1), counted over whole shed cycles (steady at any step length).
	double GetServedShare() const;
	// The bank bar: 0 while IsRationingNow (the bank holds nothing it keeps), else BatteryWh / BatteryCapWh, re-targeted at most once
	// per FloorRealS and glided to linearly over FloorRealS (a bank that steps once a real second at 60x reads as one steady glide).
	float GetBankFrac() const { return BankFrac; }
	// The garden as shown (G1 r1): the grow lights draw on the bank all or nothing before the grid's walk, so while it scrapes they go
	// dark and lit on alternate steps (and the producing count with them). Shown dark, with a dark step's producing count, while a dark
	// step was seen inside the colony's hold (ShedHoldFor of the last step), else the live count; latched through the floor.
	bool AreGrowLightsDarkShown() const { return (GardenShown & 1u) != 0; }
	int32 GetProducingCellsShown() const { return (int32)(GardenShown >> 1); }
	// A keyed boolean through the same floor (first sight adopts at once; later changes wait out FloorRealS). Readers build keys
	// with MakeKey: an Id cast to uint32, an FName through GetTypeHash.
	bool LatchBool(uint64 Key, bool bWant) const;
	// The same latch over a small state (a line's kind made of several inputs, latched as one so it never changes twice inside the
	// floor because two inputs moved at different times). A change into or out of AtOnceValue (a player's own switch) applies at once.
	uint32 LatchValue(uint64 Key, uint32 Want, uint32 AtOnceValue = MAX_uint32) const;
	// A count where higher is worse (a line's number that follows a per-step read, such as the layout's clashing pairs, which reads
	// this step's circulation): while the colony rations (IsRationingNow, the only time that read alternates) the worst value seen
	// inside the colony's hold (sim seconds); otherwise the value itself (G1 r2: a fixed clash showed for a whole hold, and never
	// cleared while paused). Either way latched through the floor (G1 r1).
	uint32 LatchWorst(uint64 Key, uint32 Want) const;
	static uint64 MakeKey(uint8 Kind, uint32 Value) { return ((uint64)Kind << 32) | (uint64)Value; }
	bool Debug_HasLatch(uint64 Key) const { return BoolLatches.Contains(Key) || Worsts.Contains(Key); } // G2a flash.lab.cold: is the key alive (not yet pruned)

private:
	struct FLookState
	{
		uint8 Look = 0;          // ERHPowerLook
		double ChangedAtS = 0.0; // real seconds of the last adopted change (or first sight)
		float Glow = 1.f;
		uint64 SeenFrame = 0;
	};
	struct FBoolLatch
	{
		uint32 Shown = 0;
		double ChangedAtS = 0.0;
		uint64 SeenFrame = 0;
	};
	struct FWorst
	{
		uint32 Value = 0;
		double AtSimS = 0.0;     // the sim time it was last seen at this value or worse
		uint64 SeenFrame = 0;
	};

	// The one mapping from a building to the look it wants this frame.
	ERHPowerLook WantLook(const FRHBuildingInstance& B, const FRHBuildingRow* Def, const URHSimWorldSubsystem& Sim) const;
	// The one latch: a shown state takes a new value only once FloorRealS has passed since its last change, or at once when allowed.
	template <typename TShown>
	static bool Latch(TShown& Shown, double& ChangedAtS, TShown Want, double NowS, bool bAtOnce)
	{
		if (Want == Shown || (!bAtOnce && NowS - ChangedAtS < FloorRealS))
		{
			return false;
		}
		Shown = Want;
		ChangedAtS = NowS;
		return true;
	}
	static float RoomLevelOf(uint8 Kind) { return Kind == 2 ? 1.f : (Kind == 1 ? RationedRoomLevel : 0.f); }
	static float GlowOf(uint8 Look);
	void HandleColonyReloaded();
	void ResetAll();

	FDelegateHandle ReloadedHandle;
	TMap<int32, FLookState> States;
	mutable TMap<uint64, FBoolLatch> BoolLatches;
	mutable TMap<uint64, FWorst> Worsts;
	bool bCircSeen[MaxLevels];
	uint8 CircShown[MaxLevels];      // the floor's latched light kind: 0 dark (not circulated), 1 rationed, 2 full
	double CircChangedAtS[MaxLevels];
	float FloorLight[MaxLevels];
	int32 LevelsKnown = 0;
	uint32 FloorLightKey = 0;
	bool bRationSeen = false;
	uint8 bRationShown = 0;
	double RationChangedAtS = 0.0;
	bool bBankSeen = false;
	float BankFrac = 0.f;
	float BankFrom = 0.f;
	float BankTo = 0.f;
	float BankGlide = 1.f;
	double BankRetargetS = 0.0;
	double NextPruneS = 0.0;
	bool bNetShownSet = false;
	uint8 NetKindShown = 0;          // 1 the rationing shortfall, 0 the live pair (latched with its value as one take)
	double NetShownW = 0.0;
	double NetShownGenW = 0.0;
	double NetShownAtS = 0.0;
	bool bGardenSeen = false;
	uint32 GardenShown = 0;          // (producing cells << 1) | dark, latched
	double GardenChangedAtS = 0.0;
	double GardenDarkAtSimS = -1.0e12;
	int32 GardenDarkProducing = 0;
};
