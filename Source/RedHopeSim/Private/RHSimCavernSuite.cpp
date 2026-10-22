#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// D6a caverns self-test (docs/qa/2026-10-22/d6-caverns-design.md: the DIRECTOR ANSWERs Q-A (a), Q-B (a), Q-C (a), the MERGED DESIGN
// AND REVISION section 1, the PROFESSOR REVIEW required changes 1-3). Fails loudly. `-caverns`.
// A cavern is a box of ordinary cells on 1..3 floors that the Borers carve cell by cell with the CarveCell recipe. Every number below is
// derived from the rows or from a reference walk written in this file; the headless world ticks no Mass robot and nothing here needs one.
//  CV0 the constants, the predicates, no new row; CV1 every refusal (one per item, by its reason), the widened tunnel and sink
//  refusals, the uplink path; CV2 the literal append order for a 2 x 2 x 2 box beside a reaching column (spoil per cell, batch time, the
//  eligibility invariant, no carved point left in Plan, also through a save), and a full 8 x 8 x 3 box; CV3 two Borers on two floors, an
//  empty completion, a cavern of another shaft; CV4 D5's dig-time multiplier; CV5 cancel with a batch in flight; CV6 tunnel 1 and
//  cavern 1 at once (RC 1: the two mutations); CV7 era against the agent band; CV8 the save (the layout, mid-batch against a twin, every
//  hand-made refusal, the X >= 100 repair, a v47 file); CV9 ordinal-to-point across a save and a load.
int32 RHRunCavernsSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CAVERNS TEST (D6a, 2026-10-22) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("CAVERNS ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("CAVERNS FAIL: %s"), *What); ++Fails; }
	};
	const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NCarveCell(TEXT("CarveCell")), NRegolith(TEXT("Regolith"));
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const auto Finish = [&]() -> int32
	{
		Expect(Sim->GetFloorCellFallbacks() == 0, TEXT("CV over the whole suite not one ordinal read fell outside its floor and back to the spiral"));
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CAVERNS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	// The healthy-bank step (the bank pinned full: nothing sheds).
	int32 StepCount = 0;
	const auto Step = [&]()
	{
		++StepCount;
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep((float)EraDt);
	};
	const auto Steps = [&](int32 N) { for (int32 K = 0; K < N; ++K) { Step(); } };
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto Recipe = [&](int32 Id) { const FRHBuildingInstance* Bd = FindB(Id); return Bd ? Bd->ActiveRecipe : FName(NAME_None); };
	const auto Cells = [&](int32 Level) { TArray<FIntPoint> Out; if (const FRHFloor* Fl = Sim->FindFloor(Level)) { Out = Fl->GetCells(); } return Out; };
	const auto PlanOf = [&](int32 Level) { TArray<FIntPoint> Out; if (const FRHFloor* Fl = Sim->FindFloor(Level)) { Out = Fl->GetPlan(); } return Out; };
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Sim->GetShaftHeadCm(), FIntPoint(X, Y)); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto NoDup = [](const TArray<FIntPoint>& All) { bool bOk = true; for (int32 i = 0; i < All.Num(); ++i) { bOk &= All.Find(All[i]) == i; } return bOk; };
	const FVector Head0(2000.0, 2000.0, 0.0);
	const FVector Head1(32000.0, 3000.0, 0.0);   // shaft 1: column (30,1)
	const FIntPoint Dir4[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	// The reference rule, written out here and sharing no code with the sim: the next point a Borer takes on one floor of a box is the
	// lowest by (X, then Y) of the box's points that are not carved, not a column, and 4-adjacent to a carved cell or to a column.
	const auto OracleNext = [&](const TArray<FIntPoint>& Have, const TArray<FIntPoint>& Cols, const FIntPoint& Mn, const FIntPoint& Mx, FIntPoint& Out)
	{
		for (int32 X = Mn.X; X <= Mx.X; ++X)
		{
			for (int32 Y = Mn.Y; Y <= Mx.Y; ++Y)
			{
				const FIntPoint P(X, Y);
				if (Have.Contains(P) || Cols.Contains(P)) { continue; }
				for (const FIntPoint& D : Dir4)
				{
					if (Have.Contains(P + D) || Cols.Contains(P + D)) { Out = P; return true; }
				}
			}
		}
		return false;
	};
	// Every cell at ordinal >= From has a lower-ordinal 4-neighbour or a column beside it: each append touched open rock.
	const auto SeededHolds = [&](int32 Level, int32 From, const TArray<FIntPoint>& Cols)
	{
		const FRHFloor* Fl = Sim->FindFloor(Level);
		if (!Fl) { return false; }
		bool bOk = true;
		for (int32 i = From; i < Fl->GetCells().Num(); ++i)
		{
			bool bTouch = false;
			for (const FIntPoint& D : Dir4)
			{
				const FIntPoint Q = Fl->GetCells()[i] + D;
				const int32 O = Fl->FindOrdinal(Q);
				bTouch |= (O != INDEX_NONE && O < i) || Cols.Contains(Q);
			}
			bOk &= bTouch;
		}
		return bOk;
	};
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("cavernsbase"), SaveErr);
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("cavernsbase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("CAVERNS FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		return bOk;
	};
	// A Borer, power and a bank (CanBore is patched in memory as -shafts does, so this proves the code path).
	const auto Kit = [&]()
	{
		FRHBuildingRow* BorerRow = const_cast<FRHBuildingRow*>(DefsSub ? DefsSub->GetBuilding(FName("Borer")) : nullptr);
		if (BorerRow) { BorerRow->CanBore = true; BorerRow->H2BurnKgPerHour = 1.5f; }
		const int32 Id = Place(TEXT("Borer"), 2000.0, 2000.0);
		Place(TEXT("SolarArray"), 3500.0, 1000.0);
		Place(TEXT("BatteryBank"), 1000.0, 3500.0);
		return Id;
	};
	const auto Send = [&](const FRHCommand& C)
	{
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { Step(); }
	};
	// The DigCavern and CancelCavern orders, through the uplink (the verb's own path, execute-time preflight included).
	const auto CavernOrder = [&](int32 Top, int32 Floors, const FIntPoint& A, const FIntPoint& B)
	{
		const int32 N0 = Sim->GetCaverns().Num();
		Rejects.Reset();
		FRHCommand C; C.Verb = FName("DigCavern"); C.Level = Top; C.Value = Floors; C.Location = PtCm(A.X, A.Y); C.LocationB = PtCm(B.X, B.Y);
		Send(C);
		return Sim->GetCaverns().Num() == N0 + 1;
	};
	const auto CancelCavernOrder = [&](int32 Id)
	{
		Rejects.Reset();
		FRHCommand C; C.Verb = FName("CancelCavern"); C.Value = Id;
		Send(C);
		const FRHCavern* Cv = Sim->FindCavern(Id);
		return Cv && Cv->bCancelled;
	};
	const auto TryCavern = [&](int32 Top, int32 Floors, const FIntPoint& A, const FIntPoint& B, FString& OutWhy)
	{
		return Sim->CanDigCavern(Top, Floors, PtCm(A.X, A.Y), PtCm(B.X, B.Y), OutWhy);
	};
	// The harness orders, with the reason cleared first (a success leaves it empty, so a line that prints it never shows a stale one).
	const auto DigCavernNow = [&](int32 Top, int32 Floors, const FIntPoint& A, const FIntPoint& B, int32& OutId, FString& OutWhy) { OutWhy.Reset(); return Sim->Debug_DigCavern(Top, Floors, A, B, OutId, OutWhy); };
	const auto DigTunnelNow = [&](int32 Level, const FIntPoint& A, const FIntPoint& B, int32& OutId, FString& OutWhy) { OutWhy.Reset(); return Sim->Debug_DigTunnel(Level, A, B, OutId, OutWhy); };
	const auto SinkNow = [&](const FVector& HeadCm, int32 Depth, int32& OutId, FString& OutWhy) { OutWhy.Reset(); return Sim->Debug_SinkShaftAt(HeadCm, Depth, OutId, OutWhy); };
	const auto Uncarved = [&](int32 Id) { const FRHCavern* Cv = Sim->FindCavern(Id); return Cv ? Sim->GetCavernUncarved(*Cv) : -1; };
	const auto Digest = [&]()
	{
		FString D = FString::Printf(TEXT("next%d"), Sim->GetNextCavernId());
		for (const FRHCavern& Cv : Sim->GetCaverns())
		{
			D += FString::Printf(TEXT("[%d r%d s%d %d..%d %d,%d>%d,%d can%d"), Cv.Id, Cv.RegionId, Cv.ShaftId, Cv.Top, Cv.Bottom, Cv.Min.X, Cv.Min.Y, Cv.Max.X, Cv.Max.Y, Cv.bCancelled ? 1 : 0);
			for (int32 Fl = Cv.Top; Fl >= Cv.Bottom; --Fl) { D += FString::Printf(TEXT(" f%d:left%d,fly%d"), Fl, Sim->GetCavernUncarved(Cv, Fl), Sim->GetCavernBatchesInFlightOn(Cv.Id, Fl)); }
			D += TEXT("]");
		}
		for (int32 Fl = -1; Fl >= -3; --Fl)
		{
			D += FString::Printf(TEXT(" F%d"), Fl);
			for (const FIntPoint& P : Cells(Fl)) { D += FString::Printf(TEXT(";%d,%d"), P.X, P.Y); }
			for (const FIntPoint& P : PlanOf(Fl)) { D += FString::Printf(TEXT("|%d,%d"), P.X, P.Y); }
		}
		return D;
	};
	const auto Rd = [](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
	const auto PutI = [](TArray<uint8>& Bytes, int32 Off, int32 V) { if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 4); } };

	// --- CV0. The constants, the predicates, no new data row. ---
	{
		Expect(bBaseSaved, FString::Printf(TEXT("CV0 the fresh colony is saved as the base every case loads (%s)"), *SaveErr));
		Expect(Sim->GetCaverns().Num() == 0 && Sim->GetNextCavernId() == 1 && Sim->GetOpenCavernCount() == 0 && Sim->FindCavern(1) == nullptr,
			TEXT("CV0 a fresh colony has no cavern, the next Id is 1, and none is open"));
		Expect(RH::CavernMaxSide == 8 && RH::CavernMaxFloors == 3 && RH::MaxOpenCaverns == 2 && RH::CavernMaxSideCap == 32 && RH::CavernMaxFloorsCap == 8 && RH::MaxCavernRecords == 1024,
			FString::Printf(TEXT("CV0 the limits are code constants (Q-B (a)): side %d, floors %d, open %d; the load ceilings %d, %d and %d records"), RH::CavernMaxSide, RH::CavernMaxFloors, RH::MaxOpenCaverns, RH::CavernMaxSideCap, RH::CavernMaxFloorsCap, RH::MaxCavernRecords));
		Expect(DefsSub && DefsSub->GetConfigTableScalar(FName("CavernMaxSide"), -1.0) == -1.0 && DefsSub->GetConfigTableScalar(FName("CavernMaxFloors"), -1.0) == -1.0 && DefsSub->GetConfigTableScalar(FName("MaxOpenCaverns"), -1.0) == -1.0,
			TEXT("CV0 no data row exists for any cavern limit (Q-B (a): no new row, no DataTable to sync)"));
		const FRHRecipeRow* Carve = DefsSub ? DefsSub->GetRecipe(NCarveCell) : nullptr;
		Expect(Carve && Near(Carve->BatchTime_h, 3.0, 1.0e-9) && Near(URHDefinitionsSubsystem::ParseResourceList(Carve->Outputs).FindRef(NRegolith), 1200.0, 1.0e-9),
			TEXT("CV0 the recipe a cavern uses is the ordinary CarveCell: 3 h and 1,200 kg of spoil a cell, no other bill (Q-B (a))"));
		Expect(RH::IsTunnelWork(FIntPoint(2, 7)) && RH::IsTunnelWork(FIntPoint(3, 7)) && !RH::IsTunnelWork(FIntPoint(0, 1)) && !RH::IsTunnelWork(FIntPoint(1, 1)) && !RH::IsTunnelWork(FIntPoint(101, 1)) && !RH::IsTunnelWork(FIntPoint(99, 1))
			&& RH::IsCavernWork(FIntPoint(101, 1)) && RH::IsCavernWork(FIntPoint(100, 4)) && RH::IsCavernWork(FIntPoint(120, 4)) && !RH::IsCavernWork(FIntPoint(99, 1)) && !RH::IsCavernWork(FIntPoint(3, 1)) && !RH::IsCavernWork(FIntPoint(0, 1)),
			TEXT("CV0 IsTunnelWork is X 2 or 3, IsCavernWork is X >= 100, and no record is both (RC 1)"));
	}

	// --- CV1. The refusals, one per item, each by its reason; the widened tunnel and sink refusals; the uplink path. ---
	{
		Reset();
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-1, 6, R, 0);
		Sim->ExcavateFloor(-2, 6, R, 0);
		Sim->ExcavateFloor(-3, 10, R, 0);
		Expect(Sim->GetFloorCarvedCells(-1) == 6 && Sim->GetFloorCarvedCells(-2) == 6 && Sim->GetFloorCarvedCells(-3) == 10 && Cells(-3).Contains(FIntPoint(2, 0)) && Cells(-1).Contains(FIntPoint(1, 1)),
			TEXT("CV1 setup: shaft 0 to -6, six cells on -1 and -2 (the spiral around column (0,0)) and ten on -3, which reach (2,0)"));
		FString W;
		Expect(!TryCavern(-7, 1, FIntPoint(1, 2), FIntPoint(2, 3), W) && W.Contains(TEXT("not reached")) && W.Contains(TEXT("-7")), FString::Printf(TEXT("CV1 (1) a floor no shaft reaches is refused (%s)"), *W));
		Expect(!TryCavern(-5, 3, FIntPoint(1, 2), FIntPoint(2, 3), W) && W.Contains(TEXT("not reached")) && W.Contains(TEXT("-7")), FString::Printf(TEXT("CV1 (1) a box whose deepest floor (-7) is unreached is refused, though its top is not (%s)"), *W));
		Expect(!TryCavern(0, 1, FIntPoint(1, 2), FIntPoint(2, 3), W) && W.Contains(TEXT("not reached")), FString::Printf(TEXT("CV1 (1) the surface is not a cavern floor (%s)"), *W));
		Expect(!TryCavern(-1, 4, FIntPoint(1, 2), FIntPoint(2, 3), W) && W.Contains(TEXT("1 to 3 floors")) && W.Contains(TEXT("span 4")), FString::Printf(TEXT("CV1 (2) four floors are over CavernMaxFloors (%s)"), *W));
		Expect(!TryCavern(-1, 0, FIntPoint(1, 2), FIntPoint(2, 3), W) && W.Contains(TEXT("1 to 3 floors")), FString::Printf(TEXT("CV1 (2) no floors at all is refused (%s)"), *W));
		Expect(!TryCavern(-1, 1, FIntPoint(1, 2), FIntPoint(9, 2), W) && W.Contains(TEXT("at most 8 x 8")) && W.Contains(TEXT("9 x 1")), FString::Printf(TEXT("CV1 (2) a side of 9 cells is over CavernMaxSide (%s)"), *W));
		Expect(!TryCavern(-1, 1, FIntPoint(1, 2), FIntPoint(1, 10), W) && W.Contains(TEXT("1 x 9")), FString::Printf(TEXT("CV1 (2) and the other side (%s)"), *W));
		Expect(TryCavern(-1, 3, FIntPoint(1, 1), FIntPoint(8, 8), W), FString::Printf(TEXT("CV1 (2) a box of exactly 8 x 8 on 3 floors is allowed (%s)"), *W));
		Expect(!Sim->CanDigCavern(-1, 1, FVector(1.0e15, 0.0, 0.0), PtCm(0, 0), W) && W.Contains(TEXT("at most 8 x 8")), FString::Printf(TEXT("CV1 (2) an absurd point is a refusal, not an overflow or a huge loop (%s)"), *W));
		Expect(!TryCavern(-1, 1, FIntPoint(-1, -1), FIntPoint(1, 1), W) && W.Contains(TEXT("shaft stands")) && W.Contains(TEXT("(0, 0)")), FString::Printf(TEXT("CV1 (3) a box over shaft 0's column is refused (%s)"), *W));
		Expect(!TryCavern(-6, 1, FIntPoint(0, 0), FIntPoint(1, 0), W) && W.Contains(TEXT("shaft stands")), FString::Printf(TEXT("CV1 (3) on a floor with no cell too: a column runs through every floor (%s)"), *W));
		Expect(!TryCavern(-1, 1, FIntPoint(1, 0), FIntPoint(1, 1), W) && W.Contains(TEXT("already dug")), FString::Printf(TEXT("CV1 (7) a box whose every point is carved is refused (%s)"), *W));
		Expect(TryCavern(-1, 1, FIntPoint(1, 0), FIntPoint(2, 1), W), FString::Printf(TEXT("CV1 (7) a box with carved points and uncarved ones is allowed: the carved ones are skipped (%s)"), *W));
		// (5) One seed per floor. A box at (3..4, 0..1) on -3 and -4: (3,0) stands beside the carved (2,0) on -3, but -4 has no dig and the
		// box touches no column. The box at (-2..-1, 0..1) is seeded on -3 by a carved cell and on -4 by shaft 0's column (0,0) beside (-1,0).
		Expect(!TryCavern(-3, 2, FIntPoint(3, 0), FIntPoint(4, 1), W) && W.Contains(TEXT("floor -4")) && W.Contains(TEXT("touches no dig and no shaft")), FString::Printf(TEXT("CV1 (5) a box over -3 and -4 beside a dig on -3 only, with no column, is refused on -4 (%s)"), *W));
		Expect(TryCavern(-3, 1, FIntPoint(3, 0), FIntPoint(4, 1), W), FString::Printf(TEXT("CV1 (5) the same box on -3 alone is allowed: the dig beside it is its seed (%s)"), *W));
		Expect(TryCavern(-3, 2, FIntPoint(-2, 0), FIntPoint(-1, 1), W), FString::Printf(TEXT("CV1 (5) a box over -3 and -4 beside the shaft's column is allowed: one column seeds every floor it reaches (%s)"), *W));
		Expect(!TryCavern(-4, 1, FIntPoint(3, 3), FIntPoint(4, 4), W) && W.Contains(TEXT("floor -4")), FString::Printf(TEXT("CV1 (5) a box beside nothing at all is refused (%s)"), *W));
		// (4) An open tunnel's uncarved point.
		int32 TunnelId = INDEX_NONE;
		Expect(DigTunnelNow(-2, FIntPoint(2, 0), FIntPoint(6, 0), TunnelId, Why) && TunnelId == 1, FString::Printf(TEXT("CV1 a tunnel from (2,0) to (6,0) on -2 is ordered (%s)"), *Why));
		Expect(!TryCavern(-2, 1, FIntPoint(4, 0), FIntPoint(5, 1), W) && W.Contains(TEXT("tunnel #1")) && W.Contains(TEXT("(4, 0)")) && W.Contains(TEXT("floor -2")), FString::Printf(TEXT("CV1 (4) a box across tunnel 1's uncarved line is refused (%s)"), *W));
		Expect(TryCavern(-3, 1, FIntPoint(3, 0), FIntPoint(4, 1), W), FString::Printf(TEXT("CV1 (4) the same box on the floor below, where the tunnel is not, is allowed (%s)"), *W));
		// Cavern 1: through the uplink, Value = the number of floors.
		Expect(CavernOrder(-1, 1, FIntPoint(1, 2), FIntPoint(3, 3)) && Sim->GetCaverns().Last().Id == 1 && Sim->GetNextCavernId() == 2 && Sim->GetOpenCavernCount() == 1, TEXT("CV1 cavern 1 is ordered through the uplink: (1,2) to (3,3) on -1, Id 1"));
		const FRHCavern* C1 = Sim->FindCavern(1);
		Expect(C1 && C1->Top == -1 && C1->Bottom == -1 && C1->Min == FIntPoint(1, 2) && C1->Max == FIntPoint(3, 3) && C1->ShaftId == 0 && !C1->bCancelled && C1->RegionId == 0 && Uncarved(1) == 6 && Sim->IsCavernOpen(*C1) && C1->GetFloors() == 1 && C1->GetSizeX() == 3 && C1->GetSizeY() == 2,
			TEXT("CV1 the record: floor -1 only, the corners as snapped, worked from shaft 0, open, six points uncarved"));
		Expect(!TryCavern(-1, 1, FIntPoint(3, 3), FIntPoint(4, 4), W) && W.Contains(TEXT("cavern #1")) && W.Contains(TEXT("(3, 3)")), FString::Printf(TEXT("CV1 (4) a box over cavern 1's uncarved point is refused (%s)"), *W));
		Expect(!TryCavern(-1, 1, FIntPoint(4, 3), FIntPoint(4, 4), W) && !W.Contains(TEXT("cavern #1")) && W.Contains(TEXT("touches no dig")), FString::Printf(TEXT("CV1 (4) a box beside cavern 1's uncarved rock but not over it is not an overlap: cavern rock is no seed either (%s)"), *W));
		Rejects.Reset();
		{
			FRHCommand C; C.Verb = FName("DigCavern"); C.Level = -1; C.Value = 1; C.Location = PtCm(3, 3); C.LocationB = PtCm(4, 4);
			Send(C);
			Expect(LastReject().Contains(TEXT("cavern #1")) && Sim->GetCaverns().Num() == 1, FString::Printf(TEXT("CV1 the same refusal comes back through the uplink as a rejected order (%s)"), *LastReject()));
		}
		Rejects.Reset();
		{
			FRHCommand C; C.Verb = FName("DigCavern"); C.Level = -1; C.Value = 4; C.Location = PtCm(1, 2); C.LocationB = PtCm(2, 3);
			Send(C);
			Expect(LastReject().Contains(TEXT("1 to 3 floors")) && Sim->GetCaverns().Num() == 1, FString::Printf(TEXT("CV1 a Value of 4 floors is rejected at the uplink (%s)"), *LastReject()));
		}
		// The widened refusals: DigTunnel refusal 6 and SinkShaft refusal 8 gain "an uncarved point of an open cavern".
		Expect(!Sim->CanDigTunnel(-1, PtCm(2, 0), PtCm(2, 4), W) && W.Contains(TEXT("cavern #1")) && W.Contains(TEXT("(2, 2)")), FString::Printf(TEXT("CV1 a tunnel across cavern 1's uncarved point is refused (%s)"), *W));
		Place(TEXT("Pylon"), 6000.0, 2000.0);
		int32 NewId = INDEX_NONE;
		Expect(!SinkNow(PtCm(2, 2), 1, NewId, W) && W.Contains(TEXT("cavern #1")) && W.Contains(TEXT("not finished")) && Sim->GetShaftCount() == 1, FString::Printf(TEXT("CV1 a shaft cannot be sunk on an uncarved point of an open cavern (%s)"), *W));
		// (6) At most MaxOpenCaverns open at once.
		Expect(CavernOrder(-3, 1, FIntPoint(-2, 0), FIntPoint(-1, 1)) && Sim->GetCaverns().Last().Id == 2 && Sim->GetOpenCavernCount() == 2, TEXT("CV1 cavern 2 is ordered on -3: two open"));
		Expect(!TryCavern(-3, 1, FIntPoint(3, 0), FIntPoint(4, 1), W) && W.Contains(TEXT("No more open caverns")) && W.Contains(TEXT("2")), FString::Printf(TEXT("CV1 (6) a third open cavern is refused (%s)"), *W));
		Expect(!CavernOrder(-3, 1, FIntPoint(3, 0), FIntPoint(4, 1)) && LastReject().Contains(TEXT("No more open caverns")), FString::Printf(TEXT("CV1 (6) and the third order is rejected at the uplink (%s)"), *LastReject()));
		Expect(CancelCavernOrder(2) && Sim->GetOpenCavernCount() == 1 && TryCavern(-3, 1, FIntPoint(3, 0), FIntPoint(4, 1), W), TEXT("CV1 cancelling cavern 2 frees one of the two places"));
		CancelCavernOrder(99);
		Expect(LastReject().Contains(TEXT("Unknown cavern #99")), FString::Printf(TEXT("CV1 cancelling a cavern that does not exist (%s)"), *LastReject()));
		CancelCavernOrder(2);
		Expect(LastReject().Contains(TEXT("already cancelled")), FString::Printf(TEXT("CV1 cancelling a cancelled cavern (%s)"), *LastReject()));
		Expect(Sim->Debug_CarveCavernCells(1, 100) == 6 && Uncarved(1) == 0 && !Sim->IsCavernOpen(*Sim->FindCavern(1)) && Sim->GetOpenCavernCount() == 0, TEXT("CV1 the harness carves cavern 1 to the end: six cells, none open"));
		CancelCavernOrder(1);
		Expect(LastReject().Contains(TEXT("already finished")) && !Sim->FindCavern(1)->bCancelled, FString::Printf(TEXT("CV1 cancelling a finished cavern (%s)"), *LastReject()));
		// The execute-time race: the whole box is dug while the order is in flight.
		Reset();
		Sim->ExtendShaft(3, Head0);
		Sim->ExcavateFloor(-1, 1, R, 0);
		{
			FRHCommand C; C.Verb = FName("DigCavern"); C.Level = -1; C.Value = 1; C.Location = PtCm(1, 0); C.LocationB = PtCm(1, 1);
			Sim->EnqueueCommand(C);
			Sim->ExcavateFloor(-1, 1, R, 0); // before the order lands: spiral cell 1 = (1,1), so (1,0) and (1,1) are both carved
			const bool bDug = Sim->FindFloorCell(-1, FIntPoint(1, 0)) != INDEX_NONE && Sim->FindFloorCell(-1, FIntPoint(1, 1)) != INDEX_NONE;
			Rejects.Reset();
			for (int32 K = 0; K < 5 && Sim->GetUplinkQueue().Num() > 0; ++K) { Step(); }
			Expect(bDug && Sim->GetCaverns().Num() == 0 && LastReject().Contains(TEXT("already dug")), FString::Printf(TEXT("CV1 the order is refused at the uplink step when its box was dug meanwhile (%s)"), *LastReject()));
		}
		// Two shafts: a column beside the box seeds only the floors it reaches.
		Reset();
		Sim->ExtendShaft(6, Head0);
		Place(TEXT("Pylon"), 32000.0, 2000.0);
		int32 Sh1 = INDEX_NONE;
		Expect(SinkNow(Head1, 3, Sh1, Why) && Sh1 == 1 && Sim->GetShaft(1).Column == FIntPoint(30, 1), FString::Printf(TEXT("CV1 setup: shaft 1 sunk at (30,1) and bored to -3 (%s)"), *Why));
		Expect(!TryCavern(-3, 2, FIntPoint(31, 0), FIntPoint(32, 1), W) && W.Contains(TEXT("floor -4")), FString::Printf(TEXT("CV1 (5) shaft 1's column (30,1) beside the box seeds -3 but does not reach -4: refused on -4 (%s)"), *W));
		Expect(TryCavern(-2, 2, FIntPoint(31, 0), FIntPoint(32, 1), W), FString::Printf(TEXT("CV1 (5) the same corner on -2 and -3, both of which the column reaches, is allowed (%s)"), *W));
		Reset();
		Sim->ExtendShaft(6, Head0);
		Place(TEXT("Pylon"), 32000.0, 2000.0);
		Expect(SinkNow(Head1, 4, Sh1, Why) && TryCavern(-3, 2, FIntPoint(31, 0), FIntPoint(32, 1), W), FString::Printf(TEXT("CV1 (5) with shaft 1 bored to -4 the column reaches both floors of the box: allowed (%s)"), *W));
		int32 Far = INDEX_NONE, Near0 = INDEX_NONE;
		Expect(DigCavernNow(-1, 2, FIntPoint(31, 0), FIntPoint(32, 1), Far, Why) && DigCavernNow(-1, 1, FIntPoint(1, 0), FIntPoint(2, 1), Near0, Why)
			&& Sim->FindCavern(Far)->ShaftId == 1 && Sim->FindCavern(Near0)->ShaftId == 0,
			FString::Printf(TEXT("CV1 the work shaft is fixed at the order: the box by (30,1) is shaft 1's, the box by the first column is shaft 0's (%s)"), *Why));
	}

	// --- CV2. The literal append order, the spoil, the batch time, the eligibility invariant, Plan; a full 8 x 8 x 3 box. ---
	{
		Reset();
		const int32 BorerId = Kit();
		Sim->ExtendShaft(3, Head0);
		const int32 Landed0 = Sim->GetCavernBatchesLanded(), Empty0 = Sim->GetCavernBatchesEmpty();
		const int32 StepsAtOrder = StepCount;
		Expect(CavernOrder(-1, 2, FIntPoint(1, 0), FIntPoint(2, 1)) && Uncarved(1) == 8, TEXT("CV2 a 2 x 2 x 2 cavern (1,0) to (2,1) on -1 and -2 is ordered beside shaft 0's column: eight points uncarved"));
		Expect(Sim->GetFloorCarvedCells(-1) == 0 && Sim->GetFloorCarvedCells(-2) == 0 && Sim->FindCavern(1) && Sim->GetCavernUncarved(*Sim->FindCavern(1), -1) == 4 && Sim->GetCavernUncarved(*Sim->FindCavern(1), -2) == 4 && Sim->GetCavernUncarved(*Sim->FindCavern(1), -3) == 0,
			TEXT("CV2 four points uncarved on each floor of the box and none on a floor outside it"));
		TArray<FIntPoint> Want[2];
		for (int32 F = 0; F < 2; ++F)
		{
			TArray<FIntPoint> Have;
			FIntPoint P;
			while (OracleNext(Have, { FIntPoint(0, 0) }, FIntPoint(1, 0), FIntPoint(2, 1), P)) { Have.Add(P); Want[F].Add(P); }
		}
		Expect(Want[0].Num() == 4 && Want[0][0] == FIntPoint(1, 0) && Want[0][1] == FIntPoint(1, 1) && Want[0][2] == FIntPoint(2, 0) && Want[0][3] == FIntPoint(2, 1),
			TEXT("CV2 the reference rule gives (1,0) (1,1) (2,0) (2,1): only (1,0) touches the column at first, then the lowest (X, then Y) of what each cell opens"));
		TArray<FIntPoint> Got[2];
		TArray<double> AppendedAt;
		bool bBatchRecipe = false;
		int32 Seen[2] = { 0, 0 };
		for (int32 K = 0; K < 200 && Got[0].Num() + Got[1].Num() < 8; ++K)
		{
			Step();
			bBatchRecipe |= Recipe(BorerId) == NCarveCell;
			for (int32 F = 0; F < 2; ++F)
			{
				const TArray<FIntPoint> Now = Cells(-1 - F);
				for (int32 i = Seen[F]; i < Now.Num(); ++i) { Got[F].Add(Now[i]); AppendedAt.Add(Clock->GetSimSecondsTotal()); }
				Seen[F] = Now.Num();
			}
		}
		Expect(bBatchRecipe && Got[0] == Want[0] && Got[1] == Want[1], FString::Printf(TEXT("CV2 the Borer carved exactly the reference order, floor -1 first and then -2: %d and %d cells, the CarveCell recipe"), Got[0].Num(), Got[1].Num()));
		Expect(SeededHolds(-1, 0, { FIntPoint(0, 0) }) && SeededHolds(-2, 0, { FIntPoint(0, 0) }) && NoDup(Cells(-1)) && NoDup(Cells(-2)), TEXT("CV2 every cell touched a carved cell or the column when it was appended, and no point is carved twice"));
		const double Spent = AppendedAt.Num() == 8 ? AppendedAt[7] - AppendedAt[0] : -1.0;
		Expect(FMath::Abs(Spent - 7.0 * 150.0) <= EraDt, FString::Printf(TEXT("CV2 seven batches of 3 sol-hours (150 sim-s) lay the first cell to the last: %.0f sim-s (7 x 150 = 1050, within one era step)"), Spent));
		Steps(4);
		const double SolidWith = Sim->GetTotalSolid(NRegolith);
		const int32 StepsRun = StepCount - StepsAtOrder;
		Expect(Sim->GetCavernBatchesLanded() - Landed0 == 8 && Sim->GetCavernBatchesEmpty() == Empty0, TEXT("CV2 eight batches landed for eight cells, none empty"));
		Expect(Uncarved(1) == 0 && !Sim->IsCavernOpen(*Sim->FindCavern(1)) && Sim->GetOpenCavernCount() == 0 && Sim->GetCavernBatchesInFlight(1) == 0 && Recipe(BorerId).IsNone(), TEXT("CV2 the cavern is finished: nothing uncarved, none open, no batch in flight, the Borer idle"));
		// The spoil: a twin colony that runs the same number of steps with an order that is refused (a box nine cells wide) holds the same
		// regolith as this one but for the batches' spoil (other sources add regolith at their own rate, the same in both).
		Reset();
		Kit();
		Sim->ExtendShaft(3, Head0);
		const int32 TwinAt = StepCount;
		Expect(!CavernOrder(-1, 2, FIntPoint(1, 0), FIntPoint(9, 1)) && LastReject().Contains(TEXT("at most 8 x 8")), FString::Printf(TEXT("CV2 the twin's order is refused (%s)"), *LastReject()));
		while (StepCount - TwinAt < StepsRun) { Step(); }
		const double Spoil = SolidWith - Sim->GetTotalSolid(NRegolith);
		Expect(Near(Spoil, 8 * 1200.0, 1.0e-6), FString::Printf(TEXT("CV2 each of the eight batches dropped its 1,200 kg of spoil: %.0f kg more regolith than the twin after %d steps"), Spoil, StepsRun));

		// RC 3: no carved point stays in Plan, and the save round trip proves it (a carved point in the plan is a refused save).
		Reset();
		Kit();
		Sim->ExtendShaft(3, Head0);
		Sim->ExcavateFloor(-1, 6, R, 0);
		FString PaintWhy;
		const bool bPainted = Sim->Debug_PaintCarve(-1, FIntPoint(2, 2), PaintWhy) && Sim->Debug_PaintCarve(-1, FIntPoint(1, 3), PaintWhy) && Sim->Debug_PaintCarve(-1, FIntPoint(5, 5), PaintWhy);
		Expect(bPainted && PlanOf(-1).Num() == 3, FString::Printf(TEXT("CV2 (RC 3) setup: three plan points on -1, two of them inside the box (1,2) to (2,3) (%s)"), *PaintWhy));
		Expect(CavernOrder(-1, 1, FIntPoint(1, 2), FIntPoint(2, 3)) && Uncarved(1) == 4, TEXT("CV2 (RC 3) a cavern over (1,2) to (2,3) on -1 is ordered: four uncarved points"));
		for (int32 K = 0; K < 200 && Uncarved(1) > 0; ++K) { Step(); }
		Steps(3);
		const TArray<FIntPoint> PlanAfter = PlanOf(-1);
		Expect(Uncarved(1) == 0 && !PlanAfter.Contains(FIntPoint(2, 2)) && !PlanAfter.Contains(FIntPoint(1, 3)) && PlanAfter.Num() == 1 && PlanAfter.Contains(FIntPoint(5, 5)),
			FString::Printf(TEXT("CV2 (RC 3) the cavern carved both planned points and they left the plan; the point outside the box stays (%d left in the plan)"), PlanAfter.Num()));
		bool bNoneBoth = true;
		for (const FIntPoint& P : PlanAfter) { bNoneBoth &= !Cells(-1).Contains(P); }
		FString E3;
		Expect(bNoneBoth && Sim->SaveColony(TEXT("cavernsplan"), SaveErr) && Sim->LoadColony(TEXT("cavernsplan"), E3) && E3.IsEmpty() && PlanOf(-1) == PlanAfter,
			FString::Printf(TEXT("CV2 (RC 3) no carved point is in Plan, and the colony saves and loads (a carved point in the plan is a refused load): %s"), *E3));

		// A full 8 x 8 x 3 box, by the harness: every point once, every append seeded, the bill the ghost quotes.
		Reset();
		Sim->ExtendShaft(3, Head0);
		int32 FullId = INDEX_NONE;
		URHSimWorldSubsystem::FRHCavernPreview Pre;
		Expect(Sim->CanDigCavern(-1, 3, PtCm(1, 0), PtCm(8, 7), Why, &Pre) && Pre.Cells == 192 && Pre.Points == 192 && Pre.Top == -1 && Pre.Bottom == -3 && Pre.Min == FIntPoint(1, 0) && Pre.Max == FIntPoint(8, 7),
			FString::Printf(TEXT("CV2 the preview of an 8 x 8 x 3 box: %d cells to carve of %d points, floors %d to %d (%s)"), Pre.Cells, Pre.Points, Pre.Top, Pre.Bottom, *Why));
		Expect(DigCavernNow(-1, 3, FIntPoint(1, 0), FIntPoint(8, 7), FullId, Why) && Sim->Debug_CarveCavernCells(FullId, 1000) == 192, TEXT("CV2 the harness carves all 192 cells of a full box"));
		bool bBox = true;
		for (int32 F = 1; F <= 3; ++F)
		{
			bBox &= Sim->GetFloorCarvedCells(-F) == 64 && SeededHolds(-F, 0, { FIntPoint(0, 0) }) && NoDup(Cells(-F));
			for (const FIntPoint& P : Cells(-F)) { bBox &= P.X >= 1 && P.X <= 8 && P.Y >= 0 && P.Y <= 7; }
		}
		Expect(bBox && Uncarved(FullId) == 0 && Sim->GetOpenCavernCount() == 0, TEXT("CV2 each of the three floors holds exactly the 64 points of the box, each appended beside carved rock or the column, none twice"));
	}

	// --- CV3. Two Borers on two floors, an empty completion, a cavern that another shaft owns. ---
	{
		Reset();
		const int32 B1 = Kit();
		const int32 B2 = Place(TEXT("Borer"), 2000.0, 5500.0);
		Sim->ExtendShaft(3, Head0);
		Expect(B1 != 0 && B2 != 0 && B1 != B2, TEXT("CV3 setup: two Borers, neither anchoring a shaft (both work shaft 0)"));
		int32 TwoFloorId = INDEX_NONE;
		Expect(DigCavernNow(-1, 2, FIntPoint(1, 0), FIntPoint(1, 0), TwoFloorId, Why) && Uncarved(1) == 2, TEXT("CV3 a 1 x 1 x 2 cavern beside the column: one point on -1, one on -2"));
		bool bBoth = false;
		for (int32 K = 0; K < 6 && !bBoth; ++K)
		{
			Step();
			bBoth = Sim->GetCavernBatchesInFlightOn(1, -1) == 1 && Sim->GetCavernBatchesInFlightOn(1, -2) == 1 && Recipe(B1) == NCarveCell && Recipe(B2) == NCarveCell;
		}
		Expect(bBoth && Sim->GetCavernBatchesInFlight(1) == 2, TEXT("CV3 both Borers have a batch in flight at once, one on each floor: the second does not start a second batch on -1, where one point is left"));
		Steps(4);
		Expect(Cells(-1) == TArray<FIntPoint>({ FIntPoint(1, 0) }) && Cells(-2) == TArray<FIntPoint>({ FIntPoint(1, 0) }) && Uncarved(1) == 0 && Sim->GetCavernBatchesEmpty() == 0 && Sim->GetCavernBatchesInFlight(1) == 0,
			TEXT("CV3 each floor got its one cell, no batch completed empty, none is in flight"));

		// An empty completion (D3 O5): the only point is carved by someone else while the batch is in flight.
		Reset();
		const int32 BorerE = Kit();
		Sim->ExtendShaft(3, Head0);
		int32 OnePointId = INDEX_NONE;
		const int32 EmptyAt = StepCount;
		Expect(DigCavernNow(-1, 1, FIntPoint(1, 0), FIntPoint(1, 0), OnePointId, Why), TEXT("CV3 a one-point cavern is ordered"));
		for (int32 K = 0; K < 6 && Sim->GetCavernBatchesInFlight(1) == 0; ++K) { Step(); }
		Expect(Sim->GetCavernBatchesInFlight(1) == 1 && Recipe(BorerE) == NCarveCell, TEXT("CV3 its one batch is in flight"));
		Expect(Sim->ExcavateFloor(-1, 1, R, 0) && Cells(-1) == TArray<FIntPoint>({ FIntPoint(1, 0) }) && Uncarved(1) == 0, TEXT("CV3 the spiral carves (1,0) under it: the cavern is finished before its batch lands"));
		const int32 EmptyBefore = Sim->GetCavernBatchesEmpty(), LandedBefore = Sim->GetCavernBatchesLanded();
		Steps(4);
		const double SolidEmpty = Sim->GetTotalSolid(NRegolith);
		const int32 EmptySteps = StepCount - EmptyAt;
		Expect(Sim->GetCavernBatchesEmpty() == EmptyBefore + 1 && Sim->GetCavernBatchesLanded() == LandedBefore + 1 && Sim->GetFloorCarvedCells(-1) == 1, TEXT("CV3 the batch completes empty: no cell is added and it is counted"));
		// The spoil, against a twin that runs the same steps with no cavern and no spiral cell (the spiral's own cell drops no regolith).
		Reset();
		Kit();
		Sim->ExtendShaft(3, Head0);
		const int32 EmptyTwinAt = StepCount;
		while (StepCount - EmptyTwinAt < EmptySteps) { Step(); }
		Expect(Near(SolidEmpty - Sim->GetTotalSolid(NRegolith), 1200.0, 1.0e-6), FString::Printf(TEXT("CV3 and its 1,200 kg of spoil is dropped: %.0f kg more regolith than the twin"), SolidEmpty - Sim->GetTotalSolid(NRegolith)));

		// A cavern of another shaft is not worked by shaft 0's helper Borer.
		Reset();
		const int32 BorerH = Kit();
		Sim->ExtendShaft(3, Head0);
		Place(TEXT("Pylon"), 32000.0, 2000.0);
		int32 SinkId = INDEX_NONE;
		Expect(SinkNow(Head1, 3, SinkId, Why) && SinkId == 1, TEXT("CV3 setup: shaft 1 sunk at (30,1)"));
		Expect(CavernOrder(-1, 1, FIntPoint(31, 0), FIntPoint(32, 1)) && Sim->FindCavern(1) && Sim->FindCavern(1)->ShaftId == 1, TEXT("CV3 a cavern beside shaft 1 is worked from shaft 1"));
		Steps(20);
		Expect(Recipe(BorerH).IsNone() && Sim->GetFloorCarvedCells(-1) == 0 && Sim->GetCavernBatchesInFlight(1) == 0, TEXT("CV3 shaft 0's helper Borer starts nothing on it in 20 steps: the work belongs to shaft 1's Borer (none here)"));
	}

	// --- CV4. D5's dig-time multiplier applies to a cavern batch, by the floor its record names. ---
	{
		const auto Permille = [](int32 Level) { return 1000 + 150 * FMath::Max(0, -Level - 5); };
		const int32 Floors[3] = { -5, -6, -8 };
		for (int32 F = 0; F < 3; ++F)
		{
			Reset();
			const int32 BorerD = Kit();
			Sim->ExtendShaft(8, Head0);
			int32 Id = INDEX_NONE;
			Expect(DigCavernNow(Floors[F], 1, FIntPoint(1, 0), FIntPoint(1, 0), Id, Why), FString::Printf(TEXT("CV4 a one-point cavern on %d is ordered (%s)"), Floors[F], *Why));
			for (int32 K = 0; K < 6 && Recipe(BorerD).IsNone(); ++K) { Step(); }
			const FRHBuildingInstance* Bd = FindB(BorerD);
			const double Started = Bd ? Bd->BatchRemaining_h + EraDt / 50.0 : -1.0;
			const double Want = 3.0 * Permille(Floors[F]) / 1000.0;
			Expect(Bd && Bd->ActiveRecipe == NCarveCell && Near(Started, Want, 1.0e-9), FString::Printf(TEXT("CV4 a cavern batch on %d starts at %.2f h (3 h x %d per mille): %.9f"), Floors[F], Want, Permille(Floors[F]), Started));
		}
	}

	// --- CV5. Cancel with a batch in flight. ---
	{
		Reset();
		const int32 BorerC = Kit();
		Sim->ExtendShaft(3, Head0);
		Place(TEXT("Pylon"), 6000.0, 2000.0);
		Expect(CavernOrder(-1, 1, FIntPoint(1, 0), FIntPoint(2, 1)) && Uncarved(1) == 4, TEXT("CV5 a 2 x 2 cavern is ordered"));
		for (int32 K = 0; K < 6 && Sim->GetCavernBatchesInFlight(1) == 0; ++K) { Step(); }
		Expect(Sim->GetCavernBatchesInFlight(1) == 1 && Sim->GetFloorCarvedCells(-1) == 0, TEXT("CV5 one batch is in flight and nothing is carved yet"));
		Expect(CancelCavernOrder(1) && Sim->FindCavern(1)->bCancelled && !Sim->IsCavernOpen(*Sim->FindCavern(1)) && Sim->GetOpenCavernCount() == 0 && Sim->GetCavernBatchesInFlight(1) == 1, TEXT("CV5 the cancel lands: cancelled, not open, the batch in flight still counted"));
		int32 NewId = INDEX_NONE;
		Expect(!SinkNow(PtCm(2, 1), 1, NewId, Why) && Why.Contains(TEXT("cavern #1")), FString::Printf(TEXT("CV5 while a batch of a cancelled cavern is in flight its rock is still protected: no shaft there (%s)"), *Why));
		Steps(4);
		Expect(Sim->GetFloorCarvedCells(-1) == 1 && Cells(-1)[0] == FIntPoint(1, 0) && Sim->GetCavernBatchesInFlight(1) == 0 && Uncarved(1) == 3, TEXT("CV5 the batch in flight landed its one cell, (1,0), and nothing more is in flight"));
		Steps(60);
		Expect(Sim->GetFloorCarvedCells(-1) == 1 && Recipe(BorerC).IsNone() && Uncarved(1) == 3, TEXT("CV5 sixty steps later no new batch started: the cells carved stay, the rest is rock"));
		FString W;
		Expect(TryCavern(-1, 1, FIntPoint(1, 1), FIntPoint(2, 1), W), FString::Printf(TEXT("CV5 a new cavern may be ordered over the cancelled one's rock (%s)"), *W));
		Expect(SinkNow(PtCm(2, 1), 1, NewId, Why) && NewId == 1, FString::Printf(TEXT("CV5 and, with no batch in flight, a shaft may be sunk on it (%s)"), *Why));
	}

	// --- CV6. RC 1: tunnel 1 and cavern 1 at once. Each completion goes to its own branch; GetTunnelBatchesInFlight counts only tunnel work. ---
	{
		Reset();
		const int32 BorerA = Kit();
		const int32 BorerB = Place(TEXT("Borer"), 2000.0, 5500.0);
		const int32 BorerC = Place(TEXT("Borer"), -1500.0, 2000.0);
		Sim->ExtendShaft(3, Head0);
		Sim->ExcavateFloor(-1, 6, R, 0);
		int32 TunnelId = INDEX_NONE, CavernId = INDEX_NONE;
		Expect(BorerA != BorerB && BorerB != BorerC && DigTunnelNow(-1, FIntPoint(2, 0), FIntPoint(3, 0), TunnelId, Why) && TunnelId == 1,
			FString::Printf(TEXT("CV6 setup: three Borers, a two-cell tunnel 1 on -1 from (2,0) to (3,0) (%s)"), *Why));
		Expect(DigCavernNow(-2, 1, FIntPoint(1, 0), FIntPoint(2, 1), CavernId, Why) && CavernId == 1, FString::Printf(TEXT("CV6 and a four-cell cavern 1 on -2 beside the column (%s)"), *Why));
		bool bAll = false;
		for (int32 K = 0; K < 8 && !bAll; ++K)
		{
			Step();
			bAll = Recipe(BorerA) == NCarveCell && Recipe(BorerB) == NCarveCell && Recipe(BorerC) == NCarveCell;
		}
		Expect(bAll, TEXT("CV6 all three Borers have a CarveCell batch in flight"));
		Expect(Sim->GetTunnelBatchesInFlight(1) == 2 && Sim->GetCavernBatchesInFlight(1) == 1 && Sim->GetCavernBatchesInFlightOn(1, -2) == 1 && Sim->GetCavernBatchesInFlightOn(1, -1) == 0,
			FString::Printf(TEXT("CV6 tunnel 1 has its own two batches in flight and cavern 1 its one on -2: GetTunnelBatchesInFlight(1) = %d (it must not count the cavern's batch, which shares the Id), GetCavernBatchesInFlight(1) = %d (%d on -2, %d on -1)"), Sim->GetTunnelBatchesInFlight(1), Sim->GetCavernBatchesInFlight(1), Sim->GetCavernBatchesInFlightOn(1, -2), Sim->GetCavernBatchesInFlightOn(1, -1)));
		for (int32 K = 0; K < 300 && (Uncarved(1) > 0 || Sim->GetTunnelUncarved(*Sim->FindTunnel(1)) > 0); ++K) { Step(); }
		Steps(4);
		const TArray<FIntPoint> OnMinus1 = Cells(-1), OnMinus2 = Cells(-2);
		Expect(Sim->GetTunnelUncarved(*Sim->FindTunnel(1)) == 0 && OnMinus1.Num() == 8 && OnMinus1.Contains(FIntPoint(2, 0)) && OnMinus1.Contains(FIntPoint(3, 0)),
			FString::Printf(TEXT("CV6 each completion went to its own branch: the tunnel is finished with exactly its two cells on -1 (%d cells there)"), OnMinus1.Num()));
		bool bInBox = true;
		for (const FIntPoint& P : OnMinus2) { bInBox &= P.X >= 1 && P.X <= 2 && P.Y >= 0 && P.Y <= 1; }
		Expect(Uncarved(1) == 0 && OnMinus2.Num() == 4 && bInBox && SeededHolds(-2, 0, { FIntPoint(0, 0) }), FString::Printf(TEXT("CV6 and the cavern is finished with exactly its four cells on -2, none of them a tunnel cell (%d cells there)"), OnMinus2.Num()));
		Expect(Sim->GetTunnelBatchesInFlight(1) == 0 && Sim->GetCavernBatchesInFlight(1) == 0, TEXT("CV6 no batch is left in flight on either"));
	}

	// --- CV7. Era against the agent band: one open cavern, the same cells in the same order. ---
	{
		Reset();
		Kit();
		Sim->ExtendShaft(3, Head0);
		FString W0, W1;
		const bool bGate0 = Sim->CanEnterEraMode(W0);
		int32 Id = INDEX_NONE;
		Expect(DigCavernNow(-1, 2, FIntPoint(1, 0), FIntPoint(2, 1), Id, Why), TEXT("CV7 setup: the 2 x 2 x 2 cavern is ordered"));
		const bool bGate1 = Sim->CanEnterEraMode(W1);
		Expect(bGate0 == bGate1 && W0 == W1 && !W1.Contains(TEXT("cavern")), FString::Printf(TEXT("CV7 CanEnterEraMode answers the same with the open cavern (%s; reason \"%s\")"), bGate1 ? TEXT("yes") : TEXT("no"), *W1));
		FString PairErr;
		Expect(Sim->SaveColony(TEXT("cavernspair"), PairErr), FString::Printf(TEXT("CV7 the colony is saved for both legs (%s)"), *PairErr));
		struct FLeg { TArray<FIntPoint> Top, Bottom; bool bFinished = false; };
		const auto RunLeg = [&](bool bAgent) -> FLeg
		{
			FString Le;
			if (!Sim->LoadColony(TEXT("cavernspair"), Le) || !Le.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("CAVERNS: leg load failed: %s"), *Le); ++Fails; }
			FLeg L;
			if (bAgent)
			{
				const int32 Total = 2 * (int32)(SolLen / (double)AgentDt);
				for (int32 K = 0; K < Total; ++K)
				{
					Clock->Debug_AdvanceSimSeconds((double)AgentDt);
					Sim->Debug_SetBatteryWh(1.0e9);
					Sim->Debug_StepSim(AgentDt);
				}
			}
			else
			{
				const int32 Total = 2 * (int32)(SolLen / EraDt);
				for (int32 K = 0; K < Total; ++K) { Step(); }
			}
			L.Top = Cells(-1);
			L.Bottom = Cells(-2);
			L.bFinished = Sim->FindCavern(1) && Sim->GetCavernUncarved(*Sim->FindCavern(1)) == 0;
			return L;
		};
		const FLeg Agent = RunLeg(true);
		const FLeg Era = RunLeg(false);
		Expect(Agent.bFinished && Era.bFinished && Agent.Top.Num() == 4 && Era.Top.Num() == 4 && Agent.Bottom.Num() == 4 && Era.Bottom.Num() == 4, FString::Printf(TEXT("CV7 over 2 sols both bands finish the cavern: agent %d + %d cells, era %d + %d cells"), Agent.Top.Num(), Agent.Bottom.Num(), Era.Top.Num(), Era.Bottom.Num()));
		Expect(Agent.Top == Era.Top && Agent.Bottom == Era.Bottom, TEXT("CV7 and they land the same cells in the same ordinal order on both floors (era and agent steps share the batch boundaries)"));
	}

	// --- CV8. The save: the layout, mid-batch against a twin, every hand-made refusal, the X >= 100 repair, a v47 file. ---
	{
		// The file colony: shaft 0 to -10, six cells on -1 and -2, four on -9 (warm), an open tunnel 1 on -1. Z has no cavern.
		const auto FileColony = [&]()
		{
			Reset();
			Sim->ExtendShaft(10, Head0);
			Sim->ExcavateFloor(-1, 6, R, 0);
			Sim->ExcavateFloor(-2, 6, R, 0);
			Sim->ExcavateFloor(-9, 4, R, 0);
			int32 T = INDEX_NONE;
			return DigTunnelNow(-1, FIntPoint(2, 0), FIntPoint(6, 0), T, Why) && T == 1;
		};
		Expect(FileColony(), TEXT("CV8 file setup: shaft 0 to -10, six cells on -1 and -2, four on -9, tunnel 1 open on -1"));
		Expect(Sim->SaveColony(TEXT("cavz"), SaveErr), FString::Printf(TEXT("CV8 the colony with no cavern is saved (%s)"), *SaveErr));
		Sim->Debug_SetFloorWarmth(-9, 0.25);
		Expect(Sim->SaveColony(TEXT("cavzw"), SaveErr), TEXT("CV8 and with a warm floor"));
		Sim->Debug_SetFloorWarmth(-9, 0.0);
		int32 Id1 = INDEX_NONE, Id2 = INDEX_NONE;
		Expect(DigCavernNow(-2, 2, FIntPoint(1, 0), FIntPoint(2, 1), Id1, Why) && Id1 == 1 && DigCavernNow(-5, 1, FIntPoint(1, 0), FIntPoint(2, 1), Id2, Why) && Id2 == 2, FString::Printf(TEXT("CV8 two caverns are ordered: 1 on -2 and -3, 2 on -5 (%s)"), *Why));
		Expect(Sim->SaveColony(TEXT("cavw2"), SaveErr), TEXT("CV8 and saved (harness path: no sim step between the saves, so only the block differs)"));
		Sim->Debug_SetFloorWarmth(-9, 0.25);
		Expect(Sim->SaveColony(TEXT("cavzw2"), SaveErr), TEXT("CV8 and saved once more with the warm floor"));
		TArray<uint8> Z, ZW, W2, ZW2;
		const bool bRead = FFileHelper::LoadFileToArray(Z, *SaveFilePath(TEXT("cavz"))) && FFileHelper::LoadFileToArray(ZW, *SaveFilePath(TEXT("cavzw")))
			&& FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("cavw2"))) && FFileHelper::LoadFileToArray(ZW2, *SaveFilePath(TEXT("cavzw2")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W2.GetData() + 4, 4); }
		int32 At = INDEX_NONE;
		for (int32 I = 0; bRead && I < FMath::Min(Z.Num(), W2.Num()) && At == INDEX_NONE; ++I) { if (Z[I] != W2[I]) { At = I; } }
		Expect(bRead && Ver == 48 && At != INDEX_NONE && Rd(Z, At) == 1 && Rd(Z, At + 4) == 0 && Rd(W2, At) == 3 && Rd(W2, At + 4) == 2 && W2.Num() == Z.Num() + 80,
			FString::Printf(TEXT("CV8 the file says version %u; the cavern block at byte %d: the default colony writes NextCavernId 1 and 0 caverns (8 bytes), two caverns write 3, 2 and 40 bytes each (%d -> %d bytes)"), Ver, At, Z.Num(), W2.Num()));
		if (!bRead || At == INDEX_NONE) { return Finish(); }
		Expect(Rd(Z, At - 4) == 0, TEXT("CV8 straight before the default block stands the warmth block's count 0"));
		int32 At2 = INDEX_NONE;
		for (int32 I = 0; I < FMath::Min(ZW.Num(), ZW2.Num()) && At2 == INDEX_NONE; ++I) { if (ZW[I] != ZW2[I]) { At2 = I; } }
		Expect(At2 != INDEX_NONE && Rd(ZW, At2 - 20) == 1 && Rd(ZW, At2 - 16) == 0 && Rd(ZW, At2 - 12) == -9 && Rd(ZW, At2) == 1,
			FString::Printf(TEXT("CV8 with a warm floor the cavern block starts at byte %d, straight after the 20 bytes of the warmth block (1 floor: region 0, level -9, Warmth), so no earlier offset moved"), At2));
		const int32 R1 = At + 8, R2 = At + 48;
		Expect(Rd(W2, R1) == 1 && Rd(W2, R1 + 4) == 0 && Rd(W2, R1 + 8) == 0 && Rd(W2, R1 + 12) == -2 && Rd(W2, R1 + 16) == -3 && Rd(W2, R1 + 20) == 1 && Rd(W2, R1 + 24) == 0 && Rd(W2, R1 + 28) == 2 && Rd(W2, R1 + 32) == 1 && Rd(W2, R1 + 36) == 0
			&& Rd(W2, R2) == 2 && Rd(W2, R2 + 12) == -5 && Rd(W2, R2 + 16) == -5 && Rd(W2, R2 + 36) == 0,
			TEXT("CV8 the layout reads back as written: per cavern Id, region, work shaft, top, bottom, the four corner values and the cancelled flag (cavern 1: floors -2 to -3, (1,0) to (2,1); cavern 2: -5)"));
		// Round trip.
		FString E;
		Expect(Sim->LoadColony(TEXT("cavw2"), E) && E.IsEmpty() && Sim->GetCaverns().Num() == 2 && Sim->GetNextCavernId() == 3 && Sim->FindCavern(2) && Sim->FindCavern(2)->Top == -5 && Sim->FindCavern(1)->Bottom == -3 && Sim->FindCavern(1)->Max == FIntPoint(2, 1),
			FString::Printf(TEXT("CV8 the two caverns load back exactly (%s)"), *E));
		Expect(Sim->SaveColony(TEXT("cavw2b"), SaveErr), TEXT("CV8 saved again"));
		TArray<uint8> W2b;
		FFileHelper::LoadFileToArray(W2b, *SaveFilePath(TEXT("cavw2b")));
		Expect(W2b.Num() == W2.Num() && FMemory::Memcmp(W2b.GetData() + At, W2.GetData() + At, 88) == 0, FString::Printf(TEXT("CV8 the cavern block round-trips byte-equal (88 bytes; whole files %s)"), W2b == W2 ? TEXT("equal too") : TEXT("differ elsewhere (runtime stamps), the block is the claim")));
		// Hand-made files: every structural refusal, the colony untouched.
		const auto TryFile = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			const int32 CavernsN = Sim->GetCaverns().Num(), Cells1 = Sim->GetFloorCarvedCells(-1), Builds = Sim->GetBuildings().Num(), Tunnels = Sim->GetTunnels().Num();
			const bool bOk = Sim->LoadColony(Slot, OutErr);
			const bool bSame = Sim->GetCaverns().Num() == CavernsN && Sim->GetFloorCarvedCells(-1) == Cells1 && Sim->GetBuildings().Num() == Builds && Sim->GetTunnels().Num() == Tunnels;
			return !bOk && bSame && OutErr.Contains(TEXT("Save cavern list invalid"));
		};
		FString Err;
		{ TArray<uint8> T = W2; PutI(T, At + 4, -1); Expect(TryFile(TEXT("cavr1"), T, Err) && Err.Contains(TEXT("cavern count -1")), FString::Printf(TEXT("CV8 a count below 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, At + 4, RH::MaxCavernRecords + 1); Expect(TryFile(TEXT("cavr2"), T, Err) && Err.Contains(TEXT("cavern count 1025")), FString::Printf(TEXT("CV8 a count above the cap of %d records is refused (%s)"), RH::MaxCavernRecords, *Err)); }
		{ TArray<uint8> T = W2; PutI(T, At, 0); Expect(TryFile(TEXT("cavr3"), T, Err) && Err.Contains(TEXT("next id 0")), FString::Printf(TEXT("CV8 a next Id below 1 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, At, 2); Expect(TryFile(TEXT("cavr4"), T, Err) && Err.Contains(TEXT("cavern count 2")), FString::Printf(TEXT("CV8 a count that is not below the next Id is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; T.SetNum(R1 + 40); Expect(TryFile(TEXT("cavr5"), T, Err) && Err.Contains(TEXT("cavern count 2")), FString::Printf(TEXT("CV8 a count larger than the bytes left is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R2, 1); Expect(TryFile(TEXT("cavr6"), T, Err) && Err.Contains(TEXT("ids must ascend")), FString::Printf(TEXT("CV8 a repeated Id is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R2, 3); Expect(TryFile(TEXT("cavr7"), T, Err) && Err.Contains(TEXT("ids must ascend")), FString::Printf(TEXT("CV8 an Id not below the next Id is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1, 0); Expect(TryFile(TEXT("cavr8"), T, Err) && Err.Contains(TEXT("ids must ascend")), FString::Printf(TEXT("CV8 an Id of 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 4, 1); Expect(TryFile(TEXT("cavr9"), T, Err) && Err.Contains(TEXT("region")), FString::Printf(TEXT("CV8 a RegionId other than 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 12, 0); Expect(TryFile(TEXT("cavr10"), T, Err) && Err.Contains(TEXT("outside -1..-20")), FString::Printf(TEXT("CV8 a top floor of 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 16, -21); Expect(TryFile(TEXT("cavr11"), T, Err) && Err.Contains(TEXT("outside -1..-20")), FString::Printf(TEXT("CV8 a bottom floor below -20 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 12, -3); PutI(T, R1 + 16, -2); Expect(TryFile(TEXT("cavr12"), T, Err) && Err.Contains(TEXT("outside -1..-20")), FString::Printf(TEXT("CV8 a bottom above the top is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R2 + 12, -11); PutI(T, R2 + 16, -11); Expect(TryFile(TEXT("cavr13"), T, Err) && Err.Contains(TEXT("does not reach floor -11")), FString::Printf(TEXT("CV8 a floor no loaded shaft reaches is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 8, 5); Expect(TryFile(TEXT("cavr14"), T, Err) && Err.Contains(TEXT("shaft 5")), FString::Printf(TEXT("CV8 a work shaft that does not exist is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 28, 9); Expect(TryFile(TEXT("cavr15"), T, Err) && Err.Contains(TEXT("9 x 2")), FString::Printf(TEXT("CV8 a side over CavernMaxSide is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 28, 0); Expect(TryFile(TEXT("cavr16"), T, Err) && Err.Contains(TEXT("0 x 2")), FString::Printf(TEXT("CV8 corners the wrong way round are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 12, -1); PutI(T, R1 + 16, -4); Expect(TryFile(TEXT("cavr17"), T, Err) && Err.Contains(TEXT("on 4 floors")), FString::Printf(TEXT("CV8 four floors are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 20, 0); PutI(T, R1 + 24, 0); Expect(TryFile(TEXT("cavr18"), T, Err) && Err.Contains(TEXT("shaft column (0,0)")), FString::Printf(TEXT("CV8 a box over a shaft column is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R2 + 12, -2); PutI(T, R2 + 16, -3); Expect(TryFile(TEXT("cavr19"), T, Err) && Err.Contains(TEXT("another open cavern")), FString::Printf(TEXT("CV8 two open caverns sharing an uncarved point are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; PutI(T, R1 + 12, -1); PutI(T, R1 + 16, -1); PutI(T, R1 + 20, 4); PutI(T, R1 + 24, 0); PutI(T, R1 + 28, 4); PutI(T, R1 + 32, 0); Expect(TryFile(TEXT("cavr20"), T, Err) && Err.Contains(TEXT("open tunnel")), FString::Printf(TEXT("CV8 an open cavern sharing an uncarved point with an open tunnel is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W2; const uint32 V47 = 47; FMemory::Memcpy(T.GetData() + 4, &V47, 4); FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("cavr21"))); FString E47; const bool bOk = Sim->LoadColony(TEXT("cavr21"), E47); Expect(!bOk && E47.Contains(TEXT("Save format mismatch")) && E47.Contains(TEXT("version 47")) && E47.Contains(TEXT("/48")), FString::Printf(TEXT("CV8 a v47 file is refused (%s)"), *E47)); }
		// A cancelled cavern's rock may be sunk on later, so a column in its box is not a structural refusal; an open one is.
		{
			TArray<uint8> T = W2; PutI(T, R1 + 20, 0); PutI(T, R1 + 24, 0); PutI(T, R1 + 36, 1);
			FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("cavr22")));
			FString Ec;
			Expect(Sim->LoadColony(TEXT("cavr22"), Ec) && Ec.IsEmpty() && Sim->FindCavern(1) && Sim->FindCavern(1)->bCancelled && Sim->FindCavern(1)->Min == FIntPoint(0, 0), FString::Printf(TEXT("CV8 a cancelled cavern with a column in its box loads (%s)"), *Ec));
		}
		// Mid-batch: a save equals a twin that never saved.
		{
			Reset();
			Kit();
			Sim->ExtendShaft(3, Head0);
			Expect(CavernOrder(-1, 2, FIntPoint(1, 0), FIntPoint(2, 1)), TEXT("CV8 mid-batch setup: the 2 x 2 x 2 cavern is ordered"));
			for (int32 K = 0; K < 200 && !(Sim->GetFloorCarvedCells(-1) >= 3 && Sim->GetCavernBatchesInFlight(1) == 1); ++K) { Step(); }
			Expect(Sim->GetFloorCarvedCells(-1) >= 3 && Sim->GetCavernBatchesInFlight(1) == 1, FString::Printf(TEXT("CV8 mid-carve: %d cells on -1 and a batch in flight"), Sim->GetFloorCarvedCells(-1)));
			const FString AtSave = Digest();
			Expect(Sim->SaveColony(TEXT("cavmid"), SaveErr), FString::Printf(TEXT("CV8 saved mid-batch (%s)"), *SaveErr));
			Steps(80);
			const FString Control = Digest();
			FString Le;
			Expect(Sim->LoadColony(TEXT("cavmid"), Le) && Le.IsEmpty() && Digest() == AtSave && Sim->GetCavernBatchesInFlight(1) == 1, FString::Printf(TEXT("CV8 the load restores the cavern, its cells and the batch in flight (%s)"), *Le));
			Steps(80);
			Expect(Digest() == Control && Uncarved(1) == 0, TEXT("CV8 run on for 80 steps: the loaded colony equals the control that never saved, and the cavern is finished"));
		}
		// The X >= 100 repair: a batch in flight whose cavern the file no longer lists (or whose floor is outside its box) completes with spoil and no cell.
		{
			Reset();
			Kit();
			Sim->ExtendShaft(3, Head0);
			Expect(CavernOrder(-1, 1, FIntPoint(1, 0), FIntPoint(2, 1)), TEXT("CV8 repair setup: a 2 x 2 cavern on -1"));
			for (int32 K = 0; K < 8 && Sim->GetCavernBatchesInFlight(1) == 0; ++K) { Step(); }
			Expect(Sim->GetCavernBatchesInFlight(1) == 1 && Sim->SaveColony(TEXT("cavrep"), SaveErr), TEXT("CV8 repair setup: a batch is in flight and the colony is saved"));
			TArray<uint8> Rep;
			FFileHelper::LoadFileToArray(Rep, *SaveFilePath(TEXT("cavrep")));
			// Find the block by its literal ints: NextCavernId 2, count 1, then the record (1, 0, 0, -1, -1, 1, 0, 2, 1, 0).
			const int32 Pattern[12] = { 2, 1, 1, 0, 0, -1, -1, 1, 0, 2, 1, 0 };
			int32 Where = INDEX_NONE;
			for (int32 I = 0; I + 48 <= Rep.Num() && Where == INDEX_NONE; I += 4)
			{
				bool bMatch = true;
				for (int32 J = 0; J < 12 && bMatch; ++J) { bMatch &= Rd(Rep, I + 4 * J) == Pattern[J]; }
				if (bMatch) { Where = I; }
			}
			Expect(Where != INDEX_NONE, FString::Printf(TEXT("CV8 repair setup: the cavern block is at byte %d"), Where));
			if (Where != INDEX_NONE)
			{
				// (a) the record is gone from the file (count 0), the work record stays.
				TArray<uint8> Gone;
				Gone.Append(Rep.GetData(), Where + 8);
				Gone.Append(Rep.GetData() + Where + 48, Rep.Num() - (Where + 48));
				PutI(Gone, Where + 4, 0);
				FFileHelper::SaveArrayToFile(Gone, *SaveFilePath(TEXT("cavrepa")));
				FString Lg;
				Expect(Sim->LoadColony(TEXT("cavrepa"), Lg) && Lg.IsEmpty() && Sim->GetCaverns().Num() == 0, FString::Printf(TEXT("CV8 repair (a): a file whose cavern list lost the cavern still loads (%s)"), *Lg));
				const int32 Empty0 = Sim->GetCavernBatchesEmpty();
				Steps(8);
				Expect(Sim->GetCavernBatchesEmpty() == Empty0 + 1 && Sim->GetFloorCarvedCells(-1) == 0,
					TEXT("CV8 repair (a): the batch names a cavern the file does not list, so it completes with its spoil and no cell, counted as empty"));
				// (b) the cavern now stands on -2 only, but the batch in flight names -1.
				TArray<uint8> Moved = Rep;
				PutI(Moved, Where + 8 + 12, -2);
				PutI(Moved, Where + 8 + 16, -2);
				FFileHelper::SaveArrayToFile(Moved, *SaveFilePath(TEXT("cavrepb")));
				FString Lm;
				Expect(Sim->LoadColony(TEXT("cavrepb"), Lm) && Lm.IsEmpty() && Sim->GetCaverns().Num() == 1 && Sim->FindCavern(1)->Top == -2, FString::Printf(TEXT("CV8 repair (b): a file whose cavern moved to -2 loads (%s)"), *Lm));
				const int32 Empty1 = Sim->GetCavernBatchesEmpty();
				Steps(8);
				Expect(Sim->GetCavernBatchesEmpty() == Empty1 + 1 && Sim->GetFloorCarvedCells(-1) == 0, TEXT("CV8 repair (b): the batch's floor (-1) is outside its cavern's box, so it completes with spoil and no cell"));
			}
		}
	}

	// --- CV9. Ordinal-to-point is identical across a save and a load. ---
	{
		Reset();
		Kit();
		Sim->ExtendShaft(3, Head0);
		Sim->ExcavateFloor(-1, 6, R, 0);
		Expect(CavernOrder(-1, 3, FIntPoint(1, 0), FIntPoint(3, 1)) && Uncarved(1) == 16, TEXT("CV9 a 3 x 2 x 3 cavern (1,0) to (3,1) is ordered over a six-cell dig on -1: 4 + 6 + 6 points uncarved"));
		for (int32 K = 0; K < 400 && Uncarved(1) > 0; ++K) { Step(); }
		Steps(3);
		TArray<FIntPoint> Before[3];
		for (int32 F = 0; F < 3; ++F) { Before[F] = Cells(-1 - F); }
		Expect(Uncarved(1) == 0 && Before[0].Num() == 10 && Before[1].Num() == 6 && Before[2].Num() == 6, FString::Printf(TEXT("CV9 the cavern is finished: %d, %d and %d cells on -1, -2 and -3"), Before[0].Num(), Before[1].Num(), Before[2].Num()));
		FString E;
		Expect(Sim->SaveColony(TEXT("cavord"), SaveErr) && Sim->LoadColony(TEXT("cavord"), E) && E.IsEmpty(), FString::Printf(TEXT("CV9 saved and loaded (%s)"), *E));
		bool bSame = true;
		for (int32 F = 0; F < 3; ++F)
		{
			const TArray<FIntPoint> After = Cells(-1 - F);
			bSame &= After == Before[F];
			for (int32 i = 0; i < After.Num(); ++i) { bSame &= Sim->GetFloorCell(-1 - F, i) == Before[F][i] && Sim->FindFloorCell(-1 - F, Before[F][i]) == i; }
		}
		Expect(bSame && Sim->GetFloorCellFallbacks() == 0, TEXT("CV9 every floor's ordinal-to-point map is identical after the load, in both directions, and no read fell back to the spiral"));
	}

	return Finish();
}
