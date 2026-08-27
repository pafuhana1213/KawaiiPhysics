// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEd.h"

#include "CoreGlobals.h"
#include "EditorModeRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "KawaiiPhysicsEdStyle.h"
#include "KawaiiPhysicsEditorTabFactories.h"
#include "KawaiiPhysicsEditMode.h"
#include "KawaiiPhysicsPresetDataAssetDetails.h"
#include "ISequencerModule.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Modules/ModuleManager.h"
#include "PersonaModule.h"
#include "PropertyEditorModule.h"
#include "Sequencer/KawaiiPhysicsSettingsMultiplierTrackEditor.h"
#include "SKawaiiPhysicsNodeAuditWindow.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "SKawaiiPhysicsWindScopeWindow.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

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
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerTrackEditorHandle = SequencerModule.RegisterTrackEditor(
			FOnCreateTrackEditor::CreateStatic(&FKawaiiPhysicsSettingsMultiplierTrackEditor::CreateTrackEditor));

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
		if (FPersonaModule* PersonaModule = FModuleManager::GetModulePtr<FPersonaModule>("Persona"))
		{
			PersonaModule->OnRegisterTabs().Remove(PersonaRegisterTabsHandle);
		}
		PersonaRegisterTabsHandle.Reset();

		SKawaiiPhysicsNodeAuditWindow::UnregisterTabSpawner();

		FKawaiiPhysicsEdStyle::Shutdown();
		bTabSpawnersRegistered = false;
	}

	if (SequencerTrackEditorHandle.IsValid())
	{
		if (ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer"))
		{
			SequencerModule->UnRegisterTrackEditor(SequencerTrackEditorHandle);
		}
		SequencerTrackEditorHandle.Reset();
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

	// ABP エディタごとの Window メニューへ WorkflowTabFactory 経由で登録する
	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	PersonaRegisterTabsHandle = PersonaModule.OnRegisterTabs().AddRaw(
		this,
		&FKawaiiPhysicsEdModule::HandleRegisterPersonaTabs);

	// プリセット DataAsset 詳細から開くため、Node Audit だけはグローバルの Hidden Nomad タブを残す
	SKawaiiPhysicsNodeAuditWindow::RegisterTabSpawner();

	bTabSpawnersRegistered = true;
}

void FKawaiiPhysicsEdModule::HandleRegisterPersonaTabs(
	FWorkflowAllowedTabSet& TabSet,
	TSharedPtr<FAssetEditorToolkit> InHostingApp)
{
	if (!InHostingApp.IsValid() || InHostingApp->GetToolkitFName() != FName(TEXT("AnimationBlueprintEditor")))
	{
		return;
	}

	TabSet.RegisterFactory(MakeShared<FKawaiiPhysicsWindScopeTabFactory>(InHostingApp));
	TabSet.RegisterFactory(MakeShared<FKawaiiPhysicsPresetDiffTabFactory>(InHostingApp));
	TabSet.RegisterFactory(MakeShared<FKawaiiPhysicsNodeAuditTabFactory>(InHostingApp));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKawaiiPhysicsEdModule, KawaiiPhysicsEd)
//IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, KawaiiPhysicsEd, "KawaiiPhysicsEd" );
