// Copyright 2019-2025 pafuhana1213. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FKawaiiPhysicsEdModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
