#include "RedHope.h"

DEFINE_LOG_CATEGORY(LogRedHope);

void FRedHopeModule::StartupModule()
{
	UE_LOG(LogRedHope, Log, TEXT("RedHope module started"));
}

void FRedHopeModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FRedHopeModule, RedHope, "RedHope");
