// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest,
                                 "KawaiiPhysics.SharedCollision.SubsystemSupportsEditorPreview",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSharedCollisionSubsystemSupportsEditorPreviewTest::RunTest(const FString& Parameters)
{
	const UKawaiiPhysicsSharedCollisionSubsystem* CDO = GetDefault<UKawaiiPhysicsSharedCollisionSubsystem>();
	UWorld* World = NewObject<UWorld>(GetTransientPackage(), NAME_None, RF_Transient);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.EnableInPreviewWorld"));
	if (!TestNotNull(TEXT("EnableInPreviewWorld CVar exists"), CVar))
	{
		return false;
	}

	const int32 Saved = CVar->GetInt();
	ON_SCOPE_EXIT
	{
		CVar->Set(Saved, ECVF_SetByCode);
	};

	CVar->Set(1, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestTrue(TEXT("EditorPreview world creates subsystem by default"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Editor;
	TestTrue(TEXT("Editor world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::PIE;
	TestTrue(TEXT("PIE world creates subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::GamePreview;
	TestFalse(TEXT("GamePreview world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Inactive;
	TestFalse(TEXT("Inactive world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::None;
	TestFalse(TEXT("None world does not create subsystem"), CDO->ShouldCreateSubsystem(World));

	CVar->Set(0, ECVF_SetByCode);

	World->WorldType = EWorldType::EditorPreview;
	TestFalse(TEXT("EditorPreview world does not create subsystem when disabled by CVar"),
	          CDO->ShouldCreateSubsystem(World));

	World->WorldType = EWorldType::Game;
	TestTrue(TEXT("Game world still creates subsystem when preview CVar is disabled"),
	         CDO->ShouldCreateSubsystem(World));

	return true;
}

#endif
