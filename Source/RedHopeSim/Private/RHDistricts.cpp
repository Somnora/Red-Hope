#include "RHDistricts.h"

namespace
{
	// About forty real Martian place names (regions, plains, craters, plateaus), the
	// way the colonists' names are a hard-coded pool: a district's generated name is
	// a pure function of its Id, so the same colony always names its districts the
	// same way. Placeholder content pending the director's review.
	const TCHAR* GRHDistrictNames[] = {
		TEXT("Gale"), TEXT("Jezero"), TEXT("Holden"), TEXT("Eberswalde"), TEXT("Gusev"),
		TEXT("Meridiani"), TEXT("Utopia"), TEXT("Elysium"), TEXT("Tharsis"), TEXT("Arcadia"),
		TEXT("Acidalia"), TEXT("Chryse"), TEXT("Hellas"), TEXT("Argyre"), TEXT("Isidis"),
		TEXT("Amazonis"), TEXT("Syrtis"), TEXT("Noachis"), TEXT("Aeolis"), TEXT("Margaritifer"),
		TEXT("Cydonia"), TEXT("Hesperia"), TEXT("Xanthe"), TEXT("Lunae"), TEXT("Sinai"),
		TEXT("Tyrrhena"), TEXT("Cimmeria"), TEXT("Promethei"), TEXT("Ausonia"), TEXT("Arabia"),
		TEXT("Mawrth"), TEXT("Oxia"), TEXT("Aram"), TEXT("Nili"), TEXT("Libya"),
		TEXT("Hadriaca"), TEXT("Ismenius"), TEXT("Casius"), TEXT("Deuteronilus"), TEXT("Protonilus"),
	};
}

const TArray<FName>& RHDistricts::GetKinds()
{
	static const TArray<FName> Kinds = {
		FName(TEXT("Homes")), FName(TEXT("Industry")), FName(TEXT("Farming")), FName(TEXT("Research")), FName(TEXT("Civic")) };
	return Kinds;
}

FName RHDistricts::ResolveKind(FName Kind)
{
	for (const FName& K : GetKinds())
	{
		if (K == Kind)
		{
			return K;
		}
	}
	return NAME_None;
}

int32 RHDistricts::GetNameCount()
{
	return (int32)UE_ARRAY_COUNT(GRHDistrictNames);
}

FString RHDistricts::RomanSuffix(int32 N)
{
	if (N < 2 || N > 3999)
	{
		return FString();
	}
	static const struct { int32 Value; const TCHAR* Glyphs; } Table[] = {
		{ 1000, TEXT("M") }, { 900, TEXT("CM") }, { 500, TEXT("D") }, { 400, TEXT("CD") },
		{ 100, TEXT("C") }, { 90, TEXT("XC") }, { 50, TEXT("L") }, { 40, TEXT("XL") },
		{ 10, TEXT("X") }, { 9, TEXT("IX") }, { 5, TEXT("V") }, { 4, TEXT("IV") }, { 1, TEXT("I") } };
	FString Out;
	for (const auto& Row : Table)
	{
		while (N >= Row.Value)
		{
			Out += Row.Glyphs;
			N -= Row.Value;
		}
	}
	return Out;
}

FString RHDistricts::NameForId(int32 DistrictId)
{
	const int32 Count = GetNameCount();
	const int32 Index = FMath::Max(0, DistrictId - 1);
	FString Name = GRHDistrictNames[Index % Count];
	const int32 Cycle = Index / Count; // 0 the first time round the list, then II, III, ...
	if (Cycle >= 1)
	{
		const FString Suffix = RomanSuffix(Cycle + 1);
		Name += Suffix.IsEmpty() ? FString::Printf(TEXT(" %d"), Cycle + 1) : (TEXT(" ") + Suffix);
	}
	return Name;
}

bool RHDistricts::NormalizeName(const FString& Typed, FString& OutName)
{
	FString Clean;
	Clean.Reserve(Typed.Len());
	for (const TCHAR C : Typed)
	{
		if (C >= 0x20 && C != 0x7F) // no control characters: a name must stay on one line
		{
			Clean.AppendChar(C);
		}
	}
	Clean.TrimStartAndEndInline();
	if (Clean.Len() > MaxNameLength)
	{
		Clean.LeftInline(MaxNameLength);
		if (Clean.Len() > 0 && (uint32)Clean[Clean.Len() - 1] >= 0xD800u && (uint32)Clean[Clean.Len() - 1] <= 0xDBFFu) // a lone high surrogate
		{
			Clean.LeftChopInline(1); // never cut a character in half
		}
		Clean.TrimEndInline();
	}
	OutName = Clean;
	return !OutName.IsEmpty();
}
