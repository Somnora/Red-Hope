#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

REDHOPE_API DECLARE_LOG_CATEGORY_EXTERN(LogRedHope, Log, All);

class FRedHopeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
