#pragma once

#include "CoreMinimal.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "RHSimWorldSubsystem.h"
#include "Data/RHRows.h"

// D74 (Gate-D framing review, 2026-10-21): the words the player reads for an order. The sim's verbs stay
// as they are (they are internal names, saved in the uplink queue and typed into the RH.* cheats); only the
// text on screen is mapped: the confirm line, the uplink list and the refusal notice all fill their formats
// from here. Presentation only; it reads the definitions and writes nothing in the sim.
// G2a (2026-11, plain words): this is also the home of every on-screen building, resource, recipe and deposit name, and of credit
// amounts and plurals. A name always comes from a CSV DisplayName (or, for a recipe or a deposit, is built from one), and the last
// resort for a missing row is the key spaced out ("ChargePad" -> "Charge Pad"), never the raw key. The RH.* cheats' console output
// keeps the internal names (players type them).
namespace RHOrder
{
	inline FString VerbText(FName Verb)
	{
		if (Verb.IsNone())                  { return FString(); } // G2a r2: no verb names nothing (never the engine's "None")
		if (Verb == FName("Launder"))       { return TEXT("Amends"); }
		if (Verb == FName("Pacify"))        { return TEXT("Reassure"); }
		if (Verb == FName("Solidarity"))    { return TEXT("Answer Earth"); }
		if (Verb == FName("Covert"))        { return TEXT("Covert op"); }
		if (Verb == FName("MarketOrder"))   { return TEXT("Market order"); }
		if (Verb == FName("PaintDistrict")) { return TEXT("Paint district"); }
		if (Verb == FName("EraseDistrict")) { return TEXT("Erase district"); }
		if (Verb == FName("RenameDistrict")) { return TEXT("Rename district"); }
		if (Verb == FName("Currency"))      { return TEXT("Issue"); } // E5 (PLACEHOLDER, gate-d-wording Y7): "Issue a Martian currency (the mark)"
		if (Verb == FName("Exchange"))      { return TEXT("Bloc exchange"); }
		if (Verb == FName("Treaty"))        { return TEXT("Pact"); } // E6 (PLACEHOLDER, gate-d-wording Z4): the pact's sign or break
		if (Verb == FName("OpenAtrium"))    { return TEXT("Open atrium"); }  // D7 (PLACEHOLDER, gate-d-wording L21)
		if (Verb == FName("CloseAtrium"))   { return TEXT("Close atrium"); } // D7 (PLACEHOLDER, gate-d-wording L21)
		if (Verb == FName("Move"))          { return TEXT("Move"); }         // BM-7 (PLACEHOLDER, gate-d-wording B8): a furnishing's or a fitting's free move
		if (Verb == FName("StopDig"))       { return TEXT("Stop digging"); } // C0-2 (PLACEHOLDER, gate-d-wording BF12): "Stop digging Regolith A"
		// G2a fix round 1 (PLACEHOLDER, gate-d-wording PW23): the sim's two-word verbs read as words on the uplink list and in the rejection
		// notice ("SinkShaft" showed as is); any verb not mapped here is spaced out, never shown as a CamelCase key.
		if (Verb == FName("SinkShaft"))     { return TEXT("Sink shaft"); }
		if (Verb == FName("FitLift"))       { return TEXT("Fit lift car"); }
		if (Verb == FName("LayTramLine"))   { return TEXT("Lay tram line"); }
		if (Verb == FName("DigTunnel"))     { return TEXT("Dig tunnel"); }
		if (Verb == FName("CancelTunnel"))  { return TEXT("Cancel tunnel"); }
		if (Verb == FName("DigCavern"))     { return TEXT("Dig cavern"); }
		if (Verb == FName("CancelCavern"))  { return TEXT("Cancel cavern"); }
		if (Verb == FName("BuildRoad"))     { return TEXT("Build road"); }
		if (Verb == FName("DemolishRoad"))  { return TEXT("Demolish road"); }
		return FName::NameToDisplayString(Verb.ToString(), false);
	}

	// D7 (PLACEHOLDER, gate-d-wording L8): the "Open as atrium" tip's preview, the one formatter for the card's tip and RH.AtriumGhost's confirm
	// line. The count is the homes and seats that would face THIS atrium (each head once); none has its own form.
	inline FString AtriumPreviewText(int32 OpenCells, double PremiumKg, double ExtraLeakKgPerSol, int32 FacingHeads)
	{
		const FString Facing = FacingHeads <= 0 ? FString(TEXT("No home or seat would face it yet."))
			: FacingHeads == 1 ? FString(TEXT("1 home or seat would face it."))
			: FString::Printf(TEXT("%d homes and seats would face it."), FacingHeads);
		return FString::Printf(TEXT("Open the floors above the hall into one tall space: %d cell%s open. %.0f kg of oxygen fills the new volume now, and it leaks %.0f kg more a sol. %s"),
			OpenCells, OpenCells == 1 ? TEXT("") : TEXT("s"), PremiumKg, ExtraLeakKgPerSol, *Facing);
	}

	// G2a: the last resort for a row with no display name: the key spaced out, never a CamelCase id. G2a r2: it spaces whatever it is
	// given, so the null name reads "None" here. G2a r3 (exactly which helpers guard None): VerbText above, and below BuildingTitle (so
	// BuildingName), ResourceName (so ResourceShort), RecipeText, FurnitureName, FittingName and TargetText answer a None key with an empty
	// string before they get this far, and OrderText drops the empty part; the deposit helpers (DepositNameOf, DepositName, DepositNameOfRow,
	// IsDepositRow) have no None guard and never receive None (TargetText guards first, and a deposit's state always carries its row name).
	inline FString SpacedName(FName Key) { return FName::NameToDisplayString(Key.ToString(), false); }

	// G2a (PLACEHOLDER, gate-d-wording PW1): a building's CSV DisplayName as the card title shows it ("The Forge", "Charging Pad"): the hover label.
	inline FString BuildingTitle(const URHDefinitionsSubsystem* Defs, FName Def)
	{
		if (Def.IsNone()) { return FString(); } // G2a r2: no building names nothing
		const FRHBuildingRow* Row = Defs ? Defs->GetBuilding(Def) : nullptr;
		return (Row && !Row->DisplayName.IsEmpty()) ? Row->DisplayName : SpacedName(Def);
	}

	// G2a: the same name in running text: "Build Forge", "the Forge is still being built", not "The Forge" (RHCommandDeckGuide.cpp's "your
	// Borer", not "your The Borer").
	inline FString BuildingName(const URHDefinitionsSubsystem* Defs, FName Def)
	{
		FString Name = BuildingTitle(Defs, Def);
		Name.RemoveFromStart(TEXT("The "));
		return Name;
	}

	// G2a (PW6, PW11): a resource's CSV DisplayName ("Structural Materials", "Spoil Chemicals").
	inline FString ResourceName(const URHDefinitionsSubsystem* Defs, FName Res)
	{
		if (Res.IsNone()) { return FString(); } // G2a r2: no resource names nothing
		const FRHResourceRow* Row = Defs ? Defs->GetResource(Res) : nullptr;
		return (Row && !Row->DisplayName.IsEmpty()) ? Row->DisplayName : SpacedName(Res);
	}

	// G2a: the deck's short form for tight rows (the objective bars, market rows, lot words): the three short labels the deck already
	// showed (RHCommandDeckPanels.cpp's ResourceLabel), else the display name. It replaces ResourceLabel's raw-key fallback.
	inline FString ResourceShort(const URHDefinitionsSubsystem* Defs, FName Res)
	{
		if (Res == FName("Struct"))     { return TEXT("Structural"); }
		if (Res == FName("SpareParts")) { return TEXT("Spare parts"); }
		if (Res == FName("HabSegment")) { return TEXT("Habitat segments"); }
		return ResourceName(Defs, Res);
	}

	// G2a (PLACEHOLDER, gate-d-wording PW5): what a batch makes, in words (the recipe rows carry no display name): "making Structural
	// Materials", "making Oxygen and Hydrogen"; the Borer's two batches by what they do. A building's ActiveRecipe holds either a recipe
	// row's name (the Borer's work recipes) or, for every production batch, that recipe's Outputs list ("Struct:25", "Oxygen:8.9;Hydrogen:1.1",
	// RHSimWorldSubsystem.cpp's batch start): both read the same.
	inline FString RecipeText(const URHDefinitionsSubsystem* Defs, FName Recipe)
	{
		if (Recipe.IsNone())              { return FString(); } // G2a r2: no batch names nothing (the card shows the line only while one runs)
		if (Recipe == FName("BoreFloor")) { return TEXT("boring the next floor"); }
		if (Recipe == FName("CarveCell")) { return TEXT("carving a cell"); }
		const FRHRecipeRow* Row = Defs ? Defs->GetRecipe(Recipe) : nullptr;
		TArray<FString> Made;
		for (const TPair<FName, double>& Out : URHDefinitionsSubsystem::ParseResourceList(Row ? Row->Outputs : Recipe.ToString()))
		{
			Made.Add(ResourceName(Defs, Out.Key));
		}
		if (Made.Num() > 0)
		{
			return TEXT("making ") + FString::Join(Made, TEXT(" and "));
		}
		return SpacedName(Recipe).ToLower();
	}

	// G2a (D5 A, PLACEHOLDER, gate-d-wording PW2): a deposit's name from its type and its row letter (the part of the row name after its
	// last '_'): "Regolith A", "Ice B"; an outpost reads "Outpost ice C". No row letter: the type's name alone.
	inline FString DepositNameOf(const URHDefinitionsSubsystem* Defs, FName RowName, FName Type, bool bOutpost)
	{
		const FString Row = RowName.ToString();
		int32 Under = INDEX_NONE;
		const FString Letter = Row.FindLastChar(TEXT('_'), Under) ? Row.Mid(Under + 1) : FString();
		const FString TypeName = ResourceName(Defs, Type);
		const FString Base = bOutpost ? TEXT("Outpost ") + TypeName.ToLower() : TypeName;
		return Letter.IsEmpty() ? Base : Base + TEXT(" ") + Letter;
	}
	inline FString DepositName(const URHDefinitionsSubsystem* Defs, const FRHDepositState& D)
	{
		return DepositNameOf(Defs, D.RowName, D.Type, D.bOutpost);
	}
	// The same from a row name alone (the Dig order's target), through the deposit rows; a row the table does not hold parses "Type_Letter".
	inline FString DepositNameOfRow(const URHDefinitionsSubsystem* Defs, FName RowName)
	{
		bool bFound = false;
		FString Out;
		if (Defs)
		{
			Defs->ForEachDeposit([&](FName Name, const FRHDepositRow& Row)
			{
				if (!bFound && Name == RowName)
				{
					bFound = true;
					Out = DepositNameOf(Defs, Name, Row.Type, Row.Outpost);
				}
			});
		}
		if (!bFound)
		{
			const FString Row = RowName.ToString();
			int32 Under = INDEX_NONE;
			Out = Row.FindLastChar(TEXT('_'), Under) ? DepositNameOf(Defs, RowName, FName(*Row.Left(Under)), false) : SpacedName(RowName);
		}
		return Out;
	}
	inline bool IsDepositRow(const URHDefinitionsSubsystem* Defs, FName RowName)
	{
		bool bFound = false;
		if (Defs)
		{
			Defs->ForEachDeposit([&](FName Name, const FRHDepositRow&) { bFound |= Name == RowName; });
		}
		return bFound;
	}

	// G2a (PW4, PW8): a furnishing's and a fitting's CSV DisplayName ("Bunk Bed", "Window").
	inline FString FurnitureName(const URHDefinitionsSubsystem* Defs, FName Row)
	{
		if (Row.IsNone()) { return FString(); } // G2a r2: no furnishing names nothing
		const FRHFurnitureRow* R = Defs ? Defs->GetFurniture(Row) : nullptr;
		return (R && !R->DisplayName.IsEmpty()) ? R->DisplayName : SpacedName(Row);
	}
	inline FString FittingName(const URHDefinitionsSubsystem* Defs, FName Row)
	{
		if (Row.IsNone()) { return FString(); } // G2a r2: no fitting names nothing
		const FRHDoorRow* R = Defs ? Defs->GetDoor(Row) : nullptr;
		return (R && !R->DisplayName.IsEmpty()) ? R->DisplayName : SpacedName(Row);
	}

	// G2a (PLACEHOLDER, gate-d-wording PW17): a credit or mark AMOUNT in whole units, rounded half away from zero ("14063", not
	// "14063.332"). Rates and per-kg prices keep URHSimWorldSubsystem::FormatCredits (a price under one credit would read 0).
	inline FString CreditsText(int64 Milli)
	{
		const int64 Whole = Milli >= 0 ? (Milli + 500) / 1000 : (Milli - 500) / 1000;
		return FString::Printf(TEXT("%lld"), Whole);
	}

	// G2a (PW19): a count with its noun in the right number ("1 bay", "2 bays"), and the noun alone.
	inline FString Plural(int32 N, const TCHAR* One, const TCHAR* Many) { return FString::Printf(TEXT("%d %s"), N, N == 1 ? One : Many); }
	inline const TCHAR* PluralWord(int32 N, const TCHAR* One, const TCHAR* Many) { return N == 1 ? One : Many; }

	// A settlement's DisplayName instead of its row name; E5's targets in words; G2a: a building, deposit, resource, furnishing or fitting
	// by its display name, and the placeholder targets in words. G2a r2 (PLACEHOLDER, gate-d-wording PW24): an order with no target (a tunnel,
	// a cavern, a shaft, a lift car, a tram line, an atrium) names none - an empty string, never the engine's "None"; any other target with
	// no row (a district kind, a second shaft's "Shaft1") is spaced out ("Shaft 1"), never shown raw. G2a r3 (PW24): a room key reads its
	// RH_Rooms row's DisplayName, as the zone palette's buttons do ("Designate Lounge", not "Designate Smoking"; "Designate Laboratory").
	// Print a verb and its target through OrderText (below), which drops an empty part and its space.
	inline FString TargetText(const URHDefinitionsSubsystem* Defs, FName Target)
	{
		if (Target.IsNone())              { return FString(); }
		if (Target == FName("DefyIssue")) { return TEXT("Defy, and issue a Martian currency"); }
		if (Target == FName("Issue"))     { return TEXT("a Martian currency (the mark)"); }
		if (Target == FName("ToMarks"))   { return TEXT("credits to marks"); }
		if (Target == FName("ToCredits")) { return TEXT("marks to credits"); }
		const FRHRivalRow* Rival = (Defs && !Target.IsNone()) ? Defs->GetRival(Target) : nullptr;
		if (Rival)
		{
			return Rival->DisplayName;
		}
		if (Defs && !Target.IsNone())
		{
			if (Defs->GetBuilding(Target))  { return BuildingName(Defs, Target); }
			if (IsDepositRow(Defs, Target)) { return DepositNameOfRow(Defs, Target); }
			if (Defs->GetResource(Target))  { return ResourceName(Defs, Target); }
			if (Defs->GetFurniture(Target)) { return FurnitureName(Defs, Target); }
			if (Defs->GetDoor(Target))      { return FittingName(Defs, Target); }
			if (const FRHRoomRow* Room = Defs->GetRoom(Target)) { if (!Room->DisplayName.IsEmpty()) { return Room->DisplayName; } } // G2a r3 (PW24)
		}
		if (Target == FName("Furniture"))   { return TEXT("a furnishing"); }
		if (Target == FName("WallFeature")) { return TEXT("a fitting"); }
		if (Target == FName("Building"))    { return TEXT("a building"); }
		return SpacedName(Target);
	}

	// G2a r2 (PW24): an order in words, its verb and its target ("Build Charging Pad", "Pact Zarya Station"); an order with no target reads as
	// its verb alone ("Dig tunnel", never "Dig tunnel None"): the empty part and its space are dropped. The rejection notice, the uplink rows
	// and the rival confirms print an order through this. G2a r3 (PLACEHOLDER, gate-d-wording PW24): a Designate with no target clears a zone,
	// so it reads "Clear zone" (the uplink row, "REJECTED Clear zone: ..."), never "Designate" alone; and the result is never empty, so no
	// caller's separator dangles ("REJECTED : ..."): an order with neither a verb nor a target (none is sent in play) reads "Order".
	inline FString OrderText(const URHDefinitionsSubsystem* Defs, FName Verb, FName Target)
	{
		if (Verb == FName("Designate") && Target.IsNone()) { return TEXT("Clear zone"); }
		const FString Doing = VerbText(Verb);
		const FString What = TargetText(Defs, Target);
		if (Doing.IsEmpty() && What.IsEmpty()) { return TEXT("Order"); }
		return Doing.IsEmpty() ? What : What.IsEmpty() ? Doing : Doing + TEXT(" ") + What;
	}

	// C0-6 (round 1): the ONE meaning of "in store" on every C0a line (the site card's suffix, the ghost's chain line, the dome's
	// Glass lines): what could be taken from a store now, that is the colony's pool plus what finished buildings on a level linked to
	// Level hold. Material already delivered to a site is on that site, not in store. The sim's FRHSiteLineState::InStoreKg, which the
	// card's suffix reads, counts the finished buildings the same way (its delivery leg cannot take from the pool).
	inline double InStoreKg(const URHSimWorldSubsystem* Sim, FName Resource, int32 Level)
	{
		if (!Sim)
		{
			return 0.0;
		}
		double Kg = Sim->GetStock(Resource);
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (!B.bUnderConstruction && Sim->AreLevelsLinked(B.Level, Level))
			{
				Kg += B.InputKg.FindRef(Resource) + B.OutputKg.FindRef(Resource);
			}
		}
		return Kg;
	}

	// C0-6 (PLACEHOLDER, gate-d-wording BF8, BF9): where a material comes from, for a site line with none in store and for the ghost's
	// chain line. "made at the Forge from Structural Materials and Ore" when some live building row has a live recipe that makes it (the
	// first such row in table order; the building's name with a leading "The " dropped; the recipe's inputs by display name), else "comes
	// by supply ship"; then ", or packed from regolith" on the surface Shielding line. With bWithCounts (the ghost) an input the colony
	// holds none of is followed by " (0 kg <name>)".
	inline FString SourceText(const URHDefinitionsSubsystem* Defs, const URHSimWorldSubsystem* Sim, FName Resource, bool bRegolithRoute, bool bWithCounts)
	{
		FString Out;
		if (Defs)
		{
			Defs->ForEachBuilding([&](FName Name, const FRHBuildingRow& Row)
			{
				const FRHRecipeRow* Recipe = (Out.IsEmpty() && Row.SliceActive) ? Defs->FindRecipeByOutput(Name, Resource) : nullptr;
				if (!Recipe)
				{
					return;
				}
				TArray<FString> From;
				for (const TPair<FName, double>& In : URHDefinitionsSubsystem::ParseResourceList(Recipe->Inputs))
				{
					FString Part = ResourceName(Defs, In.Key);
					if (bWithCounts && Sim && Sim->GetTotalSolid(In.Key) + Sim->GetStock(In.Key) <= 0.0)
					{
						Part += FString::Printf(TEXT(" (0 kg %s)"), *ResourceName(Defs, In.Key));
					}
					From.Add(MoveTemp(Part));
				}
				Out = From.Num() > 0 ? FString::Printf(TEXT("made at the %s from %s"), *BuildingName(Defs, Name), *FString::Join(From, TEXT(" and ")))
					: FString::Printf(TEXT("made at the %s"), *BuildingName(Defs, Name));
			});
		}
		if (Out.IsEmpty())
		{
			Out = TEXT("comes by supply ship");
		}
		if (bRegolithRoute)
		{
			Out += TEXT(", or packed from regolith");
		}
		return Out;
	}

	// G2b (B4, PLACEHOLDER, gate-d-wording MS5): a manifest item's CSV DisplayName ("Solar Array Flat-Pack"), else the key spaced out; None
	// names nothing (as the G2a helpers guard it).
	inline FString ManifestItemName(const URHDefinitionsSubsystem* Defs, FName Item)
	{
		if (Item.IsNone()) { return FString(); }
		const FRHManifestItemRow* Row = Defs ? Defs->GetManifestItem(Item) : nullptr;
		return (Row && !Row->DisplayName.IsEmpty()) ? Row->DisplayName : SpacedName(Item);
	}

	// G2b (B4, PLACEHOLDER, gate-d-wording MS5): what a supply ship landed, for the deck line. Equal items are grouped in manifest order (the
	// order each first appears: "Solar Array Flat-Pack x2"); at most three groups are named, then " and N more" (N the items not named);
	// "Supply ship landed: <names>." or, with nothing aboard, "Supply ship landed with an empty hold." Built once per landing.
	inline FString LandingText(const URHDefinitionsSubsystem* Defs, const TArray<FName>& Items)
	{
		if (Items.Num() == 0) { return TEXT("Supply ship landed with an empty hold."); }
		TArray<TPair<FName, int32>> Groups;
		for (const FName& Item : Items)
		{
			TPair<FName, int32>* Found = Groups.FindByPredicate([&Item](const TPair<FName, int32>& G) { return G.Key == Item; });
			if (Found) { ++Found->Value; } else { Groups.Emplace(Item, 1); }
		}
		constexpr int32 MaxNamed = 3;
		FString Names;
		int32 NotNamed = 0;
		for (int32 i = 0; i < Groups.Num(); ++i)
		{
			if (i >= MaxNamed)
			{
				NotNamed += Groups[i].Value;
				continue;
			}
			const FString Name = ManifestItemName(Defs, Groups[i].Key);
			Names += (i > 0 ? TEXT(", ") : TEXT("")) + (Groups[i].Value > 1 ? FString::Printf(TEXT("%s x%d"), *Name, Groups[i].Value) : Name);
		}
		if (NotNamed > 0)
		{
			Names += FString::Printf(TEXT(" and %d more"), NotNamed);
		}
		return FString::Printf(TEXT("Supply ship landed: %s."), *Names);
	}
}
