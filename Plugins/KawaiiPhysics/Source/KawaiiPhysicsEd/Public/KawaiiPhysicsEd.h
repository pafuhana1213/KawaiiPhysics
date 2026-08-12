// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FWorkspaceItem;

class FKawaiiPhysicsEdModule : public IModuleInterface
{
public:
	/** IModuleInterface 実装 / IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterTabSpawners();

	FDelegateHandle PostEngineInitHandle;
	TSharedPtr<FWorkspaceItem> KawaiiPhysicsMenuGroup;
	bool bTabSpawnersRegistered = false;
};
