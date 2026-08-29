// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FKawaiiPhysicsSequencerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
