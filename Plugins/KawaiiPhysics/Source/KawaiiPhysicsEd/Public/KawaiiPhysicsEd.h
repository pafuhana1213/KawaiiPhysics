// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Modules/ModuleInterface.h"

class FAssetEditorToolkit;
class FWorkflowAllowedTabSet;

class FKawaiiPhysicsEdModule : public IModuleInterface
{
public:
	/** IModuleInterface 実装 / IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Slate 初期化後に Persona タブ登録フックを設定する / Sets up Persona tab registration hooks after Slate is initialized. */
	void RegisterTabSpawners();

	/** Animation Blueprint エディタ専用の Workflow タブを登録する / Registers workflow tabs for Animation Blueprint editors only. */
	void HandleRegisterPersonaTabs(FWorkflowAllowedTabSet& TabSet, TSharedPtr<FAssetEditorToolkit> InHostingApp);

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle PersonaRegisterTabsHandle;
	FDelegateHandle SequencerTrackEditorHandle;
	bool bTabSpawnersRegistered = false;
};
