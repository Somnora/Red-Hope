#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSaveFrame.h"
#include "RHAgentSubsystem.h"
#include "RHAgentFragments.h"
#include "RHRobotMovement.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"

// D7 atriums self-test (docs/qa/2026-10-23/d7-atriums-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), the REFRESH AT 31ffa6f, REVISION R1-R12;
// docs/qa/2026-10-26/preflight/D7-brief.md 2.11; ORCHESTRATOR-RULINGS 1, 3; CROSS-CHECK section 6 items 4, 11, 15). Fails loudly. `-atriums`.
// An atrium converts a finished cavern of two or three floors: on every floor of its box but the deepest the box cells are open (flagged,
// never removed). Every number below is derived from the rows or written out by hand here. The headless world holds the base colony's Mass
// robots (it does not tick them); AT9 moves two of them by hand, as TR18 (iii) does, to test the load's robot repairs.
//  AT0 the rows and the switch (and every new key field 0 again after an open and a close; the default frame's 4-byte payload in a saved
//  file); AT1 every refusal 1-10 by its reason (refusal 5 by each kind and branch; refusal 10 over an atrium's open cells, which refusals 6, 8
//  and 9 do not count twice, each in a setting where a double count would decide) and an all-void dig accepted; AT2 the critical rule
//  (ordinals and points around an open, a close, a save and a load, and more carving); AT3 the open set; AT4 the consumers (zone, furnish,
//  module, circulator, an off-centre footprint, a queued Build and a queued Zone order, growth in a Homes and a Civic shape over the box
//  against a twin, the footholds, both exits of the bed count, an open cell's own beds, the slot total, the home cells on mapped floors - one
//  by a tram line, against the hand values - and the twin on floors with no open cell); AT5 facing (and a ring corner that does not face);
//  AT6 the Hope term (DP8's 10 of 40 with Shelter, and the Total chain with 1..9 seat-only heads); AT7 air (refusal 8 against the breathing
//  reserve; refusal 9 skips the hall floor); AT8 walking (and IsRouteReachable from an open cell); AT9 the save (option (i): every
//  structural failure is one Warning on the early path and on the tail-only path, the kept atrium named; every doctored copy resealed by
//  RHSaveFrame::Reseal; a well-formed list past the cap; the occupancy repair; the previous version; a lost frame; the tail read's fallback;
//  a robot saved on an open cell, on both paths); AT10 era parity (the premium share on a drained pool, the premium leak in both bands, 10
//  sols with the residents supplied and the atrium term live); AT11 -RHVerifyCaches (the facing count's own key, and the preflight's two
//  memos on a reuse) and the facing count's rebuilds over a window that reads it every step; AT12 close; AT13 a shape and an atrium on one
//  floor (and the walk landing).
// Whole-file sizes are printed only in the masked "(N bytes" form (cross-check item 15).
namespace
{
	// Counts LogRedHopeSim lines of each verbosity while it is attached (option (i): a structural failure is one Warning and never an Error), and
	// the RHVERIFY Errors (AT11).
	struct FRHAtriumLogCounter : public FOutputDevice
	{
		int32 Warnings = 0, Errors = 0, Verify = 0;
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != FName(TEXT("LogRedHopeSim")))
			{
				if (Verbosity == ELogVerbosity::Error) { ++Errors; }
				return;
			}
			if (Verbosity == ELogVerbosity::Warning) { ++Warnings; Lines.Add(Text); }
			if (Verbosity == ELogVerbosity::Error) { ++Errors; Lines.Add(Text); }
			if (FCString::Strstr(Text, TEXT("RHVERIFY"))) { ++Verify; }
		}
	};

	// Replaces the payload of the frame under Tag (found by the walk from the end) with NewPayload, rewriting its header's and footer's lengths
	// (the CRC left 0), then reseals it with the house helper RHSaveFrame::Reseal, which finds the frame by the same walk (its lengths, not its
	// CRC) and rewrites the CRC. The frames after it keep their bytes. False when no frame is found or the reseal does not find it.
	bool ReplaceFramePayload(TArray<uint8>& File, uint32 Tag, const TArray<uint8>& NewPayload)
	{
		int32 At = 0, Bytes = 0;
		if (!RHSaveFrame::Find(File, Tag, At, Bytes))
		{
			return false;
		}
		TArray<uint8> Out;
		Out.Append(File.GetData(), At);
		const uint32 Len = (uint32)NewPayload.Num(), NoCrc = 0u;
		Out.Append((const uint8*)&Tag, 4);
		Out.Append((const uint8*)&Len, 4);
		Out.Append((const uint8*)&NoCrc, 4);
		Out.Append(NewPayload);
		Out.Append((const uint8*)&Len, 4);
		Out.Append((const uint8*)&Tag, 4);
		const int32 After = At + RHSaveFrame::HeaderBytes + Bytes + RHSaveFrame::FooterBytes;
		Out.Append(File.GetData() + After, File.Num() - After);
		File = MoveTemp(Out);
		return RHSaveFrame::Reseal(File, Tag);
	}

	TArray<uint8> AtriumPayload(std::initializer_list<int32> Values)
	{
		TArray<uint8> P;
		for (const int32 V : Values) { P.Append((const uint8*)&V, 4); }
		return P;
	}

	// A well-formed payload past the cap: the count Count and the Ids 1..Count ascending (4 + 4 x Count bytes), so only the cap refuses it.
	TArray<uint8> AtriumPayloadAscending(int32 Count)
	{
		TArray<uint8> P;
		P.Append((const uint8*)&Count, 4);
		for (int32 Id = 1; Id <= Count; ++Id) { P.Append((const uint8*)&Id, 4); }
		return P;
	}
}

int32 RHRunAtriumsSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== ATRIUMS TEST (D7, 2026-10-29) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ATRIUMS ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("ATRIUMS FAIL: %s"), *What); ++Fails; }
	};
	const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NOxygen(TEXT("Oxygen")), NStruct(TEXT("Struct")), NHomes(TEXT("Homes")), NCivic(TEXT("Civic")), NLiving(TEXT("LivingQuarters"));
	const FName NWater(TEXT("Water")), NFood(TEXT("Food"));
	URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>();
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const auto Finish = [&]() -> int32
	{
		Expect(Sim->GetFloorCellFallbacks() == 0, TEXT("AT over the whole suite not one ordinal read fell outside its floor and back to the spiral"));
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ATRIUMS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	// Every fixture that fits a lift car pins the bank before each step (D6 RC 9); every step here does.
	const auto EraOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep((float)EraDt);
	};
	const auto AgentOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->Debug_StepSim(AgentDt);
	};
	const auto BuildingOf = [&](int32 Id) -> const FRHBuildingInstance* { for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } } return nullptr; };
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Origin = [&]() { return Sim->GetShaftHeadCm(); };
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Origin(), FIntPoint(X, Y)); };
	const auto Ord = [&](int32 Level, int32 X, int32 Y) { return Sim->FindFloorCell(Level, FIntPoint(X, Y)); };
	const auto Place = [&](const TCHAR* Def, int32 X, int32 Y, int32 Level)
	{
		Sim->Debug_PlaceInstant(FName(Def), PtCm(X, Y), Level);
		return LastIdOf(Def);
	};
	const auto Send = [&](const FRHCommand& C)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		return Rejects.Num() == R0;
	};
	const auto AtriumCmd = [](bool bOpen, int32 CavernId)
	{
		FRHCommand C;
		C.Verb = bOpen ? FName("OpenAtrium") : FName("CloseAtrium");
		C.Value = CavernId;
		return C;
	};
	const auto ShapeOf = [&](FName Kind, int32 Level, int32 Floors, const FIntPoint& A, const FIntPoint& B)
	{
		FRHCommand C;
		C.Verb = FName("PaintDistrict");
		C.Target = Kind;
		C.Level = Level;
		C.Value = Floors;
		C.Location = PtCm(A.X, A.Y);
		C.LocationB = PtCm(B.X, B.Y);
		return C;
	};
	const auto ShapeCmd = [&](int32 Level, int32 Floors, const FIntPoint& A, const FIntPoint& B) { return ShapeOf(NHomes, Level, Floors, A, B); };
	// Where a robot stands on a cell of a floor (the sim's walking plane: the floor's Z plus the robot offset).
	const auto RobotCm = [&](int32 Level, int32 X, int32 Y) { FVector C = PtCm(X, Y); C.Z = Level * Sim->GetFloorHeightCm() + RH::RobotZOffsetCm; return C; };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("atriumbase"), SaveErr);
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString LoadErr;
		const bool bOk = Sim->LoadColony(TEXT("atriumbase"), LoadErr);
		if (!bOk || !LoadErr.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("ATRIUMS FAIL: loading the base failed: %s"), *LoadErr); ++Fails; }
		Rejects.Reset();
		Sim->Debug_ForgetRobotTransit();
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetColonistsPerCell(1.0);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const FVector Head0(2000.0, 2000.0, 0.0);
	// The residents: N colonists housed (on a generous bed count) and then homed on HomeLevel, with no station.
	const auto AddResidents = [&](int32 N, int32 HomeLevel, double Cpc = 1.0) -> TArray<int32>
	{
		Sim->Debug_SetColonistsPerCell(100.0);
		const int32 Before = Sim->GetPopulation();
		Sim->Debug_AddColonists(N);
		Sim->Debug_SetColonistsPerCell(Cpc);
		TArray<int32> Ids;
		const TArray<FRHColonist>& All = Sim->GetColonists();
		for (int32 i = Before; i < All.Num(); ++i)
		{
			Sim->Debug_SetColonistHome(All[i].Id, 0, HomeLevel);
			Ids.Add(All[i].Id);
		}
		return Ids;
	};
	const auto Fit = [&](int32 S)
	{
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString W;
		return Sim->Debug_FitLift(S, W);
	};
	// The box colony: shaft 0 to -6 at Head0 (column (0,0)); the eight spiral cells around the column on -1, -2 and -3 (a rated floor -5 of 60
	// cells takes the colonists in); a 3 x 3 x 3 cavern (2,-1) to (4,1) on -1..-3 beside the spiral, carved to the end; an Air Filter on -1..-3
	// at (-1,-1), each floor rated; oxygen in the store. Cavern 1.
	const auto BoxColony = [&](int32& OutCavern) -> bool
	{
		Reset();
		Sim->ExtendShaft(6, Head0);
		bool bOk = true;
		for (int32 L = 1; L <= 3; ++L) { bOk &= Sim->ExcavateFloor(-L, 8, R, 0); }
		bOk &= Sim->ExcavateFloor(-5, 60, R, 0);
		OutCavern = INDEX_NONE;
		bOk &= Sim->Debug_DigCavern(-1, 3, FIntPoint(2, -1), FIntPoint(4, 1), OutCavern, Why) && Sim->Debug_CarveCavernCells(OutCavern, 27) == 27;
		for (const int32 L : { 1, 2, 3, 5 })
		{
			Place(TEXT("AirFilter"), -1, -1, -L);
			Sim->Debug_ForceRateFloor(-L);
		}
		Sim->Debug_AddStock(NOxygen, 200000.0);
		return bOk && OutCavern != INDEX_NONE;
	};
	const auto Open = [&](int32 CavernId) { FString W; return Sim->Debug_OpenAtrium(CavernId, W); };
	const auto Close = [&](int32 CavernId) { FString W; return Sim->Debug_CloseAtrium(CavernId, W); };
	const auto Cells = [&](int32 Level) { TArray<FIntPoint> Out; if (const FRHFloor* Fl = Sim->FindFloor(Level)) { Out = Fl->GetCells(); } return Out; };
	const auto BoxOrds = [&](int32 Level, const FIntPoint& Mn, const FIntPoint& Mx)
	{
		TArray<int32> Out;
		for (int32 X = Mn.X; X <= Mx.X; ++X) { for (int32 Y = Mn.Y; Y <= Mx.Y; ++Y) { const int32 O = Ord(Level, X, Y); if (O != INDEX_NONE) { Out.Add(O); } } }
		Out.Sort();
		return Out;
	};
	const auto HopeChain = [](const URHSimWorldSubsystem::FRHHopeBreakdown& T, double Atrium)
	{
		// The Total expression as GetColonyHope writes it, from a twin's terms with Atrium as the last addend.
		return FMath::Clamp(T.Base + T.Housing + T.Rooms + T.Jobs + T.Milestones + T.Comforts + T.Furnishings + T.Views + T.Solidarity + T.Shelter + Atrium
			- T.AdjacencyPenalty - T.UnsupportedPenalty - T.WaterPenalty - T.FoodPenalty - T.CrisisPenalty - T.CommutePenalty - T.WarmPenalty, 0.0, 100.0);
	};
	const auto SameTerms = [](const URHSimWorldSubsystem::FRHHopeBreakdown& A, const URHSimWorldSubsystem::FRHHopeBreakdown& B)
	{
		return A.Base == B.Base && A.Housing == B.Housing && A.Rooms == B.Rooms && A.Jobs == B.Jobs && A.Milestones == B.Milestones && A.Comforts == B.Comforts
			&& A.Furnishings == B.Furnishings && A.Views == B.Views && A.Solidarity == B.Solidarity && A.Shelter == B.Shelter && A.AdjacencyPenalty == B.AdjacencyPenalty
			&& A.UnsupportedPenalty == B.UnsupportedPenalty && A.WaterPenalty == B.WaterPenalty && A.FoodPenalty == B.FoodPenalty && A.CrisisPenalty == B.CrisisPenalty
			&& A.CommutePenalty == B.CommutePenalty && A.WarmPenalty == B.WarmPenalty;
	};
	const double Share100 = 100.0 * 0.5; // one open cell's premium fill at the rows: O2FillKgPerCell 100 x (1500 - 1000) / 1000

	Expect(bBaseSaved, FString::Printf(TEXT("AT0 the fresh colony is saved as the base every case loads (%s)"), *SaveErr));

	// --- AT0. The rows and the switch: with no atrium every new read is inert. ---
	{
		Reset();
		Expect(Sim->GetHopeAtriumMax() == 3.0 && Sim->GetAtriumFillPermille() == 1500 && Sim->GetAtriumLeakPermille() == 2000,
			FString::Printf(TEXT("AT0 the rows read HopeAtriumMax %.1f, AtriumFillPermille %d, AtriumLeakPermille %d (placeholders, director review)"), Sim->GetHopeAtriumMax(), Sim->GetAtriumFillPermille(), Sim->GetAtriumLeakPermille()));
		Expect(DefsSub && DefsSub->GetConfigTableScalar(FName("HopeAtriumMax"), -1.0) == 3.0 && DefsSub->GetConfigTableScalar(FName("AtriumFillPermille"), -1.0) == 1500.0
			&& DefsSub->GetConfigTableScalar(FName("AtriumLeakPermille"), -1.0) == 2000.0, TEXT("AT0 and the three rows are in DT_Config with the CSV's values"));
		int32 Cavern = INDEX_NONE;
		const bool bBox = BoxColony(Cavern);
		const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
		Sim->ServingShaft(-1, FIntPoint(1, 0));
		Sim->GetColonyHope();
		TArray<uint32> Keys;
		Sim->Debug_AtriumKeyFields(Keys);
		bool bKeysZero = Keys.Num() == 7;
		for (const uint32 K : Keys) { bKeysZero &= K == 0u; }
		Expect(bBox && Sim->GetAtriums().Num() == 0 && Sim->GetAtriumsVersion() == 0u && H.Atrium == 0.0 && !FMath::IsNegativeOrNegativeZero(H.Atrium) && H.FacingHeads == 0
			&& Sim->GetFloorOpenCells(-1) == 0 && Sim->GetAtriumFacingHeads() == 0 && !Sim->IsCellOpen(-1, 0) && !Sim->IsCellFacing(-1, 0) && !Sim->Debug_IsAtriumCacheBuilt()
			&& !Sim->Debug_IsAtriumFacingBuilt() && bKeysZero && Sim->GetAtriumBlockBytes() == 4,
			TEXT("AT0 a finished 3 x 3 x 3 cavern and no atrium: Atrium +0.0, no facing head, no open cell, the caches unbuilt, every new key field 0, a 4-byte payload"));
		Expect(Sim->GetFloorO2RequiredKg(-1) == (double)Sim->GetFloorCarvedCells(-1) * 100.0 && Sim->GetFloorO2LeakKgPerSol(-1) == (double)Sim->GetFloorCarvedCells(-1) * 2.0,
			FString::Printf(TEXT("AT0 a floor's air is Cells x 100 and Cells x 2 exactly (%.1f kg, %.1f kg a sol)"), Sim->GetFloorO2RequiredKg(-1), Sim->GetFloorO2LeakKgPerSol(-1)));
		// The frame actually written by a colony with no atrium: found by tag after D6e's 'RHD6', a 4-byte payload holding the count 0.
		FString Err0;
		const bool bSaved0 = Sim->SaveColony(TEXT("atr0"), Err0);
		TArray<uint8> File0;
		FFileHelper::LoadFileToArray(File0, *SaveFilePath(TEXT("atr0")));
		int32 DAt0 = INDEX_NONE, DBytes0 = 0, AAt0 = INDEX_NONE, ABytes0 = -1, Count0 = -1;
		const bool bD0 = RHSaveFrame::Find(File0, RHSaveFrame::TransitPowerTag, DAt0, DBytes0);
		const bool bA0 = RHSaveFrame::Find(File0, RHSaveFrame::AtriumTag, AAt0, ABytes0);
		if (bA0 && ABytes0 >= 4) { FMemory::Memcpy(&Count0, File0.GetData() + AAt0 + RHSaveFrame::HeaderBytes, 4); }
		Expect(bSaved0 && bD0 && bA0 && DAt0 + RHSaveFrame::HeaderBytes + DBytes0 + RHSaveFrame::FooterBytes == AAt0 && ABytes0 == 4 && Count0 == 0,
			FString::Printf(TEXT("AT0 the saved file of a colony with no atrium carries the 'RHD7' frame after 'RHD6' with a %d-byte payload, the count %d (the 24-byte default frame)"), ABytes0, Count0));
	}
	{
		// The key fields are gated, not merely unset: every cache D7 keys is built while an atrium is open (each field reads AtriumsVersion), and
		// built again after the close, when the list is empty but AtriumsVersion is 2 (each field reads 0). The caches: the deep net and the dig
		// labels of every fitted floor (a lift car), a robot's charge reserve memo, the deep homes, the membership and commute keys and the walk
		// network (a Homes shape maps -1).
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1) && Fit(0) && Send(ShapeCmd(-1, 1, FIntPoint(60, 60), FIntPoint(60, 60)));
		FRHRobotFragment Bot;
		if (const FRHRobotRow* Def = DefsSub ? DefsSub->GetRobot(FName("RC_H")) : nullptr)
		{
			Bot.DefName = FName("RC_H");
			Bot.RobotClass = Def->RobotClass;
			Bot.SpeedMps = Def->Speed_mps;
			Bot.CargoCapKg = Def->Cargo_kg;
			Bot.WorkRate = Def->WorkRate;
			Bot.DrawMoveW = Def->DrawMove_W;
		}
		const FMassEntityHandle BotHandle(900001, 1);
		const auto BuildAll = [&](TArray<uint32>& OutKeys, FString& OutBuilt)
		{
			TArray<FIntVector> Portals;
			Sim->Debug_GetPortals(Portals);
			const int32 Landing = Sim->Debug_WalkLanding(-1, 0);
			const int32 Homed = Sim->Debug_GetDeepHomeCells().Num();
			int32 PadId = 0;
			FVector PadCm = FVector::ZeroVector;
			Sim->FindNearestChargePad(RobotCm(-1, -1, 0), PadId, PadCm);
			Sim->DeepReserveWh(BotHandle, Bot, RobotCm(-1, -1, 0), PadId, PadCm);
			Sim->Debug_AtriumKeyFields(OutKeys);
			OutBuilt = FString::Printf(TEXT("%d portals, landing %d, %d homed, %d reserve memo(s)"), Portals.Num(), Landing, Homed, Sim->Debug_GetReserveMemoCount());
			return Portals.Num() > 0 && Landing != INDEX_NONE && Sim->Debug_GetReserveMemoCount() > 0;
		};
		bOk &= Open(C1);
		const uint32 VOpen = Sim->GetAtriumsVersion();
		TArray<uint32> KeysOpen, KeysClosed;
		FString BuiltOpen, BuiltClosed;
		bOk &= BuildAll(KeysOpen, BuiltOpen);
		bOk &= Close(C1);
		bOk &= BuildAll(KeysClosed, BuiltClosed);
		bool bOpenLive = KeysOpen.Num() == 7, bClosedZero = KeysClosed.Num() == 7;
		FString Fields;
		for (int32 k = 0; k < KeysOpen.Num() && k < KeysClosed.Num(); ++k)
		{
			bOpenLive &= KeysOpen[k] == VOpen;
			bClosedZero &= KeysClosed[k] == 0u;
			Fields += FString::Printf(TEXT("%s%u/%u"), k ? TEXT(", ") : TEXT(""), KeysOpen[k], KeysClosed[k]);
		}
		Expect(bOk && VOpen == 1u && Sim->GetAtriumsVersion() == 2u && Sim->GetAtriums().Num() == 0 && bOpenLive && bClosedZero,
			FString::Printf(TEXT("AT0 the seven key fields (dig labels, deep net, deep homes, membership, commute, walk, charge reserve) read AtriumsVersion %u with the atrium open and 0 once it is closed (version %u, the list empty), every cache built both times: open/closed %s (%s; %s)"),
				VOpen, Sim->GetAtriumsVersion(), *Fields, *BuiltOpen, *BuiltClosed));
	}

	// --- AT1. Every refusal by its reason. ---
	{
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		FString W;
		Expect(bBox && !Sim->CanOpenAtrium(99, W) && W.Contains(TEXT("Unknown cavern #99")), FString::Printf(TEXT("AT1 refusal 1: no such cavern (%s)"), *W));
		// A cavern ordered and cancelled before a cell is carved: (-4,-1) to (-2,1) on -1..-2 beside the spiral's west side.
		int32 Cx = INDEX_NONE;
		const bool bCx = Sim->Debug_DigCavern(-1, 2, FIntPoint(-4, -1), FIntPoint(-2, 1), Cx, Why);
		FRHCommand Cancel; Cancel.Verb = FName("CancelCavern"); Cancel.Value = Cx;
		Send(Cancel);
		W.Reset();
		Expect(bCx && Sim->FindCavern(Cx) && Sim->FindCavern(Cx)->bCancelled && !Sim->CanOpenAtrium(Cx, W) && W.Contains(TEXT("was cancelled")), FString::Printf(TEXT("AT1 refusal 1: a cancelled cavern (%s)"), *W));
		W.Reset();
		const bool bOpen1 = Open(C1);
		Expect(bOpen1 && !Sim->CanOpenAtrium(C1, W) && W.Contains(TEXT("already an atrium")), FString::Printf(TEXT("AT1 refusal 2: already an atrium (%s)"), *W));
		Close(C1);
		// Unfinished: a 2-floor cavern on the spiral's north side, two cells carved.
		int32 Cu = INDEX_NONE;
		const bool bCu = Sim->Debug_DigCavern(-1, 2, FIntPoint(-1, 2), FIntPoint(1, 3), Cu, Why) && Sim->Debug_CarveCavernCells(Cu, 2) == 2;
		W.Reset();
		Expect(bCu && !Sim->CanOpenAtrium(Cu, W) && W.Contains(TEXT("Finish digging it first: 10 cells left")), FString::Printf(TEXT("AT1 refusal 3: not finished (%s)"), *W));
		Sim->Debug_CarveCavernCells(Cu, 10);
		// One floor: a finished 1-floor cavern on -3, south of the spiral.
		int32 C1f = INDEX_NONE;
		const bool bC1f = Sim->Debug_DigCavern(-3, 1, FIntPoint(-1, -3), FIntPoint(1, -2), C1f, Why) && Sim->Debug_CarveCavernCells(C1f, 6) == 6;
		W.Reset();
		Expect(bC1f && !Sim->CanOpenAtrium(C1f, W) && W.Contains(TEXT("at least two floors tall")), FString::Printf(TEXT("AT1 refusal 4: one floor tall (%s)"), *W));
	}
	// Refusal 5, by each kind of use and each branch, each on cell (3,0) of floor -2 (the middle box cell of the lower upper floor) unless said,
	// each from a fresh box colony. "module" is a station module GetProvidedRoom answers for (an Infirmary Module: its room is slice-active), so
	// the station-module branch names it; a Lab Module (its Full Laboratory is not slice-active) is caught by the footprint branch instead.
	// "offcentre" is an Air Filter centred in the spiral cell (1,0), 450 cm east of its middle: its 400 cm footprint reaches into the box cell
	// (2,0) (R6: every footprint cell, not the centre's). "seat" is a seat-priority key (no seat taken); "civic" a civic room's Index cell.
	{
		struct FKind { const TCHAR* Name; const TCHAR* Expect; };
		const FKind Kinds[] = {
			{ TEXT("room"), TEXT("is zoned") }, { TEXT("furniture"), TEXT("is furnished") }, { TEXT("module"), TEXT("holds a station module (Infirmary Module)") },
			{ TEXT("labmodule"), TEXT("is under a Lab Module") }, { TEXT("circulator"), TEXT("is under an Air Filtration Station") },
			{ TEXT("offcentre"), TEXT("is under an Air Filtration Station") }, { TEXT("station"), TEXT("is a work station") }, { TEXT("seat"), TEXT("is a work station") },
			{ TEXT("tunnel"), TEXT("is part of a tunnel") }, { TEXT("growth"), TEXT("is held by a growth project") }, { TEXT("civic"), TEXT("is held by a growth project") } };
		for (const FKind& K : Kinds)
		{
			int32 C1 = INDEX_NONE;
			bool bSet = true;
			const FString Kind = K.Name;
			if (Kind == TEXT("tunnel"))
			{
				// A tunnel dug and carved through the box's rock BEFORE the cavern is ordered: the cavern takes in its carved chain points.
				Reset();
				Sim->ExtendShaft(6, Head0);
				for (int32 L = 1; L <= 3; ++L) { bSet &= Sim->ExcavateFloor(-L, 8, R, 0); }
				bSet &= Sim->ExcavateFloor(-5, 60, R, 0);
				int32 T = INDEX_NONE;
				bSet &= Sim->Debug_DigTunnel(-2, FIntPoint(2, 0), FIntPoint(6, 0), T, Why) && Sim->Debug_CarveTunnelCells(T, 5) == 5;
				bSet &= Sim->Debug_DigCavern(-1, 3, FIntPoint(2, -1), FIntPoint(4, 1), C1, Why) && Sim->Debug_CarveCavernCells(C1, 27) > 0;
				Sim->Debug_AddStock(NOxygen, 200000.0);
			}
			else
			{
				bSet &= BoxColony(C1);
			}
			int32 O = Ord(-2, 3, 0);
			if (Kind == TEXT("room")) { bSet &= Sim->DesignateRoom(-2, O, NLiving, R); }
			else if (Kind == TEXT("furniture"))
			{
				bSet &= Sim->DesignateRoom(-2, O, NLiving, R);
				FRHCommand C;
				C.Verb = FName("Furnish"); C.Target = FName("BunkBed"); C.Level = -2;
				C.Location = Sim->GetCellCentreCm(FIntVector(-2, O, 0));
				C.Value = URHSimWorldSubsystem::EncodeFurnish(O, 0, 0);
				Sim->Debug_AddSolidAt(LanderId, NStruct, 5000.0, false);
				bSet &= Send(C) && Sim->GetFurniture().Num() == 1;
				bSet &= Sim->GetCellFurniture(FIntVector(-2, O, 0)) > 0;
			}
			else if (Kind == TEXT("module"))
			{
				const int32 Id = Place(TEXT("InfirmaryModule"), 3, 0, -2);
				const FRHBuildingInstance* B = Id > 0 ? BuildingOf(Id) : nullptr;
				bSet &= B && Sim->GetProvidedRoom(*B, /*bRequireOnline*/ false) != nullptr;
			}
			else if (Kind == TEXT("labmodule")) { bSet &= Place(TEXT("LabModule"), 3, 0, -2) > 0; }
			else if (Kind == TEXT("circulator")) { bSet &= Place(TEXT("AirFilter"), 3, 0, -2) > 0; }
			else if (Kind == TEXT("offcentre"))
			{
				Sim->Debug_PlaceInstant(FName("AirFilter"), PtCm(1, 0) + FVector(450.0, 0.0, 0.0), -2);
				const int32 Id = LastIdOf(TEXT("AirFilter"));
				const FRHBuildingInstance* B = Id > 0 ? BuildingOf(Id) : nullptr;
				bSet &= B && B->Level == -2 && Sim->FindCarvedCellAt(-2, B->LocationCm) == Ord(-2, 1, 0); // its centre is outside the box
				O = Ord(-2, 2, 0);
			}
			else if (Kind == TEXT("station")) { const TArray<int32> Ids = AddResidents(1, -5); bSet &= Ids.Num() == 1 && Sim->Debug_SetColonistStation(Ids[0], FIntVector(-2, O, 0)); }
			else if (Kind == TEXT("seat")) { Sim->Debug_AddSeatPriority(FIntVector(-2, O, 0)); bSet &= Sim->GetSeatPriorityRank(FIntVector(-2, O, 0)) == 0; }
			else if (Kind == TEXT("growth"))
			{
				FRHGrowthProject G;
				G.DistrictId = 1; G.Kind = ERHGrowthKind::DeepCells; G.Level = -2; G.Points.Add(FIntPoint(3, 0)); G.TotalSols = 2.0; G.RemainingSols = 2.0;
				Sim->Debug_AddGrowthProject(G);
			}
			else if (Kind == TEXT("civic"))
			{
				// A whole-floor civic room's target: its Index cell, no lattice point.
				FRHGrowthProject G;
				G.DistrictId = 1; G.Kind = ERHGrowthKind::CivicRoom; G.Level = -2; G.Index = O; G.bCarve = false; G.Room = FName("Infirmary"); G.TotalSols = 2.0; G.RemainingSols = 2.0;
				Sim->Debug_AddGrowthProject(G);
			}
			if (Kind == TEXT("tunnel")) { O = Ord(-2, 2, 0); } // the chain's first box cell is the lowest ordinal in use
			FString W;
			const bool bRefused = !Sim->CanOpenAtrium(C1, W);
			// Furniture always stands in a room; the furniture test comes first, so a furnished cell reads as furnished and a zoned one as zoned.
			const TCHAR* Want = K.Expect;
			Expect(bSet && bRefused && W.Contains(FString::Printf(TEXT("Floor -2 cell %d"), O)) && W.Contains(Want),
				FString::Printf(TEXT("AT1 refusal 5 (%s): an upper-floor box cell in use is refused, naming the floor and the cell (%s)"), K.Name, *W));
		}
	}
	{
		// Refusal 6: ten residents homed on -1 (17 cells, CPC 1) would have 8 beds after the open.
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		AddResidents(10, -1);
		FString W;
		Expect(bBox && !Sim->CanOpenAtrium(C1, W) && W.Contains(TEXT("Floor -1 would have 8 beds for 10 residents: add beds or move people first")),
			FString::Printf(TEXT("AT1 refusal 6: beds (%s); an atrium never rehouses anyone"), *W));
	}
	{
		// Refusal 7, a split: one more cell east of the box on -1, (5,0), joined to the dig only through the box.
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		const bool bCarved = Sim->CarveLatticePoint(-1, FIntPoint(5, 0));
		FString W;
		Expect(bBox && bCarved && !Sim->CanOpenAtrium(C1, W) && W.Contains(TEXT("On floor -1")) && W.Contains(TEXT("cut the dig in two")), FString::Printf(TEXT("AT1 refusal 7: a split (%s)"), *W));
	}
	{
		// Refusal 7, a lost landing; and an all-void dig accepted. On -1 and -2 nothing is dug but a 3 x 3 x 2 cavern (1,-1) to (3,1) beside the
		// column itself: its only landing is a box cell. With one more cell (4,0) beyond it, the dig keeps a cell and loses its last landing.
		Reset();
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-5, 60, R, 0);
		Place(TEXT("AirFilter"), -1, -1, -5);
		Sim->Debug_ForceRateFloor(-5);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		const bool bCv = Sim->Debug_DigCavern(-1, 2, FIntPoint(1, -1), FIntPoint(3, 1), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 18) == 18;
		FString W;
		const bool bVoid = Sim->CanOpenAtrium(Cv, W);
		Expect(bCv && bVoid, FString::Printf(TEXT("AT1 an all-void dig is accepted: the box beside the column is the whole dig of -1 (%s)"), *W));
		const bool bCarved = Sim->CarveLatticePoint(-1, FIntPoint(4, 0));
		W.Reset();
		Expect(bCarved && !Sim->CanOpenAtrium(Cv, W) && W.Contains(TEXT("open every lift landing")), FString::Printf(TEXT("AT1 refusal 7: a lost landing (%s)"), *W));
	}
	{
		// Refusal 8: the store emptied of oxygen.
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		Sim->Debug_AddStock(NOxygen, -Sim->GetStock(NOxygen));
		FString W;
		Expect(bBox && !Sim->CanOpenAtrium(C1, W) && W.Contains(TEXT("Store 900 kg of oxygen")) && W.Contains(TEXT("short of Oxygen")), FString::Printf(TEXT("AT1 refusal 8: the air bill, with growth's reason (%s)"), *W));
	}
	// Refusal 9 is AT7's 200-cell floor; refusal 10 here: a second finished cavern over the same points on -3..-4 (its -3 is the first's hall,
	// carved already; its -4 is new), so the two boxes share floor -3.
	{
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1) && Sim->ExcavateFloor(-4, 8, R, 0);
		int32 C2 = INDEX_NONE;
		const bool bC2 = Sim->Debug_DigCavern(-3, 2, FIntPoint(2, -1), FIntPoint(4, 1), C2, Why) && Sim->Debug_CarveCavernCells(C2, 9) == 9;
		const bool bOpen1 = Open(C1);
		FString W;
		Expect(bBox && bC2 && bOpen1 && !Sim->CanOpenAtrium(C2, W) && W.Contains(FString::Printf(TEXT("The box shares a point with the atrium over cavern %d"), C1)),
			FString::Printf(TEXT("AT1 refusal 10: overlap (%s)"), *W));
	}
	{
		// Refusal 10 over an atrium's OPEN cells: atrium 1 open on -1..-3; a second finished cavern (4,-1) to (6,1) on -2..-3 takes in the open
		// column x = 4 of -2 (D6a lets a box take in dug points; its foothold on -2 is a corridor (1,2)..(5,2) off the spiral). -2 holds 28 cells,
		// 9 open: 19 beds now. Refusal 6 counts only the 6 cells -2 would newly open (13 beds for 12 residents, not 10), so the box is refused by
		// refusal 10's reason, not by a beds count that takes the open column twice. Then refusals 8 and 9, each in a setting where the double
		// count would decide: (b) the store's free oxygen set to 375 kg, covering the 300 kg bill of 6 new cells and not 450; (c) -2 carved to 224
		// cells, so with the box open it holds 224 + 9 + 6 = 239 cell-equivalents (under the ceiling's 240) and 242 counted twice.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1) && Open(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2) }) { bOk &= Sim->CarveLatticePoint(-2, P); }
		int32 C7 = INDEX_NONE;
		const bool bC7 = Sim->Debug_DigCavern(-2, 2, FIntPoint(4, -1), FIntPoint(6, 1), C7, Why) && Sim->Debug_CarveCavernCells(C7, 18) == 12;
		const TArray<int32> On2 = AddResidents(12, -2);
		const int32 Beds = Sim->GetFloorBeds(-2), Cells2 = Sim->GetFloorCarvedCells(-2), Open2 = Sim->GetFloorOpenCells(-2);
		const FString Overlap = FString::Printf(TEXT("The box shares a point with the atrium over cavern %d"), C1);
		FString W;
		const bool bRefused = !Sim->CanOpenAtrium(C7, W);
		const int32 New2 = Sim->GetAtriumNewOpenCells(C7, -2), New3 = Sim->GetAtriumNewOpenCells(C7, -3);
		Expect(bOk && bC7 && On2.Num() == 12 && Cells2 == 28 && Open2 == 9 && Beds == 19 && bRefused && W.Contains(Overlap) && New2 == 6 && New3 == 0,
			FString::Printf(TEXT("AT1 refusal 10 over an atrium's open cells: the box (4,-1)..(6,1) on -2..-3 takes in 3 open cells of -2 (%d cells, %d open, %d beds, 12 residents); refusal 6 counts the %d it would newly open (the card's count too; the hall %d), 13 beds for 12, so the reason is the overlap (%s; setup %d, cavern %d)"),
				Cells2, Open2, Beds, New2, New3, *W, bOk ? 1 : 0, bC7 ? 1 : 0));
		// (b) Refusal 8: the free oxygen set to 375 kg (held = 375 + the quota's share + what is owed + the breathing reserve).
		const URHSimWorldSubsystem::FRHGrowthAvailability A0 = Sim->GetGrowthAvailability(NOxygen);
		Sim->Debug_AddStock(NOxygen, 375.0 + A0.QuotaBill + A0.Owed + A0.Reserve - A0.Held);
		const URHSimWorldSubsystem::FRHGrowthAvailability A1 = Sim->GetGrowthAvailability(NOxygen);
		FString W8;
		const bool bRefused8 = !Sim->CanOpenAtrium(C7, W8);
		Expect(Near(A1.Free, 375.0, 1e-6) && bRefused8 && W8.Contains(Overlap),
			FString::Printf(TEXT("AT1 refusal 10 over open cells, refusal 8: with %.1f kg of oxygen free the bill for the 6 new cells (300 kg) fits and 9 counted twice (450 kg) would not; the reason is the overlap (%s)"),
				A1.Free, *W8));
		// (c) Refusal 9: -2 carved on to 224 cells (the store full again).
		Sim->Debug_AddStock(NOxygen, 200000.0);
		const bool bCarved9 = Sim->ExcavateFloor(-2, 196, R, 0);
		const int32 Cells9 = Sim->GetFloorCarvedCells(-2), Open9 = Sim->GetFloorOpenCells(-2);
		const int32 Head9 = Sim->GetFloorAirHeadroomCellsWith(-2, Sim->GetAtriumNewOpenCells(C7, -2));
		FString W9;
		const bool bRefused9 = !Sim->CanOpenAtrium(C7, W9);
		Expect(bCarved9 && Cells9 == 224 && Open9 == 9 && Head9 == 1 && bRefused9 && W9.Contains(Overlap),
			FString::Printf(TEXT("AT1 refusal 10 over open cells, refusal 9: -2 carved to %d cells (%d open): %d + %d + 6 = 239 cell-equivalents with the box open (headroom %d of the ceiling's 240; 242 counted twice would be refused); the reason is the overlap (%s)"),
				Cells9, Open9, Cells9, Open9, Head9, *W9));
	}
	{
		// The order path: OpenAtrium through the uplink re-runs the preflight at execute; a refusal comes back through OnCommandRejected.
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		const bool bBad = !Send(AtriumCmd(true, 77)) && LastReject().Contains(TEXT("Unknown cavern #77"));
		const bool bGood = Send(AtriumCmd(true, C1)) && Sim->GetAtriums().Num() == 1 && Sim->GetAtriums()[0].CavernId == C1;
		const bool bNotOne = !Send(AtriumCmd(false, 77)) && LastReject().Contains(TEXT("not an atrium"));
		Expect(bBox && bBad && bGood && bNotOne, TEXT("AT1 the orders: OpenAtrium refused at execute by its reason, then accepted; CloseAtrium of a cavern that is no atrium refused"));
	}

	// --- AT2. The critical rule: no cell is removed, moved or reordered. ---
	{
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		TArray<FIntPoint> Before[3];
		for (int32 F = 0; F < 3; ++F) { Before[F] = Cells(-1 - F); }
		const auto Same = [&]()
		{
			bool bOk = true;
			for (int32 F = 0; F < 3; ++F)
			{
				const TArray<FIntPoint> Now = Cells(-1 - F);
				bOk &= Now.Num() >= Before[F].Num();
				for (int32 i = 0; i < Before[F].Num() && bOk; ++i) { bOk &= Now[i] == Before[F][i] && Sim->GetFloorCell(-1 - F, i) == Before[F][i] && Sim->FindFloorCell(-1 - F, Before[F][i]) == i; }
			}
			return bOk;
		};
		const bool bOpened = Open(C1);
		const bool bAfterOpen = Same() && Cells(-1).Num() == Before[0].Num();
		const bool bClosed = Close(C1);
		const bool bAfterClose = Same() && Cells(-1).Num() == Before[0].Num();
		Open(C1);
		FString LoadErr;
		const bool bRound = Sim->SaveColony(TEXT("atr2"), SaveErr) && Sim->LoadColony(TEXT("atr2"), LoadErr) && LoadErr.IsEmpty() && Sim->GetAtriums().Num() == 1;
		const bool bAfterLoad = Same() && Cells(-1).Num() == Before[0].Num();
		Sim->ExcavateFloor(-1, 4, R, 0);
		const bool bAfterCarve = Same() && Cells(-1).Num() == Before[0].Num() + 4;
		Expect(bBox && bOpened && bAfterOpen && bClosed && bAfterClose && bRound && bAfterLoad && bAfterCarve && Sim->GetFloorCellFallbacks() == 0,
			FString::Printf(TEXT("AT2 a 3 x 3 x 3 atrium: every ordinal on -1..-3 maps to the same point, in both directions, after the open, the close, a save and a load, and four more cells carved (%d, %d and %d cells); no read fell back"),
				Before[0].Num(), Before[1].Num(), Before[2].Num()));
	}

	// --- AT3. The open set is literal. ---
	{
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		Open(C1);
		bool bAgree = true;
		int32 Open1 = 0;
		for (int32 L = -1; L >= -3; --L)
		{
			for (int32 X = 2; X <= 4; ++X)
			{
				for (int32 Y = -1; Y <= 1; ++Y)
				{
					const bool bCell = Sim->IsCellOpen(L, Ord(L, X, Y)), bPoint = Sim->IsPointOpen(L, FIntPoint(X, Y));
					bAgree &= bCell == bPoint && bCell == (L != -3);
					Open1 += bCell ? 1 : 0;
				}
			}
			for (const FIntPoint& P : Cells(L)) { bAgree &= (P.X >= 2 && P.X <= 4 && P.Y >= -1 && P.Y <= 1) || !Sim->IsPointOpen(L, P); }
		}
		Expect(bBox && Open1 == 18 && Sim->GetFloorOpenCells(-1) == 9 && Sim->GetFloorOpenCells(-2) == 9 && Sim->GetFloorOpenCells(-3) == 0 && bAgree,
			FString::Printf(TEXT("AT3 18 open cells (9 on each of the two upper floors), the hall floor's 9 box cells and every other cell not open; IsCellOpen and IsPointOpen agree (%d)"), Open1));
	}

	// --- AT4. The consumers skip open cells. ---
	{
		int32 C1 = INDEX_NONE;
		const bool bBox = BoxColony(C1);
		// A Build order queued before the open, for a spot over the void: refused at execute with the placement's reason.
		Sim->Debug_AddSolidAt(LanderId, NStruct, 5000.0, false);
		FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("AirFilter"); Build.Level = -1; Build.Location = PtCm(3, 1);
		Sim->EnqueueCommand(Build);
		// R6: an Air Filter centred in the spiral cell (1,0), 450 cm east of its middle, has footprint in the box cell (2,0) too.
		const FVector OffCentre = PtCm(1, 0) + FVector(450.0, 0.0, 0.0);
		FString Wo0, Wo;
		const bool bOffBefore = Sim->CanPlaceBuildingSized(FName("AirFilter"), OffCentre, Wo0, -1, 1, 1, false);
		const bool bOpened = Open(C1);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		const bool bBuildRefused = LastReject().Contains(TEXT("That spot is open to the atrium below"));
		const int32 O = Ord(-1, 3, 0);
		FString Wz, Wf, Wm, Wc;
		const bool bZone = !Sim->DesignateRoom(-1, O, NLiving, Wz) && Wz.Contains(TEXT("That cell is open to the atrium below"));
		const bool bFurn = !Sim->CanPlaceFurniture(FName("BunkBed"), FIntVector(-1, O, 0), 0, 0, Wf, false, false) && Wf.Contains(TEXT("That cell is open to the atrium below"));
		const bool bModule = !Sim->CanPlaceBuildingSized(FName("LabModule"), PtCm(3, 0), Wm, -1, 1, 1, false) && Wm.Contains(TEXT("open to the atrium below"));
		const bool bCirc = !Sim->CanPlaceBuildingSized(FName("AirFilter"), PtCm(4, -1), Wc, -1, 1, 1, false) && Wc.Contains(TEXT("That spot is open to the atrium below"));
		const bool bOffCentre = !Sim->CanPlaceBuildingSized(FName("AirFilter"), OffCentre, Wo, -1, 1, 1, false) && Wo.Contains(TEXT("That spot is open to the atrium below"))
			&& Sim->FindCarvedCellAt(-1, OffCentre) == Ord(-1, 1, 0) && !Sim->IsCellOpen(-1, Ord(-1, 1, 0));
		FRHCommand Zone; Zone.Verb = FName("Designate"); Zone.Target = NLiving; Zone.Level = -1; Zone.Value = O;
		const bool bZoneOrder = !Send(Zone) && LastReject().Contains(TEXT("That cell is open to the atrium below"));
		const bool bGrowth = !Sim->Debug_IsCellFreeForRoom(-1, O) && Sim->Debug_IsCellFreeForRoom(-1, Ord(-1, -1, 0)) && Sim->Debug_IsCellFreeForRoom(-3, Ord(-3, 3, 0));
		const bool bHallOk = Sim->CanPlaceBuildingSized(FName("AirFilter"), PtCm(3, 0), Wc, -3, 1, 1, false);
		Expect(bBox && bOpened && bBuildRefused && bZone && bFurn && bModule && bCirc && bZoneOrder && bGrowth && bHallOk,
			FString::Printf(TEXT("AT4 over the void: zone (%s), furnish (%s), a module (%s), a circulator (%s), a queued Build and a queued Zone order are refused; IsCellFreeForRoom never answers an open cell; the hall floor takes a building"), *Wz, *Wf, *Wm, *Wc));
		Expect(bOffBefore && bOffCentre, FString::Printf(TEXT("AT4 R6: an Air Filter centred on the floor cell (1,0) beside the box, whose footprint reaches the box cell (2,0), was placeable before the open (%s) and is refused after it on that footprint cell (%s)"),
			bOffBefore ? TEXT("accepted") : *Wo0, *Wo));
		// The bed count at both exits, at CPC 1 and 0.5 (floor -1: 17 cells, 9 open). No furniture: the early exit.
		const int32 Beds1 = Sim->GetFloorBeds(-1);
		Sim->Debug_SetColonistsPerCell(0.5);
		const int32 BedsHalf = Sim->GetFloorBeds(-1);
		Sim->Debug_SetColonistsPerCell(1.0);
		Expect(Beds1 == 8 && BedsHalf == 4 && Sim->GetFloorBeds(-3) == 17, FString::Printf(TEXT("AT4 with no furniture (the early exit): floor -1 holds (17 - 9) x 1 = %d beds, (int)((17 - 9) x 0.5) = %d at 0.5; the hall floor keeps all 17"), Beds1, BedsHalf));
		// A cell's own beds (GetCellBeds, the deep homes' slots read it): 0 on an open cell; 1 (CPC) on the spiral cell beside it and on the hall's
		// box cell under it.
		const int32 CellOpen = Sim->GetCellBeds(FIntVector(-1, O, 0)), CellBeside = Sim->GetCellBeds(FIntVector(-1, Ord(-1, 1, 0), 0)), CellHall = Sim->GetCellBeds(FIntVector(-3, Ord(-3, 3, 0), 0));
		Expect(Sim->IsCellOpen(-1, O) && CellOpen == 0 && CellBeside == 1 && CellHall == 1,
			FString::Printf(TEXT("AT4 GetCellBeds: the open cell (3,0) of -1 holds %d beds, the spiral cell (1,0) beside it %d, the hall's (3,0) under it %d"), CellOpen, CellBeside, CellHall));
		// With furniture: a bunk (two beds) in a Living spiral cell of -1, so the shared formula runs.
		const int32 Ob = Ord(-1, -1, 1);
		Sim->Debug_AddSolidAt(LanderId, NStruct, 5000.0, false);
		FRHCommand Furnish; Furnish.Verb = FName("Furnish"); Furnish.Target = FName("BunkBed"); Furnish.Level = -1;
		Furnish.Location = Sim->GetCellCentreCm(FIntVector(-1, Ob, 0)); Furnish.Value = URHSimWorldSubsystem::EncodeFurnish(Ob, 0, 0);
		const bool bBunk = Sim->DesignateRoom(-1, Ob, NLiving, R) && Send(Furnish) && Sim->GetFurniture().Num() == 1;
		const int32 BedsF1 = Sim->GetFloorBeds(-1);
		Sim->Debug_SetColonistsPerCell(0.5);
		const int32 BedsFHalf = Sim->GetFloorBeds(-1);
		Sim->Debug_SetColonistsPerCell(1.0);
		Expect(bBunk && BedsF1 == 9 && BedsFHalf == 5, FString::Printf(TEXT("AT4 with a bunk (the shared formula): (17 - 9 - 1) x 1 + 2 = %d beds, (int)(7 x 0.5) + 2 = %d at 0.5"), BedsF1, BedsFHalf));
		// The home-cell slots total the bed count: as many residents as beds all get a home cell on the upper floor, one more does not.
		const TArray<int32> Res = AddResidents(BedsF1 + 1, -1);
		int32 WithCell = 0;
		bool bNeverOpen = true;
		for (const int32 Id : Res)
		{
			const int32 Cell = Sim->GetColonistAtriumHomeCell(Id);
			WithCell += Cell != INDEX_NONE ? 1 : 0;
			bNeverOpen &= Cell == INDEX_NONE || !Sim->IsCellOpen(-1, Cell);
		}
		Expect(Res.Num() == BedsF1 + 1 && WithCell == BedsF1 && bNeverOpen && Sim->GetColonistAtriumHomeCell(Res.Last()) == INDEX_NONE,
			FString::Printf(TEXT("AT4 the slots total CountFloorBeds: %d residents on -1, %d with a home cell (never an open one), the last none"), Res.Num(), WithCell));
	}
	{
		// Home cells. Floors -1 (a Homes shape), -2 (a tram line and no shape), -3 (the hall, unmapped) and -4 (a shape, no atrium). The 3 x 3 x 3
		// box (1,-1) to (3,1) beside the column is carved FIRST on -1..-3 (ordinals 0-8), then the rest of the spiral's first ring (ordinals 9-13),
		// so the open ordinals come before the residents' slots: five residents a floor take ordinals 0-4 in the twin and 9-13 with the box open.
		// HomeCell equals the no-atrium twin's on the floors with no open cell (-3 and -4) and differs on the mapped atrium floors (-1, -2), where
		// it is a hand derivation that skips the open ordinals; an unmapped atrium floor (the hall) writes AtriumHomeCell, never HomeCell.
		Reset();
		Sim->ExtendShaft(6, Head0);
		bool bOk = Sim->ExcavateFloor(-5, 60, R, 0);
		int32 Cv = INDEX_NONE;
		bOk &= Sim->Debug_DigCavern(-1, 3, FIntPoint(1, -1), FIntPoint(3, 1), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 27) == 27;
		for (int32 L = 1; L <= 3; ++L) { bOk &= Sim->ExcavateFloor(-L, 5, R, 0); }
		bOk &= Sim->ExcavateFloor(-4, 8, R, 0);
		for (const int32 L : { 1, 2, 3, 4, 5 })
		{
			Place(TEXT("AirFilter"), -1, -1, -L);
			Sim->Debug_ForceRateFloor(-L);
		}
		Sim->Debug_AddStock(NOxygen, 200000.0);
		bOk &= Fit(0);
		int32 T = INDEX_NONE;
		bOk &= Sim->Debug_DigTunnel(-2, FIntPoint(-2, 0), FIntPoint(-14, 0), T, Why) && Sim->Debug_CarveTunnelCells(T, 13) == 13;
		Sim->Debug_AddSolidAt(LanderId, NStruct, 5000.0, false);
		FString Wl;
		bOk &= Sim->Debug_LayTramLine(T, Wl);
		bOk &= Send(ShapeCmd(-1, 1, FIntPoint(60, 60), FIntPoint(60, 60))) && Send(ShapeCmd(-4, 1, FIntPoint(60, 60), FIntPoint(60, 60)));
		const bool bMaps = Sim->IsFloorMapped(-1) && Sim->IsFloorMapped(-2) && Sim->IsFloorMapped(-4) && !Sim->IsFloorMapped(-3);
		bool bBoxFirst = true;
		for (const int32 L : { -1, -2 }) { for (int32 i = 0; i < 9; ++i) { const FIntPoint P = Sim->GetFloorCell(L, i); bBoxFirst &= P.X >= 1 && P.X <= 3 && P.Y >= -1 && P.Y <= 1; } }
		TMap<int32, TArray<int32>> On;
		for (const int32 L : { 1, 2, 3, 4 }) { On.Add(-L, AddResidents(5, -L)); }
		const TMap<int32, int32> Twin = Sim->Debug_GetDeepHomeCells();
		const bool bOpened = Open(Cv);
		const TMap<int32, int32>& Now = Sim->Debug_GetDeepHomeCells();
		bool bTwin = true;
		for (const int32 L : { -3, -4 })
		{
			for (const int32 Id : On[L]) { const int32* A = Twin.Find(Id); const int32* B = Now.Find(Id); bTwin &= (A == nullptr) == (B == nullptr) && (!A || *A == *B); }
		}
		bool bHall = true;
		for (const int32 Id : On[-3]) { bHall &= !Now.Contains(Id) && Sim->GetColonistAtriumHomeCell(Id) != INDEX_NONE; }
		// The hand derivation, written out (not read from the open flags under test): CPC 1, no bed piece, residents in Id order take the
		// non-open ordinals in order. -1 holds 14 cells and -2 27 (the tram line's 13 tunnel cells after the ring), the box's 0-8 open on each,
		// so the five residents of each take 9, 10, 11, 12 and 13 (the twin's 0-4: every home cell moves off the box).
		bool bHand = true;
		int32 Moved[2] = { 0, 0 };
		FString Got;
		for (const int32 L : { -1, -2 })
		{
			bHand &= Sim->GetFloorCarvedCells(L) == (L == -1 ? 14 : 27) && Sim->GetFloorOpenCells(L) == 9;
			TArray<int32> Ids = On[L];
			Ids.Sort();
			Got += FString::Printf(TEXT("%s%d:"), L == -1 ? TEXT("") : TEXT(" "), L);
			for (int32 k = 0; k < Ids.Num(); ++k)
			{
				const int32 Cell = Sim->GetColonistHomeCell(Ids[k]);
				bHand &= Cell == 9 + k;
				Got += FString::Printf(TEXT(" %d"), Cell);
				const int32* Was = Twin.Find(Ids[k]);
				Moved[-1 - L] += (Was && *Was != Cell) ? 1 : 0;
			}
		}
		Expect(bOk && bMaps && bBoxFirst && bOpened && bTwin && bHall && bHand && Moved[0] == 5 && Moved[1] == 5,
			FString::Printf(TEXT("AT4 home cells: on -3 (unmapped hall) and -4 (a shape, no atrium) HomeCell equals the no-atrium twin's, the hall's residents in AtriumHomeCell only; on -1 (a shape) and -2 (a tram line, no shape) HomeCell is the hand derivation 9-13 past the open box (%s) and differs from the twin's for %d and %d of 5 residents (%s)"),
				*Got, Moved[0], Moved[1], *Wl));
	}
	{
		// Growth over the void (brief AT4): a Homes shape on -1 and a Civic shape on -2, each over the box and the rock round it, (2,-2) to (5,2).
		// One more floor cell east of the box on each, (6,0), makes (5,0) the shapes' only true foothold: every other rock point of them touches
		// open cells only. With the box open, DeepCells growth (-1) and civic growth (-2) run, start, and pick only rock that a non-open cell (or a
		// pick before it) holds: never an open cell, never a point whose only carved neighbours are open. The twin with no atrium starts on the
		// box's free cells, so the shapes and the city do ask for them.
		struct FGrowthRun { bool bOk = false; TArray<FRHGrowthProject> Projects; FString Notes; int32 HomesId = 0, CivicId = 0; };
		const auto GrowthRun = [&](bool bAtrium) -> FGrowthRun
		{
			FGrowthRun Run;
			int32 C1 = INDEX_NONE;
			bool bOk = BoxColony(C1);
			bOk &= Sim->CarveLatticePoint(-1, FIntPoint(6, 0)) && Sim->CarveLatticePoint(-2, FIntPoint(6, 0));
			Sim->Debug_ForceRateFloor(-1);
			Sim->Debug_ForceRateFloor(-2);
			if (bAtrium)
			{
				bOk &= Open(C1);
			}
			bOk &= Send(ShapeOf(NHomes, -1, 1, FIntPoint(2, -2), FIntPoint(5, 2))) && Send(ShapeOf(NCivic, -2, 1, FIntPoint(2, -2), FIntPoint(5, 2)));
			for (const FRHDistrict& D : Sim->GetDistricts())
			{
				if (D.Kind == NHomes) { Run.HomesId = D.Id; }
				if (D.Kind == NCivic) { Run.CivicId = D.Id; }
			}
			// A city short of homes: a quarter of a colonist a cell, every free bed taken.
			Sim->Debug_SetColonistsPerCell(0.25);
			const int32 FreeBeds = Sim->GetFreeHousing();
			bOk &= FreeBeds > 0 && FreeBeds <= 60 && Sim->Debug_AddColonists(FreeBeds) == FreeBeds;
			Sim->Debug_AddSolidAt(LanderId, NStruct, 20000.0, false);
			Sim->Debug_SetHopeSmoothed(80.0);
			Sim->Debug_SetGrowthEnabled(true);
			for (int32 K = 0; K < 3; ++K) { Sim->Debug_RunGrowthCheck(); }
			Sim->Debug_SetGrowthEnabled(false);
			Run.Projects = Sim->GetGrowthProjects();
			Run.Notes = FString::Printf(TEXT("homes '%s', civic '%s', %d free beds filled"), *Sim->GetGrowthNote(Run.HomesId), *Sim->GetGrowthNote(Run.CivicId), FreeBeds);
			Run.bOk = bOk && Run.HomesId > 0 && Run.CivicId > 0;
			return Run;
		};
		const auto Find = [](const FGrowthRun& Run, int32 DistrictId) -> const FRHGrowthProject* { return Run.Projects.FindByPredicate([DistrictId](const FRHGrowthProject& P) { return P.DistrictId == DistrictId; }); };
		const auto Text = [](const FRHGrowthProject* P) -> FString
		{
			if (!P) { return TEXT("none"); }
			FString T = FString::Printf(TEXT("k%d L%d i%d c%d first %d:"), (int32)P->Kind, P->Level, P->Index, P->bCarve ? 1 : 0, P->FirstCell);
			for (const FIntPoint& Pt : P->Points) { T += FString::Printf(TEXT(" (%d,%d)"), Pt.X, Pt.Y); }
			return T;
		};
		const FGrowthRun TwinRun = GrowthRun(false);
		const FRHGrowthProject* TwinHomes = Find(TwinRun, TwinRun.HomesId);
		const FRHGrowthProject* TwinCivic = Find(TwinRun, TwinRun.CivicId);
		const auto InBox = [](const FIntPoint& P) { return P.X >= 2 && P.X <= 4 && P.Y >= -1 && P.Y <= 1; };
		const bool bTwinBox = TwinHomes && TwinHomes->Points.Num() > 0 && InBox(TwinHomes->Points[0]) && TwinHomes->FirstCell > 0 && TwinCivic && TwinCivic->Points.Num() == 1 && InBox(TwinCivic->Points[0]) && !TwinCivic->bCarve;
		const FString TwinText = FString::Printf(TEXT("homes %s, civic %s"), *Text(TwinHomes), *Text(TwinCivic));
		const FGrowthRun Run = GrowthRun(true);
		const FRHGrowthProject* Homes = Find(Run, Run.HomesId);
		const FRHGrowthProject* Civic = Find(Run, Run.CivicId);
		// Every point: not an open cell; every point still to carve holds on to a carved non-open cell, or (DeepCells) to an earlier pick.
		bool bNeverOpen = true, bFoothold = true;
		for (const FRHGrowthProject& P : Run.Projects)
		{
			bNeverOpen &= P.Index == INDEX_NONE || !Sim->IsCellOpen(P.Level, P.Index);
			for (int32 k = 0; k < P.Points.Num(); ++k)
			{
				const FIntPoint Pt = P.Points[k];
				bNeverOpen &= !Sim->IsPointOpen(P.Level, Pt);
				if (Ord(P.Level, Pt.X, Pt.Y) != INDEX_NONE)
				{
					continue; // a free cell to zone
				}
				bool bHeld = false;
				for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 N = Ord(P.Level, Pt.X + D.X, Pt.Y + D.Y);
					bHeld |= (N != INDEX_NONE && !Sim->IsCellOpen(P.Level, N));
					for (int32 j = 0; j < k && !bHeld; ++j) { bHeld |= P.Points[j] == Pt + D && Ord(P.Level, P.Points[j].X, P.Points[j].Y) == INDEX_NONE; }
				}
				bFoothold &= bHeld;
			}
		}
		const bool bHomes = Homes && Homes->Kind == ERHGrowthKind::DeepCells && Homes->Level == -1 && Homes->FirstCell == 0 && Homes->Points.Num() > 0 && Homes->Points[0] == FIntPoint(5, 0);
		const bool bCivic = Civic && Civic->Kind == ERHGrowthKind::CivicRoom && Civic->Level == -2 && Civic->bCarve && Civic->Points.Num() == 1 && Civic->Points[0] == FIntPoint(5, 0);
		Expect(TwinRun.bOk && bTwinBox && Run.bOk && bHomes && bCivic && bNeverOpen && bFoothold,
			FString::Printf(TEXT("AT4 growth over the void: with no atrium the Homes shape zones the box's free cells and the Civic room takes one (%s); with the box open DeepCells and civic growth skip every open cell and every rock point beside open cells only, and both start on (5,0), the one point a floor cell holds: homes %s, civic %s (%s)"),
				*TwinText, *Text(Homes), *Text(Civic), *Run.Notes));
	}
	{
		// The footholds (R9), directly: a rock point whose only carved 4-neighbours are open is no foothold - not for a cavern (whose rule D6b's
		// growth shares) and not for a tunnel's start; a point beside a floor cell still is.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		FString Wt0, Wt, Wt2, We0, We;
		const bool bEligible0 = Sim->IsCavernPointEligible(-1, FIntPoint(3, -2)), bEast0 = Sim->IsCavernPointEligible(-1, FIntPoint(5, 0));
		const bool bTunnel0 = Sim->CanDigTunnel(-1, PtCm(3, -2), PtCm(3, -4), Wt0), bTunnelE0 = Sim->CanDigTunnel(-1, PtCm(5, 0), PtCm(7, 0), We0);
		bOk &= Open(C1);
		const bool bEligible = Sim->IsCavernPointEligible(-1, FIntPoint(3, -2)), bEast = Sim->IsCavernPointEligible(-1, FIntPoint(5, 0));
		const bool bTunnel = Sim->CanDigTunnel(-1, PtCm(3, -2), PtCm(3, -4), Wt), bTunnelE = Sim->CanDigTunnel(-1, PtCm(5, 0), PtCm(7, 0), We);
		const bool bBeside = Sim->IsCavernPointEligible(-1, FIntPoint(1, 2)) && Sim->CanDigTunnel(-1, PtCm(1, 2), PtCm(1, 4), Wt2);
		Expect(bOk && bEligible0 && bEast0 && bTunnel0 && bTunnelE0 && !bEligible && !bEast && !bTunnel && !bTunnelE && Wt.Contains(TEXT("Start a tunnel at the edge of a dig"))
			&& We.Contains(TEXT("Start a tunnel at the edge of a dig")) && bBeside,
			FString::Printf(TEXT("AT4 footholds: (3,-2) and (5,0), beside the box only, were each a cavern's and a tunnel's foothold before the open (%s; %s) and are neither after it (%s; %s); (1,2), beside the floor cell (1,1), still is (%s)"),
				bTunnel0 ? TEXT("tunnel accepted") : *Wt0, bTunnelE0 ? TEXT("tunnel accepted") : *We0, *Wt, *We, bBeside ? TEXT("accepted") : *Wt2));
	}

	// --- AT5. Facing, literally: a 4 x 4 x 2 box (2,-1) to (5,2) beside a dig. ---
	{
		Reset();
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-1, 8, R, 0);
		Sim->ExcavateFloor(-2, 8, R, 0);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		const bool bCv = Sim->Debug_DigCavern(-1, 2, FIntPoint(2, -1), FIntPoint(5, 2), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 32) == 32;
		// (1,-2): a corner of the ring, carved off the spiral cell (1,-1); it touches the box point (2,-1) only diagonally, so it never faces.
		const bool bCorner = Sim->CarveLatticePoint(-1, FIntPoint(1, -2));
		const bool bOpened = Open(Cv);
		const auto Faces = [&](int32 L, int32 X, int32 Y) { return Sim->IsCellFacing(L, Ord(L, X, Y)); };
		const bool bUpperRing = Faces(-1, 1, -1) && Faces(-1, 1, 0) && Faces(-1, 1, 1) && !Faces(-1, 3, 0) && !Faces(-1, 0, 1) && !Faces(-1, -1, 0)
			&& bCorner && Ord(-1, 1, -2) != INDEX_NONE && !Faces(-1, 1, -2);
		bool bHall = Faces(-2, 1, -1) && Faces(-2, 1, 0) && Faces(-2, 1, 1);
		for (int32 X = 2; X <= 5; ++X) { for (int32 Y = -1; Y <= 2; ++Y) { bHall &= Faces(-2, X, Y); } }
		const bool bTwoOut = !Faces(-2, 0, 1) && !Faces(-2, -1, 1);
		// (1,2): carved after the open, beside the box point (2,2) and the ring cell (1,1): it faces at once.
		const bool bCarved = Sim->CarveLatticePoint(-1, FIntPoint(1, 2));
		Expect(bCv && bOpened && bUpperRing && bHall && bTwoOut && bCarved && Faces(-1, 1, 2),
			TEXT("AT5 the upper floor's ring faces (not the void, not a cell two steps out, not the corner (1,-2) that touches the box only diagonally); the hall and its ring face; a cell carved beside the box after the open faces at once"));
	}

	// --- AT6. The Hope term: 40 residents, 10 face by home only, 4 by seat only, 3 by both. ---
	{
		// Floors -1..-3 hold only a 3 x 3 x 3 cavern (1,-1) to (3,1) beside the column, so -1 and -2 are all void and the hall -3 holds 9 cells,
		// every one facing; at CPC 2 they take 18 legacy beds. 13 residents live on -3 (each faces by home), 27 on -5 (no home cell: no facing).
		Reset();
		Sim->ExtendShaft(7, Head0);
		Sim->ExcavateFloor(-5, 60, R, 0);
		Place(TEXT("AirFilter"), -1, -1, -5);
		Sim->Debug_ForceRateFloor(-5);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		bool bOk = Sim->Debug_DigCavern(-1, 3, FIntPoint(1, -1), FIntPoint(3, 1), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 27) == 27;
		Sim->Debug_SetColonistsPerCell(2.0);
		const TArray<int32> Hall = AddResidents(13, -3, 2.0);
		const TArray<int32> Away = AddResidents(27, -5, 2.0);
		bOk &= Hall.Num() == 13 && Away.Num() == 27;
		for (int32 k = 0; k < 3; ++k) { Sim->Debug_SetColonistStation(Hall[k], FIntVector(-3, Ord(-3, 2, 0), 0)); }        // both
		for (int32 k = 0; k < 4; ++k) { Sim->Debug_SetColonistStation(Away[k], FIntVector(-3, Ord(-3, 3, k % 2), 0)); }  // seat only
		for (int32 k = 4; k < 9; ++k) { Sim->Debug_SetColonistStation(Away[k], FIntVector(-5, k, 0)); }                   // a seat that faces nothing
		const URHSimWorldSubsystem::FRHHopeBreakdown Twin = Sim->GetColonyHope();
		bOk &= Open(Cv);
		const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
		Expect(bOk && H.FacingHeads == 17 && H.Atrium == 3.0 * 17.0 / 40.0 && H.Atrium == 1.275 && H.Population == 40,
			FString::Printf(TEXT("AT6 FacingHeads %d of %d (10 by home only, 4 by seat only, 3 by both, each once): Atrium %.4f = 3 x 17 / 40 exactly"), H.FacingHeads, H.Population, H.Atrium));
		Expect(SameTerms(Twin, H) && H.Total == HopeChain(Twin, H.Atrium) && Twin.Atrium == 0.0 && H.Views == Twin.Views,
			FString::Printf(TEXT("AT6 every other term equals the no-atrium twin's, Views unchanged, and Total is the twin's chain with Atrium as the last addend after Shelter, to the bit (%.6f -> %.6f)"), Twin.Total, H.Total));
		// A second atrium (-3,-1) to (-1,1) beside the column's west side; (0,1) on the hall floor faces both. One of the faceless seats moves there.
		int32 Cw = INDEX_NONE;
		const bool bCw = Sim->Debug_DigCavern(-1, 3, FIntPoint(-3, -1), FIntPoint(-1, 1), Cw, Why) && Sim->Debug_CarveCavernCells(Cw, 27) == 27 && Sim->CarveLatticePoint(-3, FIntPoint(0, 1));
		const bool bOpenW = bCw && Open(Cw);
		Sim->Debug_SetColonistStation(Away[4], FIntVector(-3, Ord(-3, 0, 1), 0));
		const URHSimWorldSubsystem::FRHHopeBreakdown H2 = Sim->GetColonyHope();
		Expect(bOpenW && Sim->IsCellFacing(-3, Ord(-3, 0, 1)) && H2.FacingHeads == 18, FString::Printf(TEXT("AT6 a head whose seat faces two atriums counts once: %d heads"), H2.FacingHeads));
	}
	{
		// Shelter on a -6 atrium stacks (DP8's 10 of 40): a 3 x 3 x 2 cavern on -6..-7 beside the column, 10 residents on its hall -7 (past the
		// line, each facing by home), 30 on -5 (the cut-off itself: no Shelter).
		Reset();
		Sim->ExtendShaft(8, Head0);
		Sim->ExcavateFloor(-5, 60, R, 0);
		Place(TEXT("AirFilter"), -1, -1, -5);
		Sim->Debug_ForceRateFloor(-5);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		bool bOk = Sim->Debug_DigCavern(-6, 2, FIntPoint(1, -1), FIntPoint(3, 1), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 18) == 18;
		Sim->Debug_SetColonistsPerCell(2.0); // the hall's nine cells take the ten residents
		AddResidents(10, -7, 2.0);
		const TArray<int32> Line = AddResidents(30, -5, 2.0);
		const URHSimWorldSubsystem::FRHHopeBreakdown Twin = Sim->GetColonyHope();
		bOk &= Open(Cv) && Line.Num() == 30;
		const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
		Expect(bOk && H.DeepResidents == 10 && H.Shelter == Sim->GetHopeShelterMax() * 10.0 / 40.0 && H.FacingHeads == 10 && H.Atrium == 3.0 * 10.0 / 40.0 && Twin.Shelter == H.Shelter
			&& SameTerms(Twin, H) && H.Total == HopeChain(Twin, H.Atrium) && H.Total > Twin.Total,
			FString::Printf(TEXT("AT6 Shelter on a -6 atrium stacks: Shelter +%.3f (10 of 40 below the line) and Atrium +%.3f (10 facing), Total %.6f -> %.6f"), H.Shelter, H.Atrium, Twin.Total, H.Total));
		// The addend's place, as far as a sum can show it. Seats on the hall for s of the -5 residents (s = 1..9) make Atrium 3 x (10 + s) / 40
		// while Shelter stays 0.75: each Total must be the chain with Atrium after Shelter, to the bit. A chain with Atrium before Shelter is
		// computed too and the configurations where it differs are counted, but none is required: every partial sum here lies on the grid of its
		// own binade, so (P + S) + A and (P + A) + S round alike (a fixture could tell them apart only at a binade crossing or an exact tie). The
		// place itself is the code's (GetColonyHope's Total chain); this is the limit the review asked to be noted.
		const auto Misplaced = [](const URHSimWorldSubsystem::FRHHopeBreakdown& T)
		{
			return FMath::Clamp(T.Base + T.Housing + T.Rooms + T.Jobs + T.Milestones + T.Comforts + T.Furnishings + T.Views + T.Solidarity + T.Atrium + T.Shelter
				- T.AdjacencyPenalty - T.UnsupportedPenalty - T.WaterPenalty - T.FoodPenalty - T.CrisisPenalty - T.CommutePenalty - T.WarmPenalty, 0.0, 100.0);
		};
		bool bAll = true;
		int32 Telling = 0;
		FString Seen;
		for (int32 s = 1; s <= 9; ++s)
		{
			Sim->Debug_SetColonistStation(Line[s - 1], FIntVector(-7, Ord(-7, 1 + (s - 1) % 3, -1 + (s - 1) / 3), 0));
			const URHSimWorldSubsystem::FRHHopeBreakdown Hs = Sim->GetColonyHope();
			const bool bTells = Misplaced(Hs) != HopeChain(Hs, Hs.Atrium);
			bAll &= Hs.FacingHeads == 10 + s && Hs.Shelter == H.Shelter && Hs.Atrium == 3.0 * (double)(10 + s) / 40.0 && Hs.Total == HopeChain(Hs, Hs.Atrium);
			Telling += bTells ? 1 : 0;
			if (bTells && Seen.IsEmpty()) { Seen = FString::Printf(TEXT("s = %d: %.17g, misplaced %.17g"), s, Hs.Total, Misplaced(Hs)); }
		}
		Expect(bAll,
			FString::Printf(TEXT("AT6 with 1..9 more heads facing by a hall seat (Shelter 0.75, Atrium 3 x (10 + s) / 40) every Total is the chain with Atrium after Shelter, to the bit; %d of the 9 would differ with Atrium before Shelter (%s: the partial sums lie on their binade's grid, so the two orders round alike - the limit is noted)"),
				Telling, Seen.IsEmpty() ? TEXT("none") : *Seen));
	}

	// --- AT7. Air. ---
	{
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		bOk &= Sim->ExcavateFloor(-4, 8, R, 0);
		const double Req4 = Sim->GetFloorO2RequiredKg(-4), Leak4 = Sim->GetFloorO2LeakKgPerSol(-4), Req3 = Sim->GetFloorO2RequiredKg(-3), Leak3 = Sim->GetFloorO2LeakKgPerSol(-3);
		URHSimWorldSubsystem::FRHAtriumPreview Pre;
		FString W;
		bOk &= Sim->CanOpenAtrium(C1, W, &Pre);
		// -1's pool is drained 300 kg below its requirement first, so the share is seen added to a pool that is not simply full.
		Sim->Debug_SetFloorO2Kg(-1, Sim->GetFloorO2RequiredKg(-1) - 300.0);
		const double Stock0 = Sim->GetStock(NOxygen), Standing0 = Sim->GetStandingO2LeakKgPerSol();
		const double Fill1 = Sim->GetFloorO2Kg(-1), Fill2 = Sim->GetFloorO2Kg(-2), Fill3 = Sim->GetFloorO2Kg(-3);
		bOk &= Open(C1);
		const double Stock1 = Sim->GetStock(NOxygen);
		Expect(bOk && Pre.PremiumKg == 900.0 && Pre.ExtraLeakKgPerSol == 36.0 && Pre.OpenCells.Num() == 18 && Pre.Floors.Num() == 3 && Pre.Floors[2].OpenCells == 0 && Pre.Floors[2].PremiumKg == 0.0,
			FString::Printf(TEXT("AT7 the 3 x 3 x 3 preview: premium %.1f kg (18 x 100 x 0.5), extra leak %.1f kg a sol (18 x 2 x 1), 18 cells to open, none on the hall floor"), Pre.PremiumKg, Pre.ExtraLeakKgPerSol));
		Expect(Sim->GetFloorO2RequiredKg(-1) == 17.0 * 100.0 + 9.0 * Share100 && Sim->GetFloorO2LeakKgPerSol(-1) == 17.0 * 2.0 + 9.0 * 2.0 && Sim->GetFloorO2RequiredKg(-2) == 2150.0
			&& Sim->GetFloorO2LeakKgPerSol(-2) == 52.0 && Sim->GetFloorO2RequiredKg(-3) == Req3 && Sim->GetFloorO2LeakKgPerSol(-3) == Leak3 && Req3 == 1700.0 && Leak3 == 34.0,
			FString::Printf(TEXT("AT7 by hand: each upper floor needs 1,700 + 450 = %.1f kg and leaks 34 + 18 = %.1f kg a sol; the hall floor keeps 1,700 and 34 (no premium)"), Sim->GetFloorO2RequiredKg(-1), Sim->GetFloorO2LeakKgPerSol(-1)));
		Expect(Sim->GetFloorO2RequiredKg(-4) == Req4 && Sim->GetFloorO2LeakKgPerSol(-4) == Leak4, TEXT("AT7 a floor with no open cell in the same colony is bit-identical to the twin (-4)"));
		Expect(Stock1 == Stock0 - Pre.PremiumKg && Pre.PremiumKg == 900.0 && Sim->GetFloorO2Kg(-1) == Fill1 + 9.0 * Share100 && Fill1 == Sim->GetFloorO2RequiredKg(-1) - 450.0 - 300.0
			&& Sim->GetFloorO2Kg(-2) == Fill2 + 9.0 * Share100 && Sim->GetFloorO2Kg(-3) == Fill3,
			FString::Printf(TEXT("AT7 the open takes exactly the premium, %.1f kg of oxygen, and adds each upper floor's share to its pool at once (to the bit, -1 drained 300 kg below its old requirement first: %.1f -> %.1f); the hall's pool is untouched"),
				Stock0 - Stock1, Fill1, Sim->GetFloorO2Kg(-1)));
		Expect(Sim->GetStandingO2LeakKgPerSol() == Standing0 + 36.0, FString::Printf(TEXT("AT7 the colony's standing leak (the life-support cover's) takes the premium leak: %.1f -> %.1f kg a sol (+ 18 open cells x 2)"), Standing0, Sim->GetStandingO2LeakKgPerSol()));
	}
	{
		// 8 x 8 x 3 beside the column on -1..-3, the floors otherwise empty: 6.4 t and 256 kg a sol.
		Reset();
		Sim->ExtendShaft(6, Head0);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		bool bOk = Sim->Debug_DigCavern(-1, 3, FIntPoint(1, -3), FIntPoint(8, 4), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 192) == 192;
		URHSimWorldSubsystem::FRHAtriumPreview Pre;
		FString W;
		bOk &= Sim->CanOpenAtrium(Cv, W, &Pre);
		Expect(bOk && Pre.PremiumKg == 6400.0 && Pre.ExtraLeakKgPerSol == 256.0 && Pre.OpenCells.Num() == 128,
			FString::Printf(TEXT("AT7 the 8 x 8 x 3 preview: premium %.1f kg (6.4 t), extra leak %.1f kg a sol (%s)"), Pre.PremiumKg, Pre.ExtraLeakKgPerSol, *W));
		// Opened: the store falls by exactly 6,400 kg; each upper floor (64 cells, all open) needs 6,400 + 3,200 kg and leaks 128 + 128 kg a sol,
		// the hall 6,400 kg and 128 kg (by hand).
		const double Stock0 = Sim->GetStock(NOxygen);
		bOk &= Open(Cv);
		const double Stock1 = Sim->GetStock(NOxygen);
		Expect(bOk && Stock1 == Stock0 - 6400.0 && Sim->GetFloorO2RequiredKg(-1) == 9600.0 && Sim->GetFloorO2RequiredKg(-2) == 9600.0 && Sim->GetFloorO2RequiredKg(-3) == 6400.0
			&& Sim->GetFloorO2LeakKgPerSol(-1) == 256.0 && Sim->GetFloorO2LeakKgPerSol(-2) == 256.0 && Sim->GetFloorO2LeakKgPerSol(-3) == 128.0,
			FString::Printf(TEXT("AT7 the 8 x 8 x 3 opened: the store falls by %.1f kg; -1 and -2 need %.1f and %.1f kg and leak %.1f and %.1f kg a sol; the hall -3 %.1f kg and %.1f kg a sol"),
				Stock0 - Stock1, Sim->GetFloorO2RequiredKg(-1), Sim->GetFloorO2RequiredKg(-2), Sim->GetFloorO2LeakKgPerSol(-1), Sim->GetFloorO2LeakKgPerSol(-2), Sim->GetFloorO2RequiredKg(-3), Sim->GetFloorO2LeakKgPerSol(-3)));
	}
	{
		// Refusal 8 against the breathing reserve: four residents make growth's Oxygen reserve (GrowthReserveO2Sols of everyone's draw) more than
		// 0, and a store that covers the 900 kg bill but not the bill plus that reserve is refused by refusal 8 with growth's reason; raised to
		// the bill plus the reserve, the same box opens.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		AddResidents(4, -3);
		const URHSimWorldSubsystem::FRHGrowthAvailability A0 = Sim->GetGrowthAvailability(NOxygen);
		const double Floor = 900.0 + A0.QuotaBill + A0.Owed; // held at or above this covers the bill; Free subtracts the reserve as well
		Sim->Debug_AddStock(NOxygen, Floor + 0.5 * A0.Reserve - A0.Held);
		const URHSimWorldSubsystem::FRHGrowthAvailability A1 = Sim->GetGrowthAvailability(NOxygen);
		FString W;
		const bool bRefused = !Sim->CanOpenAtrium(C1, W) && W.Contains(TEXT("Store 900 kg of oxygen")) && W.Contains(TEXT("short of Oxygen"))
			&& W.Contains(FString::Printf(TEXT("%.0f reserve"), A1.Reserve));
		Sim->Debug_AddStock(NOxygen, 0.5 * A0.Reserve + 1.0);
		FString W2;
		const bool bOpens = Sim->CanOpenAtrium(C1, W2) && Open(C1);
		Expect(bOk && A1.Reserve > 0.0 && A1.Held - A1.QuotaBill - A1.Owed >= 900.0 && A1.Free < 900.0 && bRefused && bOpens,
			FString::Printf(TEXT("AT7 refusal 8 against the breathing reserve: %.1f kg held covers the bill (%.1f free past %.1f quota, %.1f owed) but not the bill plus the %.1f kg reserve: refused (%s); with %.1f kg more it opens (%s)"),
				A1.Held, A1.Free, A1.QuotaBill, A1.Owed, A1.Reserve, *W, 0.5 * A0.Reserve + 1.0, W2.IsEmpty() ? TEXT("accepted") : *W2));
	}
	// Refusal 9: an 8 x 8 x 2 on a 200-cell upper floor (264 cell-equivalents against 240) is refused "24 fewer"; on a 170-cell floor (234) it is
	// accepted and the floor stays rated for 20 sols in both bands.
	const auto BigFloor = [&](int32 Spiral, int32& OutCavern) -> bool
	{
		Reset();
		Sim->ExtendShaft(6, Head0);
		bool bOk = Sim->ExcavateFloor(-1, Spiral, R, 0) && Sim->ExcavateFloor(-2, Spiral, R, 0);
		OutCavern = INDEX_NONE;
		for (int32 Dx = 4; Dx <= 12 && OutCavern == INDEX_NONE; ++Dx)
		{
			for (int32 Dy = -8; Dy <= 8 && OutCavern == INDEX_NONE; ++Dy)
			{
				URHSimWorldSubsystem::FRHCavernPreview CP;
				FString W;
				const FIntPoint A(Dx, Dy), B(Dx + 7, Dy + 7);
				if (Sim->CanDigCavern(-1, 2, PtCm(A.X, A.Y), PtCm(B.X, B.Y), W, &CP) && CP.Cells == 128)
				{
					Sim->Debug_DigCavern(-1, 2, A, B, OutCavern, W);
				}
			}
		}
		bOk &= OutCavern != INDEX_NONE && Sim->Debug_CarveCavernCells(OutCavern, 128) == 128;
		Place(TEXT("AirFilter"), -1, -1, -1);
		Place(TEXT("AirFilter"), -1, -1, -2);
		Sim->Debug_ForceRateFloor(-1);
		Sim->Debug_ForceRateFloor(-2);
		Sim->Debug_AddStock(NOxygen, 100000.0);
		return bOk;
	};
	{
		int32 Cv = INDEX_NONE;
		const bool bBig = BigFloor(136, Cv);
		FString W;
		Expect(bBig && Sim->GetFloorCarvedCells(-1) == 200 && !Sim->CanOpenAtrium(Cv, W) && W.Contains(TEXT("Floor -1 would leak more air than its circulation can replace: choose a cavern with 24 fewer cells on this floor")),
			FString::Printf(TEXT("AT7 refusal 9: a %d-cell upper floor with 64 open cells is refused (%s)"), Sim->GetFloorCarvedCells(-1), *W));
	}
	{
		// Refusal 9 skips the hall floor (it gets no premium): under the 170-cell upper floor (234 cell-equivalents with the box open) a hall of
		// 250 cells, past the ceiling's 240 on its own, does not refuse the open.
		int32 Cv = INDEX_NONE;
		const bool bOk = BigFloor(106, Cv) && Sim->ExcavateFloor(-2, 80, R, 0);
		const int32 Hall = Sim->GetFloorCarvedCells(-2), Head = Sim->GetFloorAirHeadroomCells(-2);
		FString W;
		const bool bOpens = Sim->CanOpenAtrium(Cv, W);
		Expect(bOk && Sim->GetFloorCarvedCells(-1) == 170 && Hall == 250 && Head == -10 && bOpens,
			FString::Printf(TEXT("AT7 refusal 9 skips the hall floor: under the 170-cell upper floor a %d-cell hall (headroom %d cells) does not refuse the open (%s)"), Hall, Head, W.IsEmpty() ? TEXT("accepted") : *W));
	}
	for (int32 Band = 0; Band < 2; ++Band)
	{
		int32 Cv = INDEX_NONE;
		const bool bBig = BigFloor(106, Cv);
		const bool bOpened = bBig && Sim->GetFloorCarvedCells(-1) == 170 && Open(Cv);
		bool bRated = true;
		int32 SolsRated = 0;
		for (int32 Sol = 0; Sol < 20 && bOpened; ++Sol)
		{
			if (Band == 0) { for (int32 K = 0; K < (int32)(SolLen / EraDt); ++K) { EraOnce(); } }
			else { for (int32 K = 0; K < (int32)(SolLen / AgentDt); ++K) { AgentOnce(); } }
			bRated &= Sim->IsFloorRated(-1);
			SolsRated += Sim->IsFloorRated(-1) ? 1 : 0;
		}
		Expect(bOpened && bRated && SolsRated == 20, FString::Printf(TEXT("AT7 the 170-cell floor (234 cell-equivalents) opens and stays rated for 20 sols in the %s band (%d of 20 sols)"), Band == 0 ? TEXT("era") : TEXT("agent"), SolsRated));
	}

	// --- AT8. Walking. ---
	{
		// Floor -1: the spiral, the 3 x 3 box (2,-1) to (4,1), and a corridor round its north side to its east side: (1,2), (2,2) .. (5,2), (5,1),
		// (5,0). A lift car on shaft 0.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2), FIntPoint(5, 1), FIntPoint(5, 0) }) { bOk &= Sim->CarveLatticePoint(-1, P); }
		bOk &= Fit(0);
		const double Stamp = Clock->GetSimSecondsTotal();
		const auto CellCm = [&](int32 L, int32 X, int32 Y) { FVector C = Sim->GetFloorCellCentreCm(L, Ord(L, X, Y)); C.Z = L * Sim->GetFloorHeightCm() + 50.0; return C; };
		const auto NeverOpen = [&](const FRHRobotRoadPath& Path)
		{
			bool bNever = true;
			for (int32 i = 0; i < Path.Waypoints.Num(); ++i)
			{
				const int32 L = Path.Levels.IsValidIndex(i) ? Path.Levels[i] : 0;
				const int32 O = L < 0 ? Sim->FindCarvedCellAt(L, Path.Waypoints[i]) : (int32)INDEX_NONE;
				bNever &= O == INDEX_NONE || !Sim->IsCellOpen(L, O);
			}
			return bNever;
		};
		FRHRobotRoadPath Before;
		FRHDeepRouteCost CostBefore;
		const bool bPlanBefore = Sim->PlanDeepRoute(-1, CellCm(-1, 1, 0), -1, CellCm(-1, 5, 0), Stamp, Before, &CostBefore);
		bOk &= Open(C1);
		FRHRobotRoadPath Direct, Up;
		FRHDeepRouteCost Cost, CostUp;
		const bool bDirect = Sim->PlanDeepRoute(-1, CellCm(-1, 1, 0), -1, CellCm(-1, 5, 0), Stamp, Direct, &Cost);
		const bool bUp = Sim->PlanDeepRoute(-1, CellCm(-1, 5, 0), 0, Sim->GetShaftHeadCm() + FVector(3000.0, 0.0, 0.0), Stamp, Up, &CostUp);
		Expect(bOk && bPlanBefore && CostBefore.LatticeCm == 4000 && bDirect && Cost.LatticeCm == 8000 && NeverOpen(Direct) && bUp && NeverOpen(Up),
			FString::Printf(TEXT("AT8 a same-dig direct leg from (1,0) to (5,0) went straight through the box (%lld cm) and now walks round it: %lld cm, the hand count of 8 orthogonal steps; it and a route to the surface never visit an open cell"),
				CostBefore.LatticeCm, Cost.LatticeCm));
		// A robot parked on a box cell when it opens - on the rim (2,0) and in the interior (3,0) - still plans to its next site, from the nearest
		// floor cell; the off-lattice count does not move.
		const int32 Off0 = Sim->GetDeepOffLattice();
		FRHRobotRoadPath FromRim, FromMid;
		const bool bRim = Sim->PlanDeepRoute(-1, CellCm(-1, 2, 0), 0, Sim->GetShaftHeadCm() + FVector(3000.0, 0.0, 0.0), Stamp, FromRim);
		const bool bMid = Sim->PlanDeepRoute(-1, CellCm(-1, 3, 0), -1, CellCm(-1, 5, 2), Stamp, FromMid);
		Expect(bRim && bMid && NeverOpen(FromRim) && NeverOpen(FromMid) && Sim->GetDeepOffLattice() == Off0,
			TEXT("AT8 robots parked on the rim and in the interior of the void when it opens each plan a route from the nearest floor cell, never through an open cell"));
		// A cell carved after the open beside open cells only, (3,-2): its own dig (not the spiral's), no portal lands in it, unreachable.
		const bool bLone = Sim->CarveLatticePoint(-1, FIntPoint(3, -2));
		const int32 LoneId = Place(TEXT("AirFilter"), 3, -2, -1);
		FRHSiteRef Site; Site.BuildingId = LoneId;
		TArray<FIntVector> Portals;
		Sim->Debug_GetPortals(Portals);
		const int32 LoneDig = Sim->Debug_GetDigOf(-1, Ord(-1, 3, -2)), SpiralDig = Sim->Debug_GetDigOf(-1, Ord(-1, 1, 0));
		bool bNoLanding = true;
		int32 OnFloor = 0;
		for (const FIntVector& P : Portals)
		{
			if (P.X % 100 == 1) { ++OnFloor; bNoLanding &= Sim->Debug_GetDigOf(-1, Ord(-1, P.Y, P.Z)) != LoneDig; } // the landings on -1 (Shaft x 100 + 1)
		}
		Expect(bLone && LoneId > 0 && LoneDig != INDEX_NONE && SpiralDig != INDEX_NONE && LoneDig != SpiralDig && OnFloor > 0 && bNoLanding && !Sim->IsRouteReachable(-1, CellCm(-1, 1, 0), Site),
			FString::Printf(TEXT("AT8 a cell carved after the open beside open cells only is its own dig (%d, the spiral's %d), none of the %d landing(s) on -1 is in it, and no robot route reaches it"), LoneDig, SpiralDig, OnFloor));
		// IsRouteReachable's other answers in the same colony: a site in the fitted spiral dig is reached (the control: the -1 Air Filter at
		// (-1,-1)); and a robot standing on an open cell of the site's floor maps its own cell as Resolve does (the nearest non-open cell,
		// distance first, then the lowest ordinal) before the own-dig test: from the rim cell (3,-1) the nearest is the lone cell (3,-2), one
		// step, so the lone site IS reached; from (2,-1) it is the spiral's (1,-1), so it is not.
		int32 SpiralFilter = 0;
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName("AirFilter") && Bd.Level == -1 && Sim->FindCarvedCellAt(-1, Bd.LocationCm) == Ord(-1, -1, -1)) { SpiralFilter = Bd.Id; } }
		FRHSiteRef SpiralSite; SpiralSite.BuildingId = SpiralFilter;
		const bool bControl = SpiralFilter > 0 && Sim->IsRouteReachable(-1, CellCm(-1, 1, 0), SpiralSite);
		const bool bFromRim = Sim->IsCellOpen(-1, Ord(-1, 3, -1)) && Sim->IsRouteReachable(-1, CellCm(-1, 3, -1), Site);
		const bool bFromCorner = Sim->IsCellOpen(-1, Ord(-1, 2, -1)) && !Sim->IsRouteReachable(-1, CellCm(-1, 2, -1), Site);
		Expect(bControl && bFromRim && bFromCorner,
			FString::Printf(TEXT("AT8 IsRouteReachable: the spiral's Air Filter is reached from (1,0) (%d); a robot on the open rim cell (3,-1) maps to the lone cell (3,-2) and reaches the lone site (%d); one on the open (2,-1) maps to the spiral's (1,-1) and does not (%d)"),
				bControl ? 1 : 0, bFromRim ? 1 : 0, bFromCorner ? 1 : 0));
	}
	{
		// A landing in the box is replaced by the lowest-ordinal non-open neighbour: on -6 a 3 x 3 x 2 cavern (1,-1) to (3,1) beside the column is
		// carved first (its (1,0) is the landing), then (0,1) and (-1,0) on the column's other sides, in that order: two non-open candidates, the
		// lower ordinal (0,1) not the first in step order.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		Sim->Debug_ExtendShaftOf(0, 8);
		int32 Cl = INDEX_NONE;
		bOk &= Sim->Debug_DigCavern(-6, 2, FIntPoint(1, -1), FIntPoint(3, 1), Cl, Why) && Sim->Debug_CarveCavernCells(Cl, 18) == 18 && Sim->CarveLatticePoint(-6, FIntPoint(0, 1))
			&& Sim->CarveLatticePoint(-6, FIntPoint(-1, 0)) && Ord(-6, 0, 1) < Ord(-6, -1, 0);
		bOk &= Fit(0);
		const auto LandingOn = [&](int32 Level)
		{
			TArray<FIntVector> Portals;
			Sim->Debug_GetPortals(Portals);
			for (const FIntVector& P : Portals) { if (P.X == -Level) { return FIntPoint(P.Y, P.Z); } }
			return FIntPoint(INT32_MAX, INT32_MAX);
		};
		const FIntPoint Land0 = LandingOn(-6);
		bOk &= Open(Cl);
		const FIntPoint Land1 = LandingOn(-6);
		Expect(bOk && Land0 == FIntPoint(1, 0) && Land1 == FIntPoint(0, 1), FString::Printf(TEXT("AT8 the landing on -6 was the box cell (%d,%d); with the box open it is (%d,%d), the lower-ordinal of the two non-open neighbours"), Land0.X, Land0.Y, Land1.X, Land1.Y));
	}
	{
		// The commute across the ring: on a mapped floor the rim is one compartment and an open cell is in none.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2), FIntPoint(5, 1), FIntPoint(5, 0) }) { bOk &= Sim->CarveLatticePoint(-1, P); }
		bOk &= Send(ShapeCmd(-1, 1, FIntPoint(60, 60), FIntPoint(60, 60)));
		bOk &= Open(C1);
		const int64 Across = Sim->Debug_WalkCm(-1, Ord(-1, 1, 0), Ord(-1, 5, 0));
		const int64 IntoVoid = Sim->Debug_WalkCm(-1, Ord(-1, 3, 0), Ord(-1, 1, 0));
		// Twelve residents: more than the eight spiral cells before the box's ordinals, so the last four land past the box (in the corridor) only
		// because the open cells are skipped.
		const TArray<int32> Res = AddResidents(12, -1);
		const TArray<int32> BoxCells1 = BoxOrds(-1, FIntPoint(2, -1), FIntPoint(4, 1));
		const int32 LastBox = BoxCells1.Num() > 0 ? BoxCells1.Last() : MAX_int32;
		bool bNoVoidHome = true;
		int32 PastBox = 0;
		for (const int32 Id : Res)
		{
			const int32 Cell = Sim->GetColonistHomeCell(Id);
			bNoVoidHome &= Cell != INDEX_NONE && !Sim->IsCellOpen(-1, Cell);
			PastBox += Cell > LastBox ? 1 : 0;
		}
		Expect(bOk && Across == 4000 && (IntoVoid == MAX_int64 || IntoVoid < 0) && bNoVoidHome && PastBox == 4,
			FString::Printf(TEXT("AT8 a commute across the ring stays in the rim compartment (the octile line, %lld cm: D6's R2 undercount across the void, logged as R1) and never anchors in the void (an open cell walks nowhere: %lld; no home cell of 12 residents is open, %d of them past the box's ordinals: %d; setup %d)"),
				Across, IntoVoid, PastBox, bNoVoidHome ? 1 : 0, bOk ? 1 : 0));
	}

	// --- AT9. The save. ---
	{
		// The file colony: the box colony with atrium 1 open, a second finished atrium-to-be (-4,-1) to (-2,1) on -1..-2 (cavern 2), a cavern over
		// the first's points on -3..-4 (3: it shares the hall floor), a 1-floor cavern (4), a cancelled one (5) and an unfinished one (6).
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		int32 C2 = INDEX_NONE, C3 = INDEX_NONE, C4 = INDEX_NONE, C5 = INDEX_NONE, C6 = INDEX_NONE;
		bOk &= Sim->Debug_DigCavern(-1, 2, FIntPoint(-4, -1), FIntPoint(-2, 1), C2, Why) && Sim->Debug_CarveCavernCells(C2, 18) == 18;
		bOk &= Sim->ExcavateFloor(-4, 8, R, 0) && Sim->Debug_DigCavern(-3, 2, FIntPoint(2, -1), FIntPoint(4, 1), C3, Why) && Sim->Debug_CarveCavernCells(C3, 9) == 9;
		bOk &= Sim->Debug_DigCavern(-3, 1, FIntPoint(-1, -3), FIntPoint(1, -2), C4, Why) && Sim->Debug_CarveCavernCells(C4, 6) == 6;
		bOk &= Sim->Debug_DigCavern(-2, 2, FIntPoint(-1, 2), FIntPoint(1, 3), C5, Why);
		FRHCommand Cancel; Cancel.Verb = FName("CancelCavern"); Cancel.Value = C5;
		bOk &= Send(Cancel);
		bOk &= Sim->Debug_DigCavern(-1, 1, FIntPoint(-1, -3), FIntPoint(1, -2), C6, Why) && Sim->Debug_CarveCavernCells(C6, 1) == 1;
		AddResidents(4, -3);
		bOk &= Open(C1);
		// A round trip equals a twin that never saved.
		const URHSimWorldSubsystem::FRHHopeBreakdown H0 = Sim->GetColonyHope();
		TArray<double> Req0, Fill0;
		TArray<uint8> Flags0;
		for (int32 L = -1; L >= -3; --L)
		{
			Req0.Add(Sim->GetFloorO2RequiredKg(L));
			Fill0.Add(Sim->GetFloorO2Kg(L));
			for (int32 i = 0; i < Sim->GetFloorCarvedCells(L); ++i) { Flags0.Add((Sim->IsCellOpen(L, i) ? 1 : 0) | (Sim->IsCellFacing(L, i) ? 2 : 0)); }
		}
		FString LoadErr;
		bOk &= Sim->SaveColony(TEXT("atr9"), SaveErr);
		const uint32 V0 = Sim->GetAtriumsVersion();
		bOk &= Sim->LoadColony(TEXT("atr9"), LoadErr) && LoadErr.IsEmpty();
		const URHSimWorldSubsystem::FRHHopeBreakdown H1 = Sim->GetColonyHope();
		TArray<double> Req1, Fill1;
		TArray<uint8> Flags1;
		for (int32 L = -1; L >= -3; --L)
		{
			Req1.Add(Sim->GetFloorO2RequiredKg(L));
			Fill1.Add(Sim->GetFloorO2Kg(L));
			for (int32 i = 0; i < Sim->GetFloorCarvedCells(L); ++i) { Flags1.Add((Sim->IsCellOpen(L, i) ? 1 : 0) | (Sim->IsCellFacing(L, i) ? 2 : 0)); }
		}
		Expect(bOk && Sim->GetAtriums().Num() == 1 && Sim->GetAtriums()[0].CavernId == C1 && Sim->Debug_GetAtriumLoadPath() == 1 && Sim->GetAtriumsVersion() == V0 + 1
			&& H1.Total == H0.Total && H1.FacingHeads == H0.FacingHeads && H1.Atrium == H0.Atrium && Req1 == Req0 && Fill1 == Fill0 && Flags1 == Flags0,
			FString::Printf(TEXT("AT9 a round trip equals the twin that never saved: Hope %.6f, %d facing, each floor's requirement and fill, every flag (the early read installed the list) (%s %s)"), H1.Total, H1.FacingHeads, *SaveErr, *LoadErr));
		TArray<uint8> File;
		FFileHelper::LoadFileToArray(File, *SaveFilePath(TEXT("atr9")));
		uint32 Ver = 0;
		if (File.Num() > 8) { FMemory::Memcpy(&Ver, File.GetData() + 4, 4); }
		int32 DAt = INDEX_NONE, DBytes = 0, AAt = INDEX_NONE, ABytes = 0;
		const bool bD = RHSaveFrame::Find(File, RHSaveFrame::TransitPowerTag, DAt, DBytes);
		const bool bA = RHSaveFrame::Find(File, RHSaveFrame::AtriumTag, AAt, ABytes);
		int32 Count = -1, First = -1;
		if (bA && ABytes >= 8) { FMemory::Memcpy(&Count, File.GetData() + AAt + 12, 4); FMemory::Memcpy(&First, File.GetData() + AAt + 16, 4); }
		Expect(bD && bA && Ver == 59 && DAt + RHSaveFrame::HeaderBytes + DBytes + RHSaveFrame::FooterBytes == AAt && ABytes == 8 && Count == 1 && First == C1,
			FString::Printf(TEXT("AT9 v%u: 'RHD7' FOLLOWS 'RHD6' (found by tag, never \"the last frame\"); its payload is the count %d and cavern %d (%d bytes; a colony with none writes 4)"), Ver, Count, First, ABytes));
		// Every structural failure drops with exactly one Warning and no Error (option (i)). KeepId is the atrium the load keeps (ascending Ids:
		// the later, lower Id is dropped; of two that overlap, the higher Id). Every doctored copy is resealed by the house helper
		// RHSaveFrame::Reseal: case e is a same-size edit of the file's own payload {1, C1}, made in place; the others change the payload's size,
		// so the suite's ReplaceFramePayload rewrites the frame's lengths round them and then reseals it. Case b is well formed past the cap (the
		// count 1025 and 1025 ascending Ids, 4,104 bytes): only the cap refuses it (without the cap each Id would be read and dropped in turn).
		struct FCase { const TCHAR* Slot; TArray<uint8> Payload; int32 Keep; int32 KeepId; FString Text; bool bInPlace; };
		TArray<FCase> Cases;
		Cases.Add({ TEXT("atr9a"), AtriumPayload({ -1 }), 0, INDEX_NONE, TEXT("counts -1 atriums"), false });
		Cases.Add({ TEXT("atr9b"), AtriumPayloadAscending(1025), 0, INDEX_NONE, TEXT("counts 1025 atriums in 4104 bytes"), false });
		Cases.Add({ TEXT("atr9c"), AtriumPayload({ 2, C1 }), 0, INDEX_NONE, TEXT("counts 2 atriums in 8 bytes"), false });
		Cases.Add({ TEXT("atr9d"), AtriumPayload({ 2, C2, C1 }), 1, C2, TEXT("names cavern 1 after 2"), false });
		Cases.Add({ TEXT("atr9e"), AtriumPayload({ 1, 99 }), 0, INDEX_NONE, TEXT("over cavern 99 names no cavern"), true });
		Cases.Add({ TEXT("atr9f"), AtriumPayload({ 1, C5 }), 0, INDEX_NONE, TEXT("names a cancelled cavern"), false });
		Cases.Add({ TEXT("atr9g"), AtriumPayload({ 1, C6 }), 0, INDEX_NONE, TEXT("names an unfinished cavern"), false });
		Cases.Add({ TEXT("atr9h"), AtriumPayload({ 1, C4 }), 0, INDEX_NONE, TEXT("names a cavern one floor tall"), false });
		Cases.Add({ TEXT("atr9i"), AtriumPayload({ 2, C1, C3 }), 1, C1, FString::Printf(TEXT("atriums over caverns %d and %d overlap; the atrium over cavern %d is dropped"), C1, C3, C3), false });
		const auto Doctor = [&](const FCase& K, TArray<uint8>& T)
		{
			T = File;
			if (K.bInPlace)
			{
				// The same 8 bytes as the file's payload, written over it, and the frame's CRC rewritten by RHSaveFrame::Reseal.
				if (ABytes != K.Payload.Num() || AAt < 0) { return false; }
				FMemory::Memcpy(T.GetData() + AAt + RHSaveFrame::HeaderBytes, K.Payload.GetData(), K.Payload.Num());
				return RHSaveFrame::Reseal(T, RHSaveFrame::AtriumTag);
			}
			return ReplaceFramePayload(T, RHSaveFrame::AtriumTag, K.Payload);
		};
		const auto KeptRight = [&](const FCase& K) { return Sim->GetAtriums().Num() == K.Keep && (K.KeepId == INDEX_NONE || Sim->GetAtriums()[0].CavernId == K.KeepId); };
		for (FCase& K : Cases)
		{
			TArray<uint8> T;
			const bool bMade = Doctor(K, T) && FFileHelper::SaveArrayToFile(T, *SaveFilePath(K.Slot));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString CaseErr;
			const bool bLoaded = Sim->LoadColony(K.Slot, CaseErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			const bool bText = Log.Lines.Num() == 1 && Log.Lines[0].Contains(TEXT("LoadColony v59")) && Log.Lines[0].Contains(K.Text);
			Expect(bMade && bLoaded && CaseErr.IsEmpty() && Log.Warnings == 1 && Log.Errors == 0 && bText && KeptRight(K) && Sim->Debug_GetAtriumLoadPath() == 1,
				FString::Printf(TEXT("AT9 a doctored atrium block (%s; %s, resealed by RHSaveFrame::Reseal) loads by the early read (path %d) with exactly one Warning and no Error, keeping %d atrium(s)%s: %s"), *K.Text,
					K.bInPlace ? TEXT("edited in place") : TEXT("its frame rewritten round the new size"), Sim->Debug_GetAtriumLoadPath(), Sim->GetAtriums().Num(),
					Sim->GetAtriums().Num() > 0 ? *FString::Printf(TEXT(" (cavern %d)"), Sim->GetAtriums()[0].CavernId) : TEXT(""), Log.Lines.Num() > 0 ? *Log.Lines[0] : TEXT("(none)")));
		}
		// The same nine structural failures on the tail-only path: bytes past the last frame break the walk from the end, so the early read finds
		// nothing and the tail's forward read installs the doctored payload (cross-check item 4: still exactly one Warning a failure, the
		// structural one; never "lost", never an Error).
		for (const FCase& K : Cases)
		{
			TArray<uint8> T;
			const bool bMade = Doctor(K, T);
			T.Append(AtriumPayload({ 7, 7, 7 }));
			const FString Slot = FString(K.Slot) + TEXT("t");
			const bool bSaved = bMade && FFileHelper::SaveArrayToFile(T, *SaveFilePath(*Slot));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString CaseErr;
			const bool bLoaded = Sim->LoadColony(Slot, CaseErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			bool bLost = false;
			for (const FString& Line : Log.Lines) { bLost |= Line.Contains(TEXT("is lost")); }
			const bool bText = Log.Lines.Num() == 1 && Log.Lines[0].Contains(TEXT("LoadColony v59")) && Log.Lines[0].Contains(K.Text);
			Expect(bSaved && bLoaded && CaseErr.IsEmpty() && Sim->Debug_GetAtriumLoadPath() == 2 && Log.Warnings == 1 && Log.Errors == 0 && bText && !bLost && KeptRight(K),
				FString::Printf(TEXT("AT9 tail-only: the doctored block (%s) after a broken walk from the end loads by the tail read (path %d) with exactly one Warning and no Error, keeping %d atrium(s)%s: %s"),
					*K.Text, Sim->Debug_GetAtriumLoadPath(), Sim->GetAtriums().Num(), Sim->GetAtriums().Num() > 0 ? *FString::Printf(TEXT(" (cavern %d)"), Sim->GetAtriums()[0].CavernId) : TEXT(""),
					Log.Lines.Num() > 0 ? *Log.Lines[0] : TEXT("(none)")));
		}
		// A lost frame: both of its tags destroyed. Loads with no atrium and one Warning.
		{
			TArray<uint8> T = File;
			const uint32 Junk = 0x5A5A5A5Au;
			FMemory::Memcpy(T.GetData() + AAt, &Junk, 4);
			FMemory::Memcpy(T.GetData() + AAt + RHSaveFrame::HeaderBytes + ABytes + 4, &Junk, 4);
			FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("atr9lost")));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString CaseErr;
			const bool bLoaded = Sim->LoadColony(TEXT("atr9lost"), CaseErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			Expect(bLoaded && CaseErr.IsEmpty() && Sim->GetAtriums().Num() == 0 && Log.Warnings == 1 && Log.Errors == 0 && Sim->Debug_GetAtriumLoadPath() == 3 && Log.Lines.Num() == 1 && Log.Lines[0].Contains(TEXT("the atrium block is lost")),
				FString::Printf(TEXT("AT9 a lost frame loads with no atrium and one Warning: %s"), Log.Lines.Num() > 0 ? *Log.Lines[0] : TEXT("(none)")));
		}
		// The tail read's fallback (cross-check item 4): bytes past the last frame end the walk from the end, so the early read finds nothing; the
		// forward read at the tail still stands on the frame, installs the list and warns nothing.
		{
			TArray<uint8> T = File;
			T.Append(AtriumPayload({ 7, 7, 7 }));
			FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("atr9tail")));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString CaseErr;
			const bool bLoaded = Sim->LoadColony(TEXT("atr9tail"), CaseErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			Expect(bLoaded && CaseErr.IsEmpty() && Sim->GetAtriums().Num() == 1 && Sim->GetAtriums()[0].CavernId == C1 && Sim->Debug_GetAtriumLoadPath() == 2 && Log.Errors == 0
				&& Log.Warnings == 0 && Log.Lines.Num() == 0 && Sim->IsCellOpen(-1, Ord(-1, 3, 0)) && Sim->Debug_AtriumCachesFresh(),
				FString::Printf(TEXT("AT9 with the walk from the end broken the early read fails and the tail read installs the atrium, silently: \"lost\" only when both reads fail (%d Warning(s), %d Error(s))"), Log.Warnings, Log.Errors));
		}
		// The occupancy repair: a file whose open cell is zoned. Made from a twin with no atrium and the cell zoned, the payload set to [1, 1].
		{
			FString LoadZ;
			Sim->LoadColony(TEXT("atr9"), LoadZ);
			Close(C1);
			const int32 O = Ord(-1, 3, 0);
			const bool bZoned = Sim->DesignateRoom(-1, O, NLiving, R);
			Sim->SaveColony(TEXT("atr9z"), SaveErr);
			TArray<uint8> T;
			FFileHelper::LoadFileToArray(T, *SaveFilePath(TEXT("atr9z")));
			const bool bMade = ReplaceFramePayload(T, RHSaveFrame::AtriumTag, AtriumPayload({ 1, C1 })) && FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("atr9occ")));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString CaseErr;
			const bool bLoaded = Sim->LoadColony(TEXT("atr9occ"), CaseErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			const FString Want = FString::Printf(TEXT("atrium over cavern %d closed at load: floor -1 cell %d is zoned"), C1, O);
			Expect(bZoned && bMade && bLoaded && CaseErr.IsEmpty() && Sim->GetAtriums().Num() == 0 && Log.Warnings == 1 && Log.Errors == 0 && Log.Lines.Num() == 1 && Log.Lines[0].Contains(Want)
				&& Sim->GetRoomAt(-1, O) == NLiving && !Sim->IsCellOpen(-1, O),
				FString::Printf(TEXT("AT9 the occupancy repair: the file loads, the atrium closed with one Warning (%s), the room kept"), Log.Lines.Num() > 0 ? *Log.Lines[0] : TEXT("(none)")));
		}
		// The previous version's file is refused.
		{
			TArray<uint8> T = File;
			const uint32 Prev = 58;
			FMemory::Memcpy(T.GetData() + 4, &Prev, 4);
			FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("atr9v58")));
			FString E58;
			const bool bLoaded = Sim->LoadColony(TEXT("atr9v58"), E58);
			Expect(!bLoaded && E58.Contains(TEXT("Save format mismatch")) && E58.Contains(TEXT("/59")), FString::Printf(TEXT("AT9 the previous version is refused (%s)"), *E58));
		}
	}

	// --- AT9, robots saved on open cells (as TR18 (iii) moves a robot by hand): the box colony with the corridor round the box's north and east
	// sides on -1 (AT8's), a lift car on shaft 0, the atrium open. One Hauler stands on the rim cell (4,1) - its nearest floor cells, one step
	// away, are the corridor's (4,2) and (5,1), higher ordinals than the spiral's two steps away - and one in the interior (3,0), whose nearest
	// floor cells are two steps away: (1,0) of the spiral and (5,0), (3,2) of the corridor.
	{
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2), FIntPoint(5, 1), FIntPoint(5, 0) }) { bOk &= Sim->CarveLatticePoint(-1, P); }
		bOk &= Fit(0) && Open(C1);
		TArray<FRHRoverState> Hs;
		if (Agents) { Agents->CollectRobotsOfClass(FName("Hauler"), Hs); }
		bOk &= Agents && Hs.Num() > 1;
		if (bOk)
		{
			Agents->SetRobotPosition(Hs[0].Entity, RobotCm(-1, 4, 1));
			Agents->SetRobotPosition(Hs[1].Entity, RobotCm(-1, 3, 0));
		}
		bOk &= Sim->SaveColony(TEXT("atr9bot"), SaveErr);
		const auto Positions = [&](FVector& OutA, FVector& OutB)
		{
			TArray<FRHRoverState> Now;
			if (Agents) { Agents->CollectRobotsOfClass(FName("Hauler"), Now); }
			OutA = OutB = FVector(-1.0e9);
			return Now.Num() > 1 && Agents->GetRobotPosition(Now[0].Entity, OutA) && Agents->GetRobotPosition(Now[1].Entity, OutB);
		};
		const auto OnOpen = [&](const FVector& P) { const int32 L = Sim->LevelOfZ(P.Z); const int32 O = L < 0 ? Sim->FindCarvedCellAt(L, P) : (int32)INDEX_NONE; return O != INDEX_NONE && Sim->IsCellOpen(L, O); };
		const auto Count = [](const FRHAtriumLogCounter& Log, const TCHAR* Text) { int32 N = 0; for (const FString& Line : Log.Lines) { N += Line.Contains(Text) ? 1 : 0; } return N; };
		// (a) The normal load: the early read installs the list before RepairLoadedRobots, which treats an open cell as no cell - both robots go
		// to the surface deploy ring, each with the open-cell Warning.
		{
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString LoadErr;
			const bool bLoaded = Sim->LoadColony(TEXT("atr9bot"), LoadErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			FVector A, B;
			const bool bRead = Positions(A, B);
			const int32 Ring = Count(Log, TEXT("stands on a cell open to an atrium - moved to the surface deploy ring"));
			Expect(bOk && bLoaded && LoadErr.IsEmpty() && Sim->Debug_GetAtriumLoadPath() == 1 && bRead && Sim->LevelOfZ(A.Z) == 0 && Sim->LevelOfZ(B.Z) == 0 && A.Z == RH::RobotZOffsetCm
				&& B.Z == RH::RobotZOffsetCm && Ring == 2 && Log.Warnings == 2 && Log.Errors == 0 && Sim->IsAtrium(C1),
				FString::Printf(TEXT("AT9 robots saved on the open cells (4,1) and (3,0) of -1: the normal load (path %d) repairs each as off the lattice, to the surface deploy ring (%.0f, %.0f, %.0f) and (%.0f, %.0f, %.0f), with %d open-cell Warning(s) of %d: %s"),
					Sim->Debug_GetAtriumLoadPath(), A.X, A.Y, A.Z, B.X, B.Y, B.Z, Ring, Log.Warnings, Log.Lines.Num() > 0 ? *Log.Lines[0] : TEXT("(none)")));
		}
		// (b) The tail-only load (the walk from the end broken): RepairLoadedRobots runs with no atrium yet and keeps both (their cells are in a
		// fitted dig then); the tail read installs the list and RemapRobotsOffOpenCells moves each to Resolve's cell - distance first, then the
		// lowest ordinal: the rim robot to the lower of (4,2) and (5,1), the interior one to (1,0).
		{
			TArray<uint8> T;
			FFileHelper::LoadFileToArray(T, *SaveFilePath(TEXT("atr9bot")));
			T.Append(AtriumPayload({ 7, 7, 7 }));
			const bool bSaved = FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("atr9bott")));
			FRHAtriumLogCounter Log;
			GLog->AddOutputDevice(&Log);
			FString LoadErr;
			const bool bLoaded = bSaved && Sim->LoadColony(TEXT("atr9bott"), LoadErr);
			GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
			GLog->RemoveOutputDevice(&Log);
			FVector A, B;
			const bool bRead = Positions(A, B);
			const FIntPoint WantA = Ord(-1, 4, 2) < Ord(-1, 5, 1) ? FIntPoint(4, 2) : FIntPoint(5, 1);
			const bool bA = bRead && A.Equals(RobotCm(-1, WantA.X, WantA.Y), 1.0), bB = bRead && B.Equals(RobotCm(-1, 1, 0), 1.0);
			const int32 Kept = Count(Log, TEXT("stands in a dig a fitted lift reaches - kept")), Moved = Count(Log, TEXT("stands on a cell open to an atrium - moved to the nearest floor cell"));
			bool bLost = false;
			for (const FString& Line : Log.Lines) { bLost |= Line.Contains(TEXT("is lost")); }
			Expect(bOk && bLoaded && LoadErr.IsEmpty() && Sim->Debug_GetAtriumLoadPath() == 2 && bA && bB && !OnOpen(A) && !OnOpen(B) && Ord(-1, 1, 0) < Ord(-1, 5, 0) && Ord(-1, 1, 0) < Ord(-1, 3, 2)
				&& Kept == 2 && Moved == 2 && Log.Warnings == 4 && Log.Errors == 0 && !bLost && Sim->IsAtrium(C1),
				FString::Printf(TEXT("AT9 the same file on the tail-only path (path %d): kept at RepairLoadedRobots (%d), then remapped off the open cells (%d): the rim robot to (%d,%d) %s, the interior robot to (1,0) %s; %d Warning(s), no \"lost\""),
					Sim->Debug_GetAtriumLoadPath(), Kept, Moved, WantA.X, WantA.Y, bA ? TEXT("yes") : TEXT("NO"), bB ? TEXT("yes") : TEXT("NO"), Log.Warnings));
		}
	}

	// --- AT10. Era parity: an era and an agent twin open the same atrium at the same uplink. ---
	{
		// The four residents of the hall -3 are kept supplied in both bands for the whole case (water and food in the store: the pools are
		// colony-wide; the hall is rated), so they stay, their breath draws the hall's pool, and the Hope compared after 10 sols has the atrium
		// term live: two of them face (their home cells are the spiral's (1,0) and (1,1), beside the box).
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		Sim->Debug_AddStock(NWater, 2.0e6);
		Sim->Debug_AddStock(NFood, 2.0e6);
		AddResidents(4, -3);
		bOk &= Sim->SaveColony(TEXT("atr10"), SaveErr);
		double Fill[2][3] = {}, Hope[2] = {}, Drop[2][3] = {}, Window[2] = {}, Atr[2] = {};
		int32 Pop[2] = {}, Heads[2] = {};
		bool bOrder[2] = { false, false }, bBit = true;
		const double FillRate = Sim->Debug_GetO2FillRateKgPerHour();
		for (int32 Band = 0; Band < 2; ++Band)
		{
			// Right after the open each upper floor's pool is its pre-open value plus its premium share, to the bit: the harness open (the order's
			// own ExecuteOpenAtrium, without the uplink) from the twin's state in each band, with -1's pool drained 200 kg first so the share is
			// added to a pool that is not simply full. The uplink order is run below in both bands too (it must open the atrium); its pools are
			// not compared to the bit, because the step that executes it also runs the habitability step.
			FString LoadErr;
			bOk &= Sim->LoadColony(TEXT("atr10"), LoadErr);
			if (Band == 0) { EraOnce(); } else { for (int32 K = 0; K < (int32)(EraDt / AgentDt); ++K) { AgentOnce(); } }
			Sim->Debug_SetFloorO2Kg(-1, Sim->GetFloorO2Kg(-1) - 200.0);
			const double P1 = Sim->GetFloorO2Kg(-1), P2 = Sim->GetFloorO2Kg(-2), P3 = Sim->GetFloorO2Kg(-3);
			bBit &= Open(C1) && Sim->GetFloorO2Kg(-1) == P1 + 9.0 * Share100 && Sim->GetFloorO2Kg(-2) == P2 + 9.0 * Share100 && Sim->GetFloorO2Kg(-3) == P3;
			// The premium leak inside StepHabitability, in this band: with the circulation row at 0 for a quarter of a sol nothing refills, so each
			// pool falls by exactly its own leak a sol x the time (52 kg a sol on each upper floor, 34 on the hall).
			Sim->Debug_SetO2FillRateKgPerHour(0.0);
			const double Q0 = Clock->GetSimSecondsTotal();
			const double D0[3] = { Sim->GetFloorO2Kg(-1), Sim->GetFloorO2Kg(-2), Sim->GetFloorO2Kg(-3) };
			if (Band == 0) { for (int32 K = 0; K < (int32)(SolLen / EraDt / 4.0); ++K) { EraOnce(); } }
			else { for (int32 K = 0; K < (int32)(SolLen / AgentDt / 4.0); ++K) { AgentOnce(); } }
			Window[Band] = Clock->GetSimSecondsTotal() - Q0;
			for (int32 F = 0; F < 3; ++F) { Drop[Band][F] = D0[F] - Sim->GetFloorO2Kg(-1 - F); }
			Sim->Debug_SetO2FillRateKgPerHour(FillRate);
			// The order through the uplink in this band, then 10 sols.
			bOk &= Sim->LoadColony(TEXT("atr10"), LoadErr);
			Sim->EnqueueCommand(AtriumCmd(true, C1));
			for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K)
			{
				if (Band == 0) { EraOnce(); } else { for (int32 J = 0; J < (int32)(EraDt / AgentDt); ++J) { AgentOnce(); } }
			}
			bOrder[Band] = Sim->IsAtrium(C1);
			for (int32 Sol = 0; Sol < 10; ++Sol)
			{
				if (Band == 0) { for (int32 K = 0; K < (int32)(SolLen / EraDt); ++K) { EraOnce(); } }
				else { for (int32 K = 0; K < (int32)(SolLen / AgentDt); ++K) { AgentOnce(); } }
			}
			for (int32 F = 0; F < 3; ++F) { Fill[Band][F] = Sim->GetFloorO2Kg(-1 - F); }
			const URHSimWorldSubsystem::FRHHopeBreakdown HB = Sim->GetColonyHope();
			Hope[Band] = HB.Total;
			Atr[Band] = HB.Atrium;
			Heads[Band] = HB.FacingHeads;
			Pop[Band] = Sim->GetPopulation();
		}
		const double Tol = FillRate * EraDt / 50.0; // one era step's fill at the row (StepHabitability: O2FillRateKgPerHour x (Dt / 50))
		Expect(bOk && bOrder[0] && bOrder[1] && bBit, TEXT("AT10 in both bands the order through the uplink opens the atrium; and right after the harness open (ExecuteOpenAtrium) each upper floor's pool is its pre-open value plus its premium share, to the bit (-1 drained first; the hall's untouched)"));
		// The upper floors hold nobody, so their pools only leak: 52 kg a sol (34 + the 18 the premium adds), within one agent sub-step's leak (the
		// agent spine's habitability step trails the clock by one sub-step). The hall's pool also feeds its four residents' breathing, so it falls
		// by its own 34 kg a sol and more, but by less than an upper floor: the premium is on the upper floors only.
		bool bLeak = true;
		const double StepTol = 52.0 * (double)AgentDt / SolLen * 1.5;
		for (int32 Band = 0; Band < 2; ++Band)
		{
			const double Upper = 52.0 * Window[Band] / SolLen, Hall = 34.0 * Window[Band] / SolLen;
			bLeak &= Window[Band] > 0.0 && Near(Drop[Band][0], Upper, StepTol) && Near(Drop[Band][1], Upper, StepTol) && Drop[Band][2] >= Hall - StepTol && Drop[Band][2] < Drop[Band][0];
		}
		Expect(bLeak, FString::Printf(TEXT("AT10 the premium leak runs in StepHabitability in both bands: with no circulation for a quarter sol each upper floor falls at 52 kg a sol (34 + 18) and the hall less (its own 34 and its residents' breath): era %.4f / %.4f / %.4f kg in %.0f s, agent %.4f / %.4f / %.4f kg in %.0f s"),
			Drop[0][0], Drop[0][1], Drop[0][2], Window[0], Drop[1][0], Drop[1][1], Drop[1][2], Window[1]));
		Expect(Pop[0] == 4 && Pop[1] == 4 && Heads[0] > 0 && Heads[0] == Heads[1] && Atr[0] > 0.0 && Atr[1] > 0.0 && Atr[0] == Atr[1],
			FString::Printf(TEXT("AT10 after 10 sols both bands keep their %d / %d residents (supplied) and the atrium term live: %d / %d facing, Atrium %.4f / %.4f"), Pop[0], Pop[1], Heads[0], Heads[1], Atr[0], Atr[1]));
		Expect(Near(Fill[0][0], Fill[1][0], Tol) && Near(Fill[0][1], Fill[1][1], Tol) && Near(Fill[0][2], Fill[1][2], Tol) && Near(Hope[0], Hope[1], 0.05 * FMath::Max(Hope[0], Hope[1])),
			FString::Printf(TEXT("AT10 over 10 sols the fill agrees within one era step's fill at the row (%.0f kg/h x 60 s / 50 = %.0f kg), the hall's under its residents' breath: -1 %.2f / %.2f, -2 %.2f / %.2f, -3 %.2f / %.2f; Hope with the atrium term in it within 5%%: %.3f / %.3f"),
				FillRate, Tol, Fill[0][0], Fill[1][0], Fill[0][1], Fill[1][1], Fill[0][2], Fill[1][2], Hope[0], Hope[1]));
	}

	// --- AT11. -RHVerifyCaches is clean; the facing count rebuilds 0 times over 1,000 unchanged steps. ---
	{
		// The four residents of the hall -3 are kept supplied for the whole case (water and food in the store), so every Hope read below runs the
		// facing count: two of them face (their home cells are the spiral's (1,0) and (1,1), beside the box). The facing count's reuses are
		// counted, so the window shows it read the cached count on every step.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		Sim->Debug_AddStock(NWater, 2.0e6);
		Sim->Debug_AddStock(NFood, 2.0e6);
		AddResidents(4, -3);
		FRHAtriumLogCounter Log;
		GLog->AddOutputDevice(&Log);
		Sim->Debug_SetVerifyCaches(true);
		bOk &= Open(C1);
		const int32 HeadsOpen = Sim->GetColonyHope().FacingHeads;
		for (int32 K = 0; K < 30; ++K) { EraOnce(); Sim->GetColonyHope(); Sim->GetFloorOpenCells(-1); }
		const bool bCarved = Sim->CarveLatticePoint(-1, FIntPoint(1, 2)); // a cell beside the box after the open: every key on FloorCellsVersion moves
		Sim->IsCellFacing(-1, Ord(-1, 1, 2));
		for (int32 K = 0; K < 30; ++K) { EraOnce(); Sim->GetColonyHope(); Sim->GetFloorOpenCells(-1); Sim->GetFloorOpenCells(-2); }
		Close(C1);
		const int32 HeadsClosed = Sim->GetColonyHope().FacingHeads;
		Open(C1);
		const int32 HeadsReopen = Sim->GetColonyHope().FacingHeads;
		for (int32 K = 0; K < 10; ++K) { EraOnce(); Sim->GetColonyHope(); }
		const bool bFresh = Sim->Debug_AtriumCachesFresh();
		const int32 PopVerify = Sim->GetPopulation();
		Sim->Debug_SetVerifyCaches(false);
		GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
		GLog->RemoveOutputDevice(&Log);
		Expect(bOk && bCarved && Log.Verify == 0 && bFresh && PopVerify == 4 && HeadsOpen > 0 && HeadsClosed == 0 && HeadsReopen == HeadsOpen,
			FString::Printf(TEXT("AT11 under -RHVerifyCaches every reuse of the atrium caches matches a fresh derivation over an open, a carve, a close and a reopen, the facing count read at each with the residents in (%d of them; facing %d open, %d closed, %d reopened; %d RHVERIFY line(s))"),
				PopVerify, HeadsOpen, HeadsClosed, HeadsReopen, Log.Verify));
		for (int32 K = 0; K < 5; ++K) { AgentOnce(); Sim->GetColonyHope(); }
		const int32 Pop0 = Sim->GetPopulation(), Heads0 = Sim->GetColonyHope().FacingHeads;
		const int32 Builds0 = Sim->Debug_GetAtriumFacingBuilds(), Reuses0 = Sim->Debug_GetAtriumFacingReuses();
		int32 Same = 0;
		for (int32 K = 0; K < 1000; ++K) { AgentOnce(); Same += Sim->GetColonyHope().FacingHeads == Heads0 ? 1 : 0; }
		const int32 Rebuilds = Sim->Debug_GetAtriumFacingBuilds() - Builds0, Reuses = Sim->Debug_GetAtriumFacingReuses() - Reuses0;
		const int32 Pop1 = Sim->GetPopulation(), Heads1 = Sim->GetColonyHope().FacingHeads;
		Expect(Pop0 == 4 && Pop1 == 4 && Heads0 > 0 && Heads1 == Heads0 && Same == 1000 && Reuses >= 1000 && Rebuilds == 0,
			FString::Printf(TEXT("AT11 the facing count is rebuilt %d times over 1,000 unchanged steps, each with a Hope read: the window read the cached count %d times (at least once a step), %d residents before and %d after it, %d facing on all %d reads"),
				Rebuilds, Reuses, Pop0, Pop1, Heads0, Same));
	}

	{
		// The facing COUNT's own key (not the flags'): under -RHVerifyCaches, (i) a carve alone - FloorCellsVersion moves, no other field of the
		// key - gives a resident of an atrium floor with no home cell a facing home cell, and (ii) a seat moved onto a facing cell while the
		// atrium stays open - JobsVersion - adds a head that faces by its seat. Each moves FacingHeads by exactly one, with no RHVERIFY line.
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1) && Open(C1);
		// After the open -1 keeps its eight spiral cells (three face the box: (1,-1), (1,0), (1,1)); nine residents: eight take them, the ninth has
		// no home cell. One more resident lives on -5, facing nothing.
		const TArray<int32> Upper = AddResidents(9, -1);
		const TArray<int32> Away = AddResidents(1, -5);
		FRHAtriumLogCounter Log;
		GLog->AddOutputDevice(&Log);
		Sim->Debug_SetVerifyCaches(true);
		const int32 H0 = Sim->GetAtriumFacingHeads();
		const int32 Ninth0 = Sim->GetColonistAtriumHomeCell(Upper.Last());
		const uint32 Jobs0 = Sim->GetJobsVersion(), Bed0 = Sim->GetBedAssignVersion(), Dist0 = Sim->GetDistrictsVersion(), Cells0 = Sim->GetFloorCellsVersion();
		const bool bCarved = Sim->CarveLatticePoint(-1, FIntPoint(5, 0)); // beside the box's east side: a ninth floor cell, facing
		const int32 H1 = Sim->GetAtriumFacingHeads();
		const int32 Ninth1 = Sim->GetColonistAtriumHomeCell(Upper.Last());
		const bool bOnlyCells = Sim->GetJobsVersion() == Jobs0 && Sim->GetBedAssignVersion() == Bed0 && Sim->GetDistrictsVersion() == Dist0 && Sim->GetFloorCellsVersion() != Cells0;
		const bool bSeat = Away.Num() == 1 && Sim->Debug_SetColonistStation(Away[0], FIntVector(-1, Ord(-1, 1, 0), 0));
		const int32 H2 = Sim->GetAtriumFacingHeads();
		Sim->GetColonyHope();
		Sim->Debug_SetVerifyCaches(false);
		GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
		GLog->RemoveOutputDevice(&Log);
		Expect(bOk && Upper.Num() == 9 && Ninth0 == INDEX_NONE && bCarved && Ninth1 == Ord(-1, 5, 0) && Sim->IsCellFacing(-1, Ninth1) && bOnlyCells && H1 == H0 + 1 && bSeat && H2 == H1 + 1 && Log.Verify == 0,
			FString::Printf(TEXT("AT11 the facing count's key: a carve alone (only FloorCellsVersion moved: %d) gives the ninth resident of -1 the facing cell (5,0) (home %d -> %d) and FacingHeads %d -> %d; a seat moved onto (1,0) adds one more: %d; %d RHVERIFY line(s)"),
				bOnlyCells ? 1 : 0, Ninth0, Ninth1, H0, H1, H2, Log.Verify));
	}
	{
		// R12's two preflight memos under -RHVerifyCaches (ruling 20: they stay compared): with three residents on -1 (the one upper floor with
		// residents), a second preflight with nothing changed reuses refusal 6's bed count of -1 and refusal 7's walk test - one reuse each - and
		// compares each with a fresh derivation: no RHVERIFY line. Both preflights accept. (The load resets the memos, so the first is a miss.)
		int32 C1 = INDEX_NONE;
		const bool bOk = BoxColony(C1);
		AddResidents(3, -1);
		Sim->GetFloorBeds(-1);
		Sim->GetColonyHope();
		FRHAtriumLogCounter Log;
		GLog->AddOutputDevice(&Log);
		Sim->Debug_SetVerifyCaches(true);
		FString W1, W2;
		const bool bFirst = Sim->CanOpenAtrium(C1, W1);
		const int32 Bed0 = Sim->Debug_GetAtriumBedMemoReuses(), Walk0 = Sim->Debug_GetAtriumWalkMemoReuses();
		const bool bSecond = Sim->CanOpenAtrium(C1, W2);
		const int32 BedReuses = Sim->Debug_GetAtriumBedMemoReuses() - Bed0, WalkReuses = Sim->Debug_GetAtriumWalkMemoReuses() - Walk0;
		Sim->Debug_SetVerifyCaches(false);
		GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
		GLog->RemoveOutputDevice(&Log);
		Expect(bOk && bFirst && bSecond && W1.IsEmpty() && W2.IsEmpty() && BedReuses == 1 && WalkReuses == 1 && Log.Verify == 0,
			FString::Printf(TEXT("AT11 the preflight's memos: a second CanOpenAtrium with nothing changed reuses the bed count of -1 (%d reuse(s)) and the walk test (%d), each compared with a fresh derivation under -RHVerifyCaches: %d RHVERIFY line(s); both accept (%s / %s)"),
				BedReuses, WalkReuses, Log.Verify, W1.IsEmpty() ? TEXT("accepted") : *W1, W2.IsEmpty() ? TEXT("accepted") : *W2));
	}

	// --- AT12. Close. ---
	{
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2), FIntPoint(5, 1), FIntPoint(5, 0) }) { bOk &= Sim->CarveLatticePoint(-1, P); }
		bOk &= Fit(0);
		const TArray<int32> Hall = AddResidents(4, -3);
		const URHSimWorldSubsystem::FRHHopeBreakdown Twin = Sim->GetColonyHope();
		const double Req = Sim->GetFloorO2RequiredKg(-1);
		const int32 Beds = Sim->GetFloorBeds(-1);
		bOk &= Send(AtriumCmd(true, C1));
		const bool bClosed = Send(AtriumCmd(false, C1)) && Sim->GetAtriums().Num() == 0;
		// Nothing refunded: the open and the close by the harness path, with no step between, move neither the store nor the pools back.
		for (const int32 Id : Hall) { Sim->Debug_SetColonistStation(Id, FIntVector(-3, Ord(-3, 3, 0), 0)); } // seats in the hall: they face it
		const URHSimWorldSubsystem::FRHHopeBreakdown HPre = Sim->GetColonyHope();
		bOk &= Open(C1);
		const double Stock1 = Sim->GetStock(NOxygen), Fill1 = Sim->GetFloorO2Kg(-1);
		const double HopeOpen = Sim->GetColonyHope().Total;
		bOk &= Close(C1);
		const bool bNoRefund = Sim->GetStock(NOxygen) == Stock1 && Sim->GetFloorO2Kg(-1) == Fill1;
		const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
		FRHRobotRoadPath Path;
		FRHDeepRouteCost Cost;
		const double Stamp = Clock->GetSimSecondsTotal();
		FVector A = Sim->GetFloorCellCentreCm(-1, Ord(-1, 1, 0)), B = Sim->GetFloorCellCentreCm(-1, Ord(-1, 5, 0));
		A.Z = B.Z = -Sim->GetFloorHeightCm() + 50.0;
		const bool bWalk = Sim->PlanDeepRoute(-1, A, -1, B, Stamp, Path, &Cost) && Cost.LatticeCm == 4000;
		const int32 O = Ord(-1, 3, 0);
		const bool bZone = Sim->DesignateRoom(-1, O, NLiving, R) && Sim->DesignateRoom(-1, O, NAME_None, R);
		Expect(bOk && bClosed && bZone && Sim->GetFloorBeds(-1) == Beds && bWalk && !Sim->IsCellOpen(-1, O) && H.Total == HPre.Total && HopeOpen > HPre.Total && Twin.Atrium == 0.0 && H.Atrium == 0.0 && Sim->GetFloorO2RequiredKg(-1) == Req && bNoRefund,
			FString::Printf(TEXT("AT12 closed through the order: the cells are ordinary again (zoned and cleared %d, %d beds back, the direct leg straight %d), Hope %.6f with the atrium and back to the twin's %.6f (%.6f), the premium gone (%.1f kg needed), nothing refunded %d (setup %d, closed %d)"),
				bZone ? 1 : 0, Beds, bWalk ? 1 : 0, HopeOpen, HPre.Total, H.Total, Req, bNoRefund ? 1 : 0, bOk ? 1 : 0, bClosed ? 1 : 0));
	}

	// --- AT13. A Homes shape and an atrium on one floor. ---
	{
		// -1 and -2: a 3 x 3 x 2 cavern (1,-1) to (3,1) beside the column, carved first (ordinals 0-8), then the rest of the spiral's first ring
		// (five cells, the three the box holds skipped). A Homes shape over (-2,-2) to (-1,2) of -1 maps the floor. Five residents on -1 take the
		// box's cells first (outside the shape) and, once the box opens, the ring's (three of them inside it).
		Reset();
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-5, 60, R, 0);
		Place(TEXT("AirFilter"), -1, -1, -5);
		Sim->Debug_ForceRateFloor(-5);
		Sim->Debug_AddStock(NOxygen, 200000.0);
		int32 Cv = INDEX_NONE;
		bool bOk = Sim->Debug_DigCavern(-1, 2, FIntPoint(1, -1), FIntPoint(3, 1), Cv, Why) && Sim->Debug_CarveCavernCells(Cv, 18) == 18;
		bOk &= Sim->ExcavateFloor(-1, 5, R, 0) && Sim->ExcavateFloor(-2, 5, R, 0);
		bOk &= Send(ShapeCmd(-1, 1, FIntPoint(-2, -2), FIntPoint(-1, 2)));
		AddResidents(5, -1);
		// The walk network's landing of shaft 0 on -1 (BuildWalkFloor: the column's lowest-ordinal 4-neighbour that is not open): the box cell (1,0)
		// (ordinal 0, carved first) before the open; with the box open, never a landing on the void: (0,1), the lowest of the three spiral
		// neighbours left (ordinals 9, 11 and 13); (1,0) again after the close.
		const int32 Land0 = Sim->Debug_WalkLanding(-1, 0);
		const auto ShapeResidents = [&]()
		{
			TArray<FRHDistrictCounts> Counts;
			Sim->GetDistrictCounts(Counts);
			int32 N = 0;
			for (const FRHDistrictCounts& C : Counts) { N += C.Residents; }
			return N;
		};
		const int32 Before = ShapeResidents();
		FRHAtriumLogCounter Log;
		GLog->AddOutputDevice(&Log);
		Sim->Debug_SetVerifyCaches(true);
		bOk &= Open(Cv);
		const int32 After = ShapeResidents();
		const bool bFreshOpen = Sim->Debug_DistrictCachesFresh() && Sim->Debug_DeepWalkFresh() && Sim->Debug_CommuteCachesFresh() && Sim->Debug_AtriumCachesFresh();
		const int32 Land1 = Sim->Debug_WalkLanding(-1, 0);
		Sim->GetColonyHope();
		bOk &= Close(Cv);
		const int32 Closed = ShapeResidents();
		const bool bFreshClose = Sim->Debug_DistrictCachesFresh() && Sim->Debug_DeepWalkFresh() && Sim->Debug_CommuteCachesFresh();
		const int32 Land2 = Sim->Debug_WalkLanding(-1, 0);
		Sim->GetColonyHope();
		Sim->Debug_SetVerifyCaches(false);
		GLog->FlushThreadedLogs(); // the log runs on its own thread: what the steps above logged reaches the counter before it is read
		GLog->RemoveOutputDevice(&Log);
		Expect(bOk && Sim->IsFloorMapped(-1) && Before == 0 && After == 3 && Closed == 0 && bFreshOpen && bFreshClose && Log.Verify == 0,
			FString::Printf(TEXT("AT13 a shape and an atrium on -1: the shape's residents %d -> %d at the open (the three ring cells (-1,y) inside it) and back to %d at the close, each matching a fresh derivation (membership, the walk network, the commute, the atrium caches); %d RHVERIFY line(s)"),
				Before, After, Closed, Log.Verify));
		const int32 Box10 = Ord(-1, 1, 0), Spiral01 = Ord(-1, 0, 1);
		Expect(Land0 == Box10 && Box10 == 0 && Land1 == Spiral01 && Land1 != INDEX_NONE && !Sim->IsCellOpen(-1, Land1) && Land2 == Box10,
			FString::Printf(TEXT("AT13 the walk landing of shaft 0 on -1: %d (the box cell (1,0)) before the open, %d (the spiral cell (0,1), ordinal %d: never a landing on the void) with it open, %d after the close"),
				Land0, Land1, Spiral01, Land2));
	}

	return Finish();
}
