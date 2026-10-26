#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistricts.h"
#include "RHGrowth.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

// D6b floor shapes self-test (docs/qa/2026-10-22/d6-caverns-design.md: the DIRECTOR ANSWERs Q-A (a), Q-B (a), Q-C (a), the MERGED DESIGN
// AND REVISION section 2 with RC 3, 4, 5, 6 and 10). Fails loudly. `-deepdistricts`.
// A deep district is a SHAPE: points on one or more floors in region 0's underground frame (the lattice anchored on shaft 0's OFF-GRID head).
// Two fixture rules (RC 10): every colony here holds 60 colonists or fewer (so the commute ramp is 0), and no case prints a commute distance,
// a long count or a flow, so D6c's real walks cannot move this log.
//  DD1 paint, join, a multi-floor shape, every refusal, the cap; DD2 the frame (an off-grid head maps a building exactly, LatticeOf would not);
//  DD3 home cells (RC 4: an unfurnished cell 0 before a furnished cell 3, CPC 1, 0.5, 0 and 1.5, an overfull floor, a departure) against an
//  independent oracle; DD4 counts, beds and seats per shape; DD5 a whole-floor district is byte-equal with and without a shape elsewhere;
//  DD6 DeepCells zones free cells, then carves eligible rock, the ghost equals the carve, no carved point left in Plan (RC 3); DD7 civic
//  stays inside the shape; DD8 era against the agent band; DD9 the save (the layout, two shapes on one floor, every hand-made refusal,
//  a v48 file, and a district list with a duplicated Id - step-2 fix 4); DD10 the keys (RC 6) and -RHVerifyCaches' fresh checks.
//  DD1 also holds step-2 fix 5: a paint corner past the lattice bound (2^20 cells) is refused, and the box at the int32 edge at once.
namespace
{
	// Counts the Error lines LogRedHopeSim prints while it is attached: a structural refusal at load must come back through OutError and
	// never as an Error line (RC 5), and the battery's own gate cannot tell which case printed one.
	struct FRHErrorLineCounter : public FOutputDevice
	{
		int32 Count = 0;
		virtual void Serialize(const TCHAR* /*Text*/, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error && Category == FName(TEXT("LogRedHopeSim"))) { ++Count; }
		}
	};

	// Keeps the Warning lines LogRedHopeSim prints while it is attached (step-2 fix 4: a repair after teardown is a Warning that names what it dropped).
	struct FRHWarningLineCapture : public FOutputDevice
	{
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Warning && Category == FName(TEXT("LogRedHopeSim"))) { Lines.Add(Text); }
		}
	};
}

int32 RHRunDeepDistrictsSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== DEEP DISTRICTS TEST (D6b, 2026-10-22) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DEEPDISTRICTS ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPDISTRICTS FAIL: %s"), *What); ++Fails; }
	};
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NHomes(TEXT("Homes")), NIndustry(TEXT("Industry")), NResearch(TEXT("Research")), NCivic(TEXT("Civic")), NFarming(TEXT("Farming"));
	const FName NStruct(TEXT("Struct")), NOx(TEXT("Oxygen"));
	FString R, Why, Note;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
	double PinHope = -1.0; // >= 0: smoothed Hope is held there before every step
	const auto Finish = [&]() -> int32
	{
		Expect(Sim->GetFloorCellFallbacks() == 0, TEXT("DD over the whole suite not one ordinal read fell outside its floor and back to the spiral"));
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DEEP DISTRICTS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto Land = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		if (PinHope >= 0.0) { Sim->Debug_SetHopeSmoothed(PinHope); }
		Sim->EraStep((float)EraDt);
	};
	const auto Steps = [&](int32 N) { for (int32 K = 0; K < N; ++K) { Land(); } };
	const auto RunSols = [&](double Sols) { Steps(FMath::RoundToInt32(Sols * SolLen / EraDt)); };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, 1, 1);
		int32 Id = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def) && B.Level == Level && FMath::Abs(B.LocationCm.X - X) < 1.0 && FMath::Abs(B.LocationCm.Y - Y) < 1.0) { Id = B.Id; } }
		return Id;
	};
	// A load with the Error lines counted.
	int32 LoadErrorLines = 0;
	const auto LoadCounted = [&](const TCHAR* Slot, FString& OutErr)
	{
		FRHErrorLineCounter Counter;
		GLog->AddOutputDevice(&Counter);
		const bool bOk = Sim->LoadColony(Slot, OutErr);
		GLog->RemoveOutputDevice(&Counter);
		LoadErrorLines = Counter.Count;
		return bOk;
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto Rd = [](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
	const auto PutI = [](TArray<uint8>& Bytes, int32 Off, int32 V) { if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 4); } };
	const auto Cells = [&](int32 Level) { TArray<FIntPoint> Out; if (const FRHFloor* Fl = Sim->FindFloor(Level)) { Out = Fl->GetCells(); } return Out; };
	const auto PlanOf = [&](int32 Level) { TArray<FIntPoint> Out; if (const FRHFloor* Fl = Sim->FindFloor(Level)) { Out = Fl->GetPlan(); } return Out; };

	// --- The base colony. Shaft 0's head is OFF the 10 m grid (1370, 1480) cm, so the underground frame and the surface grid disagree by a cell
	// on both axes; floors -1 (12 cells) and -2 (8 cells) are rated; shaft 0 reaches -4; nobody lives here yet. ---
	const FVector Head(1370.f, 1480.f, 0.f);
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Sim->GetShaftHeadCm(), FIntPoint(X, Y)); };
	Sim->Debug_SetGrowthEnabled(false);
	Sim->Debug_SetBigArrivalDone(true);
	Place(TEXT("SolarArray"), 3500, 1000);
	Place(TEXT("BatteryBank"), 1000, 3500);
	Sim->AddStock(NStruct, 5000.0);
	Sim->AddStock(FName("Shielding"), 3000.0);
	Sim->AddStock(NOx, 6000.0);
	Sim->AddStock(FName("Water"), 150.0); // under the quota's 200: the first quota stays open
	Sim->AddStock(FName("Food"), 6000.0);
	Sim->ExtendShaft(4, Head);
	Sim->ExcavateFloor(-1, 12, R);
	Sim->ExcavateFloor(-2, 8, R);
	const int32 FiltF1 = Place(TEXT("AirFilter"), Head.X, Head.Y + 1000.0, -1); // head-frame (0,1): ordinal 2 on both floors
	const int32 FiltF2 = Place(TEXT("AirFilter"), Head.X, Head.Y + 1000.0, -2);
	RunSols(3.0);
	Sim->Debug_SetColonistsPerCell(1.0);
	Sim->Debug_SetCityShare(0.0);
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("deepdistricts_base"), SaveErr);
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("deepdistricts_base"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPDISTRICTS FAIL: loading the base failed: %s"), *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetBigArrivalDone(true);
		Sim->Debug_SetColonistsPerCell(1.0);
		PinHope = -1.0;
		Rejects.Reset();
		return bOk;
	};
	// One order, one era step (the 60 s step covers the 45 s order lag). True when it was not rejected.
	const auto Send = [&](const FRHCommand& C)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(C);
		Land();
		Note = Rejects.Num() == R0 ? FString(TEXT("accepted")) : Rejects.Last();
		return Rejects.Num() == R0;
	};
	// A shape order: Floors >= 1 below ground; the corners in the head frame.
	const auto ShapeCmd = [&](FName Kind, int32 Level, int32 Floors, int32 X1, int32 Y1, int32 X2, int32 Y2)
	{
		FRHCommand C;
		C.Verb = FName("PaintDistrict");
		C.Target = Kind;
		C.Level = Level;
		C.Value = Floors;
		C.Location = PtCm(X1, Y1);
		C.LocationB = PtCm(X2, Y2);
		return C;
	};
	const auto FloorCmd = [&](FName Kind, int32 Level) { FRHCommand C; C.Verb = FName("PaintDistrict"); C.Target = Kind; C.Level = Level; return C; };
	const auto EraseCmd = [](int32 Id) { FRHCommand C; C.Verb = FName("EraseDistrict"); C.Value = (double)Id; return C; };
	const auto Shape = [&](FName Kind, int32 Level, int32 Floors, int32 X1, int32 Y1, int32 X2, int32 Y2) { return Send(ShapeCmd(Kind, Level, Floors, X1, Y1, X2, Y2)); };
	const auto CanShape = [&](FName Kind, int32 Level, int32 Floors, int32 X1, int32 Y1, int32 X2, int32 Y2, FString& Out, FRHDistrictPaintPreview* Pre = nullptr)
	{
		Out.Reset();
		return Sim->CanPaintDistrict(Kind, Level, PtCm(X1, Y1), PtCm(X2, Y2), Out, Pre, Floors);
	};
	const auto Find = [&](int32 Id) { return Sim->FindDistrict(Id); };
	const auto CountsOf = [&](int32 Id) -> FRHDistrictCounts
	{
		TArray<FRHDistrictCounts> All;
		Sim->GetDistrictCounts(All);
		for (const FRHDistrictCounts& C : All) { if (C.DistrictId == Id) { return C; } }
		return FRHDistrictCounts();
	};
	const auto Sorted = [](const TArray<FIntVector>& L)
	{
		for (int32 i = 1; i < L.Num(); ++i) { if (!RHDistricts::DeepLess(L[i - 1], L[i])) { return false; } }
		return true;
	};
	const auto DistrictDigest = [&]() -> FString
	{
		FString D = FString::Printf(TEXT("next%d"), Sim->GetNextDistrictId());
		for (const FRHDistrict& Dist : Sim->GetDistricts())
		{
			D += FString::Printf(TEXT("[%d %s %s L%d lat%d deep%d"), Dist.Id, *Dist.Kind.ToString(), *Dist.Name, Dist.Level, Dist.Lattice.Num(), Dist.Deep.Num());
			for (const FIntVector& P : Dist.Deep) { D += FString::Printf(TEXT(";%d,%d,%d"), P.X, P.Y, P.Z); }
			D += TEXT("]");
		}
		return D;
	};
	// Colonists onto floor -1, by the real fill (beds first on the shallowest floor).
	const auto AddOnFloor1 = [&](int32 N)
	{
		const int32 Housed = Sim->Debug_AddColonists(N);
		int32 OnFloor1 = 0;
		for (const FRHColonist& C : Sim->GetColonists()) { OnFloor1 += (C.HomeHab == 0 && C.HomeLevel == -1) ? 1 : 0; }
		return Housed == N && OnFloor1 == N;
	};

	Expect(bBaseSaved && Sim->IsFloorRated(-1) && Sim->IsFloorRated(-2) && FiltF1 && FiltF2 && Sim->GetFloorCarvedCells(-1) == 12 && Sim->GetFloorCarvedCells(-2) == 8 && Sim->GetPopulation() == 0
		&& Cells(-1)[0] == FIntPoint(1, 0) && Cells(-1)[3] == FIntPoint(-1, 1) && Cells(-1)[11] == FIntPoint(2, 2) && Cells(-2)[7] == FIntPoint(1, -1),
		FString::Printf(TEXT("DD0 the base: shaft 0 to -4 under an off-grid head, floors -1 (12 cells) and -2 (8) rated, nobody here, saved (%s)"), *SaveErr));
	{
		const FVector HeadCm = Sim->GetShaftHeadCm();
		Expect(!FMath::IsNearlyEqual(FMath::Fmod(HeadCm.X, 1000.0), 0.0) && !FMath::IsNearlyEqual(FMath::Fmod(HeadCm.Y, 1000.0), 0.0) && Sim->GetDistricts().Num() == 0 && Sim->GetNextDistrictId() == 1,
			FString::Printf(TEXT("DD0 the head stands at (%.0f, %.0f) cm, off the 10 m grid, and a fresh colony has no district"), HeadCm.X, HeadCm.Y));
		uint32 MC = 7, MB = 7, CC = 7;
		Sim->Debug_DeepKeys(MC, MB, CC);
		Expect(MC == 0 && MB == 0 && CC == 0 && Sim->Debug_DistrictCachesFresh(), TEXT("DD0 with no shape the membership and commute keys' floor-shape fields are 0 (RC 6)"));
	}

	// --- DD1. Paint, join, a multi-floor shape, every refusal, the cap. ---
	{
		Reset();
		FRHDistrictPaintPreview Pre;
		int32 CarvedInRect = 0;
		for (const FIntPoint& P : Cells(-1)) { CarvedInRect += (P.X >= 1 && P.X <= 3 && P.Y >= 0 && P.Y <= 2) ? 1 : 0; }
		Expect(CanShape(NHomes, -1, 1, 1, 0, 3, 2, Why, &Pre) && Pre.bDeep && !Pre.bFloor && Pre.Floors == 1 && Pre.Min == FIntPoint(1, 0) && Pre.Max == FIntPoint(3, 2) && Pre.CellsInRect == 9 && Pre.CellsNew == 9
			&& Pre.CellsCarved == CarvedInRect && CarvedInRect == 5 && Pre.JoinId == 0 && Sim->GetDistricts().Num() == 0,
			FString::Printf(TEXT("DD1 the preflight of a 3 x 3 shape on -1: nine points, all new, %d already dug, no join; it changes nothing (%s)"), Pre.CellsCarved, *Why));
		Expect(Shape(NHomes, -1, 1, 1, 0, 3, 2) && Sim->GetDistricts().Num() == 1 && Find(1) && Find(1)->IsDeep() && !Find(1)->IsFloor() && Find(1)->Lattice.IsEmpty() && Find(1)->Deep.Num() == 9 && Sorted(Find(1)->Deep)
			&& Find(1)->Level == -1 && Find(1)->Kind == NHomes && Find(1)->Name == TEXT("Gale") && Sim->GetNextDistrictId() == 2 && Find(1)->RegionId == 0,
			FString::Printf(TEXT("DD1 the order lands: district #1 'Gale', Homes, a shape of nine sorted points on -1, no surface cell, region 0 (%s)"), *Note));
		Expect(Sim->GetDistrictOfFloor(-1) == 0 && !Sim->IsFloorMapped(-2) && Sim->IsFloorMapped(-1) && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(1, 0)) == 1 && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(3, 2)) == 1
			&& Sim->GetDistrictOfFloorPoint(-1, FIntPoint(4, 2)) == 0 && Sim->GetDistrictOfFloorPoint(-2, FIntPoint(1, 0)) == 0 && Sim->GetDistrictAtCell(FIntPoint(1, 0)) == 0,
			TEXT("DD1 the floor is mapped, not whole (GetDistrictOfFloor is 0); the shape holds (1,0) and (3,2) on -1 only, and the surface cell (1,0) is no part of it"));
		// Rock may be painted: a shape far from any dig.
		Expect(CanShape(NIndustry, -1, 1, 6, 6, 7, 7, Why, &Pre) && Pre.CellsCarved == 0 && Pre.CellsNew == 4 && Shape(NIndustry, -1, 1, 6, 6, 7, 7) && Find(2) && Find(2)->Deep.Num() == 4 && Find(2)->Kind == NIndustry && Find(2)->Name == TEXT("Jezero"),
			FString::Printf(TEXT("DD1 rock may be painted: four points of Industry far from any dig found #2 (%s)"), *Note));
		// A multi-floor shape: Value 3 from -2 is -2, -3 and -4.
		Expect(CanShape(NResearch, -2, 3, -3, 2, -2, 3, Why, &Pre) && Pre.Floors == 3 && Pre.CellsInRect == 12 && Shape(NResearch, -2, 3, -3, 2, -2, 3) && Find(3) && Find(3)->Deep.Num() == 12 && Sorted(Find(3)->Deep)
			&& Find(3)->Level == -2 && Find(3)->Deep[0].Z == -2 && Find(3)->Deep.Last().Z == -4 && Sim->IsFloorMapped(-3) && Sim->IsFloorMapped(-4) && Sim->GetDistrictOfFloorPoint(-3, FIntPoint(-3, 2)) == 3 && Sim->GetDistrictOfFloorPoint(-5, FIntPoint(-3, 2)) == 0,
			FString::Printf(TEXT("DD1 a shape three floors tall: 2 x 2 on -2, -3 and -4 is twelve points, its Level the shallowest floor (-2), its last point on -4 (%s)"), *Note));
		// The same Kind joins the district it overlaps, and keeps its Id; the shape grows.
		Expect(CanShape(NHomes, -1, 1, 3, 2, 5, 3, Why, &Pre) && Pre.JoinId == 1 && Pre.CellsInRect == 6 && Pre.CellsNew == 5 && Shape(NHomes, -1, 1, 3, 2, 5, 3) && Sim->GetDistricts().Num() == 3 && Find(1)->Deep.Num() == 14 && Sorted(Find(1)->Deep) && Sim->GetNextDistrictId() == 4,
			FString::Printf(TEXT("DD1 same Kind joins: Homes over (3,2) to (5,3) keeps #1 and adds the five new points (14 in all, next Id still 4) (%s)"), *Note));
		Expect(Shape(NHomes, -1, 2, 1, 0, 1, 0) && Find(1)->Deep.Num() == 15 && Find(1)->Level == -1 && Find(1)->Deep.Last().Z == -2 && Sorted(Find(1)->Deep) && Sim->GetDistrictOfFloorPoint(-2, FIntPoint(1, 0)) == 1 && Sim->GetDistrictOfFloorPoint(-2, FIntPoint(-3, 2)) == 3,
			FString::Printf(TEXT("DD1 a multi-floor join: Value 2 over (1,0) adds only the point on -2 to #1, which now spans -1 and -2 (%s)"), *Note));
		const FString Before = DistrictDigest();
		// Every refusal, each by its reason; nothing consumed.
		Expect(!CanShape(NIndustry, -1, 1, 1, 0, 2, 1, Why) && Why.Contains(TEXT("Homes")) && Why.Contains(TEXT("Gale")) && Why.Contains(TEXT("erase it first")) && !Shape(NIndustry, -1, 1, 1, 0, 2, 1) && Note.Contains(TEXT("erase it first")),
			FString::Printf(TEXT("DD1 another Kind over a shape is refused, naming it and the way out (%s)"), *Why));
		Expect(!CanShape(NHomes, -1, 1, 5, 3, 6, 6, Why) && Why.Contains(TEXT("two districts")) && Why.Contains(TEXT("Gale")) && Why.Contains(TEXT("Jezero")) && !Shape(NHomes, -1, 1, 5, 3, 6, 6) && Note.Contains(TEXT("two districts")),
			FString::Printf(TEXT("DD1 a rectangle over two districts is refused, even of its own Kind (%s)"), *Why));
		Expect(!CanShape(NHomes, -1, 1, 1, 0, 2, 1, Why, &Pre) && Why.Contains(TEXT("already part of Gale")) && Pre.CellsNew == 0 && Pre.JoinId == 1 && !Shape(NHomes, -1, 1, 1, 0, 2, 1),
			FString::Printf(TEXT("DD1 an order that adds nothing is refused (%s)"), *Why));
		Expect(!CanShape(NHomes, -5, 1, 20, 20, 21, 21, Why) && Why.Contains(TEXT("not reached")) && Why.Contains(TEXT("-5")) && !CanShape(NHomes, -4, 2, 20, 20, 21, 21, Why) && Why.Contains(TEXT("Floor -5 not reached")),
			FString::Printf(TEXT("DD1 every floor of the range must be reached: -5 is not, and a shape from -4 down two floors needs it (%s)"), *Why));
		Expect(!CanShape(NHomes, -1, 9, 20, 20, 21, 21, Why) && Why.Contains(TEXT("1 to 8 floors")) && Why.Contains(TEXT("span 9")) && !Shape(NHomes, -1, 9, 20, 20, 21, 21) && Note.Contains(TEXT("1 to 8 floors")),
			FString::Printf(TEXT("DD1 more than RH::CavernMaxFloorsCap (8) floors is refused (%s)"), *Why));
		Expect(!CanShape(NHomes, -20, 3, 20, 20, 21, 21, Why) && Why.Contains(TEXT("No such floor")) && Why.Contains(TEXT("-22")), FString::Printf(TEXT("DD1 a shape below -MaxDepth is refused (%s)"), *Why));
		Expect(!CanShape(NHomes, -1, 1, 20, 20, 52, 20, Why) && Why.Contains(TEXT("at a time")) && Why.Contains(TEXT("33 x 1")) && !CanShape(NHomes, -1, 1, 20, 20, 20, 52, Why) && Why.Contains(TEXT("1 x 33")),
			FString::Printf(TEXT("DD1 a side of 33 cells is over the 32 cap (%s)"), *Why));
		Expect(CanShape(NHomes, -1, 1, 20, 20, 51, 51, Why, &Pre) && Pre.CellsInRect == 1024, FString::Printf(TEXT("DD1 32 x 32 is within the cap (1,024 points) (%s)"), *Why));
		Expect(!CanShape(NHomes, -1, 1, 0, 0, 0, 0, Why) && Why.Contains(TEXT("shaft columns")), FString::Printf(TEXT("DD1 a rectangle of nothing but shaft columns is refused (%s)"), *Why));
		Expect(CanShape(NFarming, -3, 1, -1, -1, 1, 1, Why, &Pre) && Pre.CellsInRect == 8 && Pre.CellsNew == 8, TEXT("DD1 a rectangle over shaft 0's column leaves the column out: eight points, not nine"));
		Expect(!Sim->CanPaintDistrict(FName("Mining"), -1, PtCm(20, 20), PtCm(21, 21), Why, nullptr, 1) && Why.Contains(TEXT("not a district kind")), TEXT("DD1 a sixth kind is refused for a shape too"));
		Expect(!Send(FloorCmd(NHomes, -1)) && Note.Contains(TEXT("already holds a shape")) && Note.Contains(TEXT("Gale")) && !Send(FloorCmd(NHomes, -3)) && Note.Contains(TEXT("already holds a shape")) && Note.Contains(TEXT("Research")),
			FString::Printf(TEXT("DD1 a whole-floor order on a floor that holds a shape is refused, naming it (%s)"), *Note));
		Expect(DistrictDigest() == Before && Sim->GetNextDistrictId() == 4, TEXT("DD1 no refusal changed a district or consumed an Id"));
		// A floor is either whole or shaped, the other way round.
		Expect(Send(EraseCmd(1)) && Send(EraseCmd(2)) && Send(EraseCmd(3)) && Sim->GetDistricts().Num() == 0 && !Sim->IsFloorMapped(-1) && !Sim->IsFloorMapped(-4), TEXT("DD1 the three shapes are erased: no floor is mapped any more"));
		Expect(Send(FloorCmd(NHomes, -2)) && Find(4) && Find(4)->IsFloor() && !Find(4)->IsDeep() && Find(4)->Level == -2 && Find(4)->Deep.IsEmpty() && Sim->GetDistrictOfFloor(-2) == 4 && !Sim->IsFloorMapped(-2),
			FString::Printf(TEXT("DD1 Value 0 is today's whole-floor order: floor -2 is Homes #4, IsFloor, not mapped (%s)"), *Note));
		Expect(!CanShape(NHomes, -2, 1, 1, 0, 1, 0, Why) && Why.Contains(TEXT("whole Homes district")) && Why.Contains(TEXT("erase it first")) && !CanShape(NIndustry, -1, 2, 1, 0, 1, 0, Why) && Why.Contains(TEXT("Floor -2")) && Why.Contains(TEXT("whole")),
			FString::Printf(TEXT("DD1 a shape on a floor held whole is refused, and so is a two-floor shape whose second floor is (%s)"), *Why));
		Expect(CanShape(NIndustry, -1, 1, 1, 0, 1, 0, Why) && Shape(NIndustry, -1, 1, 1, 0, 1, 0) && Find(5) && Find(5)->IsDeep() && Find(4)->IsFloor() && Sim->GetDistrictOfFloor(-2) == 4 && Sim->IsFloorMapped(-1) && Sim->GetDistrictOfFloorPoint(-2, FIntPoint(2, 2)) == 4 && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(1, 0)) == 5,
			FString::Printf(TEXT("DD1 a whole floor and a shape live side by side on different floors (%s)"), *Note));
		Expect(Send(EraseCmd(4)) && Shape(NIndustry, -2, 1, 1, 1, 1, 1) && Find(6) && Find(6)->Level == -2 && Sim->IsFloorMapped(-2), TEXT("DD1 erasing the whole-floor district frees the floor for a shape"));
		// Value is a double: NaN, below 1 and negative are the whole-floor order; a surface order ignores it.
		Reset();
		{
			FRHCommand Nan = FloorCmd(NHomes, -1);
			Nan.Value = std::numeric_limits<double>::quiet_NaN();
			FRHCommand Neg = FloorCmd(NIndustry, -2);
			Neg.Value = -3.0;
			FRHCommand Half = FloorCmd(NResearch, -3);
			Half.Value = 0.5;
			Expect(Send(Nan) && Find(1) && Find(1)->IsFloor() && Send(Neg) && Find(2) && Find(2)->IsFloor() && Send(Half) && Find(3) && Find(3)->IsFloor() && Sim->GetDistricts().Num() == 3,
				TEXT("DD1 a Value of NaN, -3 or 0.5 is the whole-floor order, line for line (a deep paint needs Value >= 1)"));
			FRHCommand Surf;
			Surf.Verb = FName("PaintDistrict"); Surf.Target = NCivic; Surf.Level = 0; Surf.Value = 3.0;
			Surf.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(-1, -1)); Surf.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(1, 1));
			Expect(Send(Surf) && Find(4) && !Find(4)->IsDeep() && !Find(4)->IsFloor() && Find(4)->Level == 0 && Find(4)->Lattice.Num() == 9 && Find(4)->Deep.IsEmpty(), FString::Printf(TEXT("DD1 a surface order ignores Value: nine surface cells (%s)"), *Note));
		}
		// The cap: eight 32 x 32 x 8 shapes are 65,536 points, the most the sim keeps; one point more is refused.
		Reset();
		Sim->ExtendShaft(8, Head);
		int32 Painted = 0;
		for (int32 K = 0; K < 8; ++K) { Painted += Shape(NHomes, -1, 8, 40 * (K + 1), 40, 40 * (K + 1) + 31, 71) ? 1 : 0; }
		int32 Points = 0;
		for (const FRHDistrict& D : Sim->GetDistricts()) { Points += D.Deep.Num(); }
		Expect(Painted == 8 && Points == RH::MaxDeepPoints && Sim->GetDistricts().Num() == 8 && !CanShape(NHomes, -1, 1, 20, 20, 20, 20, Why) && Why.Contains(TEXT("most the sim keeps")) && Why.Contains(TEXT("65536")),
			FString::Printf(TEXT("DD1 the cap: eight shapes of 8,192 points are %d, the ninth point is refused (%s)"), Points, *Why));
	}

	// --- DD1 (step-2 fix 5): a deep paint corner past the lattice bound is refused before any loop; the int32 edge is refused at once. ---
	{
		Reset();
		FString Wr;
		const bool bRange = !CanShape(NHomes, -1, 1, 1048577, 0, 1048577, 0, Wr) && Wr.Contains(TEXT("out of range"));
		Expect(bRange && Sim->GetDistricts().Num() == 0, FString::Printf(TEXT("DD1 (5) a deep paint corner at x 1,048,577 is out of range, nothing is painted (%s)"), *Wr));
		Expect(!CanShape(NHomes, -1, 1, 0, -1048577, 1, -1048577, Wr) && Wr.Contains(TEXT("out of range")) && !CanShape(NHomes, -1, 1, -1048577, 0, -1048577, 0, Wr) && Wr.Contains(TEXT("out of range")),
			TEXT("DD1 (5) and on the other axis and the other sign"));
		Expect(!CanShape(NHomes, -1, 1, 1048576, 0, 1048576, 0, Wr) || !Wr.Contains(TEXT("out of range")), TEXT("DD1 (5) the bound itself, 2^20 = 1,048,576, is not out of range"));
		if (bRange)
		{
			// Only when the bound held: a rectangle from 2,147,483,640 to 2,147,483,647 is 8 wide, and its loops would never end.
			Expect(!CanShape(NHomes, -1, 1, 2147483640, 0, 2147483647, 0, Wr) && Wr.Contains(TEXT("out of range")), FString::Printf(TEXT("DD1 (5) a rectangle at the int32 edge is refused at once (%s)"), *Wr));
			Expect(!Shape(NHomes, -1, 1, 2147483640, 0, 2147483647, 0) && Note.Contains(TEXT("out of range")) && Sim->GetDistricts().Num() == 0, FString::Printf(TEXT("DD1 (5) and the order is rejected at the uplink, nothing painted (%s)"), *Note));
		}
	}

	// --- DD2. The frame: an off-grid head maps a building exactly; the same point through LatticeOf would not. ---
	{
		Reset();
		Expect(Shape(NHomes, -1, 1, 1, 0, 1, 0) && Sim->GetDistricts().Num() == 1 && Find(1)->Deep.Num() == 1 && Find(1)->Deep[0] == FIntVector(1, 0, -1), TEXT("DD2 a one-point shape at head-frame (1, 0) on -1"));
		const int32 B1 = Place(TEXT("AirFilter"), Head.X + 1000.0, Head.Y, -1);
		const int32 B2 = Place(TEXT("AirFilter"), Head.X + 2000.0, Head.Y, -1);
		const FRHBuildingInstance* In = nullptr;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == B1) { In = &B; } }
		const FIntPoint Naive = In ? URHSimWorldSubsystem::LatticeOf(In->LocationCm) : FIntPoint(INT32_MIN, 0);
		Expect(B1 && B2 && In && Sim->GetDistrictOfBuilding(B1) == 1 && Sim->GetDistrictOfBuilding(B2) == 0 && Sim->GetDistrictAtPoint(-1, In->LocationCm) == 1,
			TEXT("DD2 a filter at head-frame (1, 0) belongs to the shape; the one at (2, 0) belongs to none"));
		Expect(In && Naive != FIntPoint(1, 0) && Naive == FIntPoint(2, 1) && Sim->GetDistrictOfFloorPoint(-1, Naive) == 0,
			FString::Printf(TEXT("DD2 the same building through LatticeOf is surface cell (%d, %d), not (1, 0): reading it that way would have found no district"), Naive.X, Naive.Y));
		// The corners of an order are snapped by CmToLattice: the world position of the filter paints the same point.
		FRHCommand ByWorld;
		ByWorld.Verb = FName("PaintDistrict"); ByWorld.Target = NIndustry; ByWorld.Level = -1; ByWorld.Value = 1;
		ByWorld.Location = In ? In->LocationCm + FVector(2000.0, 0, 0) : FVector::ZeroVector; ByWorld.LocationB = ByWorld.Location;
		Expect(Send(ByWorld) && Find(2) && Find(2)->Deep.Num() == 1 && Find(2)->Deep[0] == FIntVector(3, 0, -1), TEXT("DD2 an order given at a world position snaps through the head frame, (3, 0): not through the surface grid"));
		const TArray<FRHDistrictCounts> Dummy;
		Expect(CountsOf(1).Buildings == 1 && CountsOf(2).Buildings == 0 && Sim->Debug_DistrictCachesFresh(), TEXT("DD2 the counts agree: the shape holds one building, the other none, and the caches are fresh"));
	}

	// A bunk (two beds) in a cell, through the Furnish order the real game sends; the room is zoned first.
	const auto ZoneLiving = [&](int32 Level, int32 Cell) { return Sim->DesignateRoom(Level, Cell, FName("LivingQuarters"), R); };
	const auto FurnishBunk = [&](int32 Level, int32 Cell)
	{
		const FIntVector Key(Level, Cell, 0);
		FRHCommand C;
		C.Verb = FName("Furnish"); C.Target = FName("BunkBed"); C.Level = Level;
		C.Location = Sim->GetCellCentreCm(Key);
		C.Value = URHSimWorldSubsystem::EncodeFurnish(Cell, 0, 0);
		const bool bOk = Send(C);
		Sim->Debug_RefreshJobs();
		return bOk;
	};
	// The reference rule for a resident's home cell (RC 4), written here and sharing no code with the sim: a resident with a bed piece lives
	// in that piece's cell (pieces go to the lowest Ids first, in (cell, piece Id) order); the rest, in Id order, take the legacy slots of the
	// cells with no bed pieces, in ordinal order, the k-th such cell offering floor((k+1) x CPC) - floor(k x CPC) of them; none left is INDEX_NONE.
	const auto Oracle = [&](int32 Level, double Cpc) -> TMap<int32, int32>
	{
		TMap<int32, int32> Out;
		TArray<TPair<int64, int32>> PieceSlots;
		TSet<int32> BedCells;
		for (const FRHFurnitureInstance& F : Sim->GetFurniture())
		{
			if (F.Key.Z != 0 || F.Key.X != Level) { continue; }
			const FRHFurnitureRow* Row = DefsSub->GetFurniture(F.Row);
			if (!Row || Row->Provides != FName("Bed")) { continue; }
			const FRHRoomRow* Room = DefsSub->GetRoom(Sim->GetRoomAt(Level, F.Key.Y));
			if (!Room || Room->Function != FName("Living")) { continue; }
			BedCells.Add(F.Key.Y);
			for (int32 k = 0; k < Row->Capacity; ++k) { PieceSlots.Emplace((int64)F.Key.Y * 1000000 + F.Id, F.Key.Y); }
		}
		PieceSlots.StableSort([](const TPair<int64, int32>& A, const TPair<int64, int32>& B) { return A.Key < B.Key; });
		TArray<int32> Residents;
		for (const FRHColonist& C : Sim->GetColonists()) { if (C.HomeHab == 0 && C.HomeLevel == Level) { Residents.Add(C.Id); } }
		Residents.Sort();
		int32 N = 0;
		TArray<int32> Rest;
		for (const int32 Id : Residents)
		{
			if (N < PieceSlots.Num()) { Out.Add(Id, PieceSlots[N++].Value); } else { Rest.Add(Id); }
		}
		TArray<int32> Slots;
		int32 K = 0;
		for (int32 Ordinal = 0; Ordinal < Sim->GetFloorCarvedCells(Level); ++Ordinal)
		{
			if (BedCells.Contains(Ordinal)) { continue; }
			const int32 Count = FMath::FloorToInt((double)(K + 1) * Cpc) - FMath::FloorToInt((double)K * Cpc);
			++K;
			for (int32 j = 0; j < Count; ++j) { Slots.Add(Ordinal); }
		}
		int32 M = 0;
		for (const int32 Id : Rest) { Out.Add(Id, M < Slots.Num() ? Slots[M++] : (int32)INDEX_NONE); }
		return Out;
	};
	const auto HomesMatch = [&](int32 Level, double Cpc, FString& OutWhat)
	{
		const TMap<int32, int32> Want = Oracle(Level, Cpc);
		bool bOk = Want.Num() > 0;
		OutWhat.Reset();
		for (const TPair<int32, int32>& P : Want) // an AND over every resident: the visiting order cannot matter
		{
			const int32 Got = Sim->GetColonistHomeCell(P.Key);
			bOk &= Got == P.Value;
			if (Got != P.Value) { OutWhat += FString::Printf(TEXT(" #%d: %d not %d;"), P.Key, Got, P.Value); }
		}
		return bOk;
	};

	// --- DD3 [RC 4]. Home cells: the literal case, CPC 1, 0.5, 0 and 1.5, an overfull floor, a departure. ---
	{
		Reset();
		Expect(ZoneLiving(-1, 3) && FurnishBunk(-1, 3) && Sim->GetFurniture().Num() == 1 && Sim->GetFloorBeds(-1) == 13, FString::Printf(TEXT("DD3 setup: cell 3 on -1 (point (-1,1)) is Living with one bunk (two beds); the floor holds 2 + 11 legacy = %d beds"), Sim->GetFloorBeds(-1)));
		Expect(AddOnFloor1(8), TEXT("DD3 setup: eight colonists, all on floor -1"));
		Sim->Debug_RefreshJobs();
		// Two shapes on the one floor: Homes on cell 3's point, Industry on cell 0's.
		Expect(Shape(NHomes, -1, 1, -1, 1, -1, 1) && Shape(NIndustry, -1, 1, 1, 0, 1, 0) && Find(1) && Find(2), FString::Printf(TEXT("DD3 setup: Homes #1 over (-1,1) (cell 3) and Industry #2 over (1,0) (cell 0), on one floor (%s)"), *Note));
		bool bPieces = true;
		for (int32 Id = 1; Id <= 8; ++Id) { bPieces &= (Sim->GetColonistBedPiece(Id) != 0) == (Id <= 2); }
		Expect(bPieces, TEXT("DD3 BuildBedAssignments gave the two bed pieces' slots to residents 1 and 2, the lowest Ids"));
		// The literal case: an unfurnished cell 0 before a furnished cell 3.
		Expect(Sim->GetColonistHomeCell(1) == 3 && Sim->GetColonistHomeCell(2) == 3 && Sim->GetColonistHomeCell(3) == 0 && Sim->GetColonistHomeCell(4) == 1 && Sim->GetColonistHomeCell(5) == 2 && Sim->GetColonistHomeCell(6) == 4 && Sim->GetColonistHomeCell(8) == 6,
			FString::Printf(TEXT("DD3 the literal case: residents 1 and 2 live in cell 3, resident 3 in cell 0 (not 3 interleaved), 4 in 1, 5 in 2, 6 in 4 (cell 3 is skipped), 8 in 6: %d %d %d %d %d %d %d"),
				Sim->GetColonistHomeCell(1), Sim->GetColonistHomeCell(2), Sim->GetColonistHomeCell(3), Sim->GetColonistHomeCell(4), Sim->GetColonistHomeCell(5), Sim->GetColonistHomeCell(6), Sim->GetColonistHomeCell(8)));
		Expect(Sim->GetDistrictOfColonist(1) == 1 && Sim->GetDistrictOfColonist(2) == 1 && Sim->GetDistrictOfColonist(3) == 2 && Sim->GetDistrictOfColonist(4) == 0 && Sim->GetColonistHomeCell(99) == INDEX_NONE,
			TEXT("DD3 the shapes see it: residents 1 and 2 belong to Homes #1 (cell 3), resident 3 to Industry #2 (cell 0), resident 4 (cell 1) to none"));
		{
			const FRHDistrictCounts C1 = CountsOf(1), C2 = CountsOf(2);
			Expect(C1.Residents == 2 && C1.Beds == 2 && C1.Cells == 1 && C2.Residents == 1 && C2.Beds == 1 && C2.Cells == 1 && Sim->Debug_DistrictCachesFresh(),
				FString::Printf(TEXT("DD3 the counts: #1 holds 2 residents in 2 beds on 1 dug cell; #2 holds 1 resident in 1 legacy bed (%d, %d, %d | %d, %d, %d)"), C1.Residents, C1.Beds, C1.Cells, C2.Residents, C2.Beds, C2.Cells));
		}
		FString What;
		for (const double Cpc : { 1.0, 0.5, 0.0, 1.5, 2.0, 0.25 })
		{
			Sim->Debug_SetColonistsPerCell(Cpc);
			Sim->Debug_RefreshJobs();
			const bool bMatch = HomesMatch(-1, Cpc, What);
			int32 SumBeds = 0;
			for (const FRHDistrictCounts& C : TArray<FRHDistrictCounts>({ CountsOf(1), CountsOf(2) })) { SumBeds += C.Beds; }
			Expect(bMatch, FString::Printf(TEXT("DD3 at %.2f colonists a cell all eight residents match the reference rule (%s)"), Cpc, *What));
			Expect(Sim->GetFloorBeds(-1) == FMath::FloorToInt((double)11 * Cpc) + 2, FString::Printf(TEXT("DD3 at %.2f the floor's beds are (int)(11 x CPC) + 2 = %d, the figure CountFloorBeds gives, which the cells' beds add up to"), Cpc, Sim->GetFloorBeds(-1)));
		}
		Sim->Debug_SetColonistsPerCell(0.5);
		Sim->Debug_RefreshJobs();
		// Overfull: at 0.5 the floor holds 2 + 5 = 7 beds for 8 residents. The eighth has no cell and belongs to no shape.
		Expect(Sim->GetFloorBeds(-1) == 7 && Sim->GetColonistHomeCell(8) == INDEX_NONE && Sim->GetColonistHomeCell(7) != INDEX_NONE && Sim->GetDistrictOfColonist(8) == 0 && Sim->GetColonistHomeCell(3) == 1 && Sim->GetColonistHomeCell(4) == 4,
			FString::Printf(TEXT("DD3 an overfull floor: at 0.5 there are 7 beds for 8 residents; resident 8 has no cell (%d) and no district; residents 3 and 4 take the slots of cells 1 and 4 (%d, %d)"), Sim->GetColonistHomeCell(8), Sim->GetColonistHomeCell(3), Sim->GetColonistHomeCell(4)));
		Sim->Debug_SetColonistsPerCell(1.0);
		Sim->Debug_RefreshJobs();
		// A departure: resident 1 leaves; the pieces go to residents 2 and 3 and everyone after moves up a slot.
		Expect(Sim->Debug_RemoveColonist(1) && Sim->GetPopulation() == 7, TEXT("DD3 resident 1 departs"));
		Expect(Sim->GetColonistHomeCell(2) == 3 && Sim->GetColonistHomeCell(3) == 3 && Sim->GetColonistHomeCell(4) == 0 && Sim->GetColonistHomeCell(5) == 1 && Sim->GetColonistHomeCell(8) == 5 && HomesMatch(-1, 1.0, What),
			FString::Printf(TEXT("DD3 after the departure residents 2 and 3 hold the bunk (cell 3), 4 moves to cell 0, 5 to 1, 8 to 5 - as seats move; the reference agrees (%s)"), *What));
		Expect(Sim->GetDistrictOfColonist(3) == 1 && Sim->GetDistrictOfColonist(4) == 2 && CountsOf(1).Residents == 2 && CountsOf(2).Residents == 1 && Sim->Debug_DistrictCachesFresh(), TEXT("DD3 and the shapes move with them: resident 3 is Homes #1's now, resident 4 Industry #2's; the caches are fresh"));
		// A resident of a floor with no shape has no home cell, and one of a whole-floor district neither.
		Expect(Sim->GetColonistHomeCell(2) != INDEX_NONE && !Sim->IsFloorMapped(-2), TEXT("DD3 home cells exist only on mapped floors"));
	}

	// --- DD4. Counts, beds and seats per shape. ---
	{
		Reset();
		Expect(ZoneLiving(-1, 0) && FurnishBunk(-1, 0) && ZoneLiving(-1, 3) && ZoneLiving(-1, 4) && Sim->DesignateRoom(-1, 1, FName("Workstation"), R) && Sim->DesignateRoom(-1, 6, FName("Workstation"), R) && Sim->DesignateRoom(-1, 8, FName("Lab"), R),
			TEXT("DD4 setup: cell 0 Living with a bunk, cells 3 and 4 Living, Workstations in cells 1 and 6, a Lab in cell 8"));
		Expect(AddOnFloor1(8), TEXT("DD4 setup: eight colonists on floor -1"));
		Sim->Debug_RefreshJobs();
		Expect(Shape(NHomes, -1, 1, -1, 0, 1, 1) && Shape(NIndustry, -1, 1, -1, -1, 2, -1) && Find(1) && Find(2) && Find(1)->Deep.Num() == 5 && Find(2)->Deep.Num() == 4,
			FString::Printf(TEXT("DD4 setup: Homes #1 over (-1,0) to (1,1) (five points: the column is left out) and Industry #2 over (-1,-1) to (2,-1) (four) (%s)"), *Note));
		const FRHDistrictCounts C1 = CountsOf(1), C2 = CountsOf(2);
		// Independent walks: carved points per shape, beds per cell (a bunk cell its beds, every other cell a legacy bed), seats per station.
		int32 Carved1 = 0, Carved2 = 0, Seats1 = 0, Seats2 = 0, Beds1 = 0, Beds2 = 0, Res1 = 0, Res2 = 0, Blds1 = 0, Blds2 = 0;
		const TMap<int32, int32> Homes = Oracle(-1, 1.0);
		for (int32 Ordinal = 0; Ordinal < Sim->GetFloorCarvedCells(-1); ++Ordinal)
		{
			const FIntPoint P = Sim->GetFloorCell(-1, Ordinal);
			const int32 Owner = Sim->GetDistrictOfFloorPoint(-1, P);
			int32 Filled = 0, Cap = 0;
			Sim->GetStationSeats(FIntVector(-1, Ordinal, 0), Filled, Cap);
			const int32 Beds = Ordinal == 0 ? 2 : 1;
			int32 Residents = 0;
			for (const TPair<int32, int32>& H : Homes) { Residents += H.Value == Ordinal ? 1 : 0; }
			if (Owner == 1) { ++Carved1; Seats1 += Cap; Beds1 += Beds; Res1 += Residents; }
			if (Owner == 2) { ++Carved2; Seats2 += Cap; Beds2 += Beds; Res2 += Residents; }
		}
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Level != -1) { continue; }
			const FIntPoint P = URHSimWorldSubsystem::CmToLattice(Sim->GetShaftHeadCm(), B.LocationCm);
			const int32 Owner = Sim->GetDistrictOfFloorPoint(-1, P);
			Blds1 += Owner == 1 ? 1 : 0; Blds2 += Owner == 2 ? 1 : 0;
		}
		Expect(Carved1 == 5 && Carved2 == 4 && C1.Cells == Carved1 && C2.Cells == Carved2, FString::Printf(TEXT("DD4 Cells are the carved points: #1 %d of its 5, #2 %d of its 4"), C1.Cells, C2.Cells));
		Expect(C1.Beds == Beds1 && Beds1 == 6 && C2.Beds == Beds2 && Beds2 == 4 && C1.Beds + C2.Beds + 3 == Sim->GetFloorBeds(-1),
			FString::Printf(TEXT("DD4 Beds are the beds of the carved cells on a rated floor: #1 %d (a bunk's two + four legacy), #2 %d; with the three cells outside both they add up to the floor's %d"), C1.Beds, C2.Beds, Sim->GetFloorBeds(-1)));
		Expect(C1.Seats == Seats1 && C2.Seats == Seats2 && Seats1 >= 1 && Seats2 >= 2, FString::Printf(TEXT("DD4 Seats come through the cell: #1 %d (the Workstation in cell 1), #2 %d (the Workstation in 6 and the Lab in 8)"), C1.Seats, C2.Seats));
		Expect(C1.Residents == Res1 && C2.Residents == Res2 && Res1 + Res2 > 0 && C1.Buildings == Blds1 && C2.Buildings == Blds2 && Blds1 == 1,
			FString::Printf(TEXT("DD4 Residents follow their home cells (%d, %d) and Buildings their lattice points (%d: the filter in cell 2; %d)"), C1.Residents, C2.Residents, C1.Buildings, C2.Buildings));
		// A shape on an unrated floor: dug cells count, beds do not.
		Sim->ExcavateFloor(-3, 3, R);
		Expect(!Sim->IsFloorRated(-3) && Shape(NResearch, -3, 1, -1, -1, 2, 2) && Find(3) && CountsOf(3).Cells == 3 && CountsOf(3).Beds == 0 && CountsOf(3).Residents == 0,
			FString::Printf(TEXT("DD4 a shape on an unrated floor: three dug cells, no beds, nobody (%s)"), *Note));
		Expect(Sim->Debug_DistrictCachesFresh(), TEXT("DD4 the caches are fresh"));
	}

	// Growth helpers: the open project of a district, the run to its end with no new start behind it, and a digest of what a floor looks like.
	const auto Open = [&](int32 Id) { return Sim->FindGrowthProject(Id); };
	const auto NoteOf = [&](int32 Id) { return Sim->GetGrowthNote(Id); };
	const auto Held = [&](FName Res) { return Sim->GetGrowthAvailability(Res).Held; };
	const auto RunToEnd = [&](int32 District, int32 MaxSteps)
	{
		Sim->Debug_SetGrowthEnabled(false);
		int32 Took = -1;
		for (int32 K = 1; K <= MaxSteps && Took < 0; ++K) { Land(); if (!Open(District)) { Took = K; } }
		Sim->Debug_SetGrowthEnabled(true);
		return Took;
	};
	const auto StartGrowth = [&](double Hope)
	{
		PinHope = Hope;
		Sim->Debug_SetHopeSmoothed(Hope);
		Sim->Debug_SetGrowthEnabled(true);
		return Sim->Debug_RunGrowthCheck();
	};
	const auto PiecesIn = [&](int32 Level, int32 Cell) { return Sim->GetCellFurniture(FIntVector(Level, Cell, 0)); };
	const auto FloorDigest = [&](int32 Level) -> FString
	{
		FString D = FString::Printf(TEXT("F%d"), Level);
		for (int32 i = 0; i < Sim->GetFloorCarvedCells(Level); ++i)
		{
			const FIntPoint P = Sim->GetFloorCell(Level, i);
			D += FString::Printf(TEXT(";%d,%d:%s:%d"), P.X, P.Y, *Sim->GetRoomAt(Level, i).ToString(), PiecesIn(Level, i));
		}
		for (const FIntPoint& P : PlanOf(Level)) { D += FString::Printf(TEXT("|%d,%d"), P.X, P.Y); }
		return D;
	};
	const auto ProjectText = [&](const FRHGrowthProject* P) -> FString
	{
		if (!P) { return TEXT("none"); }
		FString T = FString::Printf(TEXT("d%d k%d L%d n%d f%d i%d c%d room%s tot%.6f rem%.6f pts"), P->DistrictId, (int32)P->Kind, P->Level, P->Cells, P->FirstCell, P->Index, P->bCarve ? 1 : 0, *P->Room.ToString(), P->TotalSols, P->RemainingSols);
		for (const FIntPoint& Pt : P->Points) { T += FString::Printf(TEXT(";%d,%d"), Pt.X, Pt.Y); }
		TArray<FName> Keys;
		P->Bill.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const FName& K : Keys) { T += FString::Printf(TEXT(" %s=%.3f"), *K.ToString(), P->Bill.FindChecked(K)); }
		return T;
	};

	// --- DD5. A whole-floor district is byte-equal with and without a floor shape elsewhere: counts, membership, growth. ---
	{
		const auto WholeFloorRun = [&](bool bWithShape) -> FString
		{
			Reset();
			FString D;
			Send(FloorCmd(NHomes, -1));
			if (bWithShape) { Shape(NIndustry, -3, 1, 1, 1, 3, 3); Shape(NResearch, -2, 1, 6, 6, 7, 7); }
			else { Steps(2); } // the two shape orders each take one era step: the twin without them lets the same time pass
			Sim->Debug_AddColonists(20);
			Sim->Debug_RefreshJobs();
			const FRHDistrictCounts C = CountsOf(1);
			D += FString::Printf(TEXT("counts %d %d %d %d %d %d|"), C.Cells, C.Buildings, C.Bays, C.Residents, C.Beds, C.Seats);
			for (const FRHColonist& Col : Sim->GetColonists()) { D += FString::Printf(TEXT("c%d:%d,"), Col.Id, Sim->GetDistrictOfColonist(Col.Id)); }
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Level < 0) { D += FString::Printf(TEXT("b%d:%d,"), B.Id, Sim->GetDistrictOfBuilding(B.Id)); } }
			const int32 Started = StartGrowth(80.0);
			D += FString::Printf(TEXT("|start %d %s|"), Started, *ProjectText(Open(1)));
			const int32 Took = RunToEnd(1, 60);
			D += FString::Printf(TEXT("took %d|%s|%s|struct %.3f ox %.3f|"), Took, *FloorDigest(-1), *FloorDigest(-2), Held(NStruct), Sim->GetStock(NOx));
			const FRHDistrictCounts C2 = CountsOf(1);
			D += FString::Printf(TEXT("counts %d %d %d %d %d %d"), C2.Cells, C2.Buildings, C2.Bays, C2.Residents, C2.Beds, C2.Seats);
			return D;
		};
		const FString Plain = WholeFloorRun(false);
		const FString Twin = WholeFloorRun(true);
		Expect(Plain == Twin && Plain.Contains(TEXT("start 1 d1 k1 L-1 n4")), FString::Printf(TEXT("DD5 a whole-floor Homes district #1 gives the very same counts, membership, FloorCells project (4 cells, %s) and finished floor with two shapes elsewhere on the colony as without (%d characters each)"), Plain.Contains(TEXT("start 1 d1 k1 L-1 n4")) ? TEXT("kind 1") : TEXT("?"), Plain.Len()));
		if (Plain != Twin)
		{
			int32 At = 0;
			while (At < Plain.Len() && At < Twin.Len() && Plain[At] == Twin[At]) { ++At; }
			UE_LOG(LogRedHopeSim, Error, TEXT("DEEPDISTRICTS FAIL: DD5 first difference at %d: plain '%s' twin '%s'"), At, *Plain.Mid(FMath::Max(0, At - 60), 160), *Twin.Mid(FMath::Max(0, At - 60), 160));
		}
	}

	// --- DD6. DeepCells: free cells zoned first, then eligible rock carved; the ghost equals the carve; no carved point left in Plan [RC 3]. ---
	{
		Reset();
		Expect(Sim->Debug_AddColonists(20) == 20, TEXT("DD6 setup: twenty colonists fill the 20 beds of floors -1 and -2: the city is short of homes"));
		FString PW;
		Expect(Sim->Debug_PaintCarve(-2, FIntPoint(2, -1), PW) && PlanOf(-2).Num() == 1 && PlanOf(-2)[0] == FIntPoint(2, -1), FString::Printf(TEXT("DD6 setup: (2,-1) on -2 is a planned cell (%s)"), *PW));
		Expect(Shape(NHomes, -2, 1, 1, -1, 3, 1) && Find(1) && Find(1)->Deep.Num() == 9 && CountsOf(1).Cells == 3, FString::Printf(TEXT("DD6 setup: Homes #1 over (1,-1) to (3,1) on -2: nine points, three of them dug (cells 7, 0 and 1) (%s)"), *Note));
		const int32 Struct0 = FMath::RoundToInt((float)Held(NStruct)), Cells0 = Sim->GetFloorCarvedCells(-2);
		const double Ox0 = Held(NOx);
		Expect(StartGrowth(80.0) == 1 && Open(1), FString::Printf(TEXT("DD6 the project starts (%s)"), *NoteOf(1)));
		const FRHGrowthProject* P = Open(1);
		const TArray<FIntPoint> Ghost = P ? P->Points : TArray<FIntPoint>();
		Expect(P && P->Kind == ERHGrowthKind::DeepCells && P->Level == -2 && P->Cells == 4 && P->FirstCell == 3 && P->Points.Num() == 4 && P->Points[0] == FIntPoint(1, 0) && P->Points[1] == FIntPoint(1, 1) && P->Points[2] == FIntPoint(1, -1) && P->Points[3] == FIntPoint(2, -1)
			&& !P->bCarve && P->Index == INDEX_NONE,
			FString::Printf(TEXT("DD6 DeepCells on -2: the shape's three free cells by ordinal ((1,0), (1,1), (1,-1)), then the lowest eligible rock by (X, Y), (2,-1), beside a dig: %s"), *ProjectText(P)));
		Expect(P && FMath::IsNearlyEqual(P->Bill.FindRef(NStruct), 640.0, 1.0e-9) && FMath::IsNearlyEqual(P->Bill.FindRef(NOx), 100.0, 1.0e-9) && P->Bill.Num() == 2 && FMath::IsNearlyEqual(P->TotalSols, 1.0, 1.0e-9) && Sim->GetGrowthProjectBeds(*P) == 16 && Sim->GetGrowthCursor() == 1,
			TEXT("DD6 the bill: Struct 640 (four cells x two bunks x 80) and the oxygen of the one cell to carve, 100 kg; a sol's work (a dig on the floor's own permille: 1000 at -2); 16 beds"));
		Expect(FMath::IsNearlyEqual(Held(NStruct), Struct0 - 640.0, 1.0e-6) && FMath::IsNearlyEqual(Held(NOx), Ox0 - 100.0, 1.0e-6) && Sim->GetFloorCarvedCells(-2) == Cells0, TEXT("DD6 the whole bill left the holdings at the start; nothing is dug yet"));
		const int32 Took = RunToEnd(1, 60);
		Expect(Took >= 19 && Took <= 21 && !Open(1), FString::Printf(TEXT("DD6 the project finishes after a sol: %d era steps"), Took));
		// The ghost equals the carve.
		bool bZoned = true, bBunks = true;
		for (const FIntPoint& Pt : Ghost)
		{
			const int32 Ordinal = Sim->FindFloorCell(-2, Pt);
			bZoned &= Ordinal != INDEX_NONE && Sim->GetRoomAt(-2, Ordinal) == FName("LivingQuarters");
			bBunks &= Ordinal != INDEX_NONE && PiecesIn(-2, Ordinal) == 2;
		}
		int32 LivingCells = 0;
		for (int32 i = 0; i < Sim->GetFloorCarvedCells(-2); ++i) { LivingCells += Sim->GetRoomAt(-2, i) == FName("LivingQuarters") ? 1 : 0; }
		Expect(Sim->GetFloorCarvedCells(-2) == Cells0 + 1 && Sim->GetFloorCell(-2, Cells0) == FIntPoint(2, -1) && bZoned && bBunks && LivingCells == 4,
			FString::Printf(TEXT("DD6 the ghost equals the carve: the one rock point (2,-1) became ordinal %d, and exactly the four ghost points are Living cells with two bunks each (%d Living cells on the floor)"), Cells0, LivingCells));
		Expect(PlanOf(-2).IsEmpty() && !Cells(-2).Contains(FIntPoint(-9, -9)), TEXT("DD6 [RC 3] the carved point left Plan: no carved point is in it"));
		bool bPlanClean = true;
		for (const FIntPoint& Pt : PlanOf(-2)) { bPlanClean &= !Cells(-2).Contains(Pt); }
		FString E6;
		Expect(bPlanClean && Sim->SaveColony(TEXT("deepplan"), SaveErr) && Sim->LoadColony(TEXT("deepplan"), E6) && E6.IsEmpty() && Sim->GetFloorCarvedCells(-2) == Cells0 + 1, FString::Printf(TEXT("DD6 [RC 3] and the colony saves and loads (a carved point in the plan is a refused load): %s"), *E6));
		Expect(Sim->IsFloorRated(-2) && Sim->GetFloorBeds(-2) == 21 && CountsOf(1).Cells == 4 && CountsOf(1).Beds == 16 && Sim->Debug_DistrictCachesFresh(), FString::Printf(TEXT("DD6 the floor stays rated and now holds %d beds (four Living cells of four, five legacy); the shape counts 4 dug cells and 16 beds"), Sim->GetFloorBeds(-2)));
		// All rock: each point leans on the one before it.
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(Shape(NHomes, -2, 1, 2, -1, 4, -1) && StartGrowth(80.0) == 1 && Open(1), TEXT("DD6 all rock: Homes over (2,-1) to (4,-1) on -2, a row of three beside the dig; the project starts"));
		P = Open(1);
		Expect(P && P->Cells == 3 && P->FirstCell == 0 && P->Points.Num() == 3 && P->Points[0] == FIntPoint(2, -1) && P->Points[1] == FIntPoint(3, -1) && P->Points[2] == FIntPoint(4, -1) && FMath::IsNearlyEqual(P->Bill.FindRef(NOx), 300.0, 1.0e-9) && FMath::IsNearlyEqual(P->Bill.FindRef(NStruct), 480.0, 1.0e-9),
			FString::Printf(TEXT("DD6 a later pick leans on an earlier one: (2,-1) touches the dig, (3,-1) and (4,-1) touch the pick before; oxygen 300 for three new cells: %s"), *ProjectText(P)));
		RunToEnd(1, 60);
		Expect(Sim->GetFloorCarvedCells(-2) == 11 && Sim->GetFloorCell(-2, 8) == FIntPoint(2, -1) && Sim->GetFloorCell(-2, 9) == FIntPoint(3, -1) && Sim->GetFloorCell(-2, 10) == FIntPoint(4, -1) && Sim->GetRoomAt(-2, 9) == FName("LivingQuarters") && PiecesIn(-2, 10) == 2,
			TEXT("DD6 they are carved in that order (ordinals 8, 9 and 10) through the one carve path, zoned Living and furnished"));
		// The notes: a shape that cannot grow says why.
		Reset();
		Sim->Debug_AddColonists(20);
		Sim->ExcavateFloor(-3, 3, R);
		Expect(Shape(NHomes, -3, 1, -1, -1, 2, 2) && StartGrowth(80.0) == 0 && !Open(1) && NoteOf(1).Contains(TEXT("no floor of the shape is rated")), FString::Printf(TEXT("DD6 a shape on an unrated floor does not grow: '%s'"), *NoteOf(1)));
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(Shape(NHomes, -2, 1, 6, 6, 7, 7) && StartGrowth(80.0) == 0 && !Open(1) && NoteOf(1).Contains(TEXT("no free cell and no rock beside a dig")), FString::Printf(TEXT("DD6 a rated shape of rock that touches no dig does not grow: '%s'"), *NoteOf(1)));
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(Shape(NIndustry, -2, 1, 1, -1, 3, 1) && Shape(NHomes, -1, 1, 6, 6, 7, 7) && StartGrowth(80.0) == 0 && !Open(1) && NoteOf(1).IsEmpty(), TEXT("DD6 an Industry shape is the player's: the city builds nothing in it, and says nothing"));
		// A two-floor shape: one floor a project, the shallowest that has something to offer; the next project takes the next floor.
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(Shape(NHomes, -1, 2, 1, 0, 1, 1) && StartGrowth(80.0) == 1 && Open(1), TEXT("DD6 a two-floor shape over (1,0) to (1,1): the project starts"));
		P = Open(1);
		Expect(P && P->Level == -1 && P->Points.Num() == 2 && P->Cells == 2 && P->FirstCell == 2 && P->Points[0] == FIntPoint(1, 0) && P->Points[1] == FIntPoint(1, 1) && FMath::IsNearlyEqual(P->Bill.FindRef(NStruct), 320.0, 1.0e-9) && !P->Bill.Contains(NOx),
			FString::Printf(TEXT("DD6 the project works the shallowest floor with something to offer, -1: two free cells to zone, no cell to carve, so no oxygen: %s"), *ProjectText(P)));
		RunToEnd(1, 60);
		Sim->Debug_AddColonists(8);
		Sim->Debug_RefreshJobs();
		Expect(StartGrowth(80.0) == 1 && Open(1) && Open(1)->Level == -2 && Open(1)->Points.Num() == 2, FString::Printf(TEXT("DD6 the next project of the same shape takes -2 (its cells on -1 are zoned now): %s"), *ProjectText(Open(1))));
		// The district's growth switch and the erase: an open project is cancelled with a full refund.
		const double StructMid = Held(NStruct);
		Send(EraseCmd(1));
		Expect(!Open(1) && Held(NStruct) > StructMid + 300.0, TEXT("DD6 erasing the district cancels its project and returns the whole bill"));
	}

	// --- DD7. Civic stays inside the shape: a free cell of it first; with none free, an eligible shape point is carved - never the floor's spiral cell. ---
	{
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(Shape(NCivic, -1, 1, 1, 0, 2, 1) && Find(1) && Find(1)->Deep.Num() == 4 && CountsOf(1).Cells == 4, FString::Printf(TEXT("DD7 setup: Civic #1 over (1,0) to (2,1) on -1: four points, all dug (%s)"), *Note));
		Expect(StartGrowth(80.0) == 1 && Open(1), FString::Printf(TEXT("DD7 the project starts (%s)"), *NoteOf(1)));
		const FRHGrowthProject* P = Open(1);
		const FName Room = P ? P->Room : FName(NAME_None);
		Expect(P && P->Kind == ERHGrowthKind::CivicRoom && P->Level == -1 && P->Points.Num() == 1 && P->Points[0] == FIntPoint(1, 0) && !P->bCarve && P->Index == INDEX_NONE && !Room.IsNone() && FMath::IsNearlyEqual(P->TotalSols, 0.5, 1.0e-9),
			FString::Printf(TEXT("DD7 the room goes in the shape's lowest free cell, ordinal 0 at (1,0), as a lattice point; no carve; half a sol: %s"), *ProjectText(P)));
		RunToEnd(1, 30);
		int32 Zoned = 0;
		for (int32 i = 0; i < Sim->GetFloorCarvedCells(-1); ++i) { Zoned += Sim->GetRoomAt(-1, i).IsNone() ? 0 : 1; }
		Expect(!Open(1) && Sim->GetRoomAt(-1, 0) == Room && PiecesIn(-1, 0) > 0 && Zoned == 1 && Sim->GetFloorCarvedCells(-1) == 12, FString::Printf(TEXT("DD7 the room %s stands in cell 0 with its furniture; no other cell on the floor was zoned, none carved (%d zoned)"), *Room.ToString(), Zoned));
		// No free cell in the shape: the point carved is the shape's own eligible rock.
		Reset();
		Sim->Debug_AddColonists(20);
		Expect(ZoneLiving(-1, 0) && ZoneLiving(-1, 1) && ZoneLiving(-1, 9) && ZoneLiving(-1, 10) && Shape(NCivic, -1, 1, 1, 0, 3, 1) && Find(1) && Find(1)->Deep.Num() == 6, TEXT("DD7 setup: all four dug cells of the shape (1,0) to (3,1) are zoned Living, leaving no free cell; (3,0) and (3,1) are rock"));
		Expect(StartGrowth(80.0) == 1 && Open(1), FString::Printf(TEXT("DD7 a carving project starts (%s)"), *NoteOf(1)));
		P = Open(1);
		TArray<FIntPoint> Spiral;
		Sim->PeekCarveCells(-1, 1, Spiral);
		Expect(P && P->bCarve && P->Points.Num() == 1 && P->Points[0] == FIntPoint(3, 0) && Spiral.Num() == 1 && Spiral[0] != FIntPoint(3, 0) && FMath::IsNearlyEqual(P->Bill.FindRef(NOx), 100.0, 1.0e-9),
			FString::Printf(TEXT("DD7 it carves the shape's lowest eligible point (3,0), not the floor's next spiral cell (%d,%d); oxygen 100 for the new cell: %s"), Spiral.Num() ? Spiral[0].X : 0, Spiral.Num() ? Spiral[0].Y : 0, *ProjectText(P)));
		const FName Room2 = P ? P->Room : FName(NAME_None);
		RunToEnd(1, 30);
		Expect(!Open(1) && Sim->GetFloorCarvedCells(-1) == 13 && Sim->GetFloorCell(-1, 12) == FIntPoint(3, 0) && Sim->GetRoomAt(-1, 12) == Room2 && Sim->IsFloorRated(-1) && PlanOf(-1).IsEmpty(),
			TEXT("DD7 the cell (3,0) is carved as ordinal 12, zoned with the room, and the floor stays rated"));
		// A Civic shape on an unrated floor says so.
		Reset();
		Sim->Debug_AddColonists(20);
		Sim->ExcavateFloor(-3, 3, R);
		Expect(Shape(NCivic, -3, 1, -1, -1, 2, 2) && StartGrowth(80.0) == 0 && !Open(1) && NoteOf(1).Contains(TEXT("no floor of the shape is rated")), FString::Printf(TEXT("DD7 a Civic shape on an unrated floor builds nothing: '%s'"), *NoteOf(1)));
	}

	// --- DD8. An era project and an agent project finish on the same sol with the same cells. ---
	{
		Reset();
		Sim->Debug_AddColonists(20);
		FString PW;
		Sim->Debug_PaintCarve(-2, FIntPoint(2, -1), PW);
		FString W0, W1;
		const bool bGate0 = Sim->CanEnterEraMode(W0);
		Expect(Shape(NHomes, -2, 1, 1, -1, 3, 1) && StartGrowth(80.0) == 1 && Open(1), TEXT("DD8 setup: the DD6 project is open"));
		const bool bGate1 = Sim->CanEnterEraMode(W1);
		Sim->Debug_SetGrowthEnabled(false);
		Expect(bGate0 == bGate1 && W0 == W1, FString::Printf(TEXT("DD8 CanEnterEraMode answers the same with the open project (%s; reason \"%s\")"), bGate1 ? TEXT("yes") : TEXT("no"), *W1));
		FString PairErr;
		const FString AtSave = ProjectText(Open(1));
		Expect(Sim->SaveColony(TEXT("deeppair"), PairErr), FString::Printf(TEXT("DD8 the colony is saved for both legs (%s)"), *PairErr));
		struct FLeg { double DoneAt = -1.0; int32 DoneSol = -1; FString Floor1, Floor2; bool bOpenAtStart = false; };
		const auto RunLeg = [&](bool bAgent) -> FLeg
		{
			FString Le;
			if (!Sim->LoadColony(TEXT("deeppair"), Le) || !Le.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPDISTRICTS: leg load failed: %s"), *Le); ++Fails; }
			Sim->Debug_SetGrowthEnabled(false);
			PinHope = -1.0;
			FLeg L;
			L.bOpenAtStart = ProjectText(Open(1)) == AtSave;
			const int32 Total = 2 * (int32)(SolLen / (bAgent ? (double)AgentDt : EraDt));
			for (int32 K = 0; K < Total && L.DoneAt < 0.0; ++K)
			{
				if (bAgent)
				{
					Clock->Debug_AdvanceSimSeconds((double)AgentDt);
					Sim->Debug_SetBatteryWh(1.0e9);
					Sim->Debug_StepSim(AgentDt);
				}
				else
				{
					Land();
				}
				if (!Open(1)) { L.DoneAt = Clock->GetSimSecondsTotal(); L.DoneSol = Clock->GetSol(); }
			}
			L.Floor1 = FloorDigest(-1);
			L.Floor2 = FloorDigest(-2);
			return L;
		};
		const FLeg Agent = RunLeg(true);
		const FLeg Era = RunLeg(false);
		Expect(Agent.bOpenAtStart && Era.bOpenAtStart && Agent.DoneAt > 0.0 && Era.DoneAt > 0.0 && FMath::Abs(Agent.DoneAt - Era.DoneAt) <= EraDt + (double)AgentDt && Agent.DoneSol == Era.DoneSol,
			FString::Printf(TEXT("DD8 both bands finish the project on the same sol (%d), within one era step of each other: agent at %.1f s, era at %.1f s"), Era.DoneSol, Agent.DoneAt, Era.DoneAt));
		Expect(Agent.Floor2 == Era.Floor2 && Agent.Floor1 == Era.Floor1 && Agent.Floor2.Contains(TEXT("2,-1:LivingQuarters:2")), TEXT("DD8 and they leave the same cells, rooms and furniture: (2,-1) carved as ordinal 8 and every ghost point a Living cell with two bunks, in both bands"));
	}

	// --- DD9. The save: the layout, two shapes on one floor, every hand-made refusal, a v48 file; a project's points round-trip. ---
	{
		const auto SurfaceCmd = [&]()
		{
			FRHCommand C;
			C.Verb = FName("PaintDistrict"); C.Target = NHomes; C.Level = 0;
			C.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(-1, -1)); C.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(1, 1));
			return C;
		};
		Reset();
		Expect(Shape(NHomes, -1, 1, 1, 0, 2, 1) && Shape(NIndustry, -1, 1, 3, 3, 3, 4) && Shape(NResearch, -2, 2, -2, 2, -2, 2) && Send(FloorCmd(NCivic, -4)) && Send(SurfaceCmd()) && Sim->GetDistricts().Num() == 5,
			FString::Printf(TEXT("DD9 file setup: Homes #1 and Industry #2 on one floor (-1), Research #3 over -2 and -3, a whole-floor Civic #4 on -4, a surface Homes #5 (%s)"), *Note));
		const FString D0 = DistrictDigest();
		Expect(Sim->SaveColony(TEXT("deepfile"), SaveErr), FString::Printf(TEXT("DD9 the colony is saved (%s)"), *SaveErr));
		TArray<uint8> W;
		const bool bRead = FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("deepfile")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		const int32 Pattern[22] = { 5, 1, -1, 2, 2, -1, 2, 3, -2, 2, 4, -4, 1, 5, 0, 0, 3, 1, 4, 1, 0, -1 };
		int32 At = INDEX_NONE;
		for (int32 I = 0; bRead && I + 88 <= W.Num() && At == INDEX_NONE; ++I)
		{
			bool bAll = true;
			for (int32 k = 0; k < 22 && bAll; ++k) { bAll &= Rd(W, I + 4 * k) == Pattern[k]; }
			if (bAll) { At = I; }
		}
		Expect(bRead && Ver == 55 && At != INDEX_NONE && Rd(W, At - 8) == 1 && Rd(W, At - 4) == 0, FString::Printf(TEXT("DD9 the file says version %u; the floor-shape block starts at byte %d, straight after the cavern block (NextCavernId 1, 0 caverns)"), Ver, At));
		if (!bRead || At == INDEX_NONE) { return Finish(); }
		// A, B and C: counts 4, 2 and 2 points; the 5 skeleton records come first (12 bytes each), then the shape count.
		Expect(Rd(W, At + 64) == 3 && Rd(W, At + 68) == 1 && Rd(W, At + 72) == 4 && Rd(W, At + 76) == 1 && Rd(W, At + 80) == 0 && Rd(W, At + 84) == -1 && Rd(W, At + 124) == 2 && Rd(W, At + 128) == 2 && Rd(W, At + 156) == 3 && Rd(W, At + 160) == 2
			&& Rd(W, At + 164) == -2 && Rd(W, At + 168) == 2 && Rd(W, At + 172) == -2 && Rd(W, At + 188) == 0,
			TEXT("DD9 the layout reads back as written: five skeleton records (Id, Level, class), the shape count 3, then per shape its Id, its count and its (X, Y, Level) points in order, then 0 growth projects"));
		// Round trip: two shapes on one floor, a multi-floor shape, a whole floor and a surface district, no Error line (the suite's own log is checked by the battery).
		FString E;
		Expect(LoadCounted(TEXT("deepfile"), E) && LoadErrorLines == 0 && E.IsEmpty() && DistrictDigest() == D0 && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(1, 0)) == 1 && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(3, 4)) == 2 && Sim->GetDistrictOfFloorPoint(-3, FIntPoint(-2, 2)) == 3
			&& Sim->GetDistrictOfFloor(-4) == 4 && Sim->IsFloorMapped(-1) && Sim->IsFloorMapped(-3) && !Sim->IsFloorMapped(-4) && Find(5) && Find(5)->Lattice.Num() == 9 && Find(5)->Deep.IsEmpty(),
			FString::Printf(TEXT("DD9 [RC 5] it loads back exactly, with no Error line printed (counted): a Homes shape and an Industry shape on one floor (they never meet the whole-floor test), the multi-floor shape, the whole floor and the surface district (%s)"), *E));
		FString Save2Err;
		TArray<uint8> W2;
		Expect(Sim->SaveColony(TEXT("deepfile2"), Save2Err) && FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("deepfile2"))) && W2.Num() == W.Num() && FMemory::Memcmp(W2.GetData() + At, W.GetData() + At, 192) == 0,
			TEXT("DD9 the block round-trips byte-equal (192 bytes: 5 skeleton records, three shapes, no project)"));
		// Hand-made files: every structural refusal, the colony untouched.
		const auto TryFile = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			const FString Digest0 = DistrictDigest();
			const int32 Cells1 = Sim->GetFloorCarvedCells(-1), Builds = Sim->GetBuildings().Num();
			const bool bOk = LoadCounted(Slot, OutErr);
			const bool bSame = DistrictDigest() == Digest0 && Sim->GetFloorCarvedCells(-1) == Cells1 && Sim->GetBuildings().Num() == Builds;
			return !bOk && bSame && LoadErrorLines == 0 && OutErr.Contains(TEXT("Save floor-shape block invalid"));
		};
		FString Err;
		{ TArray<uint8> T = W; PutI(T, At, -1); Expect(TryFile(TEXT("deepr1"), T, Err) && Err.Contains(TEXT("district count -1")), FString::Printf(TEXT("DD9 a district count below 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At, 100000000); Expect(TryFile(TEXT("deepr2"), T, Err) && Err.Contains(TEXT("district count")), FString::Printf(TEXT("DD9 a district count larger than the bytes left is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4 + 8, 3); Expect(TryFile(TEXT("deepr3"), T, Err) && Err.Contains(TEXT("out of range")), FString::Printf(TEXT("DD9 a district class of 3 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4, 9); Expect(TryFile(TEXT("deepr4"), T, Err) && Err.Contains(TEXT("out of order")), FString::Printf(TEXT("DD9 district Ids that do not ascend are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4 + 12 * 4 + 4, -1); Expect(TryFile(TEXT("deepr5"), T, Err) && Err.Contains(TEXT("district #5")), FString::Printf(TEXT("DD9 a surface district on a floor is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4 + 12 * 3 + 4, 0); Expect(TryFile(TEXT("deepr6"), T, Err) && Err.Contains(TEXT("district #4")), FString::Printf(TEXT("DD9 a whole-floor district on the surface is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 64, 2); Expect(TryFile(TEXT("deepr7"), T, Err) && Err.Contains(TEXT("2 shape(s) listed for 3 deep")), FString::Printf(TEXT("DD9 fewer shapes than deep districts is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 124, 4); Expect(TryFile(TEXT("deepr8"), T, Err) && Err.Contains(TEXT("names no deep district")), FString::Printf(TEXT("DD9 a shape that names a whole-floor district is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 72, 0); Expect(TryFile(TEXT("deepr9"), T, Err) && Err.Contains(TEXT("lists 0 points")), FString::Printf(TEXT("DD9 a shape with no point is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 72, RH::MaxDeepPoints + 1); Expect(TryFile(TEXT("deepr10"), T, Err) && Err.Contains(TEXT("lists 65537 points")), FString::Printf(TEXT("DD9 a point count over the cap is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 76 + 8, 0); Expect(TryFile(TEXT("deepr11"), T, Err) && Err.Contains(TEXT("outside -1..-20")), FString::Printf(TEXT("DD9 a point on the surface is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 76 + 8, -5); Expect(TryFile(TEXT("deepr12"), T, Err) && Err.Contains(TEXT("floor -5")), FString::Printf(TEXT("DD9 a point on a floor no shaft reaches is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 76, 3); Expect(TryFile(TEXT("deepr13"), T, Err) && Err.Contains(TEXT("out of order or twice")), FString::Printf(TEXT("DD9 points out of order are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 76 + 12, 1); PutI(T, At + 76 + 16, 0); Expect(TryFile(TEXT("deepr14"), T, Err) && Err.Contains(TEXT("out of order or twice")), FString::Printf(TEXT("DD9 the same point listed twice is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 132, 1); PutI(T, At + 136, 0); Expect(TryFile(TEXT("deepr15"), T, Err) && Err.Contains(TEXT("sits in two districts")), FString::Printf(TEXT("DD9 a point in two districts is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 164 + 12 + 8, -4); Expect(TryFile(TEXT("deepr16"), T, Err) && Err.Contains(TEXT("held whole")), FString::Printf(TEXT("DD9 a level held whole that also holds a shape's point is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4 + 12 * 2 + 4, -3); Expect(TryFile(TEXT("deepr17"), T, Err) && Err.Contains(TEXT("starts on floor -2")), FString::Printf(TEXT("DD9 a shape that does not start where its district says is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; T.SetNum(At + 8); Expect(TryFile(TEXT("deepr18"), T, Err) && Err.Contains(TEXT("district count 5")), FString::Printf(TEXT("DD9 a file cut short is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; const uint32 V48 = 48; FMemory::Memcpy(T.GetData() + 4, &V48, 4); FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("deepr19"))); FString E48; const bool bOk = Sim->LoadColony(TEXT("deepr19"), E48); Expect(!bOk && E48.Contains(TEXT("Save format mismatch")) && E48.Contains(TEXT("version 48")) && E48.Contains(TEXT("/55")), FString::Printf(TEXT("DD9 a v48 file is refused as a version error (%s)"), *E48)); }
		Expect(DistrictDigest() == D0, TEXT("DD9 after all of that the colony is exactly as it was"));

		// Step-2 fix 4: the district list of the file names Id 2 twice (district #3's Id is overwritten). The deep block checks only the skeleton's own order,
		// so it passes before teardown; the loader must repair with a Warning that names the duplicate (never crash on the shape the first #2 already took,
		// never print an Error line) and drop it: four districts, and #2 still holds its own shape. A record starts int32 Id, FString Kind, FString Name.
		{
			const FRHDistrict* D3 = Find(3);
			const auto FStr = [](const FString& S)
			{
				TArray<uint8> B;
				const int32 N = S.Len() + 1;
				B.SetNum(4 + N);
				FMemory::Memcpy(B.GetData(), &N, 4);
				for (int32 i = 0; i < S.Len(); ++i) { B[4 + i] = (uint8)S[i]; }
				B[4 + S.Len()] = 0;
				return B;
			};
			TArray<uint8> Pat;
			if (D3) { Pat = FStr(D3->Kind.ToString()); Pat.Append(FStr(D3->Name)); }
			int32 Found = INDEX_NONE, Matches = 0;
			for (int32 I = 4; D3 && I + Pat.Num() <= W.Num(); ++I)
			{
				if (FMemory::Memcmp(W.GetData() + I, Pat.GetData(), Pat.Num()) == 0) { if (Found == INDEX_NONE) { Found = I; } ++Matches; }
			}
			Expect(D3 && Matches == 1 && Rd(W, Found - 4) == 3 && D3->Kind == NResearch,
				FString::Printf(TEXT("DD9 (fix 4) setup: district #3's Kind and Name are found once in the file, straight after its Id 3 (%d match(es), Id %d there, at byte %d)"), Matches, Found == INDEX_NONE ? -1 : Rd(W, Found - 4), Found));
			if (D3 && Matches == 1 && Rd(W, Found - 4) == 3)
			{
				TArray<uint8> T = W;
				PutI(T, Found - 4, 2);
				FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("deepdup")));
				FRHWarningLineCapture Warnings;
				GLog->AddOutputDevice(&Warnings);
				FString Ed;
				const bool bLoaded = LoadCounted(TEXT("deepdup"), Ed);
				GLog->RemoveOutputDevice(&Warnings);
				int32 Named = 0;
				for (const FString& Line : Warnings.Lines) { Named += Line.Contains(TEXT("district #2 repeats or goes back on the Id order; it is dropped")) ? 1 : 0; }
				Expect(bLoaded && Ed.IsEmpty() && LoadErrorLines == 0 && Named == 1,
					FString::Printf(TEXT("DD9 (fix 4) a district list with Id 2 twice loads (%d), with one Warning that names the duplicate (%d) and no Error line (%d) (%s)"), bLoaded ? 1 : 0, Named, LoadErrorLines, *Ed));
				Expect(Sim->GetDistricts().Num() == 4 && Sim->GetDistricts()[0].Id == 1 && Sim->GetDistricts()[1].Id == 2 && Sim->GetDistricts()[2].Id == 4 && Sim->GetDistricts()[3].Id == 5
					&& Find(2) && Find(2)->Kind == NIndustry && Find(2)->Deep.Num() == 2 && Find(2)->IsDeep() && Sim->GetDistrictOfFloorPoint(-1, FIntPoint(3, 3)) == 2 && Find(1) && Find(1)->Deep.Num() == 4,
					FString::Printf(TEXT("DD9 (fix 4) four districts (#1, #2, #4, #5) are left, and #2 holds its own shape: Industry, two points, (3,3) on -1 is its own (%d districts)"), Sim->GetDistricts().Num()));
				Expect(Sim->Debug_DistrictCachesFresh() && Sim->Debug_CommuteCachesFresh(), TEXT("DD9 (fix 4) the district caches and the commute flows are fresh after the repair"));
			}
			FString Back;
			Expect(LoadCounted(TEXT("deepfile"), Back) && Back.IsEmpty() && DistrictDigest() == D0, TEXT("DD9 (fix 4) the good file still loads, all five districts"));
		}

		// A colony with two open projects holding points: the points round-trip, and a loaded colony equals a twin that never saved.
		Reset();
		Sim->Debug_AddColonists(20);
		FString PW;
		Sim->Debug_PaintCarve(-2, FIntPoint(2, -1), PW);
		Expect(Shape(NHomes, -2, 1, 1, -1, 3, 1) && Shape(NCivic, -1, 1, 1, 0, 2, 1) && StartGrowth(80.0) == 1 && Open(1) && StartGrowth(80.0) == 1 && Open(2) && Open(1)->Points.Num() == 4 && Open(2)->Points.Num() == 1,
			FString::Printf(TEXT("DD9 growth setup: Homes #1 (a DeepCells project of four points on -2) and Civic #2 (one point on -1) both have a project open (%s | %s)"), *ProjectText(Open(1)), *ProjectText(Open(2))));
		Sim->Debug_SetGrowthEnabled(false);
		const FString Proj1 = ProjectText(Open(1)), Proj2 = ProjectText(Open(2));
		const auto GrowthDigest = [&]() { return DistrictDigest() + TEXT("|") + FloorDigest(-1) + TEXT("|") + FloorDigest(-2) + TEXT("|") + ProjectText(Open(1)) + TEXT("|") + ProjectText(Open(2)); };
		const FString G0 = GrowthDigest();
		Expect(Sim->SaveColony(TEXT("deepgrow"), SaveErr), TEXT("DD9 the colony with two projects open is saved"));
		TArray<uint8> GW;
		FFileHelper::LoadFileToArray(GW, *SaveFilePath(TEXT("deepgrow")));
		const int32 GPattern[10] = { 2, 1, -2, 2, 2, -1, 2, 2, 1, 9 };
		int32 GAt = INDEX_NONE;
		for (int32 I = 0; I + 40 <= GW.Num() && GAt == INDEX_NONE; ++I)
		{
			bool bAll = true;
			for (int32 k = 0; k < 10 && bAll; ++k) { bAll &= Rd(GW, I + 4 * k) == GPattern[k]; }
			if (bAll) { GAt = I; }
		}
		Expect(GAt != INDEX_NONE && Rd(GW, GAt + 204) == 2 && Rd(GW, GAt + 208) == 1 && Rd(GW, GAt + 212) == -2 && Rd(GW, GAt + 216) == 4 && Rd(GW, GAt + 220) == 1 && Rd(GW, GAt + 224) == 0 && Rd(GW, GAt + 252) == 2 && Rd(GW, GAt + 256) == -1 && Rd(GW, GAt + 260) == 1 && Rd(GW, GAt + 264) == 1 && Rd(GW, GAt + 268) == 0,
			FString::Printf(TEXT("DD9 the project block: the count 2, then per project its DistrictId, floor, point count and (X, Y) points (block at byte %d)"), GAt));
		if (GAt != INDEX_NONE)
		{
			const auto TryGrow = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
			{
				FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
				const FString Before = GrowthDigest();
				const bool bOk = LoadCounted(Slot, OutErr);
				return !bOk && GrowthDigest() == Before && LoadErrorLines == 0 && OutErr.Contains(TEXT("Save floor-shape block invalid"));
			};
			{ TArray<uint8> T = GW; PutI(T, GAt + 208, 3); Expect(TryGrow(TEXT("deepg1"), T, Err) && Err.Contains(TEXT("no deep district")), FString::Printf(TEXT("DD9 growth points for a district that is not deep are refused (%s)"), *Err)); }
			{ TArray<uint8> T = GW; PutI(T, GAt + 212, 0); Expect(TryGrow(TEXT("deepg2"), T, Err) && Err.Contains(TEXT("out of order or range")), FString::Printf(TEXT("DD9 growth points on the surface are refused (%s)"), *Err)); }
			{ TArray<uint8> T = GW; PutI(T, GAt + 216, 0); Expect(TryGrow(TEXT("deepg3"), T, Err) && Err.Contains(TEXT("lists 0 points")), FString::Printf(TEXT("DD9 a project with no point is refused (%s)"), *Err)); }
			{ TArray<uint8> T = GW; PutI(T, GAt + 220, 50); Expect(TryGrow(TEXT("deepg4"), T, Err) && Err.Contains(TEXT("outside its shape")), FString::Printf(TEXT("DD9 a growth point outside its shape is refused (%s)"), *Err)); }
			{ TArray<uint8> T = GW; PutI(T, GAt + 252, 1); Expect(TryGrow(TEXT("deepg5"), T, Err) && Err.Contains(TEXT("out of order")), FString::Printf(TEXT("DD9 projects whose districts do not ascend are refused (%s)"), *Err)); }
			{ TArray<uint8> T = GW; PutI(T, GAt + 204, 5); Expect(TryGrow(TEXT("deepg6"), T, Err) && Err.Contains(TEXT("growth project count 5")), FString::Printf(TEXT("DD9 more projects than districts is refused (%s)"), *Err)); }
		}
		Steps(25);
		const FString Control = GrowthDigest();
		FString Le;
		Expect(LoadCounted(TEXT("deepgrow"), Le) && LoadErrorLines == 0 && Le.IsEmpty() && GrowthDigest() == G0 && ProjectText(Open(1)) == Proj1 && ProjectText(Open(2)) == Proj2, FString::Printf(TEXT("DD9 the load restores both projects with their points, exactly (%s)"), *Le));
		Sim->Debug_SetGrowthEnabled(false);
		Steps(25);
		Expect(GrowthDigest() == Control && !Open(1) && !Open(2) && Sim->GetFloorCarvedCells(-2) == 9, TEXT("DD9 run on a sol: the loaded colony equals the control that never saved, both projects finished, (2,-1) carved"));
	}

	// --- DD10. The keys [RC 6] and the fresh checks. ---
	{
		Reset();
		Send(FloorCmd(NHomes, -1));
		Sim->Debug_AddColonists(13);
		uint32 MC = 7, MB = 7, CC = 7;
		Sim->Debug_DeepKeys(MC, MB, CC);
		const int32 Res0 = CountsOf(1).Residents;
		Sim->ExcavateFloor(-1, 1, R);
		uint32 MC2 = 7, MB2 = 7, CC2 = 7;
		Sim->Debug_DeepKeys(MC2, MB2, CC2);
		Expect(MC == 0 && MB == 0 && CC == 0 && MC2 == 0 && MB2 == 0 && CC2 == 0 && CountsOf(1).Cells == 13 && CountsOf(1).Residents == Res0 && Sim->Debug_DistrictCachesFresh() && Sim->Debug_CommuteCachesFresh(),
			TEXT("DD10 with only a whole-floor district the floor-shape key fields stay 0 through a carve (RC 6): the C5 counts and the X13 / X14 rebuild counts cannot move"));
		Reset();
		Sim->Debug_AddColonists(13);
		Sim->Debug_SetColonistHome(13, 0, -1); // the 13th joins floor -1's twelve: an overfull floor
		Sim->Debug_RefreshJobs();
		Expect(Shape(NHomes, -1, 1, -3, -3, 5, 5) && Find(1) && CountsOf(1).Cells == 12 && CountsOf(1).Residents == 12 && Sim->GetColonistHomeCell(13) == INDEX_NONE, FString::Printf(TEXT("DD10 setup: a Homes shape over every cell of -1 and the rock round it; 13 residents for 12 beds: the 13th has no home cell (%s)"), *Note));
		Sim->Debug_DeepKeys(MC, MB, CC);
		const uint32 V0 = Sim->GetFloorCellsVersion();
		Expect(MC == V0 && MC != 0 && MB != 0 && CC == V0, FString::Printf(TEXT("DD10 with a shape the membership key's DeepCells field and the commute key's are the floor cells' version (%u)"), V0));
		Sim->ExcavateFloor(-1, 1, R);
		Sim->Debug_DeepKeys(MC2, MB2, CC2);
		Expect(Sim->GetFloorCellsVersion() != V0 && MC2 == Sim->GetFloorCellsVersion() && CC2 == Sim->GetFloorCellsVersion() && MC2 != MC,
			TEXT("DD10 a carve moves both keys while a shape exists"));
		Expect(CountsOf(1).Cells == 13 && CountsOf(1).Residents == 13 && Sim->GetColonistHomeCell(13) == 12 && Sim->GetDistrictOfColonist(13) == 1,
			FString::Printf(TEXT("DD10 and the legacy bed fill re-derives with no other version moved: the carve gave the 13th resident cell 12, so the shape counts %d residents"), CountsOf(1).Residents));
		Expect(Sim->Debug_DistrictCachesFresh() && Sim->Debug_CommuteCachesFresh(), TEXT("DD10 the district caches and the commute flows match a fresh derivation"));
	}

	return Finish();
}
