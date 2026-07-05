#include "RHSimCommandlet.h"
#include "RedHopeSim.h"

int32 URHSimCommandlet::Main(const FString& Params)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("RHSim commandlet stub. Params: %s"), *Params);
	UE_LOG(LogRedHopeSim, Display, TEXT("Headless balance runs land with the M1 era integrator."));
	return 0;
}
