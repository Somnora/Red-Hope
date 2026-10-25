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
		return Verb.ToString();
	}

	// A settlement's DisplayName instead of its row name; any other target is shown as it is.
	inline FString TargetText(const URHDefinitionsSubsystem* Defs, FName Target)
	{
		const FRHRivalRow* Rival = (Defs && !Target.IsNone()) ? Defs->GetRival(Target) : nullptr;
		return Rival ? Rival->DisplayName : Target.ToString();
	}
}
