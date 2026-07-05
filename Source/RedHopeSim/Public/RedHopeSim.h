#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

REDHOPESIM_API DECLARE_LOG_CATEGORY_EXTERN(LogRedHopeSim, Log, All);

class FRedHopeSimModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
