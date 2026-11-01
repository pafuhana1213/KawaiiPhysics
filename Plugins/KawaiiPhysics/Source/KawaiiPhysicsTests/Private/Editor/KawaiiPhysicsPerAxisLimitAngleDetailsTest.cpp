// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"

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

	void CollectButtons(const TSharedPtr<SWidget>& Widget, TArray<TSharedPtr<SButton>>& OutButtons)
	{
		if (!Widget.IsValid())
		{
			return;
		}
		if (Widget->GetType() == TEXT("SButton"))
		{
			OutButtons.Add(StaticCastSharedPtr<SButton>(Widget));
		}

		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectButtons(Children->GetChildAt(Index), OutButtons);
		}
	}

	// 表示順と同じ X, Y, Z の順で値ウィジェットを返す
	bool CollectAxisValueWidgets(FAutomationTestBase& Test, const TSharedRef<IPropertyRowGenerator>& Generator,
	                             TArray<TSharedPtr<SWidget>>& OutWidgets)
	{
		const TSharedPtr<IDetailTreeNode> PerAxisNode = FindPerAxisNode(Generator->GetRootTreeNodes());
		if (!Test.TestTrue(TEXT("Per axis row exists"), PerAxisNode.IsValid()))
		{
			return false;
		}

		TArray<TSharedRef<IDetailTreeNode>> AxisRows;
		PerAxisNode->GetChildren(AxisRows, true);
		if (!Test.TestEqual(TEXT("One row per axis"), AxisRows.Num(), 3))
		{
			return false;
		}

		for (const TSharedRef<IDetailTreeNode>& Row : AxisRows)
		{
			OutWidgets.Add(Row->CreateNodeWidgets().ValueWidget);
		}
		return true;
	}

	TSharedRef<IPropertyRowGenerator> MakeGenerator(UObject* Object)
	{
		FPropertyEditorModule& PropertyEditorModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		const TSharedRef<IPropertyRowGenerator> Generator =
			PropertyEditorModule.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetObjects({Object});
		return Generator;
	}

	// IsEnabled は Slate 属性でプリパス時に更新されるため、描画しないテストでは先にプリパスを通す
	bool IsRowEnabled(const TSharedPtr<SWidget>& Row)
	{
		Row->SlatePrepass(1.0f);
		return Row->IsEnabled();
	}

	// 親ハンドルをたどって BoneForwardAxis に届いていれば、前方軸の行だけ無効になる。変更は再構築なしで反映される
	void TestTwistRow(FAutomationTestBase& Test, const TArray<TSharedPtr<SWidget>>& Rows,
	                  EBoneForwardAxis& BoneForwardAxis)
	{
		BoneForwardAxis = EBoneForwardAxis::Y_Positive;
		Test.TestTrue(TEXT("X is active when forward is Y"), IsRowEnabled(Rows[0]));
		Test.TestFalse(TEXT("Y is the twist axis when forward is Y"), IsRowEnabled(Rows[1]));
		Test.TestTrue(TEXT("Z is active when forward is Y"), IsRowEnabled(Rows[2]));

		BoneForwardAxis = EBoneForwardAxis::Z_Negative;
		Test.TestTrue(TEXT("Y becomes active after changing forward"), IsRowEnabled(Rows[1]));
		Test.TestFalse(TEXT("Z becomes the twist axis after changing forward"), IsRowEnabled(Rows[2]));
	}

	UEdGraph* FindPerAxisLimitAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Contains(TEXT("AnimGraph")))
			{
				return Graph;
			}
		}
		return Graphs.IsEmpty() ? nullptr : Graphs[0];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitDetailsTest,
                                 "KawaiiPhysics.AngleLimit.Editor.DetailsRows",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitDetailsTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	Preset->SetFlags(RF_Transactional);
	Preset->Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	Preset->Node.BoneForwardAxis = EBoneForwardAxis::Y_Positive;

	const TSharedRef<IPropertyRowGenerator> Generator = MakeGenerator(Preset);
	TArray<TSharedPtr<SWidget>> Rows;
	if (!CollectAxisValueWidgets(*this, Generator, Rows))
	{
		return false;
	}

	TestTwistRow(*this, Rows, Preset->Node.BoneForwardAxis);

	// 行の末尾に Lock、Free の順でボタンが並ぶ
	TArray<TSharedPtr<SButton>> Buttons;
	CollectButtons(Rows[2], Buttons);
	if (!TestTrue(TEXT("Axis row has Lock and Free buttons"), Buttons.Num() >= 2))
	{
		return false;
	}
	const TSharedPtr<SButton> LockButton = Buttons[Buttons.Num() - 2];
	const TSharedPtr<SButton> FreeButton = Buttons[Buttons.Num() - 1];
	const FKawaiiPhysicsAxisAngleRange& RangeZ = Preset->Node.PhysicsSettings.PerAxisLimitAngle.Z;

	LockButton->SimulateClick();
	TestEqual(TEXT("Lock sets Min to 0"), RangeZ.Min, 0.0f);
	TestEqual(TEXT("Lock sets Max to 0"), RangeZ.Max, 0.0f);

	FreeButton->SimulateClick();
	TestEqual(TEXT("Free sets Min to -180"), RangeZ.Min, -180.0f);
	TestEqual(TEXT("Free sets Max to 180"), RangeZ.Max, 180.0f);

	// Min と Max の両方が1回の Undo で戻る
	if (TestNotNull(TEXT("Editor exists"), GEditor))
	{
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo restores Min"), RangeZ.Min, 0.0f);
		TestEqual(TEXT("Undo restores Max"), RangeZ.Max, 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPerAxisLimitGraphNodeDetailsTest,
                                 "KawaiiPhysics.AngleLimit.Editor.GraphNodeDetailsRows",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPerAxisLimitGraphNodeDetailsTest::RunTest(const FString& Parameters)
{
	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/KawaiiPhysicsPerAxisDetails_%s"), *UniqueSuffix));
	Package->SetFlags(RF_Transient);

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UAnimInstance::StaticClass(), Package,
		FName(*FString::Printf(TEXT("ABP_KawaiiPhysicsPerAxisDetails_%s"), *UniqueSuffix)),
		BPTYPE_Normal, UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass()));
	if (!TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint))
	{
		return false;
	}

	UEdGraph* AnimGraph = FindPerAxisLimitAnimGraph(AnimBlueprint);
	if (!TestNotNull(TEXT("AnimGraph is found"), AnimGraph))
	{
		return false;
	}

	FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*AnimGraph);
	UAnimGraphNode_KawaiiPhysics* GraphNode = NodeCreator.CreateNode(false);
	GraphNode->Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
	NodeCreator.Finalize();

	const TSharedRef<IPropertyRowGenerator> Generator = MakeGenerator(GraphNode);
	TArray<TSharedPtr<SWidget>> Rows;
	if (!CollectAxisValueWidgets(*this, Generator, Rows))
	{
		return false;
	}

	TestTwistRow(*this, Rows, GraphNode->Node.BoneForwardAxis);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
