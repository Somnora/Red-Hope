#include "RHSaveFrame.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	uint32 ReadU32(const TArray<uint8>& File, int64 At)
	{
		uint32 V = 0;
		FMemory::Memcpy(&V, File.GetData() + At, 4);
		return V;
	}

	// A frame whose header stands at At: the tag, a length that fits the file, and a footer that repeats both.
	bool FrameAt(const TArray<uint8>& File, int64 At, uint32 Tag, int64& OutBytes)
	{
		if (At < 0 || At + RHSaveFrame::HeaderBytes + RHSaveFrame::FooterBytes > File.Num() || ReadU32(File, At) != Tag)
		{
			return false;
		}
		const int64 Bytes = ReadU32(File, At + 4);
		const int64 End = At + RHSaveFrame::HeaderBytes + Bytes + RHSaveFrame::FooterBytes;
		if (End > File.Num() || ReadU32(File, End - 8) != (uint32)Bytes || ReadU32(File, End - 4) != Tag)
		{
			return false;
		}
		OutBytes = Bytes;
		return true;
	}

	// The walk from the end: the last 8 bytes are a footer, which names its header; the 8 bytes before that header are the previous frame's
	// footer. The walk stops at the first footer whose header does not answer it (the end of the framed tail).
	int64 FindBack(const TArray<uint8>& File, uint32 Tag, int64& OutBytes)
	{
		int64 End = File.Num();
		for (int32 Step = 0; Step < 64 && End >= RHSaveFrame::HeaderBytes + RHSaveFrame::FooterBytes; ++Step)
		{
			const uint32 Its = ReadU32(File, End - 4);
			const int64 Bytes = ReadU32(File, End - 8);
			const int64 At = End - RHSaveFrame::FooterBytes - Bytes - RHSaveFrame::HeaderBytes;
			int64 Got = 0;
			if (!FrameAt(File, At, Its, Got))
			{
				return INDEX_NONE;
			}
			if (Its == Tag)
			{
				OutBytes = Got;
				return At;
			}
			End = At;
		}
		return INDEX_NONE;
	}
}

void RHSaveFrame::Write(FArchive& Ar, uint32 Tag, TFunctionRef<void(FArchive&)> WritePayload)
{
	TArray<uint8> Payload;
	FMemoryWriter PayloadAr(Payload);
	WritePayload(PayloadAr);
	uint32 TagOut = Tag;
	uint32 Bytes = (uint32)Payload.Num();
	uint32 Crc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
	Ar << TagOut << Bytes << Crc;
	Ar.Serialize(Payload.GetData(), Payload.Num());
	Ar << Bytes << TagOut;
}

bool RHSaveFrame::Read(FArchive& Ar, const TArray<uint8>& File, uint32 Tag, TArray<uint8>& OutPayload, FString& OutWhy)
{
	OutPayload.Reset();
	int64 Bytes = 0;
	int64 At = Ar.Tell();
	if (!FrameAt(File, At, Tag, Bytes))
	{
		At = FindBack(File, Tag, Bytes);
	}
	if (At == INDEX_NONE)
	{
		OutWhy = TEXT("its frame is neither where the reader stands nor on the walk back from the end of the file");
		return false;
	}
	Ar.Seek(At + HeaderBytes + Bytes + FooterBytes);
	if (FCrc::MemCrc32(File.GetData() + At + HeaderBytes, (int32)Bytes) != ReadU32(File, At + 8))
	{
		OutWhy = FString::Printf(TEXT("its %lld bytes fail their CRC"), Bytes);
		return false;
	}
	OutPayload.Append(File.GetData() + At + HeaderBytes, (int32)Bytes);
	return true;
}

bool RHSaveFrame::ReadName(FArchive& Ar, FString& Out)
{
	// FString's own reader allocates whatever length it is given, and logs an Error on some; a payload's names are short.
	Out.Reset();
	const int64 At = Ar.Tell();
	int32 Len = 0;
	Ar << Len;
	const int64 Chars = Len < 0 ? -(int64)Len : (int64)Len;
	const int64 Bytes = Len < 0 ? Chars * 2 : Chars;
	if (Ar.IsError() || Chars < 1 || Chars > 256 || At + 4 + Bytes > Ar.TotalSize())
	{
		Ar.SetError();
		return false;
	}
	Ar.Seek(At);
	Ar << Out;
	return !Ar.IsError();
}

bool RHSaveFrame::Find(const TArray<uint8>& File, uint32 Tag, int32& OutHeaderAt, int32& OutPayloadBytes)
{
	int64 Bytes = 0;
	const int64 At = FindBack(File, Tag, Bytes);
	OutHeaderAt = (int32)At;
	OutPayloadBytes = At == INDEX_NONE ? 0 : (int32)Bytes;
	return At != INDEX_NONE;
}

bool RHSaveFrame::Reseal(TArray<uint8>& File, uint32 Tag)
{
	int32 At = 0, Bytes = 0;
	if (!Find(File, Tag, At, Bytes))
	{
		return false;
	}
	const uint32 Crc = FCrc::MemCrc32(File.GetData() + At + HeaderBytes, Bytes);
	FMemory::Memcpy(File.GetData() + At + 8, &Crc, 4);
	return true;
}
