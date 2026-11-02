#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// BM-7 moving self-test (docs/qa/2026-10-31/bm7-move-design.md: DIRECTOR ANSWER (2026-10-31) and the BRIEF, section 3). Fails loudly.
// `-moves`. A free Move order picks up a furnishing or a wall fitting and puts it down anywhere a new one of its row could go; it spends and
// refunds nothing, the piece keeps its Id and its place in the array, and a furnishing's bed guard is judged on the end state. Every
// comparison is exact. Orders land only inside EraStep: they are queued with EnqueueCommand (no preflight) and stepped, as -furnish does.
//  MV0 the fixture, fail fast (two separate rated one-building habitats H1 and H2 with their beds and residents, an unrated habitat, two
//  door networks, a zoned rated floor); MV1 a move within a habitat (Id, index, count, NextId, the new spot, stock, both change events, the
//  log line); MV2 in place (a turn, a nudge inside its own footprint, the same spot); MV3 far moves (habitat to habitat, cell to cell, cell to
//  habitat, a bed into a work room, at zero stock); MV4 every destination refusal against Furnish's (and D7's open atrium cell); MV5 the
//  end-state bed guard, Before / After / Residents per place; MV6 seats; MV7 in transit, both ways and at landing; MV8 fittings (views,
//  kinds, an occupied edge, an airlock's chamber, seals, the same edge, zero stock); MV9 a queued Move across a save and a load; MV10 a bad
//  order; MV11 determinism over two fresh fixtures.
// The suite prints no file size and no save version.
namespace
{
	// The sim's "Moved ..." lines while it is attached (MV1: the move is logged).
	struct FRHMoveLogLines : public FOutputDevice
	{
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogRedHopeSim")) && FCString::Strncmp(Text, TEXT("Moved "), 6) == 0)
			{
				Lines.Add(Text);
			}
		}
	};

	// One fixture's handles: the habitats and their bays (station keys), floor -1's cells, the pieces and the fittings.
	struct FRHMoveFixture
	{
		int32 H1 = 0, H2 = 0, U = 0, S1 = 0, S2 = 0, Z = 0, Mast = 0, H1Filter = 0;
		FIntVector A, B, C, D;               // H1: Living a (the Bunk Bed, its only bed piece), Living b and c (bare), Hallway d (the Air Filter)
		FIntVector X, Y, W, F;               // H2: Living x (a Locker), Living y (bare), Workstation w (a Desk), Workstation f (the Air Filter)
		FIntVector S1L, S1D, S1R, S2L, S2D;  // S1: Living | Dining | Living (two doorways); S2: Living | Dining (one); both unrated
		FIntVector UBay, ZBay;               // U: one unzoned bay (unrated); Z: one Dining bay (a Plant; MV10 demolishes it)
		FIntVector C0, C1, C2, C3, C4, C5, C6; // floor -1: Living, Living, Workstation, Dining, Dining, Lab, Hallway (its Air Filter)
		int32 Bunk = 0, Lamp = 0, Sofa = 0, Locker = 0, Desk = 0, FloorBunk = 0, FloorPlant = 0, ZPlant = 0;
		int32 Window = 0, Airlock = 0;
		TArray<int32> H1Res, H2Res, FloorRes; // colonist Ids by home, ascending
	};

	// What MV9 and MV11 compare, field by field.
	struct FRHMoveSnapshot
	{
		TArray<FRHFurnitureInstance> Pieces;
		TArray<FRHWallFeature> Fittings;
		TArray<FIntVector> Assign;   // (colonist Id, bed piece, seat piece)
		TArray<FIntPoint> Stations;  // GetStationSeats (filled, capacity) of each work key
		TArray<int32> Beds;          // H1, H2, floor -1
	};

	bool SameSnapshot(const FRHMoveSnapshot& P, const FRHMoveSnapshot& Q, FString& OutWhy)
	{
		if (P.Pieces.Num() != Q.Pieces.Num() || P.Fittings.Num() != Q.Fittings.Num())
		{
			OutWhy = FString::Printf(TEXT("%d / %d pieces, %d / %d fittings"), P.Pieces.Num(), Q.Pieces.Num(), P.Fittings.Num(), Q.Fittings.Num());
			return false;
		}
		for (int32 i = 0; i < P.Pieces.Num(); ++i)
		{
			const FRHFurnitureInstance& L = P.Pieces[i];
			const FRHFurnitureInstance& M = Q.Pieces[i];
			if (L.Id != M.Id || L.Row != M.Row || L.Key != M.Key || L.Slot != M.Slot || L.Rot != M.Rot)
			{
				OutWhy = FString::Printf(TEXT("piece %d: #%d %s (%d, %d, %d) slot %d rot %d against #%d %s (%d, %d, %d) slot %d rot %d"), i, L.Id, *L.Row.ToString(), L.Key.X, L.Key.Y, L.Key.Z,
					L.Slot, L.Rot, M.Id, *M.Row.ToString(), M.Key.X, M.Key.Y, M.Key.Z, M.Slot, M.Rot);
				return false;
			}
		}
		for (int32 i = 0; i < P.Fittings.Num(); ++i)
		{
			if (P.Fittings[i].Id != Q.Fittings[i].Id || P.Fittings[i].Row != Q.Fittings[i].Row || P.Fittings[i].Edge != Q.Fittings[i].Edge)
			{
				OutWhy = FString::Printf(TEXT("fitting %d: #%d against #%d"), i, P.Fittings[i].Id, Q.Fittings[i].Id);
				return false;
			}
		}
		if (P.Assign != Q.Assign || P.Stations != Q.Stations || P.Beds != Q.Beds)
		{
			OutWhy = FString::Printf(TEXT("assignments %s, station seats %s, beds %s"), P.Assign == Q.Assign ? TEXT("equal") : TEXT("DIFFER"),
				P.Stations == Q.Stations ? TEXT("equal") : TEXT("DIFFER"), P.Beds == Q.Beds ? TEXT("equal") : TEXT("DIFFER"));
			return false;
		}
		return true;
	}
}

int32 RHRunMovesSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== MOVES TEST (BM-7, 2026-10-31) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("MOVES ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("MOVES FAIL: %s"), *What); ++Fails; }
	};
	const int32 StepsPerSol = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
	const auto Step = [&]()
	{
		Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
		Sim->Debug_SetBatteryWh(1.0e9);
		Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
		Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
	};
	const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * StepsPerSol); ++S) { Step(); } };
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	using EEdge = URHSimWorldSubsystem::ERHWallEdge;
	using FPlace = URHSimWorldSubsystem::FRHMoveBedPlace;
	const FName NStruct(TEXT("Struct")), NGlass(TEXT("Glass")), NLiving(TEXT("LivingQuarters")), NDining(TEXT("Dining")), NWork(TEXT("Workstation"));
	const FName NHall(TEXT("Hallway")), NLab(TEXT("Lab")), NFurniture(TEXT("Furniture")), NWallFeature(TEXT("WallFeature"));
	FString R, Why, Why2;
	TArray<FString> Rejects;
	TArray<FIntVector> FurnChanged, WallChanged;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const FDelegateHandle FurnH = Sim->OnFurnitureChanged.AddLambda([&FurnChanged](const FIntVector& K) { FurnChanged.Add(K); });
	const FDelegateHandle WallH = Sim->OnWallFeatureChanged.AddLambda([&WallChanged](const FIntVector& E) { WallChanged.Add(E); });
	FRHMoveLogLines MovedLog;
	GLog->AddOutputDevice(&MovedLog);
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const TCHAR* SaveSlots[] = { TEXT("movebase"), TEXT("movequeued") };
	const auto Finish = [&]() -> int32
	{
		GLog->RemoveOutputDevice(&MovedLog);
		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnFurnitureChanged.Remove(FurnH);
		Sim->OnWallFeatureChanged.Remove(WallH);
		for (const TCHAR* Slot : SaveSlots)
		{
			IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot)), false, true, true);
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("=== MOVES TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};

	// --- MV0. The fixture, fail fast. ---
	const bool bCatalogues = DefsSub && DefsSub->HasFurnitureCatalogue() && DefsSub->HasWallCatalogue();
	const FRHFurnitureRow* BunkRow = bCatalogues ? DefsSub->GetFurniture(FName("BunkBed")) : nullptr;
	const FRHDoorRow* WinRow = bCatalogues ? DefsSub->GetDoor(FName("Window")) : nullptr;
	const bool bRows = BunkRow && BunkRow->Capacity == 2 && WinRow && DefsSub->GetFurniture(FName("Locker")) && DefsSub->GetFurniture(FName("Lamp")) && DefsSub->GetFurniture(FName("Sofa"))
		&& DefsSub->GetFurniture(FName("Desk")) && DefsSub->GetFurniture(FName("Plant")) && DefsSub->GetFurniture(FName("Rug")) && DefsSub->GetDoor(FName("PressureDoor")) && DefsSub->GetDoor(FName("Airlock"));
	Expect(bCatalogues && bRows, TEXT("MV0 DT_Furniture and the walls catalogue are present, with every row the fixture uses (the Bunk Bed sleeps 2)"));
	if (!bCatalogues || !bRows)
	{
		return Finish();
	}
	const TArray<FName> FurnBill = { NStruct };            // GetFurnitureCost: every row's bill is Struct (-furnish F0)
	TArray<FName> WinBill;
	URHDefinitionsSubsystem::GetDoorCost(*WinRow).GenerateKeyArray(WinBill);
	WinBill.Sort([](const FName& P, const FName& Q) { return P.LexicalLess(Q); });
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Place = [&](const TCHAR* Def, double PX, double PY, int32 SX = 1, int32 SY = 1, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(PX, PY, 0.0), Level, SX, SY);
		return LastIdOf(Def);
	};
	const auto LanderKg = [&](FName Res, bool bOut)
	{
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == LanderId) { return (bOut ? Bd.OutputKg : Bd.InputKg).FindRef(Res); } }
		return 0.0;
	};
	// "Stock unchanged": per resource of the bill, the pool, the solids, the Lander's two shelves and the ledger's Used and Made legs.
	const auto StockOf = [&](const TArray<FName>& Bill)
	{
		TArray<double> V;
		for (const FName& Res : Bill)
		{
			V.Add(Sim->GetStock(Res));
			V.Add(Sim->GetTotalSolid(Res));
			V.Add(LanderKg(Res, false));
			V.Add(LanderKg(Res, true));
			V.Add((double)Sim->GetLedgerLifetimeG(Res, ELeg::Used));
			V.Add((double)Sim->GetLedgerLifetimeG(Res, ELeg::Made));
			V.Add(Sim->GetLedgerOpenKg(Res, ELeg::Used));
			V.Add(Sim->GetLedgerOpenKg(Res, ELeg::Made));
		}
		return V;
	};
	// Every kg of a resource out of the colony (each building's shelves and the pool): exactly 0 after.
	const auto Drain = [&](FName Res)
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings())
		{
			const double In = Bd.InputKg.FindRef(Res), Out = Bd.OutputKg.FindRef(Res);
			if (In != 0.0) { Sim->Debug_AddSolidAt(Bd.Id, Res, -In, false); }
			if (Out != 0.0) { Sim->Debug_AddSolidAt(Bd.Id, Res, -Out, true); }
		}
		if (Sim->GetStock(Res) != 0.0) { Sim->AddStock(Res, -Sim->GetStock(Res)); }
	};
	const auto IndexOf = [&](int32 PieceId) { return Sim->GetFurniture().IndexOfByPredicate([PieceId](const FRHFurnitureInstance& P) { return P.Id == PieceId; }); };
	const auto Piece = [&](int32 PieceId) { const FRHFurnitureInstance* P = Sim->FindFurniture(PieceId); return P ? *P : FRHFurnitureInstance(); };
	const auto FurnishCmd = [&](const TCHAR* Row, const FIntVector& InKey, int32 InSlot, int32 InRot)
	{
		FRHCommand Cmd; Cmd.Verb = FName("Furnish"); Cmd.Target = FName(Row); Cmd.Level = InKey.Z == 0 ? InKey.X : 0;
		Cmd.Location = Sim->GetCellCentreCm(InKey);
		Cmd.Value = URHSimWorldSubsystem::EncodeFurnish(InKey.Z == 0 ? InKey.Y : 0, InSlot, InRot);
		return Cmd;
	};
	// A furnishing Move carries exactly a Furnish order's Level, Location and Value for the destination, and the Id in LocationB.X.
	const auto MoveCmd = [&](int32 PieceId, const FIntVector& InKey, int32 InSlot, int32 InRot)
	{
		FRHCommand Cmd = FurnishCmd(TEXT("None"), InKey, InSlot, InRot);
		Cmd.Verb = FName("Move");
		Cmd.Target = NFurniture;
		Cmd.LocationB = FVector((double)PieceId, 0.0, 0.0);
		return Cmd;
	};
	// A fitting Move carries a Fit order's Location (a point in the bay) and side, and the Id in LocationB.X.
	const auto FitMoveCmd = [&](int32 FittingId, const FVector& At, int32 Dir)
	{
		FRHCommand Cmd; Cmd.Verb = FName("Move"); Cmd.Target = NWallFeature; Cmd.Level = 0; Cmd.Location = At; Cmd.Value = (double)Dir;
		Cmd.LocationB = FVector((double)FittingId, 0.0, 0.0);
		return Cmd;
	};
	const auto FitCmd = [&](const TCHAR* Row, const FVector& At, int32 Dir)
	{
		FRHCommand Cmd; Cmd.Verb = FName("Fit"); Cmd.Target = FName(Row); Cmd.Level = 0; Cmd.Location = At; Cmd.Value = (double)Dir;
		return Cmd;
	};
	// One order, one step (the lag of 45 s is under the 60 s era step); true when it was not rejected. Landed says how the last one went.
	FString Landed;
	const auto Land = [&](const FRHCommand& Cmd)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(Cmd);
		Step();
		Landed = Rejects.Num() == R0 ? FString(TEXT("landed")) : FString::Printf(TEXT("rejected: %s"), *LastReject());
		return Rejects.Num() == R0;
	};
	const auto Centre = [&](const FIntVector& InKey) { return Sim->GetCellCentreCm(InKey); };
	const auto EdgeOf = [&](const FIntVector& Bay, int32 Dir) { FIntVector E = FIntVector::ZeroValue; Sim->ResolveWallEdge(Centre(Bay), Dir, E); return E; };
	const auto FloorResidents = [&](int32 Level) { int32 N = 0; for (const FRHColonist& Col : Sim->GetColonists()) { N += (Col.HomeHab == 0 && Col.HomeLevel == Level) ? 1 : 0; } return N; };
	const auto PlacesText = [](const TArray<FPlace>& Places)
	{
		FString S;
		for (int32 i = 0; i < Places.Num(); ++i)
		{
			const FPlace& P = Places[i];
			S += FString::Printf(TEXT("%s%s %s: Before %d, After %d, Residents %d"), i == 0 ? TEXT("") : TEXT("; "), i == 0 ? TEXT("S") : TEXT("D"),
				P.HabId != 0 ? *FString::Printf(TEXT("habitat #%d"), P.HabId) : *FString::Printf(TEXT("floor %d"), P.Level), P.Before, P.After, P.Residents);
		}
		return S;
	};
	// The ledger rolls its open kg into the lifetime grams at a sol's turn: a step that compares them must not cross one.
	const auto ClearOfSolTurn = [&]()
	{
		const double T = Clock->GetSimSecondsTotal(), SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		if (FMath::FloorToInt64(T / SolLen) != FMath::FloorToInt64((T + (double)URHSimClockSubsystem::EraStepSimSeconds) / SolLen))
		{
			Step();
		}
	};
	const auto EdgeOfFitting = [&](int32 FittingId) { const FRHWallFeature* Wf = Sim->FindWallFeature(FittingId); return Wf ? Wf->Edge : FIntVector(-999, -999, -999); };
	const auto CancelLast = [&]() { if (Sim->GetUplinkQueue().Num() > 0) { Sim->CancelUplinkCommand(Sim->GetUplinkQueue().Last().CommandId); } };
	const auto BedReason = [](int32 Residents) { return FString::Printf(TEXT("%d resident(s) sleep here - moving this would leave someone without a bed"), Residents); };
	const auto Seats = [&](const FIntVector& InKey) { int32 Filled = 0, Cap = 0; Sim->GetStationSeats(InKey, Filled, Cap); return FIntPoint(Filled, Cap); };

	FString BaseErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("movebase"), BaseErr);
	FRHMoveFixture Fx;
	// The fixture, from the saved base colony: power, stock, floor -1 (7 cells, rated), the habitats and their pieces, and 13 residents placed
	// by hand (4 in H1, 1 in H2, 8 on floor -1: each home exactly full).
	const auto Build = [&]() -> bool
	{
		FString LoadErr;
		bool bOk = Sim->LoadColony(TEXT("movebase"), LoadErr) && LoadErr.IsEmpty();
		Rejects.Reset();
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetColonistsPerCell(1.0);
		Fx = FRHMoveFixture();
		Place(TEXT("SolarArray"), 3500.0, -1000.0);
		Place(TEXT("SolarArray"), 3500.0, -2000.0);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		Sim->AddStock(FName("Shielding"), 1000.0);
		Sim->AddStock(NGlass, 1000.0);
		Sim->Debug_AddSolidAt(LastIdOf(TEXT("Lander")), NStruct, 5000.0, false);
		Sim->ExtendShaft(1, FVector(3000.0, 3000.0, 0.0));
		bOk &= Sim->ExcavateFloor(-1, 7, R);
		Fx.C0 = FIntVector(-1, 0, 0); Fx.C1 = FIntVector(-1, 1, 0); Fx.C2 = FIntVector(-1, 2, 0); Fx.C3 = FIntVector(-1, 3, 0);
		Fx.C4 = FIntVector(-1, 4, 0); Fx.C5 = FIntVector(-1, 5, 0); Fx.C6 = FIntVector(-1, 6, 0);
		const FVector C6At = Centre(Fx.C6);
		Place(TEXT("AirFilter"), C6At.X, C6At.Y, 1, 1, -1);
		Sim->Debug_ForceRateFloor(-1);
		Step();
		const FName CellRooms[] = { NLiving, NLiving, NWork, NDining, NDining, NLab, NHall };
		for (int32 i = 0; i < 7; ++i) { bOk &= Sim->DesignateRoom(-1, i, CellRooms[i], R); }
		Fx.H1 = Place(TEXT("HabModule"), -2500.0, 1000.0, 4, 1);
		Fx.H2 = Place(TEXT("HabModule"), -2500.0, -2000.0, 4, 1);
		Fx.U = Place(TEXT("HabModule"), -7000.0, 6000.0);
		Fx.Z = Place(TEXT("HabModule"), -7000.0, -6000.0);
		Fx.S1 = Place(TEXT("HabModule"), 3000.0, 8000.0, 3, 1);
		Fx.S2 = Place(TEXT("HabModule"), 8500.0, 8000.0, 2, 1);
		Fx.A = FIntVector(0, 0, Fx.H1); Fx.B = FIntVector(0, 1, Fx.H1); Fx.C = FIntVector(0, 2, Fx.H1); Fx.D = FIntVector(0, 3, Fx.H1);
		Fx.X = FIntVector(0, 0, Fx.H2); Fx.Y = FIntVector(0, 1, Fx.H2); Fx.W = FIntVector(0, 2, Fx.H2); Fx.F = FIntVector(0, 3, Fx.H2);
		Fx.S1L = FIntVector(0, 0, Fx.S1); Fx.S1D = FIntVector(0, 1, Fx.S1); Fx.S1R = FIntVector(0, 2, Fx.S1);
		Fx.S2L = FIntVector(0, 0, Fx.S2); Fx.S2D = FIntVector(0, 1, Fx.S2);
		Fx.UBay = FIntVector(0, 0, Fx.U); Fx.ZBay = FIntVector(0, 0, Fx.Z);
		Fx.H1Filter = Place(TEXT("AirFilter"), Centre(Fx.D).X, Centre(Fx.D).Y);
		Place(TEXT("AirFilter"), Centre(Fx.F).X, Centre(Fx.F).Y);
		const TPair<FIntVector, FName> BayRooms[] = { { Fx.A, NLiving }, { Fx.B, NLiving }, { Fx.C, NLiving }, { Fx.D, NHall }, { Fx.X, NLiving }, { Fx.Y, NLiving },
			{ Fx.W, NWork }, { Fx.F, NWork }, { Fx.S1L, NLiving }, { Fx.S1D, NDining }, { Fx.S1R, NLiving }, { Fx.S2L, NLiving }, { Fx.S2D, NDining }, { Fx.ZBay, NDining } };
		for (const TPair<FIntVector, FName>& Zone : BayRooms) { bOk &= Sim->DesignateBay(Zone.Key.Z, Zone.Key.Y, Zone.Value, R); }
		// The airlock's chamber outside S1's west wall holds a machine (MV8 (iv)).
		Fx.Mast = Place(TEXT("Floodmast"), Centre(Fx.S1L).X - 700.0, Centre(Fx.S1L).Y);
		RunSols(1.0);
		Sim->Debug_SetVaultRated(true);
		// The pieces, through Furnish and Fit orders (the bed first, so a's add-only guard sees a bed bay).
		const auto LandPiece = [&](const TCHAR* Row, const FIntVector& InKey, int32 InSlot, int32 InRot, int32& OutId)
		{
			const bool bLanded = Land(FurnishCmd(Row, InKey, InSlot, InRot));
			OutId = Sim->GetFurniture().Num() > 0 ? Sim->GetFurniture().Last().Id : 0;
			return bLanded;
		};
		bOk &= LandPiece(TEXT("BunkBed"), Fx.A, 0, 0, Fx.Bunk) && LandPiece(TEXT("Lamp"), Fx.D, 0, 0, Fx.Lamp) && LandPiece(TEXT("Sofa"), Fx.S1D, 10, 0, Fx.Sofa)
			&& LandPiece(TEXT("Locker"), Fx.X, 0, 0, Fx.Locker) && LandPiece(TEXT("Desk"), Fx.W, 0, 0, Fx.Desk) && LandPiece(TEXT("BunkBed"), Fx.C0, 0, 0, Fx.FloorBunk)
			&& LandPiece(TEXT("Plant"), Fx.C3, 0, 0, Fx.FloorPlant) && LandPiece(TEXT("Plant"), Fx.ZBay, 0, 0, Fx.ZPlant);
		bOk &= Land(FitCmd(TEXT("Window"), Centre(Fx.A), 2));
		Fx.Window = Sim->GetWallFeatures().Num() > 0 ? Sim->GetWallFeatures().Last().Id : 0;
		bOk &= Land(FitCmd(TEXT("Airlock"), Centre(Fx.S2D), 0));
		Fx.Airlock = Sim->GetWallFeatures().Num() > 0 ? Sim->GetWallFeatures().Last().Id : 0;
		// The residents: housed on a generous bed count, then homed by hand, each home exactly full.
		Sim->Debug_SetColonistsPerCell(100.0);
		const int32 Before = Sim->GetPopulation();
		bOk &= Sim->Debug_AddColonists(13) == 13;
		Sim->Debug_SetColonistsPerCell(1.0);
		const TArray<FRHColonist>& All = Sim->GetColonists();
		TArray<int32> NewIds;
		for (int32 i = Before; i < All.Num(); ++i) { NewIds.Add(All[i].Id); }
		NewIds.Sort();
		for (int32 i = 0; i < NewIds.Num(); ++i)
		{
			if (i < 4) { Sim->Debug_SetColonistHome(NewIds[i], Fx.H1, 0); Fx.H1Res.Add(NewIds[i]); }
			else if (i < 5) { Sim->Debug_SetColonistHome(NewIds[i], Fx.H2, 0); Fx.H2Res.Add(NewIds[i]); }
			else { Sim->Debug_SetColonistHome(NewIds[i], 0, -1); Fx.FloorRes.Add(NewIds[i]); }
		}
		Sim->Debug_RefreshJobs();
		Step();
		Rejects.Reset();
		FurnChanged.Reset();
		WallChanged.Reset();
		GLog->FlushThreadedLogs();
		MovedLog.Lines.Reset();
		return bOk;
	};
	const auto Snapshot = [&]()
	{
		FRHMoveSnapshot S;
		S.Pieces = Sim->GetFurniture();
		S.Fittings = Sim->GetWallFeatures();
		for (const FRHColonist& Col : Sim->GetColonists()) { S.Assign.Add(FIntVector(Col.Id, Sim->GetColonistBedPiece(Col.Id), Sim->GetColonistSeatPiece(Col.Id))); }
		for (const FIntVector& K : { Fx.W, Fx.F, Fx.C2, Fx.C5 }) { S.Stations.Add(Seats(K)); }
		S.Beds = { Sim->GetHabBeds(Fx.H1), Sim->GetHabBeds(Fx.H2), Sim->GetFloorBeds(-1) };
		return S;
	};

	{
		const bool bBuilt = bBaseSaved && Build();
		Expect(bBuilt && Fx.Bunk != 0 && Fx.Window != 0 && Fx.Airlock != 0 && Fx.ZPlant != 0 && Sim->GetPopulation() >= 13,
			FString::Printf(TEXT("MV0 the fixture is built from the saved base: 8 pieces and 2 fittings landed by order, 13 residents%s"), BaseErr.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *BaseErr)));
		bool bBays = true;
		for (const FIntVector& K : { Fx.A, Fx.B, Fx.C, Fx.D, Fx.X, Fx.Y, Fx.W, Fx.F, Fx.S1L, Fx.S1D, Fx.S1R, Fx.S2L, Fx.S2D, Fx.UBay, Fx.ZBay })
		{
			int32 Hab = 0, Bay = INDEX_NONE;
			bBays &= Sim->FindBayAt(Centre(K), Hab, Bay) && Hab == K.Z && Bay == K.Y;
		}
		const auto BuildingOf = [&](int32 BuildingId) { return Sim->GetBuildings().FindByPredicate([BuildingId](const FRHBuildingInstance& Bd) { return Bd.Id == BuildingId; }); };
		const FRHBuildingInstance* H1B = BuildingOf(Fx.H1);
		const FRHBuildingInstance* H2B = BuildingOf(Fx.H2);
		Expect(bBays && H1B && H2B && H1B->SizeX * H1B->SizeY == 4 && H2B->SizeX * H2B->SizeY == 4 && Sim->GetHabNetworkRoot(Fx.H1) == Fx.H1 && Sim->GetHabNetworkRoot(Fx.H2) == Fx.H2
			&& Sim->GetHabNetworkBays(Fx.H1) == 4 && Sim->GetHabNetworkBays(Fx.H2) == 4 && Sim->IsHabRated(Fx.H1) && Sim->IsHabRated(Fx.H2),
			FString::Printf(TEXT("MV0 H1 #%d and H2 #%d are two separate rated habitat networks, each one 4-bay Habitat building (networks of %d and %d bays), and every bay key resolves from its centre"),
				Fx.H1, Fx.H2, Sim->GetHabNetworkBays(Fx.H1), Sim->GetHabNetworkBays(Fx.H2)));
		TArray<int32> H1Pieces;
		for (const FRHFurnitureInstance& P : Sim->GetFurniture()) { if (P.Key.Z == Fx.H1) { H1Pieces.Add(P.Id); } }
		Expect(Sim->GetBayRoom(Fx.H1, 0) == NLiving && Sim->GetBayRoom(Fx.H1, 1) == NLiving && Sim->GetBayRoom(Fx.H1, 2) == NLiving && Sim->GetCellFurniture(Fx.A) == 1
			&& Sim->GetCellFurniture(Fx.B) == 0 && Sim->GetCellFurniture(Fx.C) == 0 && Sim->GetCellBeds(Fx.A) == 2 && Sim->GetCellBeds(Fx.B) == 1 && Sim->GetCellBeds(Fx.C) == 1
			&& Sim->GetHabBeds(Fx.H1) == 4 && Sim->GetHabResidents(Fx.H1) == 4,
			FString::Printf(TEXT("MV0 H1: Living a holds Bunk Bed #%d (2 beds, H1's only bed piece), b and c are bare (1 legacy bed each); H1 beds %d, residents %d"),
				Fx.Bunk, Sim->GetHabBeds(Fx.H1), Sim->GetHabResidents(Fx.H1)));
		Expect(Sim->GetCellFurniture(Fx.X) == 1 && Piece(Fx.Locker).Key == Fx.X && Sim->GetCellFurniture(Fx.Y) == 0 && Sim->GetBayRoom(Fx.H2, 2) == NWork && Sim->GetBayRoom(Fx.H2, 3) == NWork
			&& Sim->GetHabBeds(Fx.H2) == 1 && Sim->GetHabResidents(Fx.H2) == 1,
			FString::Printf(TEXT("MV0 H2: Living x holds Locker #%d (no beds), Living y is bare, w and f are work bays; H2 beds %d, residents %d (full)"),
				Fx.Locker, Sim->GetHabBeds(Fx.H2), Sim->GetHabResidents(Fx.H2)));
		const FIntVector UNorth = EdgeOf(Fx.UBay, 2);
		Expect(!Sim->IsHabRated(Fx.U) && Sim->ClassifyWallEdge(UNorth) == EEdge::Exterior && !Sim->FindWallFeatureOnEdge(UNorth) && Sim->GetBayRoom(Fx.U, 0).IsNone(),
			FString::Printf(TEXT("MV0 U #%d is an unrated habitat (one unzoned bay) with its north wall (%d, %d, %d) outside and free"), Fx.U, UNorth.X, UNorth.Y, UNorth.Z));
		int32 Ways1 = 0, Doored1 = 0, Ways2 = 0, Doored2 = 0;
		Sim->GetHabNetworkDoorways(Fx.S1, Ways1, Doored1);
		Sim->GetHabNetworkDoorways(Fx.S2, Ways2, Doored2);
		Expect(Ways1 == 2 && Doored1 == 0 && Ways2 == 1 && Doored2 == 0 && Sim->GetHabNetworkRoot(Fx.S1) != Sim->GetHabNetworkRoot(Fx.S2),
			FString::Printf(TEXT("MV0 S1 #%d (Living | Dining | Living) has %d doorways, S2 #%d (Living | Dining) %d free doorway, two networks"), Fx.S1, Ways1, Fx.S2, Ways2));
		Expect(Sim->IsFloorRated(-1) && Sim->GetFloorCarvedCells(-1) == 7 && Sim->GetRoomAt(-1, 0) == NLiving && Sim->GetRoomAt(-1, 1) == NLiving && Sim->GetCellFurniture(Fx.C0) == 1
			&& Sim->GetCellBeds(Fx.C0) == 2 && Sim->GetFloorBeds(-1) == 8 && FloorResidents(-1) == 8,
			FString::Printf(TEXT("MV0 floor -1: 7 zoned cells, rated; Bunk Bed #%d in Living c0 is its only bed piece; beds %d (6 legacy + 2), residents %d"),
				Fx.FloorBunk, Sim->GetFloorBeds(-1), FloorResidents(-1)));
		UE_LOG(LogRedHopeSim, Display, TEXT("MOVES: places: H1 #%d beds %d residents %d; H2 #%d beds %d residents %d; U #%d beds %d residents %d; S1 #%d beds %d residents %d; S2 #%d beds %d residents %d; floor -1 beds %d residents %d"),
			Fx.H1, Sim->GetHabBeds(Fx.H1), Sim->GetHabResidents(Fx.H1), Fx.H2, Sim->GetHabBeds(Fx.H2), Sim->GetHabResidents(Fx.H2), Fx.U, Sim->GetHabBeds(Fx.U), Sim->GetHabResidents(Fx.U),
			Fx.S1, Sim->GetHabBeds(Fx.S1), Sim->GetHabResidents(Fx.S1), Fx.S2, Sim->GetHabBeds(Fx.S2), Sim->GetHabResidents(Fx.S2), Sim->GetFloorBeds(-1), FloorResidents(-1));
		if (!bBuilt)
		{
			return Finish();
		}
	}

	// --- MV1. A move within a habitat: the Lamp from the Hallway d into a (a bed bay: its beds stay 2). ---
	{
		const int32 Index0 = IndexOf(Fx.Lamp), Num0 = Sim->GetFurniture().Num(), Next0 = Sim->GetNextFurnitureId();
		const FRHFurnitureInstance Old = Piece(Fx.Lamp);
		ClearOfSolTurn();
		FurnChanged.Reset();
		GLog->FlushThreadedLogs(); // the log runs on its own thread: what was logged before reaches the device before it is cleared
		MovedLog.Lines.Reset();
		Sim->EnqueueCommand(MoveCmd(Fx.Lamp, Fx.A, 15, 1));
		const TArray<double> Stock0 = StockOf(FurnBill);
		const int32 R0 = Rejects.Num();
		Step();
		const TArray<double> Stock1 = StockOf(FurnBill);
		const FRHFurnitureInstance New = Piece(Fx.Lamp);
		Expect(Rejects.Num() == R0 && New.Id == Fx.Lamp && New.Row == Old.Row && IndexOf(Fx.Lamp) == Index0 && Sim->GetFurniture().Num() == Num0 && Sim->GetNextFurnitureId() == Next0,
			FString::Printf(TEXT("MV1 the Lamp moved from d to a keeps Id #%d, its index %d, the count %d and the next Id %d"), New.Id, IndexOf(Fx.Lamp), Sim->GetFurniture().Num(), Sim->GetNextFurnitureId()));
		Expect(Old.Key == Fx.D && Old.Slot == 0 && Old.Rot == 0 && New.Key == Fx.A && New.Slot == 15 && New.Rot == 1,
			FString::Printf(TEXT("MV1 its spot: (%d, %d, %d) slot %d rot %d -> (%d, %d, %d) slot %d rot %d"), Old.Key.X, Old.Key.Y, Old.Key.Z, Old.Slot, Old.Rot, New.Key.X, New.Key.Y, New.Key.Z, New.Slot, New.Rot));
		Expect(Stock0 == Stock1 && Stock0.Num() == 8, FString::Printf(TEXT("MV1 stock unchanged: Struct pool %.3f, solids %.3f, the Lander's shelves %.3f / %.3f, the ledger's Used and Made legs, each exact across the step"),
			Stock1[0], Stock1[1], Stock1[2], Stock1[3]));
		Expect(FurnChanged.Num() == 2 && FurnChanged[0] == Fx.D && FurnChanged[1] == Fx.A,
			FString::Printf(TEXT("MV1 OnFurnitureChanged fired for the old key, then the new key (%d event(s))"), FurnChanged.Num()));
		GLog->FlushThreadedLogs(); // ...and what the step logged reaches it before it is read
		const FString Want = FString::Printf(TEXT("Moved Lamp #%d from (0, 3, %d) slot 0 rot 0 to (0, 0, %d) slot 15 rot 1"), Fx.Lamp, Fx.H1, Fx.H1);
		Expect(MovedLog.Lines.Num() == 1 && MovedLog.Lines[0] == Want && Sim->GetHabBeds(Fx.H1) == 4,
			FString::Printf(TEXT("MV1 the move is logged: \"%s\"; H1 still has %d beds"), MovedLog.Lines.Num() > 0 ? *MovedLog.Lines[0] : TEXT("(none)"), Sim->GetHabBeds(Fx.H1)));
	}

	// --- MV2. In place: the Sofa (2 x 1) in S1's Dining bay. ---
	{
		const bool bTurn = Land(MoveCmd(Fx.Sofa, Fx.S1D, 10, 1));
		const FRHFurnitureInstance T = Piece(Fx.Sofa);
		Expect(bTurn && T.Key == Fx.S1D && T.Slot == 10 && T.Rot == 1, FString::Printf(TEXT("MV2 (i) the same slot, rot + 1: accepted (slot %d rot %d; its footprint 10,11 -> 10,14 overlaps its own) (%s)"), T.Slot, T.Rot, *Landed));
		const bool bNudge = Land(MoveCmd(Fx.Sofa, Fx.S1D, 6, 1));
		const FRHFurnitureInstance N = Piece(Fx.Sofa);
		Expect(bNudge && N.Key == Fx.S1D && N.Slot == 6 && N.Rot == 1, FString::Printf(TEXT("MV2 (ii) a nudge one sub-cell inside its own footprint (10,14 -> 6,10): accepted (slot %d) (%s)"), N.Slot, *Landed));
		Why.Reset();
		const bool bSame = Sim->CanMoveFurniture(Fx.Sofa, Fx.S1D, 6, 1, Why);
		const bool bSameOrder = !Land(MoveCmd(Fx.Sofa, Fx.S1D, 6, 1)) && LastReject() == TEXT("It is already there");
		Expect(!bSame && Why == TEXT("It is already there") && bSameOrder, FString::Printf(TEXT("MV2 (iii) the same slot and rot: refused, \"%s\", and the order too"), *Why));
	}

	// --- MV3. Far moves. ---
	{
		const bool bH = Land(MoveCmd(Fx.Lamp, Fx.W, 15, 0));
		Expect(bH && Piece(Fx.Lamp).Key == Fx.W && Piece(Fx.Lamp).Slot == 15 && Sim->GetHabBeds(Fx.H1) == 4 && Sim->GetHabBeds(Fx.H2) == 1,
			FString::Printf(TEXT("MV3 (i) the Lamp from H1 to H2's work bay w: accepted; beds H1 %d, H2 %d (%s)"), Sim->GetHabBeds(Fx.H1), Sim->GetHabBeds(Fx.H2), *Landed));
		const bool bCell = Land(MoveCmd(Fx.FloorPlant, Fx.C4, 0, 0));
		Expect(bCell && Piece(Fx.FloorPlant).Key == Fx.C4 && Sim->GetFloorBeds(-1) == 8, FString::Printf(TEXT("MV3 (ii) the Plant on floor -1 from c3 to c4: accepted; the floor keeps %d beds (%s)"), Sim->GetFloorBeds(-1), *Landed));
		const bool bUp = Land(MoveCmd(Fx.FloorPlant, Fx.F, 0, 0));
		Expect(bUp && Piece(Fx.FloorPlant).Key == Fx.F && Sim->GetCellFurniture(Fx.C4) == 0, FString::Printf(TEXT("MV3 (iii) the Plant from floor -1 up to H2's work bay f: accepted (%s)"), *Landed));
		Why.Reset(); Why2.Reset();
		const bool bBedWork = Sim->CanMoveFurniture(Fx.Bunk, Fx.W, 10, 0, Why);
		Sim->CanPlaceFurniture(FName("BunkBed"), Fx.W, 10, 0, Why2);
		const bool bBedOrder = !Land(MoveCmd(Fx.Bunk, Fx.W, 10, 0)) && LastReject() == Why2;
		Expect(!bBedWork && !Why.IsEmpty() && Why == Why2 && bBedOrder && Piece(Fx.Bunk).Key == Fx.A,
			FString::Printf(TEXT("MV3 (iv) a bed into a work room: refused with Furnish's own reason, \"%s\" (Furnish: \"%s\"), and the order too"), *Why, *Why2));
		const double Struct0 = Sim->GetTotalSolid(NStruct);
		Drain(NStruct);
		const bool bZero = Sim->GetTotalSolid(NStruct) == 0.0 && Sim->GetStock(NStruct) == 0.0;
		Why.Reset();
		const bool bPre = Sim->CanMoveFurniture(Fx.Lamp, Fx.D, 3, 0, Why);
		const bool bFree = Land(MoveCmd(Fx.Lamp, Fx.D, 3, 0));
		Expect(bZero && bPre && bFree && Piece(Fx.Lamp).Key == Fx.D && Piece(Fx.Lamp).Slot == 3 && Sim->GetTotalSolid(NStruct) == 0.0 && Sim->GetStock(NStruct) == 0.0,
			FString::Printf(TEXT("MV3 (v) with Struct, the whole bill, at 0: the move is accepted (%s) and lands (%s), and Struct stays %.1f solid, %.1f pooled"), bPre ? TEXT("preflight ok") : *Why, *Landed, Sim->GetTotalSolid(NStruct), Sim->GetStock(NStruct)));
		Sim->Debug_AddSolidAt(LastIdOf(TEXT("Lander")), NStruct, Struct0, false);
	}

	// --- MV4. Every destination refusal matches Furnish's for a new piece of the row at the spot (the cost aside: stock is plentiful here). ---
	{
		struct FCase { const TCHAR* What; int32 Id; FIntVector Key; int32 Slot; const TCHAR* Contains; };
		const FCase Cases[] = {
			{ TEXT("an occupied footprint"), Fx.Lamp, Fx.A, 0, TEXT("Overlaps the Bunk Bed") },
			{ TEXT("a machine standing there"), Fx.Lamp, Fx.D, 5, TEXT("AirFilter") },
			{ TEXT("an unzoned bay"), Fx.Lamp, Fx.UBay, 0, TEXT("Zone this space first") },
			{ TEXT("the wrong room function"), Fx.Desk, Fx.B, 0, TEXT("goes in a") },
			{ TEXT("off the grid"), Fx.Desk, Fx.W, 3, TEXT("It doesn't fit there - keep it inside the room") },
		};
		for (const FCase& Case : Cases)
		{
			Why.Reset(); Why2.Reset();
			const bool bMove = Sim->CanMoveFurniture(Case.Id, Case.Key, Case.Slot, 0, Why);
			Sim->CanPlaceFurniture(Piece(Case.Id).Row, Case.Key, Case.Slot, 0, Why2);
			Expect(!bMove && Why == Why2 && Why.Contains(Case.Contains) && !Why.Contains(TEXT("Insufficient")),
				FString::Printf(TEXT("MV4 %s: the move is refused, \"%s\" - Furnish says \"%s\""), Case.What, *Why, *Why2));
		}
		// D7's open atrium cell (MANDATORY): the box colony as the -atriums suite builds it, opened; a Plant on -1 moved onto an open cell.
		FString LoadErr;
		bool bBox = Sim->LoadColony(TEXT("movebase"), LoadErr) && LoadErr.IsEmpty();
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetColonistsPerCell(1.0);
		const FVector Head0(2000.0, 2000.0, 0.0);
		Sim->ExtendShaft(6, Head0);
		for (int32 L = 1; L <= 3; ++L) { bBox &= Sim->ExcavateFloor(-L, 8, R, 0); }
		bBox &= Sim->ExcavateFloor(-5, 60, R, 0);
		int32 Cavern = INDEX_NONE;
		bBox &= Sim->Debug_DigCavern(-1, 3, FIntPoint(2, -1), FIntPoint(4, 1), Cavern, Why) && Sim->Debug_CarveCavernCells(Cavern, 27) == 27;
		const auto PtCm = [&](int32 PX, int32 PY) { return URHSimWorldSubsystem::LatticeToCm(Sim->GetShaftHeadCm(), FIntPoint(PX, PY)); };
		for (const int32 L : { 1, 2, 3, 5 })
		{
			Sim->Debug_PlaceInstant(FName("AirFilter"), PtCm(-1, -1), -L);
			Sim->Debug_ForceRateFloor(-L);
		}
		Sim->Debug_AddStock(FName("Oxygen"), 200000.0);
		Sim->Debug_AddSolidAt(LastIdOf(TEXT("Lander")), NStruct, 5000.0, false);
		FString OpenWhy;
		bBox &= Sim->Debug_OpenAtrium(Cavern, OpenWhy);
		const int32 Living = Sim->FindFloorCell(-1, FIntPoint(-1, 1)), Open = Sim->FindFloorCell(-1, FIntPoint(3, 0));
		bBox &= Sim->DesignateRoom(-1, Living, NLiving, R) && Land(FurnishCmd(TEXT("Plant"), FIntVector(-1, Living, 0), 0, 0));
		const int32 AtriumPlant = Sim->GetFurniture().Num() > 0 ? Sim->GetFurniture().Last().Id : 0;
		Why.Reset(); Why2.Reset();
		const bool bMove = Sim->CanMoveFurniture(AtriumPlant, FIntVector(-1, Open, 0), 0, 0, Why);
		Sim->CanPlaceFurniture(FName("Plant"), FIntVector(-1, Open, 0), 0, 0, Why2);
		const bool bOrder = !Land(MoveCmd(AtriumPlant, FIntVector(-1, Open, 0), 0, 0)) && LastReject() == Why2;
		Expect(bBox && AtriumPlant != 0 && Sim->IsCellOpen(-1, Open) && !bMove && Why == Why2 && Why.Contains(TEXT("That cell is open to the atrium below")) && bOrder
			&& Piece(AtriumPlant).Key == FIntVector(-1, Living, 0),
			FString::Printf(TEXT("MV4 D7's open atrium cell (the -atriums box colony, cavern %d opened; cell %d of -1 is open): the move is refused, \"%s\" - Furnish says \"%s\"; the order too%s"),
				Cavern, Open, *Why, *Why2, OpenWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (the open: %s)"), *OpenWhy)));
	}

	// MV5 onward: a fresh fixture.
	if (!Build())
	{
		Expect(false, TEXT("MV5 the fresh fixture did not build"));
		return Finish();
	}

	// --- MV5. The end-state bed guard. ---
	{
		TArray<FPlace> Places;
		// (i) H1's Bunk Bed to H2's bare Living y.
		Sim->GetMoveBedPlaces(Fx.Bunk, Fx.Y, 0, 0, Places);
		Why.Reset();
		const bool bI = Sim->CanMoveFurniture(Fx.Bunk, Fx.Y, 0, 0, Why);
		const bool bIOrder = !Land(MoveCmd(Fx.Bunk, Fx.Y, 0, 0)) && LastReject() == BedReason(4);
		Expect(!bI && Why == BedReason(4) && bIOrder && Places.Num() == 2 && Places[0].HabId == Fx.H1 && Places[0].Before == 4 && Places[0].After == 3 && Places[0].Residents == 4
			&& Places[1].HabId == Fx.H2 && Places[1].Before == 1 && Places[1].After == 2 && Sim->GetHabBeds(Fx.H1) == 4 && Piece(Fx.Bunk).Key == Fx.A && Piece(Fx.Bunk).Slot == 0,
			FString::Printf(TEXT("MV5 (i) the Bunk Bed from H1's a to H2's Living y: %s; refused, \"%s\"; the order too; H1 keeps %d beds and the bed stays in a"), *PlacesText(Places), *Why, Sim->GetHabBeds(Fx.H1)));
		// (ii) to bay b: H1 stays at 4.
		const auto BedPieces = [&]() { TArray<int32> Out; for (const int32 Id : Fx.H1Res) { Out.Add(Sim->GetColonistBedPiece(Id)); } return Out; };
		const TArray<int32> Bed0 = BedPieces();
		Sim->GetMoveBedPlaces(Fx.Bunk, Fx.B, 0, 0, Places);
		const bool bII = Land(MoveCmd(Fx.Bunk, Fx.B, 0, 0));
		const TArray<int32> Bed1 = BedPieces();
		const TArray<int32> WantBeds = { Fx.Bunk, Fx.Bunk, 0, 0 };
		Expect(bII && Places.Num() == 1 && Places[0].HabId == Fx.H1 && Places[0].Before == 4 && Places[0].After == 4 && Places[0].Residents == 4 && Piece(Fx.Bunk).Key == Fx.B
			&& Sim->GetHabBeds(Fx.H1) == 4 && Bed0 == WantBeds && Bed1 == WantBeds,
			FString::Printf(TEXT("MV5 (ii) the Bunk Bed from a to b: %s; accepted; H1 has %d beds for %d; residents #%d, #%d sleep in #%d before and after, #%d and #%d in legacy beds (%s)"),
				*PlacesText(Places), Sim->GetHabBeds(Fx.H1), Sim->GetHabResidents(Fx.H1), Fx.H1Res[0], Fx.H1Res[1], Fx.Bunk, Fx.H1Res[2], Fx.H1Res[3], *Landed));
		// (iii) H2's Locker into H1's bare c: S = H2 is fine, D = H1 is not.
		Sim->GetMoveBedPlaces(Fx.Locker, Fx.C, 0, 0, Places);
		Why.Reset();
		const bool bIII = Sim->CanMoveFurniture(Fx.Locker, Fx.C, 0, 0, Why);
		const bool bIIIOrder = !Land(MoveCmd(Fx.Locker, Fx.C, 0, 0)) && LastReject() == BedReason(4);
		Expect(!bIII && Why == BedReason(4) && bIIIOrder && Places.Num() == 2 && Places[0].HabId == Fx.H2 && Places[0].Before == 1 && Places[0].After == 2 && Places[0].Residents == 1
			&& Places[1].HabId == Fx.H1 && Places[1].Before == 4 && Places[1].After == 3 && Places[1].Residents == 4 && Piece(Fx.Locker).Key == Fx.X,
			FString::Printf(TEXT("MV5 (iii) the Locker from H2's x into H1's bare c: %s; refused by D, \"%s\"; the order too"), *PlacesText(Places), *Why));
		// (iv) a Rug placed in S1's bare Living bay (no one lives in S1), moved to its Dining bay: the legacy bed comes back.
		const int32 S1Beds0 = Sim->GetHabBeds(Fx.S1);
		const bool bRug = Land(FurnishCmd(TEXT("Rug"), Fx.S1L, 0, 0));
		const int32 Rug = Sim->GetFurniture().Last().Id;
		const int32 S1Beds1 = Sim->GetHabBeds(Fx.S1);
		Sim->GetMoveBedPlaces(Rug, Fx.S1D, 15, 0, Places);
		const bool bIV = Land(MoveCmd(Rug, Fx.S1D, 15, 0));
		Expect(bRug && bIV && S1Beds0 == 2 && S1Beds1 == 1 && Sim->GetHabBeds(Fx.S1) == S1Beds1 + 1 /* ColonistsPerCell, 1 here (Build sets it) */ && Places.Num() == 1 && Places[0].Before == 1 && Places[0].After == 2
			&& Sim->GetHabResidents(Fx.S1) == 0,
			FString::Printf(TEXT("MV5 (iv) Rug #%d in S1's bare Living bay ends its legacy bed (S1 %d -> %d beds); moved to S1's Dining bay: %s; accepted, S1 rises to %d (+%d, the legacy count)"),
				Rug, S1Beds0, S1Beds1, *PlacesText(Places), Sim->GetHabBeds(Fx.S1), Sim->GetHabBeds(Fx.S1) - S1Beds1));
		// (v) floor -1's only bed piece, with residents equal to its beds.
		Sim->GetMoveBedPlaces(Fx.FloorBunk, Fx.Y, 0, 0, Places);
		Why.Reset();
		const bool bVUp = Sim->CanMoveFurniture(Fx.FloorBunk, Fx.Y, 0, 0, Why);
		const bool bVUpOrder = !Land(MoveCmd(Fx.FloorBunk, Fx.Y, 0, 0)) && LastReject() == BedReason(8);
		Expect(!bVUp && Why == BedReason(8) && bVUpOrder && Places.Num() == 2 && Places[0].HabId == 0 && Places[0].Level == -1 && Places[0].Before == 8 && Places[0].After == 7
			&& Places[0].Residents == 8 && Places[1].HabId == Fx.H2 && Places[1].Before == 1 && Places[1].After == 2 && Piece(Fx.FloorBunk).Key == Fx.C0,
			FString::Printf(TEXT("MV5 (v) floor -1's Bunk Bed to H2's Living y: %s; refused, \"%s\"; the order too"), *PlacesText(Places), *Why));
		Sim->GetMoveBedPlaces(Fx.FloorBunk, Fx.C1, 0, 0, Places);
		const bool bVCell = Land(MoveCmd(Fx.FloorBunk, Fx.C1, 0, 0));
		Expect(bVCell && Places.Num() == 1 && Places[0].Level == -1 && Places[0].Before == 8 && Places[0].After == 8 && Places[0].Residents == 8 && Piece(Fx.FloorBunk).Key == Fx.C1
			&& Sim->GetFloorBeds(-1) == 8 && Sim->GetCellBeds(Fx.C0) == 1 && Sim->GetCellBeds(Fx.C1) == 2,
			FString::Printf(TEXT("MV5 (v) ...and to the bare Living cell c1 on its own floor: %s; accepted, the floor keeps %d beds (%s)"), *PlacesText(Places), Sim->GetFloorBeds(-1), *Landed));
		// (vi) H2 full: the Locker, the only piece in x, to the bare Living y - x gets its legacy bed back, y loses one.
		FString AddOnly;
		const bool bAddOnly = Sim->CanPlaceFurniture(FName("Locker"), Fx.Y, 0, 0, AddOnly);
		Sim->GetMoveBedPlaces(Fx.Locker, Fx.Y, 0, 0, Places);
		const bool bVI = Land(MoveCmd(Fx.Locker, Fx.Y, 0, 0));
		Expect(bVI && !bAddOnly && AddOnly.Contains(TEXT("sleep")) && Places.Num() == 1 && Places[0].HabId == Fx.H2 && Places[0].Before == 1 && Places[0].After == 1 && Places[0].Residents == 1
			&& Piece(Fx.Locker).Key == Fx.Y && Sim->GetHabBeds(Fx.H2) == 1 && Sim->GetCellBeds(Fx.X) == 1 && Sim->GetCellBeds(Fx.Y) == 0,
			FString::Printf(TEXT("MV5 (vi) H2 full, its Locker from x to the bare Living y: %s; accepted, H2 still %d bed for %d (the add-only guard alone refuses it: \"%s\") (%s)"),
				*PlacesText(Places), Sim->GetHabBeds(Fx.H2), Sim->GetHabResidents(Fx.H2), *AddOnly, *Landed));
	}

	// --- MV6. Seats. ---
	{
		int32 Seated = 0;
		for (const FRHColonist& Col : Sim->GetColonists()) { if (Sim->GetColonistSeatPiece(Col.Id) == Fx.Desk) { Seated = Col.Id; } }
		const FIntPoint Seats0 = Seats(Fx.W);
		const bool bMoved = Land(MoveCmd(Fx.Desk, Fx.W, 10, 0));
		Sim->Debug_RefreshJobs();
		const FIntPoint Seats1 = Seats(Fx.W);
		Expect(bMoved && Seated != 0 && Seats0 == Seats1 && Seats1.Y == 1 && Piece(Fx.Desk).Slot == 10 && Sim->GetColonistSeatPiece(Seated) == Fx.Desk,
			FString::Printf(TEXT("MV6 the Desk moved within its room (slot 0 -> 10): w's seats %d/%d -> %d/%d, and colonist #%d still sits at Desk #%d after RefreshJobs (%s)"),
				Seats0.X, Seats0.Y, Seats1.X, Seats1.Y, Seated, Fx.Desk, *Landed));
		Why.Reset(); Why2.Reset();
		const bool bDining = Sim->CanMoveFurniture(Fx.Desk, Fx.S1D, 0, 0, Why);
		Sim->CanPlaceFurniture(FName("Desk"), Fx.S1D, 0, 0, Why2);
		Expect(!bDining && Why == Why2 && Why.Contains(TEXT("goes in a")), FString::Printf(TEXT("MV6 the Desk to a Dining bay: refused with Furnish's function reason, \"%s\""), *Why));
	}

	// --- MV7. In transit. ---
	{
		Sim->EnqueueCommand(MoveCmd(Fx.Lamp, Fx.D, 3, 0));
		Why.Reset();
		const bool bI = Sim->CanMoveFurniture(Fx.Lamp, Fx.D, 15, 0, Why);
		CancelLast();
		Expect(!bI && Why == TEXT("That piece already has an order in transit"), FString::Printf(TEXT("MV7 (i) a second Move of a piece with a Move queued: refused, \"%s\""), *Why));
		FRHCommand Sell; Sell.Verb = FName("Sell"); Sell.Target = NFurniture; Sell.Value = (double)Fx.Lamp;
		Sim->EnqueueCommand(Sell);
		Why.Reset();
		const bool bII = Sim->CanMoveFurniture(Fx.Lamp, Fx.D, 3, 0, Why);
		CancelLast();
		Expect(!bII && Why == TEXT("That piece already has an order in transit"), FString::Printf(TEXT("MV7 (ii) a Move of a piece with a Sell queued: refused, \"%s\""), *Why));
		Sim->EnqueueCommand(MoveCmd(Fx.Sofa, Fx.S1D, 0, 0));
		Why.Reset();
		const bool bIII = Sim->CanPlaceFurniture(FName("Plant"), Fx.S1D, 1, 0, Why);
		CancelLast();
		Expect(!bIII && Why == TEXT("A Sofa is already in transit for that spot"), FString::Printf(TEXT("MV7 (iii) Furnish on a queued Move's destination: refused, \"%s\""), *Why));
		Sim->EnqueueCommand(FurnishCmd(TEXT("Plant"), Fx.S1D, 1, 0));
		Why.Reset();
		const bool bIV = Sim->CanMoveFurniture(Fx.Sofa, Fx.S1D, 0, 0, Why);
		CancelLast();
		Expect(!bIV && Why == TEXT("A Potted Plant is already in transit for that spot"), FString::Printf(TEXT("MV7 (iv) a Move onto a queued Furnish's spot: refused, \"%s\""), *Why));
		// (v) a Sell, then a Move of the same piece, both queued with no preflight: the Sell lands first.
		const bool bP5 = Land(FurnishCmd(TEXT("Plant"), Fx.D, 12, 0));
		const int32 P5 = Sim->GetFurniture().Last().Id;
		TArray<FRHFurnitureInstance> Others;
		for (const FRHFurnitureInstance& P : Sim->GetFurniture()) { if (P.Id != P5) { Others.Add(P); } }
		const TArray<FRHWallFeature> Walls0 = Sim->GetWallFeatures();
		FRHCommand SellP5; SellP5.Verb = FName("Sell"); SellP5.Target = NFurniture; SellP5.Value = (double)P5;
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(SellP5);
		Sim->EnqueueCommand(MoveCmd(P5, Fx.D, 3, 1));
		Step();
		FRHMoveSnapshot SA, SB;
		SA.Pieces = Others;
		SB.Pieces = Sim->GetFurniture();
		SA.Fittings = Walls0;
		SB.Fittings = Sim->GetWallFeatures();
		Why2.Reset();
		const bool bSame = SameSnapshot(SA, SB, Why2);
		const FString Gone = FString::Printf(TEXT("No furnishing #%d - it is already gone"), P5);
		Expect(bP5 && !Sim->FindFurniture(P5) && Rejects.Num() == R0 + 1 && LastReject() == Gone && bSame,
			FString::Printf(TEXT("MV7 (v) a Sell lands before a Move queued with no preflight: Plant #%d is sold, the Move is rejected, \"%s\", and nothing else changes (%s)"), P5, *LastReject(), *Why2));
		const FIntVector UNorth = EdgeOf(Fx.UBay, 2);
		Sim->EnqueueCommand(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 2));
		Why.Reset();
		const bool bVI = Sim->CanPlaceWallFeature(FName("Window"), UNorth, Why);
		CancelLast();
		Expect(!bVI && Why == TEXT("A fitting is already in transit for that wall"), FString::Printf(TEXT("MV7 (vi) Fit on a queued fitting Move's edge: refused, \"%s\""), *Why));
		Sim->EnqueueCommand(FitCmd(TEXT("Window"), Centre(Fx.UBay), 2));
		Why.Reset();
		const bool bVII = Sim->CanMoveWallFeature(Fx.Window, UNorth, Why);
		CancelLast();
		Expect(!bVII && Why == TEXT("A fitting is already in transit for that wall"), FString::Printf(TEXT("MV7 (vii) a fitting Move onto a queued Fit's edge: refused, \"%s\""), *Why));
		// (viii) a Move and a later Furnish to the same spot, both with no preflight: the Move lands, the Furnish meets the piece.
		const int32 R1 = Rejects.Num(), Num0 = Sim->GetFurniture().Num();
		Sim->EnqueueCommand(MoveCmd(Fx.Sofa, Fx.S1D, 0, 0));
		Sim->EnqueueCommand(FurnishCmd(TEXT("Plant"), Fx.S1D, 1, 0));
		Step();
		const FString Overlap = FString::Printf(TEXT("Overlaps the Sofa #%d"), Fx.Sofa);
		Expect(Piece(Fx.Sofa).Key == Fx.S1D && Piece(Fx.Sofa).Slot == 0 && Rejects.Num() == R1 + 1 && LastReject() == Overlap && Sim->GetFurniture().Num() == Num0,
			FString::Printf(TEXT("MV7 (viii) a Move and a later Furnish to one spot, both queued: the Move lands (the Sofa at slot %d), the Furnish is refused, \"%s\""), Piece(Fx.Sofa).Slot, *LastReject()));
	}

	// --- MV8. Fittings. ---
	{
		const FIntVector ANorth = EdgeOf(Fx.A, 2), UNorth = EdgeOf(Fx.UBay, 2), USouth = EdgeOf(Fx.UBay, 3);
		// (i) the window from rated H1 to unrated U: its Views term goes.
		int32 Rated = 0;
		for (const FRHWallFeature& Wf : Sim->GetWallFeatures())
		{
			int32 Inside = 0;
			const FRHDoorRow* Dr = DefsSub->GetDoor(Wf.Row);
			Rated += (Dr && Dr->HopeViews > 0.f && Sim->ClassifyWallEdge(Wf.Edge, &Inside) == EEdge::Exterior && Sim->IsHabRated(Inside)) ? 1 : 0;
		}
		const double Term = (double)WinRow->HopeViews;
		const double WantDrop = FMath::Min(Rated * Term, Sim->GetHopeViewsMax()) - FMath::Min((Rated - 1) * Term, Sim->GetHopeViewsMax());
		const double Views0 = Sim->GetViewsHope();
		const int32 Next0 = Sim->GetNextWallFeatureId(), Num0 = Sim->GetWallFeatures().Num();
		const int32 Index0 = Sim->GetWallFeatures().IndexOfByPredicate([&Fx](const FRHWallFeature& Wf) { return Wf.Id == Fx.Window; });
		ClearOfSolTurn();
		WallChanged.Reset();
		Sim->EnqueueCommand(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 2));
		const TArray<double> Stock0 = StockOf(WinBill);
		const int32 R0 = Rejects.Num();
		Step();
		const TArray<double> Stock1 = StockOf(WinBill);
		const FRHWallFeature* Win = Sim->FindWallFeature(Fx.Window);
		const double Views1 = Sim->GetViewsHope();
		Expect(Rejects.Num() == R0 && Win && Win->Edge == UNorth && Sim->FindWallFeatureOnEdge(ANorth) == nullptr && Sim->GetNextWallFeatureId() == Next0 && Sim->GetWallFeatures().Num() == Num0
			&& Sim->GetWallFeatures().IndexOfByPredicate([&Fx](const FRHWallFeature& Wf) { return Wf.Id == Fx.Window; }) == Index0,
			FString::Printf(TEXT("MV8 (i) Window #%d from H1's a to U's north wall: the same Id at (%d, %d, %d), its index %d, %d fitting(s), next Id %d"),
				Fx.Window, UNorth.X, UNorth.Y, UNorth.Z, Index0, Num0, Next0));
		Expect(Stock0 == Stock1 && Stock0.Num() == 8 * WinBill.Num(), FString::Printf(TEXT("MV8 (i) stock unchanged for every resource of the window's bill (%d), each exact across the step"), WinBill.Num()));
		Expect(WallChanged.Num() == 2 && WallChanged[0] == ANorth && WallChanged[1] == UNorth, FString::Printf(TEXT("MV8 (i) OnWallFeatureChanged fired for the old edge, then the new (%d event(s))"), WallChanged.Num()));
		Expect(Rated == 1 && Views0 - Views1 == WantDrop && WantDrop == Term,
			FString::Printf(TEXT("MV8 (i) Views drops %.4f -> %.4f: exactly the window's term, %.4f from the row (HopeViews %.2f, cap %.1f, %d rated window)"), Views0, Views1, WantDrop, Term, Sim->GetHopeViewsMax(), Rated));
		// (ii) a door to an outside wall.
		const FIntVector E1 = EdgeOf(Fx.S1L, 0), E2 = EdgeOf(Fx.S1D, 0), E3 = EdgeOf(Fx.S2L, 0);
		const bool bDoor = Land(FitCmd(TEXT("PressureDoor"), Centre(Fx.S1L), 0));
		const int32 Door1 = Sim->GetWallFeatures().Last().Id;
		Why.Reset(); Why2.Reset();
		const bool bDoorOut = Sim->CanMoveWallFeature(Door1, USouth, Why);
		Sim->CanPlaceWallFeature(FName("PressureDoor"), USouth, Why2);
		const bool bDoorOrder = !Land(FitMoveCmd(Door1, Centre(Fx.UBay), 3)) && LastReject() == Why2;
		Expect(bDoor && !bDoorOut && Why == Why2 && Why.Contains(TEXT("Doors go in doorways")) && bDoorOrder && EdgeOfFitting(Door1) == E1,
			FString::Printf(TEXT("MV8 (ii) Pressure Door #%d to U's outside wall: refused with Fit's reason, \"%s\" (Fit: \"%s\"); the order too"), Door1, *Why, *Why2));
		// (iii) the window onto the airlock's edge.
		const FIntVector LockEdge = EdgeOfFitting(Fx.Airlock);
		Why.Reset(); Why2.Reset();
		const bool bOnto = Sim->CanMoveWallFeature(Fx.Window, LockEdge, Why);
		Sim->CanPlaceWallFeature(FName("Window"), LockEdge, Why2);
		Expect(!bOnto && Why == Why2 && Why == FString::Printf(TEXT("The Airlock #%d is already there"), Fx.Airlock), FString::Printf(TEXT("MV8 (iii) the window onto the airlock's wall: refused, \"%s\""), *Why));
		// (iv) the airlock to a wall whose chamber holds a machine.
		const FIntVector West = EdgeOf(Fx.S1L, 1);
		Why.Reset(); Why2.Reset();
		const bool bChamber = Sim->CanMoveWallFeature(Fx.Airlock, West, Why);
		Sim->CanPlaceWallFeature(FName("Airlock"), West, Why2);
		Expect(!bChamber && Why == Why2 && Why == FString::Printf(TEXT("The Floodmast #%d stands where the airlock's chamber goes"), Fx.Mast),
			FString::Printf(TEXT("MV8 (iv) the airlock to S1's west wall: refused with Fit's chamber reason, \"%s\""), *Why));
		// (v) seals.
		int32 Ways = 0, Doored = 0, Ways2 = 0, Doored2 = 0;
		const bool bV1 = Land(FitMoveCmd(Door1, Centre(Fx.S1D), 0));
		Sim->GetHabNetworkDoorways(Fx.S1, Ways, Doored);
		Expect(bV1 && EdgeOfFitting(Door1) == E2 && !Sim->FindWallFeatureOnEdge(E1) && Ways == 2 && Doored == 1 && !Sim->IsHabNetworkSealed(Fx.S1),
			FString::Printf(TEXT("MV8 (v) the door to S1's other doorway: still unsealed, %d of %d doored, the old edge free (%s)"), Doored, Ways, *Landed));
		const bool bDoor2 = Land(FitCmd(TEXT("PressureDoor"), Centre(Fx.S1L), 0));
		Sim->GetHabNetworkDoorways(Fx.S1, Ways, Doored);
		const bool bSealed = Sim->IsHabNetworkSealed(Fx.S1);
		Sim->GetHabNetworkDoorways(Fx.S2, Ways2, Doored2);
		const int32 S2Doored0 = Doored2;
		const bool bV2 = Land(FitMoveCmd(Door1, Centre(Fx.S2L), 0));
		int32 Doored1After = 0;
		Sim->GetHabNetworkDoorways(Fx.S1, Ways, Doored1After);
		Sim->GetHabNetworkDoorways(Fx.S2, Ways2, Doored2);
		Expect(bDoor2 && bSealed && Doored == 2 && bV2 && EdgeOfFitting(Door1) == E3 && !Sim->IsHabNetworkSealed(Fx.S1) && Doored1After == 1 && Doored2 == S2Doored0 + 1,
			FString::Printf(TEXT("MV8 (v) both doorways doored: S1 sealed; one door moved to S2's free doorway: S1 unsealed (%d of %d), S2's doored count %d -> %d (%s)"),
				Doored1After, Ways, S2Doored0, Doored2, *Landed));
		const bool bV3 = Land(FitMoveCmd(Door1, Centre(Fx.S1D), 0));
		Expect(bV3 && EdgeOfFitting(Door1) == E2 && Sim->IsHabNetworkSealed(Fx.S1), FString::Printf(TEXT("MV8 (v) moved back: S1 is sealed again (%s)"), *Landed));
		Why.Reset(); Why2.Reset();
		const bool bV4 = Sim->CanMoveWallFeature(Door1, USouth, Why);
		Sim->CanPlaceWallFeature(FName("PressureDoor"), USouth, Why2);
		Expect(!bV4 && Why == Why2 && !Why.IsEmpty(), FString::Printf(TEXT("MV8 (v) to an outside wall: refused with Fit's text, \"%s\""), *Why));
		// (vi) the same edge.
		Why.Reset();
		const bool bVI = Sim->CanMoveWallFeature(Fx.Window, UNorth, Why);
		const bool bVIOrder = !Land(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 2)) && LastReject() == TEXT("It is already there");
		Expect(!bVI && Why == TEXT("It is already there") && bVIOrder, FString::Printf(TEXT("MV8 (vi) the window to its own wall: refused, \"%s\", and the order too"), *Why));
		// (vii) at zero stock.
		const double Struct0 = Sim->GetTotalSolid(NStruct), Glass0 = Sim->GetStock(NGlass);
		Drain(NStruct);
		Drain(NGlass);
		const bool bZero = Sim->GetTotalSolid(NStruct) == 0.0 && Sim->GetStock(NStruct) == 0.0 && Sim->GetTotalSolid(NGlass) == 0.0 && Sim->GetStock(NGlass) == 0.0;
		const bool bVII = Land(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 3));
		Expect(bZero && bVII && EdgeOfFitting(Fx.Window) == USouth && Sim->GetTotalSolid(NStruct) == 0.0 && Sim->GetStock(NStruct) == 0.0
			&& Sim->GetTotalSolid(NGlass) == 0.0 && Sim->GetStock(NGlass) == 0.0,
			FString::Printf(TEXT("MV8 (vii) with Struct and Glass both at 0: the window move succeeds and both stay 0 (%s)"), *Landed));
		Sim->Debug_AddSolidAt(LastIdOf(TEXT("Lander")), NStruct, Struct0, false);
		Sim->AddStock(NGlass, Glass0);
	}

	// --- MV9. A queued Move survives a save and a load. ---
	{
		Sim->EnqueueCommand(MoveCmd(Fx.Lamp, Fx.D, 15, 0));
		Sim->EnqueueCommand(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 0));
		TArray<FRHCommand> Queued;
		for (const FRHCommand& Q : Sim->GetUplinkQueue()) { if (Q.Verb == FName("Move")) { Queued.Add(Q); } }
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("movequeued"), Err);
		const int32 R0 = Rejects.Num();
		for (int32 K = 0; K < 5 && Sim->GetUplinkQueue().Num() > 0; ++K) { Step(); }
		const FRHMoveSnapshot Twin = Snapshot();
		const bool bTwinLanded = Rejects.Num() == R0 && Piece(Fx.Lamp).Key == Fx.D && Piece(Fx.Lamp).Slot == 15 && EdgeOfFitting(Fx.Window) == EdgeOf(Fx.UBay, 0);
		const bool bLoaded = Sim->LoadColony(TEXT("movequeued"), Err) && Err.IsEmpty();
		TArray<FRHCommand> Loaded;
		for (const FRHCommand& Q : Sim->GetUplinkQueue()) { if (Q.Verb == FName("Move")) { Loaded.Add(Q); } }
		bool bKept = Queued.Num() == 2 && Loaded.Num() == 2;
		for (int32 i = 0; bKept && i < 2; ++i)
		{
			bKept &= Loaded[i].CommandId == Queued[i].CommandId && Loaded[i].LocationB == Queued[i].LocationB && Loaded[i].Target == Queued[i].Target
				&& Loaded[i].Location == Queued[i].Location && Loaded[i].Value == Queued[i].Value && Loaded[i].Level == Queued[i].Level;
		}
		Rejects.Reset();
		for (int32 K = 0; K < 5 && Sim->GetUplinkQueue().Num() > 0; ++K) { Step(); }
		const FRHMoveSnapshot After = Snapshot();
		Why.Reset();
		const bool bSame = SameSnapshot(Twin, After, Why);
		Expect(bSaved && bTwinLanded && bLoaded && bKept, FString::Printf(TEXT("MV9 a furnishing Move and a fitting Move queued, saved and loaded: both orders keep their CommandId (#%d, #%d) and LocationB (%.0f, %.0f) and every other field%s"),
			Queued.Num() > 0 ? Queued[0].CommandId : 0, Queued.Num() > 1 ? Queued[1].CommandId : 0, Queued.Num() > 0 ? Queued[0].LocationB.X : 0.0, Queued.Num() > 1 ? Queued[1].LocationB.X : 0.0, Err.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Err)));
		Expect(Rejects.Num() == 0 && bSame, FString::Printf(TEXT("MV9 after the load both land, and the end state equals a twin that never saved: every piece's Id and spot, every fitting's edge, beds, station seats and every bed and seat assignment (%s)"), *Why));
	}

	// --- MV10. A bad order. ---
	{
		FRHCommand Bad = MoveCmd(Fx.Lamp, Fx.D, 3, 0);
		Bad.Target = FName("Building");
		const bool bBuilding = !Land(Bad) && LastReject() == TEXT("Move needs a furnishing or a fitting");
		FRHCommand Zero = MoveCmd(Fx.Lamp, Fx.D, 3, 0);
		Zero.LocationB.X = 0.0;
		const bool bZero = !Land(Zero) && LastReject() == TEXT("Move needs a furnishing or a fitting");
		FRHCommand Half = MoveCmd(Fx.Lamp, Fx.D, 3, 0);
		Half.LocationB.X = 2.5;
		const bool bHalf = !Land(Half) && LastReject() == TEXT("Move needs a furnishing or a fitting");
		Expect(bBuilding && bZero && bHalf && Piece(Fx.Lamp).Key == Fx.D && Piece(Fx.Lamp).Slot == 15,
			FString::Printf(TEXT("MV10 Target \"Building\", LocationB.X 0 and 2.5: each refused, \"%s\", and the Lamp stays put"), *LastReject()));
		FRHCommand Demolish; Demolish.Verb = FName("Demolish"); Demolish.Target = FName("Building"); Demolish.Value = (double)Fx.Z;
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(Demolish);
		Sim->EnqueueCommand(MoveCmd(Fx.ZPlant, Fx.S1D, 12, 0));
		Step();
		const FString Gone = FString::Printf(TEXT("No furnishing #%d - it is already gone"), Fx.ZPlant);
		Expect(!Sim->HasBuilding(Fx.Z) && !Sim->FindFurniture(Fx.ZPlant) && Rejects.Num() == R0 + 1 && LastReject() == Gone,
			FString::Printf(TEXT("MV10 a Move whose habitat #%d was demolished before it landed: refused, \"%s\""), Fx.Z, *LastReject()));
	}

	// --- MV11. Determinism: two fresh fixtures, one scripted sequence. ---
	{
		const auto Script = [&]() -> bool
		{
			bool bOk = Land(MoveCmd(Fx.Bunk, Fx.B, 0, 0));                 // MV5 (ii)
			bOk &= Land(FitMoveCmd(Fx.Window, Centre(Fx.UBay), 2));        // MV8 (i)
			bOk &= Land(MoveCmd(Fx.Lamp, Fx.D, 15, 0));
			bOk &= Land(MoveCmd(Fx.Locker, Fx.Y, 0, 0));                   // MV5 (vi)
			bOk &= Land(MoveCmd(Fx.FloorBunk, Fx.C1, 0, 0));
			bOk &= Land(MoveCmd(Fx.Desk, Fx.W, 10, 0));
			bOk &= Land(MoveCmd(Fx.FloorPlant, Fx.F, 0, 0));
			Step();
			return bOk;
		};
		const bool bA = Build() && Script();
		const FRHMoveSnapshot First = Snapshot();
		const bool bB = Build() && Script();
		const FRHMoveSnapshot Second = Snapshot();
		Why.Reset();
		const bool bSame = SameSnapshot(First, Second, Why);
		Expect(bA && bB && bSame && First.Pieces.Num() > 0,
			FString::Printf(TEXT("MV11 two fresh fixtures run the same seven moves (with MV5 (ii) and MV8 (i)): identical furniture (%d pieces) and fitting (%d) arrays, bed and seat assignments (%d colonists) and station seats, field by field (%s)"),
				First.Pieces.Num(), First.Fittings.Num(), First.Assign.Num(), *Why));
	}

	return Finish();
}
