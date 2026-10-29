#pragma once

#include "CoreMinimal.h"

// The economy fixes (save v54; docs/qa/2026-10-24/economy-code-review.md finding 2, the robust fix). Each block at the end of the save
// sits in a frame, so a block that misreads can never shift the blocks after it:
//   Header   uint32 Tag, uint32 PayloadBytes, uint32 PayloadCrc32 (FCrc::MemCrc32)
//   Payload  written by its own archive, read by its own reader, which can never run past it
//   Footer   uint32 PayloadBytes, uint32 Tag
// A reader looks for its frame where the outer reader stands, then by walking the frames back from the end of the file (each footer names
// the header before it), so a lost or misaligned block cannot take the others with it. E5's currency block uses it ('RHE5', v56, written
// after 'RHE4'); E6's levers block reuses it ('RHL6', v57, written after 'RHE5'; docs/qa/2026-10-24/e6-levers-design.md section 7.3).
namespace RHSaveFrame
{
	constexpr uint32 MakeTag(char A, char B, char C, char D)
	{
		return (uint32)(uint8)A | ((uint32)(uint8)B << 8) | ((uint32)(uint8)C << 16) | ((uint32)(uint8)D << 24);
	}
	constexpr uint32 LedgerTag = MakeTag('R', 'H', 'E', '1'); // E1, the ledger
	constexpr uint32 CreditTag = MakeTag('R', 'H', 'E', '2'); // E2b, Earth credit
	constexpr uint32 ExportTag = MakeTag('R', 'H', 'E', '3'); // E3, selling to Earth
	constexpr uint32 MarketTag = MakeTag('R', 'H', 'E', '4'); // E4, the Martian market (v55)
	constexpr uint32 CurrencyTag = MakeTag('R', 'H', 'E', '5'); // E5, the Martian currency (v56)
	constexpr uint32 LeversTag = MakeTag('R', 'H', 'L', '6'); // E6, the levers (v57)
	constexpr int32 HeaderBytes = 12;
	constexpr int32 FooterBytes = 8;

	// Writes WritePayload's bytes at Ar's position, framed under Tag.
	REDHOPESIM_API void Write(FArchive& Ar, uint32 Tag, TFunctionRef<void(FArchive&)> WritePayload);
	// Finds the frame under Tag in File (the whole save Ar reads): at Ar's position, else by the walk from the end. A frame is found only when
	// its header and footer agree and it fits the file; its CRC must then match. Ar stands after any frame found and does not move otherwise.
	// File is read directly, so an error already on Ar does not stop it. On failure OutWhy says why and OutPayload is empty.
	REDHOPESIM_API bool Read(FArchive& Ar, const TArray<uint8>& File, uint32 Tag, TArray<uint8>& OutPayload, FString& OutWhy);
	// A name inside a payload: refused (an error on Ar, nothing allocated) unless its length is 1-256 characters and it fits what is left.
	REDHOPESIM_API bool ReadName(FArchive& Ar, FString& Out);
	// Harness: where the frame under Tag sits (by the walk from the end), and a re-seal after a test edits a payload in place (the CRC is
	// rewritten), so a doctored block reaches the block's own checks instead of failing its CRC.
	REDHOPESIM_API bool Find(const TArray<uint8>& File, uint32 Tag, int32& OutHeaderAt, int32& OutPayloadBytes);
	REDHOPESIM_API bool Reseal(TArray<uint8>& File, uint32 Tag);
}
