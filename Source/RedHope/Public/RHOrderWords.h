#pragma once

#include "CoreMinimal.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"

// D74 (Gate-D framing review, 2026-10-21): the words the player reads for an order. The sim's verbs stay
// as they are (they are internal names, saved in the uplink queue and typed into the RH.* cheats); only the
// text on screen is mapped: the confirm line, the uplink list and the refusal notice all fill their formats
// from here. Presentation only; it reads the definitions and writes nothing in the sim.
namespace RHOrder
{
	inline FString VerbText(FName Verb)
	{
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
		return Verb.ToString();
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

	// A settlement's DisplayName instead of its row name; E5's targets in words; any other target is shown as it is.
	inline FString TargetText(const URHDefinitionsSubsystem* Defs, FName Target)
	{
		if (Target == FName("DefyIssue")) { return TEXT("Defy, and issue a Martian currency"); }
		if (Target == FName("Issue"))     { return TEXT("a Martian currency (the mark)"); }
		if (Target == FName("ToMarks"))   { return TEXT("credits to marks"); }
		if (Target == FName("ToCredits")) { return TEXT("marks to credits"); }
		const FRHRivalRow* Rival = (Defs && !Target.IsNone()) ? Defs->GetRival(Target) : nullptr;
		return Rival ? Rival->DisplayName : Target.ToString();
	}
}
