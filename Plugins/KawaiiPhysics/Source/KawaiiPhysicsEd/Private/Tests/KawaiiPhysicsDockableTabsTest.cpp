// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "../SKawaiiPhysicsNodeAuditWindow.h"
#include "../SKawaiiPhysicsPresetDiffWindow.h"
#include "../SKawaiiPhysicsWindScopeWindow.h"

#include "CoreGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsDockableTabsSpawnersTest,
                                 "KawaiiPhysics.Editor.DockableTabs.Spawners",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsDockableTabsSpawnersTest::RunTest(const FString& Parameters)
{
	if (!GIsEditor || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slateが初期化されていない環境のため、DockableTabsスポナー登録テストをスキップします。"));
		return true;
	}

	const FName WindScopeTabId = SKawaiiPhysicsWindScopeWindow::WindScopeTabId;
	const FName PresetDiffTabId = SKawaiiPhysicsPresetDiffWindow::PresetDiffTabId;
	const FName NodeAuditTabId = SKawaiiPhysicsNodeAuditWindow::NodeAuditTabId;

	bool bOk = true;
	bOk &= TestTrue(TEXT("Wind Scope tab spawner is registered"),
	                FGlobalTabmanager::Get()->HasTabSpawner(WindScopeTabId));
	bOk &= TestTrue(TEXT("Preset Diff tab spawner is registered"),
	                FGlobalTabmanager::Get()->HasTabSpawner(PresetDiffTabId));
	bOk &= TestTrue(TEXT("Node Audit tab spawner is registered"),
	                FGlobalTabmanager::Get()->HasTabSpawner(NodeAuditTabId));
	bOk &= TestTrue(TEXT("Wind Scope and Preset Diff tab ids are distinct"),
	                WindScopeTabId != PresetDiffTabId);
	bOk &= TestTrue(TEXT("Wind Scope and Node Audit tab ids are distinct"),
	                WindScopeTabId != NodeAuditTabId);
	bOk &= TestTrue(TEXT("Preset Diff and Node Audit tab ids are distinct"),
	                PresetDiffTabId != NodeAuditTabId);
	return bOk;
}

#endif
