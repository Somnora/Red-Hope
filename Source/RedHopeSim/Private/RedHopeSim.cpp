#include "RedHopeSim.h"

DEFINE_LOG_CATEGORY(LogRedHopeSim);

void FRedHopeSimModule::StartupModule()
{
	UE_LOG(LogRedHopeSim, Log, TEXT("RedHopeSim module started"));
}

void FRedHopeSimModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FRedHopeSimModule, RedHopeSim)
