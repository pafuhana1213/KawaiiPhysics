// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsEd.h"
#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "KawaiiPhysicsEditMode.h"
#include "KawaiiPhysicsPresetDataAssetDetails.h"
#include "PropertyEditorModule.h"
#include "SKawaiiPhysicsNodeAuditWindow.h"
#include "SKawaiiPhysicsPresetDiffWindow.h"
#include "SKawaiiPhysicsWindScopeWindow.h"

#define LOCTEXT_NAMESPACE "FKawaiiPhysicsModuleEd"


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
}


void FKawaiiPhysicsEdModule::ShutdownModule()
{
	SKawaiiPhysicsWindScopeWindow::CloseAllWindows();
	SKawaiiPhysicsPresetDiffWindow::CloseAllWindows();
	SKawaiiPhysicsNodeAuditWindow::CloseAllWindows();

	if (FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(
		"PropertyEditor"))
	{
		PropertyEditorModule->UnregisterCustomClassLayout("KawaiiPhysicsPresetDataAsset");
		PropertyEditorModule->NotifyCustomizationModuleChanged();
	}

	FEditorModeRegistry::Get().UnregisterMode("AnimGraph.SkeletalControl.KawaiiPhysics");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKawaiiPhysicsEdModule, KawaiiPhysicsEd)
//IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, KawaiiPhysicsEd, "KawaiiPhysicsEd" );
