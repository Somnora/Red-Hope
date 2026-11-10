#include "RHPowerLook.h"
#include "RHDefinitionsSubsystem.h"
#include "RHFrameStats.h"
#include "RHSimWorldSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Misc/App.h"

void URHPowerLookSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ResetAll();
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		// A loaded colony starts at first sight: the sim's observer is empty after LoadColony too.
		ReloadedHandle = Sim->OnColonyReloaded.AddUObject(this, &URHPowerLookSubsystem::HandleColonyReloaded);
	}
}

void URHPowerLookSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->OnColonyReloaded.Remove(ReloadedHandle);
		}
	}
	Super::Deinitialize();
}

void URHPowerLookSubsystem::HandleColonyReloaded()
{
	ResetAll();
}

void URHPowerLookSubsystem::ResetAll()
{
	States.Reset();
	BoolLatches.Reset();
	Worsts.Reset();
	for (int32 L = 0; L < MaxLevels; ++L)
	{
		bCircSeen[L] = false;
		CircShown[L] = 0;
		CircChangedAtS[L] = 0.0;
		FloorLight[L] = 0.f;
	}
	LevelsKnown = 0;
	FloorLightKey = 0;
	bRationSeen = false;
	bRationShown = 0;
	RationChangedAtS = 0.0;
	bBankSeen = false;
	BankFrac = BankFrom = BankTo = 0.f;
	BankGlide = 1.f;
	BankRetargetS = 0.0;
	bNetShownSet = false;
	NetKindShown = 0;
	NetShownW = 0.0;
	NetShownGenW = 0.0;
	NetShownAtS = 0.0;
	bGardenSeen = false;
	GardenShown = 0;
	GardenChangedAtS = 0.0;
	GardenDarkAtSimS = -1.0e12;
	GardenDarkProducing = 0;
	bEpisodeOn = false;
	bSpellOn = false;
	bServedSettled = false;
	EpisodeOnsetS = 0.0;
	EpisodeMaxHoldS = 0.0;
}

bool URHPowerLookSubsystem::IsExempt(FName DefName, const FRHBuildingRow* Def)
{
	static const FName NLander(TEXT("Lander")), NBank(TEXT("BatteryBank")), NPylon(TEXT("Pylon"));
	return DefName == NLander || DefName == NBank || DefName == NPylon || (Def && Def->PowerDraw_W <= 0.f && Def->PowerIdle_W <= 0.f);
}

ERHPowerLook URHPowerLookSubsystem::WantLook(const FRHBuildingInstance& B, const FRHBuildingRow* Def, const URHSimWorldSubsystem& Sim) const
{
	if (B.bUnderConstruction)
	{
		return ERHPowerLook::Site;
	}
	if (B.bManualOff)
	{
		return ERHPowerLook::Off;
	}
	if (IsExempt(B.DefName, Def))
	{
		return ERHPowerLook::Powered;
	}
	switch (Sim.GetShedHold(B.Id))
	{
	case URHSimWorldSubsystem::ERHShedHold::Rationed: return ERHPowerLook::Rationed;
	case URHSimWorldSubsystem::ERHShedHold::Unserved: return ERHPowerLook::Unserved;
	default: return ERHPowerLook::Powered;
	}
}

float URHPowerLookSubsystem::GlowOf(uint8 Look)
{
	return Look == (uint8)ERHPowerLook::Powered ? 1.f : (Look == (uint8)ERHPowerLook::Rationed ? RationedGlow : 0.f);
}

void URHPowerLookSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RHFrameStats::FPowerLookScope PowerScope; // G1 frame check: the power readers' per-frame cost
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	// Real seconds at the frame's start (FApp's clock): every latch in a frame, here and in the readers' LatchValue, stamps the same time,
	// so a change is dated by the frame that shows it and the floor is measured frame to frame.
	const double NowS = FApp::GetCurrentTime();
	const float Step = FMath::Min(DeltaTime, 0.1f) / FadeS; // the most a faded value moves this frame (a long frame never jumps)
	const uint64 Frame = GFrameCounter;

	// 1. Each building's look through the latch, and its glow toward that look's level. A player's own switch (into or out of Off)
	// and a site finishing apply at once; the first sight of a building is not a change.
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		const uint8 Want = (uint8)WantLook(B, Defs->GetBuilding(B.DefName), *Sim);
		FLookState* S = States.Find(B.Id);
		if (!S)
		{
			S = &States.Add(B.Id);
			S->Look = Want;
			S->ChangedAtS = NowS;
			S->Glow = GlowOf(Want);
			S->SeenFrame = Frame;
			continue;
		}
		const bool bAtOnce = Want == (uint8)ERHPowerLook::Off || S->Look == (uint8)ERHPowerLook::Off || S->Look == (uint8)ERHPowerLook::Site;
		Latch(S->Look, S->ChangedAtS, Want, NowS, bAtOnce);
		S->Glow += FMath::Clamp(GlowOf(S->Look) - S->Glow, -Step, Step);
		S->SeenFrame = Frame;
	}

	// 2. Prune what was not seen for 60 frames (a demolished building, a card no longer asked), once a second.
	if (NowS >= NextPruneS)
	{
		NextPruneS = NowS + 1.0;
		for (auto It = States.CreateIterator(); It; ++It)
		{
			if (Frame - It.Value().SeenFrame > 60)
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = BoolLatches.CreateIterator(); It; ++It)
		{
			if (Frame - It.Value().SeenFrame > 60)
			{
				It.RemoveCurrent();
			}
		}
		for (auto It = Worsts.CreateIterator(); It; ++It)
		{
			if (Frame - It.Value().SeenFrame > 60)
			{
				It.RemoveCurrent();
			}
		}
	}

	// 3. The colony's rationing, latched (the room lights and the deck read it).
	{
		const uint8 Want = Sim->IsColonyRationing() ? 1 : 0;
		if (!bRationSeen)
		{
			bRationSeen = true;
			bRationShown = Want;
			RationChangedAtS = NowS;
		}
		else
		{
			Latch(bRationShown, RationChangedAtS, Want, NowS, false);
		}
	}

	// 3a. G2b (B6, POWER C): the colony's served share is "settled" once its running mean holds at most ServedSettleResidual of cycles
	// closed before the rationing began (the mean's own memory, never a timer: the mean moves by 1 - exp(-cycle / hold) at each closed
	// cycle, so cycles closed before the onset weigh at most exp(-closed time since the onset / hold)). Onset: LastShedS on the first
	// frame the sim rations (the latest it can be, so this errs late); the hold: the largest ShedHoldFor seen since.
	// r2: it follows the SHOWN rationing spell, never the sim's episode. The spell begins at the sim's first shed and lasts while the sim
	// rations or anything still shows rationing (the rationing latch, the shown net's kind, or a reader that printed the served share on
	// the last frame: the tile's own state latch can hold its rationing state a floor longer). A sim episode that ends and begins again
	// inside the spell keeps the onset and the settled state: the weight of the cycles closed before the first onset only shrinks, and the
	// number on screen must not fall back to MEASURING while the header still reads RATIONING (rounds 0 and 1 started over at every sim
	// onset). Measuring starts again only at the first shed after nothing shows rationing any more. O(1).
	{
		const URHSimWorldSubsystem::FRHShedColony& Colony = Sim->GetShedColony();
		const bool bSimRations = Sim->IsColonyRationing();
		const FBoolLatch* ServedKey = BoolLatches.Find(MakeKey('P', 0u));
		const bool bShown = bRationShown != 0 || (bNetShownSet && NetKindShown != 0) || (ServedKey && Frame - ServedKey->SeenFrame <= 1);
		const bool bOnset = bSimRations && !bEpisodeOn; // the sim's episode begins on this frame
		bEpisodeOn = bSimRations;
		if (bOnset && !bSpellOn)
		{
			// A new spell: measure from this onset.
			bServedSettled = false;
			EpisodeOnsetS = Colony.LastShedS;
			EpisodeMaxHoldS = 0.0;
		}
		bSpellOn = bSimRations || (bSpellOn && bShown);
		if (bSimRations)
		{
			EpisodeMaxHoldS = FMath::Max(EpisodeMaxHoldS, URHSimWorldSubsystem::ShedHoldFor(Colony.LastStepDtS));
			const double ClosedS = (Colony.LastStepEndS - EpisodeOnsetS) - Colony.CycleS;
			bServedSettled |= ClosedS > 0.0 && EpisodeMaxHoldS > 0.0 && FMath::Exp(-ClosedS / EpisodeMaxHoldS) <= ServedSettleResidual;
		}
	}

	// 3b. The shown net (G1 r2; see GetNetW): ONE latched take of (its kind, its value, the generation of the same instant). The kind is
	// the rationing shortfall while the colony rations now (the sim's own IsColonyRationing or the shown latch), else the live pair; a
	// take adopts the kind it wants with that kind's value, so a change of kind brings its own value at once, and nothing in the take
	// changes twice inside the floor. A live take is therefore never one step of the shed flag's alternation: while the colony rations
	// the take that is due is a change of kind.
	{
		const FRHPowerState& P = Sim->GetPower();
		const uint8 WantKind = (bRationShown != 0 || Sim->IsColonyRationing()) ? 1 : 0;
		const double RawW = WantKind != 0 ? -FMath::CeilToDouble(FMath::Max(0.0, Sim->GetShedColony().ShortWMean)) : P.GenW - P.LoadW;
		if (!bNetShownSet || ((WantKind != NetKindShown || RawW != NetShownW || P.GenW != NetShownGenW) && NowS - NetShownAtS >= FloorRealS))
		{
			bNetShownSet = true;
			NetKindShown = WantKind;
			NetShownW = RawW;
			NetShownGenW = P.GenW;
			NetShownAtS = NowS;
		}
	}

	// 4. Each floor's light kind - dark (its held circulation is off), rationed (the colony rations) or full - latched as ONE value, and
	// its room light toward that kind's level. One latch for both inputs, so the light never fades twice inside the floor because the
	// circulation and the rationing changed at different times.
	const bool bRationingSim = Sim->IsColonyRationing();
	const int32 Levels = FMath::Min(Sim->GetMaxDepth(), MaxLevels - 1) + 1;
	uint32 Key = HashCombine(0x47314cu, (uint32)Levels);
	for (int32 L = 0; L < Levels; ++L)
	{
		const uint8 Want = Sim->IsFloorCirculatedHeld(-L) ? (bRationingSim ? 1 : 2) : 0;
		if (!bCircSeen[L])
		{
			bCircSeen[L] = true;
			CircShown[L] = Want;
			CircChangedAtS[L] = NowS;
			FloorLight[L] = RoomLevelOf(Want);
		}
		else
		{
			Latch(CircShown[L], CircChangedAtS[L], Want, NowS, false);
			FloorLight[L] += FMath::Clamp(RoomLevelOf(CircShown[L]) - FloorLight[L], -Step, Step);
		}
		Key = HashCombine(Key, (uint32)FMath::RoundToInt(FloorLight[L] * 1000.f));
	}
	LevelsKnown = Levels;
	FloorLightKey = Key;

	// 5. The bank bar. It follows a quantity that moves continuously (and, at 60x, once a real second in era steps), so an ease that
	// settles between steps would start again every step. Instead it is re-targeted at most once per FloorRealS and glides linearly to
	// that target over FloorRealS of (clamped) frame time: either the glides chain back to back, or it rests until the next re-target, so
	// it starts or turns at most once per floor and no frame moves it by more than a thirtieth of the bar.
	{
		const FRHPowerState& P = Sim->GetPower();
		const bool bRationNow = bRationShown != 0 || bRationingSim; // never a target built from the per-step bank while it scrapes
		const float Target = bRationNow ? 0.f : (P.BatteryCapWh > 0.0 ? (float)FMath::Clamp(P.BatteryWh / P.BatteryCapWh, 0.0, 1.0) : 0.f);
		if (!bBankSeen)
		{
			bBankSeen = true;
			BankFrac = BankFrom = BankTo = Target;
			BankGlide = 1.f;
			BankRetargetS = NowS + FloorRealS;
		}
		else
		{
			if (NowS >= BankRetargetS)
			{
				BankFrom = BankFrac;
				BankTo = Target;
				BankGlide = 0.f;
				BankRetargetS = NowS + FloorRealS;
			}
			BankGlide = FMath::Min(1.f, BankGlide + FMath::Min(DeltaTime, 0.1f) / (float)FloorRealS);
			BankFrac = FMath::Lerp(BankFrom, BankTo, BankGlide);
		}
	}

	// 6. The garden (G1 r1). The grow lights take their step's watt-hours from the bank all or nothing before the grid's walk, so while
	// the bank scrapes they go dark and lit on alternate steps, and the producing count with them (the Crew panel's garden and food
	// lines and the guide's food steps read both). Shown dark, with a dark step's producing count, while a dark step was seen inside the
	// colony's hold (ShedHoldFor of the last step's length, sim seconds; the hold the machines' release waits), else the live count; the
	// pair latched as one through the floor.
	{
		const URHSimWorldSubsystem::FRHShedColony& Col = Sim->GetShedColony();
		const double SimNowS = Col.LastStepEndS;
		if (Sim->AreGrowLightsDark())
		{
			GardenDarkAtSimS = SimNowS;
			GardenDarkProducing = Sim->GetProducingCellCount();
		}
		const bool bDark = SimNowS >= GardenDarkAtSimS && SimNowS - GardenDarkAtSimS < URHSimWorldSubsystem::ShedHoldFor(Col.LastStepDtS);
		const uint32 Want = ((uint32)FMath::Max(0, bDark ? GardenDarkProducing : Sim->GetProducingCellCount()) << 1) | (bDark ? 1u : 0u);
		if (!bGardenSeen)
		{
			bGardenSeen = true;
			GardenShown = Want;
			GardenChangedAtS = NowS;
		}
		else
		{
			Latch(GardenShown, GardenChangedAtS, Want, NowS, false);
		}
	}
}

ERHPowerLook URHPowerLookSubsystem::GetLook(int32 BuildingId) const
{
	const FLookState* S = States.Find(BuildingId);
	return S ? (ERHPowerLook)S->Look : ERHPowerLook::Powered;
}

float URHPowerLookSubsystem::GetGlow(int32 BuildingId) const
{
	const FLookState* S = States.Find(BuildingId);
	return S ? S->Glow : 1.f;
}

bool URHPowerLookSubsystem::IsFloorCirculatedShown(int32 Level) const
{
	const int32 L = -Level;
	if (L >= 0 && L < LevelsKnown && bCircSeen[L])
	{
		return CircShown[L] != 0;
	}
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return Sim && Sim->IsFloorCirculatedHeld(Level);
}

float URHPowerLookSubsystem::GetFloorLight(int32 Level) const
{
	const int32 L = -Level;
	if (L >= 0 && L < LevelsKnown && bCircSeen[L])
	{
		return FloorLight[L];
	}
	return IsFloorCirculatedShown(Level) ? (bRationShown != 0 ? RationedRoomLevel : 1.f) : 0.f;
}

bool URHPowerLookSubsystem::IsRationingNow() const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return bRationShown != 0 || (Sim && Sim->IsColonyRationing());
}

double URHPowerLookSubsystem::GetNetW() const
{
	if (!bNetShownSet)
	{
		// Before the first tick of this colony: the source as Tick would first take it.
		const UWorld* World = GetWorld();
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return 0.0;
		}
		return IsRationingNow() ? -FMath::CeilToDouble(FMath::Max(0.0, Sim->GetShedColony().ShortWMean)) : Sim->GetPower().GenW - Sim->GetPower().LoadW;
	}
	return NetShownW;
}

double URHPowerLookSubsystem::GetNetGenW() const
{
	if (!bNetShownSet)
	{
		const UWorld* World = GetWorld();
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		return Sim ? Sim->GetPower().GenW : 0.0;
	}
	return NetShownGenW;
}

double URHPowerLookSubsystem::GetServedShare() const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return Sim ? FMath::Clamp(Sim->GetShedColony().ServedMean, 0.0, 1.0) : 1.0;
}

bool URHPowerLookSubsystem::LatchBool(uint64 Key, bool bWant) const
{
	return LatchValue(Key, bWant ? 1 : 0) != 0;
}

uint32 URHPowerLookSubsystem::LatchValue(uint64 Key, uint32 Want, uint32 AtOnceValue) const
{
	const double NowS = FApp::GetCurrentTime(); // the frame's time, as Tick stamps it
	FBoolLatch* L = BoolLatches.Find(Key);
	if (!L)
	{
		// First sight is not a change: adopt at once (a key is created once, when a reader first asks for it).
		L = &BoolLatches.Add(Key);
		L->Shown = Want;
		L->ChangedAtS = NowS;
		L->SeenFrame = GFrameCounter;
		return Want;
	}
	L->SeenFrame = GFrameCounter;
	Latch(L->Shown, L->ChangedAtS, Want, NowS, Want == AtOnceValue || L->Shown == AtOnceValue);
	return L->Shown;
}

uint32 URHPowerLookSubsystem::LatchWorst(uint64 Key, uint32 Want) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const double SimNowS = Sim ? Sim->GetShedColony().LastStepEndS : 0.0;
	const double HoldS = URHSimWorldSubsystem::ShedHoldFor(Sim ? Sim->GetShedColony().LastStepDtS : 0.0);
	FWorst* W = Worsts.Find(Key);
	if (!W)
	{
		W = &Worsts.Add(Key);
		W->Value = Want;
		W->AtSimS = SimNowS;
	}
	else if (!IsRationingNow() || Want >= W->Value || SimNowS < W->AtSimS || SimNowS - W->AtSimS >= HoldS)
	{
		// G1 r2: outside rationing the value itself (nothing it reads alternates then), so a fixed clash clears a floor later, paused or
		// not. While the colony rations: worse or the same, adopt and restart its hold; better, only once the worse value has not been
		// seen for a whole hold.
		W->Value = Want;
		W->AtSimS = SimNowS;
	}
	W->SeenFrame = GFrameCounter;
	return LatchValue(Key, W->Value);
}
