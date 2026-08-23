// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FAssetEditorToolkit;
class SDockTab;

class FKawaiiPhysicsWindScopeTabFactory : public FWorkflowTabFactory
{
public:
	/** Wind Scope Workflow タブファクトリを生成する / Creates the Wind Scope workflow tab factory. */
	explicit FKawaiiPhysicsWindScopeTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Wind Scope タブの中身を生成する / Creates the Wind Scope tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};

class FKawaiiPhysicsPresetDiffTabFactory : public FWorkflowTabFactory
{
public:
	/** Preset Diff Workflow タブファクトリを生成する / Creates the Preset Diff workflow tab factory. */
	explicit FKawaiiPhysicsPresetDiffTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Preset Diff タブの中身を生成する / Creates the Preset Diff tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};

class FKawaiiPhysicsNodeAuditTabFactory : public FWorkflowTabFactory
{
public:
	/** Node Audit Workflow タブファクトリを生成する / Creates the Node Audit workflow tab factory. */
	explicit FKawaiiPhysicsNodeAuditTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Node Audit タブの中身を生成する / Creates the Node Audit tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};
