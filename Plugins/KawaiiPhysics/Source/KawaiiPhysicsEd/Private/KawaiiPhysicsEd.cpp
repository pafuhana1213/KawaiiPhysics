// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEd.h"

#include "CoreGlobals.h"
#include "EditorModeRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "KawaiiPhysicsEdStyle.h"
#include "KawaiiPhysicsEditMode.h"
#include "KawaiiPhysicsPresetDataAssetDetails.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SKawaiiPhysicsNodeAuditWindow.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Textures/SlateIcon.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsModuleEd"

namespace
{
	FSimpleMulticastDelegate& GetKawaiiPhysicsPostEngineInitDelegate()
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		return FCoreDelegates::OnPostEngineInit;
#else
		return FCoreDelegates::GetOnPostEngineInit();
#endif
	}
}

void FKawaiiPhysicsEdModule::StartupModule()
{
	FEditorModeRegistry::Get().RegisterMode<FKawaiiPhysicsEditMode>("AnimGraph.SkeletalControl.KawaiiPhysics",
	                                                                LOCTEXT("FKawaiiPhysicsEditMode", "Kawaii Physics"),
	                                                                FSlateIcon(), false);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
		"PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(
		"KawaiiPhysicsPresetDataAsset",
		FOnGetDetailCustomizationInstance::CreateStatic(&FKawaiiPhysicsPresetDataAssetDetails::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();

	if (GIsEditor && !IsRunningCommandlet())
	{
		if (FSlateApplication::IsInitialized())
		{
			RegisterTabSpawners();
		}
		else
		{
			PostEngineInitHandle = GetKawaiiPhysicsPostEngineInitDelegate().AddRaw(
				this,
				&FKawaiiPhysicsEdModule::RegisterTabSpawners);
		}
	}
}


void FKawaiiPhysicsEdModule::ShutdownModule()
{
	SKawaiiPhysicsWindScopeWindow::CloseAllWindows();
	SKawaiiPhysicsPresetDiffWindow::CloseAllWindows();
	SKawaiiPhysicsNodeAuditWindow::CloseAllWindows();

	if (PostEngineInitHandle.IsValid())
	{
		GetKawaiiPhysicsPostEngineInitDelegate().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (bTabSpawnersRegistered)
	{
		SKawaiiPhysicsWindScopeWindow::UnregisterTabSpawner();
		SKawaiiPhysicsPresetDiffWindow::UnregisterTabSpawner();
		SKawaiiPhysicsNodeAuditWindow::UnregisterTabSpawner();

		if (KawaiiPhysicsMenuGroup.IsValid())
		{
			WorkspaceMenu::GetMenuStructure().GetStructureRoot()->RemoveItem(KawaiiPhysicsMenuGroup.ToSharedRef());
			KawaiiPhysicsMenuGroup.Reset();
		}

		FKawaiiPhysicsEdStyle::Shutdown();
		bTabSpawnersRegistered = false;
	}

	if (FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(
		"PropertyEditor"))
	{
		PropertyEditorModule->UnregisterCustomClassLayout("KawaiiPhysicsPresetDataAsset");
		PropertyEditorModule->NotifyCustomizationModuleChanged();
	}

	FEditorModeRegistry::Get().UnregisterMode("AnimGraph.SkeletalControl.KawaiiPhysics");
}

void FKawaiiPhysicsEdModule::RegisterTabSpawners()
{
	if (bTabSpawnersRegistered || !GIsEditor || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FKawaiiPhysicsEdStyle::Initialize();

	const FSlateIcon KawaiiPhysicsIcon(
		FKawaiiPhysicsEdStyle::GetStyleSetName(),
		TEXT("KawaiiPhysics.TabIcon"));
	KawaiiPhysicsMenuGroup = WorkspaceMenu::GetMenuStructure().GetStructureRoot()->AddGroup(
		LOCTEXT("KawaiiPhysicsMenuGroup", "Kawaii Physics"),
		KawaiiPhysicsIcon,
		false);

	SKawaiiPhysicsWindScopeWindow::RegisterTabSpawner(KawaiiPhysicsMenuGroup.ToSharedRef());
	SKawaiiPhysicsPresetDiffWindow::RegisterTabSpawner(KawaiiPhysicsMenuGroup.ToSharedRef());
	SKawaiiPhysicsNodeAuditWindow::RegisterTabSpawner(KawaiiPhysicsMenuGroup.ToSharedRef());
	bTabSpawnersRegistered = true;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKawaiiPhysicsEdModule, KawaiiPhysicsEd)
//IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, KawaiiPhysicsEd, "KawaiiPhysicsEd" );
