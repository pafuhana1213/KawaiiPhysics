// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "../KawaiiPhysicsEditorTabFactories.h"
#include "../KawaiiPhysicsEdWindowUtils.h"
#include "../SKawaiiPhysicsNodeAuditWindow.h"
#include "../SKawaiiPhysicsPresetDiffWindow.h"
#include "../SKawaiiPhysicsWindScopeWindow.h"

#include "CoreGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkflowOrientedApp/ApplicationMode.h"

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
	bOk &= TestFalse(TEXT("Wind Scope tab spawner is not registered globally"),
	                 FGlobalTabmanager::Get()->HasTabSpawner(WindScopeTabId));
	bOk &= TestFalse(TEXT("Preset Diff tab spawner is not registered globally"),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsDockableTabsMenuGroupTest,
                                 "KawaiiPhysics.Editor.DockableTabs.MenuGroup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsDockableTabsMenuGroupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TSharedRef<FWorkspaceItem> Parent = FWorkspaceItem::NewGroup(FText::FromString(TEXT("Dummy")));
	const TSharedRef<FWorkspaceItem> FirstGroup = KawaiiPhysicsEdWindowUtils::FindOrAddKawaiiPhysicsMenuGroup(Parent);
	const TSharedRef<FWorkspaceItem> SecondGroup = KawaiiPhysicsEdWindowUtils::FindOrAddKawaiiPhysicsMenuGroup(Parent);

	bool bOk = true;
	bOk &= TestTrue(TEXT("FindOrAddKawaiiPhysicsMenuGroup returns the existing group"),
	                &FirstGroup.Get() == &SecondGroup.Get());
	bOk &= TestEqual(TEXT("FindOrAddKawaiiPhysicsMenuGroup creates only one parent child"),
	                 Parent->GetChildItems().Num(),
	                 1);

	const FName TestTabId(TEXT("KawaiiPhysicsDockableTabsMenuGroupTest"));
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TestTabId);

	FTabSpawnerEntry& SpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TestTabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs& SpawnTabArgs)
		{
			(void)SpawnTabArgs;
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab);
		}));

	SpawnerEntry.SetGroup(FirstGroup);
	SpawnerEntry.SetGroup(FirstGroup);
	bOk &= TestEqual(TEXT("SetGroup allows duplicate spawner children"),
	                 FirstGroup->GetChildItems().Num(),
	                 2);

	KawaiiPhysicsEdWindowUtils::RemoveStaleSpawnerChildren(FirstGroup, TestTabId);
	bOk &= TestEqual(TEXT("RemoveStaleSpawnerChildren removes matching spawner children"),
	                 FirstGroup->GetChildItems().Num(),
	                 0);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TestTabId);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsDockableTabsMenuGroupRegistrationTest,
                                 "KawaiiPhysics.Editor.DockableTabs.MenuGroupRegistration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsDockableTabsMenuGroupRegistrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	if (!GIsEditor || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slateが初期化されていない環境のため、MenuGroup登録テストをスキップします。"));
		return true;
	}

	bool bOk = true;

	// 実運用の RegisterTabSpawner 経路を通すため、モードとローカル TabManager を用意する
	FApplicationMode Mode(FName(TEXT("KawaiiPhysicsMenuGroupTestMode")));
	const TSharedRef<FWorkspaceItem> Category = Mode.GetWorkspaceMenuCategory();

	TSharedRef<SDockTab> OwnerTab = SNew(SDockTab)
		.TabRole(ETabRole::MajorTab);
	TSharedRef<FTabManager> TabManager = FGlobalTabmanager::Get()->NewTabManager(OwnerTab);

	const FName WindScopeTabId = SKawaiiPhysicsWindScopeWindow::WindScopeTabId;
	const FName PresetDiffTabId = SKawaiiPhysicsPresetDiffWindow::PresetDiffTabId;
	const FName NodeAuditTabId = SKawaiiPhysicsNodeAuditWindow::NodeAuditTabId;

	// HostingApp = nullptr のまま、実際の3ファクトリを RegisterTabSpawner に通す
	// 基底 FWorkflowTabFactory が SharedThis(this) を使うため、本番同様に MakeShared で生成する
	const TSharedRef<FKawaiiPhysicsWindScopeTabFactory> WindScopeFactory = MakeShared<FKawaiiPhysicsWindScopeTabFactory>(nullptr);
	const TSharedRef<FKawaiiPhysicsPresetDiffTabFactory> PresetDiffFactory = MakeShared<FKawaiiPhysicsPresetDiffTabFactory>(nullptr);
	const TSharedRef<FKawaiiPhysicsNodeAuditTabFactory> NodeAuditFactory = MakeShared<FKawaiiPhysicsNodeAuditTabFactory>(nullptr);

	WindScopeFactory->RegisterTabSpawner(TabManager, &Mode);
	PresetDiffFactory->RegisterTabSpawner(TabManager, &Mode);
	NodeAuditFactory->RegisterTabSpawner(TabManager, &Mode);

	bOk &= TestEqual(TEXT("Category has exactly one child (the Kawaii Physics group)"),
	                 Category->GetChildItems().Num(),
	                 1);

	if (Category->GetChildItems().Num() == 1)
	{
		const TSharedRef<FWorkspaceItem> GroupChild = Category->GetChildItems()[0];
		bOk &= TestFalse(TEXT("The single child is a group, not a flat spawner entry"),
		                 GroupChild->AsSpawnerEntry().IsValid());
		bOk &= TestEqual(TEXT("The group is named Kawaii Physics"),
		                 GroupChild->GetDisplayName().ToString(),
		                 FString(TEXT("Kawaii Physics")));

		const TArray<TSharedRef<FWorkspaceItem>>& GroupChildren = GroupChild->GetChildItems();
		bOk &= TestEqual(TEXT("The Kawaii Physics group has 3 spawner children"),
		                 GroupChildren.Num(),
		                 3);

		TSet<FName> ActualTabTypes;
		for (const TSharedRef<FWorkspaceItem>& SpawnerChild : GroupChildren)
		{
			const TSharedPtr<FTabSpawnerEntry> Spawner = SpawnerChild->AsSpawnerEntry();
			if (Spawner.IsValid())
			{
				ActualTabTypes.Add(Spawner->GetTabType());
			}
		}
		bOk &= TestTrue(TEXT("The group's spawner children match the 3 registered tab ids"),
		                ActualTabTypes.Num() == 3 &&
		                ActualTabTypes.Contains(WindScopeTabId) &&
		                ActualTabTypes.Contains(PresetDiffTabId) &&
		                ActualTabTypes.Contains(NodeAuditTabId));
	}

	// カテゴリ直下に平坦な spawner 重複が無いことを確認する（グループ化漏れの検出）
	int32 FlatSpawnerCount = 0;
	for (const TSharedRef<FWorkspaceItem>& Child : Category->GetChildItems())
	{
		if (Child->AsSpawnerEntry().IsValid())
		{
			++FlatSpawnerCount;
		}
	}
	bOk &= TestEqual(TEXT("No flat spawner duplicates directly under the category"),
	                 FlatSpawnerCount,
	                 0);

	// モード切替後の PushTabFactories 再登録を模して、同じ TabManager+Mode へ再登録する
	TabManager->UnregisterAllTabSpawners();
	WindScopeFactory->RegisterTabSpawner(TabManager, &Mode);
	PresetDiffFactory->RegisterTabSpawner(TabManager, &Mode);
	NodeAuditFactory->RegisterTabSpawner(TabManager, &Mode);

	bOk &= TestEqual(TEXT("Re-registration does not grow the category child count"),
	                 Category->GetChildItems().Num(),
	                 1);

	if (Category->GetChildItems().Num() == 1)
	{
		const TSharedRef<FWorkspaceItem> GroupChild = Category->GetChildItems()[0];
		bOk &= TestEqual(TEXT("Re-registration does not grow the group's spawner child count"),
		                 GroupChild->GetChildItems().Num(),
		                 3);
	}

	TabManager->UnregisterAllTabSpawners();
	return bOk;
}

#endif
