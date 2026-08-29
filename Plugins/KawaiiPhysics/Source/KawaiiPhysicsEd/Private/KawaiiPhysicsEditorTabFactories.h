// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FAssetEditorToolkit;
class SDockTab;

class FKawaiiPhysicsTabFactoryBase : public FWorkflowTabFactory
{
public:
	/** Window メニュー登録時に Kawaii Physics サブメニューへ配置する / Registers the tab spawner under the Kawaii Physics submenu in the Window menu. */
	virtual FTabSpawnerEntry& RegisterTabSpawner(TSharedRef<FTabManager> InTabManager, const FApplicationMode* CurrentApplicationMode) const override;

	/** Window メニューのツールチップテキストを返す / Returns tooltip text for the Window menu entry. */
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

protected:
	/** Kawaii Physics 共通 Workflow タブファクトリを生成する / Creates the shared Kawaii Physics workflow tab factory. */
	FKawaiiPhysicsTabFactoryBase(FName TabId, TSharedPtr<FAssetEditorToolkit> InHostingApp);
};

class FKawaiiPhysicsWindScopeTabFactory : public FKawaiiPhysicsTabFactoryBase
{
public:
	/** Wind Scope Workflow タブファクトリを生成する / Creates the Wind Scope workflow tab factory. */
	explicit FKawaiiPhysicsWindScopeTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Wind Scope タブの中身を生成する / Creates the Wind Scope tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};

class FKawaiiPhysicsPresetDiffTabFactory : public FKawaiiPhysicsTabFactoryBase
{
public:
	/** Preset Diff Workflow タブファクトリを生成する / Creates the Preset Diff workflow tab factory. */
	explicit FKawaiiPhysicsPresetDiffTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Preset Diff タブの中身を生成する / Creates the Preset Diff tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};

class FKawaiiPhysicsNodeAuditTabFactory : public FKawaiiPhysicsTabFactoryBase
{
public:
	/** Node Audit Workflow タブファクトリを生成する / Creates the Node Audit workflow tab factory. */
	explicit FKawaiiPhysicsNodeAuditTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	/** Node Audit タブの中身を生成する / Creates the Node Audit tab body. */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	/** 生成したタブへ所有タブ参照を設定する / Sets the owning tab reference on the spawned tab body. */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};
