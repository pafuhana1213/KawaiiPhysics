// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSettingsRegistrationTest,
                                 "KawaiiPhysics.Editor.EditorSettings.Registration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSettingsRegistrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UKawaiiPhysicsEditorSettings* Settings = GetDefault<UKawaiiPhysicsEditorSettings>();
	TestEqual(TEXT("Shown in Editor Preferences"), Settings->GetContainerName(), FName(TEXT("Editor")));
	TestEqual(TEXT("Listed under Plugins"), Settings->GetCategoryName(), FName(TEXT("Plugins")));
	TestEqual(TEXT("Saved per user"), UKawaiiPhysicsEditorSettings::StaticClass()->ClassConfigName,
	          FName(TEXT("EditorPerProjectUserSettings")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSettingsDefaultStyleTest,
                                 "KawaiiPhysics.Editor.EditorSettings.DefaultStyle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSettingsDefaultStyleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// ユーザーの ini に影響されないよう、設定クラスではなく構造体の既定値を確かめる
	const FKawaiiPhysicsCollisionDrawStyle Style;
	TestEqual(TEXT("AnimNode keeps the orange fill"), FLinearColor(Style.AnimNodeColor.R, Style.AnimNodeColor.G,
	                                                               Style.AnimNodeColor.B),
	          FLinearColor(FColor::Orange));
	TestEqual(TEXT("DataAsset keeps the blue fill"), FLinearColor(Style.DataAssetColor.R, Style.DataAssetColor.G,
	                                                              Style.DataAssetColor.B), FLinearColor::Blue);
	TestEqual(TEXT("Mirror keeps the red fill"), FLinearColor(Style.MirrorColor.R, Style.MirrorColor.G,
	                                                          Style.MirrorColor.B), FLinearColor::Red);
	TestEqual(TEXT("PhysicsAsset keeps its faint fill"), Style.PhysicsAssetColor.A, 0.2f);
	TestTrue(TEXT("Border is drawn by default"), Style.bDrawBorder);
	TestEqual(TEXT("Border is black by default"), Style.BorderColor, FLinearColor::Black);
	TestEqual(TEXT("Border is the thinnest line by default"), Style.BorderThickness, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSettingsLookupTest,
                                 "KawaiiPhysics.Editor.EditorSettings.Lookup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSettingsLookupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UKawaiiPhysicsEditorSettings* Settings = GetDefault<UKawaiiPhysicsEditorSettings>();
	TestEqual(TEXT("Sphere"), &Settings->GetCollisionDrawStyle(ECollisionLimitType::Spherical), &Settings->Sphere);
	TestEqual(TEXT("Capsule"), &Settings->GetCollisionDrawStyle(ECollisionLimitType::Capsule), &Settings->Capsule);
	TestEqual(TEXT("Tapered capsule"), &Settings->GetCollisionDrawStyle(ECollisionLimitType::TaperedCapsule),
	          &Settings->TaperedCapsule);
	TestEqual(TEXT("Box"), &Settings->GetCollisionDrawStyle(ECollisionLimitType::Box), &Settings->Box);
	TestEqual(TEXT("Planar"), &Settings->GetCollisionDrawStyle(ECollisionLimitType::Planar), &Settings->Planar);

	const FKawaiiPhysicsCollisionDrawStyle& Style = Settings->Capsule;
	TestEqual(TEXT("AnimNode"), &Style.GetColor(ECollisionSourceType::AnimNode), &Style.AnimNodeColor);
	TestEqual(TEXT("DataAsset"), &Style.GetColor(ECollisionSourceType::DataAsset), &Style.DataAssetColor);
	TestEqual(TEXT("PhysicsAsset"), &Style.GetColor(ECollisionSourceType::PhysicsAsset), &Style.PhysicsAssetColor);
	TestEqual(TEXT("Mirror"), &Style.GetColor(ECollisionSourceType::Mirror), &Style.MirrorColor);
	return true;
}

// Edit Mode は設定の色と不透明度をこのパラメータ経由で反映する。エンジン更新で消えると設定が無言で効かなくなる
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSettingsCollisionMaterialTest,
                                 "KawaiiPhysics.Editor.EditorSettings.CollisionMaterialParameters",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSettingsCollisionMaterialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UMaterialInterface* Material = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EditorMaterials/PhAT_ElemSelectedMaterial.PhAT_ElemSelectedMaterial"));
	if (!TestNotNull(TEXT("Engine collision material"), Material))
	{
		return false;
	}

	FLinearColor Color;
	float Opacity = 0.0f;
	TestTrue(TEXT("Has a color parameter"), Material->GetVectorParameterValue(TEXT("SelectionColor"), Color));
	TestTrue(TEXT("Has an opacity parameter"), Material->GetScalarParameterValue(TEXT("Opacity"), Opacity));
	return true;
}

#endif
