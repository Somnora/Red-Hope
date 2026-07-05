#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"

void URHSimWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogRedHopeSim, Log, TEXT("SimWorldSubsystem initialized (lag %.0f sim-s)"), OrderLagSeconds);
}

void URHSimWorldSubsystem::EnqueueCommand(FRHCommand Command)
{
	Command.ExecuteAtSimSeconds = Command.IssuedAtSimSeconds + OrderLagSeconds;
	UplinkQueue.Add(MoveTemp(Command));
}

void URHSimWorldSubsystem::ProcessDueCommands(double NowSimSeconds)
{
	for (int32 i = UplinkQueue.Num() - 1; i >= 0; --i)
	{
		if (UplinkQueue[i].ExecuteAtSimSeconds <= NowSimSeconds)
		{
			FRHCommand Cmd = UplinkQueue[i];
			UplinkQueue.RemoveAt(i);
			// Verb dispatch lands with the M0 systems; the seam contract is what matters here.
			OnCommandExecuted.Broadcast(Cmd);
		}
	}
}

double URHSimWorldSubsystem::GetStock(FName Resource) const
{
	const double* Found = Stocks.Find(Resource);
	return Found ? *Found : 0.0;
}

void URHSimWorldSubsystem::AddStock(FName Resource, double Delta)
{
	double& Amount = Stocks.FindOrAdd(Resource);
	Amount += Delta;
	OnStockChanged.Broadcast(Resource, Amount);
}
