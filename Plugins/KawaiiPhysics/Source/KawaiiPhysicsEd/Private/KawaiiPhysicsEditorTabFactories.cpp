// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEditorTabFactories.h"

#include "KawaiiPhysicsEdStyle.h"
#include "SKawaiiPhysicsNodeAuditWindow.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysicsEditorTabFactories"

namespace
{
	FSlateIcon MakeKawaiiPhysicsTabIcon()
	{
		return FSlateIcon(
			FKawaiiPhysicsEdStyle::GetStyleSetName(),
			TEXT("KawaiiPhysics.TabIcon"));
	}
}

FKawaiiPhysicsWindScopeTabFactory::FKawaiiPhysicsWindScopeTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FWorkflowTabFactory(SKawaiiPhysicsWindScopeWindow::WindScopeTabId, InHostingApp)
{
	TabLabel = LOCTEXT("WindScopeTabLabel", "Kawaii Wind Scope");
	TabIcon = MakeKawaiiPhysicsTabIcon();
	ViewMenuDescription = LOCTEXT("WindScopeViewMenuDescription", "Wind Scope");
	ViewMenuTooltip = LOCTEXT("WindScopeMenuTooltip", "KawaiiPhysics の風プレビュータブを開きます / Opens the KawaiiPhysics wind preview tab.");
	bIsSingleton = true;
}

TSharedRef<SWidget> FKawaiiPhysicsWindScopeTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	(void)Info;

	TSharedRef<SKawaiiPhysicsWindScopeWindow> ScopeWidget = SNew(SKawaiiPhysicsWindScopeWindow);
	if (!ScopeWidget->HasTargetArgs())
	{
		ScopeWidget->LoadPendingReconnectFromConfig();
	}
	return ScopeWidget;
}

TSharedRef<SDockTab> FKawaiiPhysicsWindScopeTabFactory::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> Tab = FWorkflowTabFactory::SpawnTab(Info);
	if (TSharedPtr<SWidget> TabContent = Tab->GetContent();
		TabContent.IsValid() && TabContent->GetType() == FName(TEXT("SKawaiiPhysicsWindScopeWindow")))
	{
		StaticCastSharedPtr<SKawaiiPhysicsWindScopeWindow>(TabContent)->SetOwnerTab(Tab);
	}
	return Tab;
}

FKawaiiPhysicsPresetDiffTabFactory::FKawaiiPhysicsPresetDiffTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FWorkflowTabFactory(SKawaiiPhysicsPresetDiffWindow::PresetDiffTabId, InHostingApp)
{
	TabLabel = LOCTEXT("PresetDiffTabLabel", "Kawaii Preset Diff");
	TabIcon = MakeKawaiiPhysicsTabIcon();
	ViewMenuDescription = LOCTEXT("PresetDiffViewMenuDescription", "Preset Diff");
	ViewMenuTooltip = LOCTEXT("PresetDiffMenuTooltip", "KawaiiPhysics のプリセット差分タブを開きます / Opens the KawaiiPhysics preset diff tab.");
	bIsSingleton = true;
}

TSharedRef<SWidget> FKawaiiPhysicsPresetDiffTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	(void)Info;
	return SNew(SKawaiiPhysicsPresetDiffWindow);
}

TSharedRef<SDockTab> FKawaiiPhysicsPresetDiffTabFactory::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> Tab = FWorkflowTabFactory::SpawnTab(Info);
	if (TSharedPtr<SWidget> TabContent = Tab->GetContent();
		TabContent.IsValid() && TabContent->GetType() == FName(TEXT("SKawaiiPhysicsPresetDiffWindow")))
	{
		StaticCastSharedPtr<SKawaiiPhysicsPresetDiffWindow>(TabContent)->SetOwnerTab(Tab);
	}
	return Tab;
}

FKawaiiPhysicsNodeAuditTabFactory::FKawaiiPhysicsNodeAuditTabFactory(TSharedPtr<FAssetEditorToolkit> InHostingApp)
	: FWorkflowTabFactory(SKawaiiPhysicsNodeAuditWindow::NodeAuditTabId, InHostingApp)
{
	TabLabel = LOCTEXT("NodeAuditTabLabel", "Kawaii Node Audit");
	TabIcon = MakeKawaiiPhysicsTabIcon();
	ViewMenuDescription = LOCTEXT("NodeAuditViewMenuDescription", "Node Audit");
	ViewMenuTooltip = LOCTEXT("NodeAuditMenuTooltip", "KawaiiPhysics のノード監査タブを開きます / Opens the KawaiiPhysics node audit tab.");
	bIsSingleton = true;
}

TSharedRef<SWidget> FKawaiiPhysicsNodeAuditTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	(void)Info;
	return SNew(SKawaiiPhysicsNodeAuditWindow);
}

TSharedRef<SDockTab> FKawaiiPhysicsNodeAuditTabFactory::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> Tab = FWorkflowTabFactory::SpawnTab(Info);
	if (TSharedPtr<SWidget> TabContent = Tab->GetContent();
		TabContent.IsValid() && TabContent->GetType() == FName(TEXT("SKawaiiPhysicsNodeAuditWindow")))
	{
		StaticCastSharedPtr<SKawaiiPhysicsNodeAuditWindow>(TabContent)->SetOwnerTab(Tab);
	}
	return Tab;
}

#undef LOCTEXT_NAMESPACE
