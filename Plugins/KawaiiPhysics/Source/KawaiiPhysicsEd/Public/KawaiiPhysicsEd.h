// KawaiiPhysics : Copyright (c) 2019-2025 pafuhana1213

#pragma once

#include "Modules/ModuleInterface.h"

class FKawaiiPhysicsEdModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
