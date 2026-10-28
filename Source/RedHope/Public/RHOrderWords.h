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
		return Verb.ToString();
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
