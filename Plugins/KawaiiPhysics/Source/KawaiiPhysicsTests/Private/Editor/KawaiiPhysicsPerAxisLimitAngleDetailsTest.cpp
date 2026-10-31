// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimNode_KawaiiPhysics.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/Package.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	TSharedPtr<IDetailTreeNode> FindPerAxisNode(const TArray<TSharedRef<IDetailTreeNode>>& Nodes)
	{
		for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
		{
			const TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
			if (Handle.IsValid() && Handle->GetProperty() &&
				Handle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FKawaiiPhysicsSettings, PerAxisLimitAngle))
			{
				return Node;
			}

			TArray<TSharedRef<IDetailTreeNode>> Children;
			Node->GetChildren(Children, true);
			if (TSharedPtr<IDetailTreeNode> Found = FindPerAxisNode(Children))
			{
				return Found;
			}
		}
		return nullptr;
	}

	FString FindFirstText(const TSharedPtr<SWidget>& Widget)
	{
		if (!Widget.IsValid())
		{
			return FString();
		}
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			return StaticCastSharedPtr<STextBlock>(Widget)->GetText().ToString();
		}

		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			const FString Text = FindFirstText(Children->GetChildAt(Index));
			if (!Text.IsEmpty())
			{
				return Text;
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitDetailsTest,
                                 "KawaiiPhysics.AngleLimit.Editor.DetailsRows",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitDetailsTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	Preset->Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	Preset->Node.BoneForwardAxis = EBoneForwardAxis::Y_Positive;

	FPropertyEditorModule& PropertyEditorModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const TSharedRef<IPropertyRowGenerator> Generator =
		PropertyEditorModule.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	Generator->SetObjects({Preset});

	const TSharedPtr<IDetailTreeNode> PerAxisNode = FindPerAxisNode(Generator->GetRootTreeNodes());
	if (!TestTrue(TEXT("Per axis row exists"), PerAxisNode.IsValid()))
	{
		return false;
	}

	TArray<TSharedRef<IDetailTreeNode>> AxisRows;
	PerAxisNode->GetChildren(AxisRows, true);
	if (!TestEqual(TEXT("One row per axis"), AxisRows.Num(), 3))
	{
		return false;
	}

	// カスタマイズの中で親ハンドルをたどって BoneForwardAxis に届いていれば、前方軸の行だけツイスト表示になる
	const TSharedPtr<SWidget> RowX = AxisRows[0]->CreateNodeWidgets().NameWidget;
	const TSharedPtr<SWidget> RowY = AxisRows[1]->CreateNodeWidgets().NameWidget;
	const TSharedPtr<SWidget> RowZ = AxisRows[2]->CreateNodeWidgets().NameWidget;
	TestEqual(TEXT("X is active when forward is Y"), FindFirstText(RowX), FString(TEXT("X")));
	TestEqual(TEXT("Y is the twist axis when forward is Y"), FindFirstText(RowY), FString(TEXT("Y (Twist: unused)")));
	TestEqual(TEXT("Z is active when forward is Y"), FindFirstText(RowZ), FString(TEXT("Z")));

	// BoneForwardAxis の変更は再構築なしで表示へ反映される
	Preset->Node.BoneForwardAxis = EBoneForwardAxis::Z_Negative;
	TestEqual(TEXT("Y becomes active after changing forward"), FindFirstText(RowY), FString(TEXT("Y")));
	TestEqual(TEXT("Z becomes the twist axis after changing forward"), FindFirstText(RowZ),
	          FString(TEXT("Z (Twist: unused)")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
