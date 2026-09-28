// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimationGraphSchema.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	// 同一モジュールの他テストとユニティビルドで衝突しないよう、ヘルパー名に AnimGraphInput を付ける
	struct FKawaiiPhysicsAnimGraphInputFixture
	{
		USkeleton* Skeleton = nullptr;
		UAnimBlueprint* AnimBlueprint = nullptr;
		UEdGraph* AnimGraph = nullptr;
		UAnimSequence* Animation = nullptr;
	};

	USkeleton* CreateAnimGraphInputTestSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("pelvis"), TEXT("pelvis"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_01"), TEXT("hair_01"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_02"), TEXT("hair_02"), 2), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("tail_01"), TEXT("tail_01"), 1), FTransform::Identity);
		return Skeleton;
	}

	UEdGraph* FindAnimGraphInputTestGraph(UAnimBlueprint* AnimBlueprint)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		TArray<UEdGraph*> Graphs;
		AnimBlueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	FKawaiiPhysicsAnimGraphInputFixture MakeAnimGraphInputFixture(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsAnimGraphInputFixture Fixture;

		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(TEXT("/Temp/KawaiiPhysicsAnimGraphInput_%s"), *UniqueSuffix);
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);

		const FName BlueprintName(*FString::Printf(TEXT("ABP_KawaiiPhysicsAnimGraphInput_%s"), *UniqueSuffix));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass());
		Fixture.AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		Test.TestNotNull(TEXT("Transient AnimBlueprint is created"), Fixture.AnimBlueprint);

		Fixture.Skeleton = CreateAnimGraphInputTestSkeleton(Package);
		if (Fixture.AnimBlueprint)
		{
			Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
		}

		Fixture.Animation = NewObject<UAnimSequence>(Package, TEXT("A_KawaiiPhysicsAnimGraphInput"));
		if (Fixture.Animation)
		{
			Fixture.Animation->SetSkeleton(Fixture.Skeleton);
		}
		Test.TestNotNull(TEXT("Transient AnimSequence is created"), Fixture.Animation);

		Fixture.AnimGraph = FindAnimGraphInputTestGraph(Fixture.AnimBlueprint);
		Test.TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);
		return Fixture;
	}

	bool IsAnimGraphInputFixtureValid(const FKawaiiPhysicsAnimGraphInputFixture& Fixture)
	{
		return Fixture.AnimBlueprint && Fixture.AnimGraph && Fixture.Skeleton && Fixture.Animation;
	}

	UEdGraphPin* GetAnimGraphInputResultPin(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
			{
				return RootNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_Root, Result), EGPD_Input);
			}
		}

		return nullptr;
	}

	UEdGraphPin* FindAnimGraphInputFirstPosePin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Dir && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphNode* GetAnimGraphInputLinkedNode(const UEdGraphPin* Pin)
	{
		return Pin && Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0] ? Pin->LinkedTo[0]->GetOwningNode() : nullptr;
	}

	TArray<UAnimGraphNode_SequencePlayer*> CollectAnimGraphInputSequencePlayers(UEdGraph* Graph)
	{
		TArray<UAnimGraphNode_SequencePlayer*> SequencePlayers;
		if (!Graph)
		{
			return SequencePlayers;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				SequencePlayers.Add(SequencePlayer);
			}
		}

		return SequencePlayers;
	}

	FKawaiiPhysicsNodePlacementRequest MakeAnimGraphInputAutoConnectRequest(FName RootBoneName)
	{
		FKawaiiPhysicsNodePlacementRequest Request;
		Request.RootBoneName = RootBoneName;
		Request.bAutoConnect = true;
		return Request;
	}

	// Result 側から ComponentToLocalSpace -> KawaiiPhysics -> LocalToComponentSpace -> SequencePlayer の順に繋がっているかを検証する
	bool TestAnimGraphInputPlayerFeedsKawaiiPhysics(
		FAutomationTestBase& Test,
		const FString& Context,
		UEdGraph* Graph,
		UAnimGraphNode_KawaiiPhysics* KawaiiNode,
		UAnimGraphNode_SequencePlayer* SequencePlayer)
	{
		bool bOk = true;
		UEdGraphPin* ResultPin = GetAnimGraphInputResultPin(Graph);
		UEdGraphNode* ResultSourceNode = GetAnimGraphInputLinkedNode(ResultPin);
		bOk &= Test.TestTrue(
			*FString::Printf(TEXT("%s: Result is fed by ComponentToLocalSpace"), *Context),
			ResultSourceNode && ResultSourceNode->IsA<UAnimGraphNode_ComponentToLocalSpace>());

		UEdGraphPin* ConversionInputPin = FindAnimGraphInputFirstPosePin(ResultSourceNode, EGPD_Input);
		bOk &= Test.TestTrue(
			*FString::Printf(TEXT("%s: ComponentToLocalSpace is fed by the KawaiiPhysics node"), *Context),
			KawaiiNode && GetAnimGraphInputLinkedNode(ConversionInputPin) == KawaiiNode);

		UEdGraphPin* KawaiiComponentPosePin = KawaiiNode
			? KawaiiNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_SkeletalControlBase, ComponentPose), EGPD_Input)
			: nullptr;
		UEdGraphNode* KawaiiInputNode = GetAnimGraphInputLinkedNode(KawaiiComponentPosePin);
		bOk &= Test.TestTrue(
			*FString::Printf(TEXT("%s: KawaiiPhysics ComponentPose is fed by LocalToComponentSpace"), *Context),
			KawaiiInputNode && KawaiiInputNode->IsA<UAnimGraphNode_LocalToComponentSpace>());

		UEdGraphPin* LocalToComponentInputPin = FindAnimGraphInputFirstPosePin(KawaiiInputNode, EGPD_Input);
		bOk &= Test.TestTrue(
			*FString::Printf(TEXT("%s: LocalToComponentSpace is fed by the SequencePlayer"), *Context),
			SequencePlayer && GetAnimGraphInputLinkedNode(LocalToComponentInputPin) == SequencePlayer);
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphInputPlayerFirstTest,
                                 "KawaiiPhysics.EditorScripting.AnimGraphInput.PlayerFirst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphInputPlayerFirstTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsAnimGraphInputFixture Fixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(Fixture))
	{
		return false;
	}

	bool bOk = true;
	bOk &= TestTrue(TEXT("SetAnimGraphInputAnimation succeeds on a fresh AnimBlueprint"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, Fixture.Animation));

	TArray<UAnimGraphNode_SequencePlayer*> SequencePlayers = CollectAnimGraphInputSequencePlayers(Fixture.AnimGraph);
	bOk &= TestEqual(TEXT("One SequencePlayer is added"), SequencePlayers.Num(), 1);
	UAnimGraphNode_SequencePlayer* SequencePlayer = SequencePlayers.IsValidIndex(0) ? SequencePlayers[0] : nullptr;
	bOk &= TestTrue(TEXT("SequencePlayer references the animation"),
	                SequencePlayer && SequencePlayer->GetAnimationAsset() == Fixture.Animation);
	bOk &= TestTrue(TEXT("SequencePlayer feeds Result directly"),
	                SequencePlayer && GetAnimGraphInputLinkedNode(GetAnimGraphInputResultPin(Fixture.AnimGraph)) == SequencePlayer);

	// 後から AutoConnect で追加した KawaiiPhysics ノードはプレイヤーと Result の間へ挿入される
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("hair_01")));
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);
	bOk &= TestEqual(TEXT("One KawaiiPhysics node is added after the SequencePlayer"), Handles.Num(), 1);
	UAnimGraphNode_KawaiiPhysics* KawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;

	bOk &= TestAnimGraphInputPlayerFeedsKawaiiPhysics(
		*this, TEXT("PlayerFirst"), Fixture.AnimGraph, KawaiiNode, SequencePlayer);
	bOk &= TestEqual(TEXT("Still one SequencePlayer after inserting KawaiiPhysics"),
	                 CollectAnimGraphInputSequencePlayers(Fixture.AnimGraph).Num(), 1);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphInputKawaiiPhysicsFirstTest,
                                 "KawaiiPhysics.EditorScripting.AnimGraphInput.KawaiiPhysicsFirst",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphInputKawaiiPhysicsFirstTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsAnimGraphInputFixture Fixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(Fixture))
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("hair_01")));
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("tail_01")));
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Two KawaiiPhysics nodes are added first"), Handles.Num(), 2);
	// AutoConnect のチェーンはリクエスト順が上流→下流なので、先頭リクエストが最上流になる
	UAnimGraphNode_KawaiiPhysics* UpstreamKawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;

	bOk &= TestTrue(TEXT("SetAnimGraphInputAnimation succeeds after KawaiiPhysics nodes"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, Fixture.Animation));

	TArray<UAnimGraphNode_SequencePlayer*> SequencePlayers = CollectAnimGraphInputSequencePlayers(Fixture.AnimGraph);
	bOk &= TestEqual(TEXT("One SequencePlayer is added"), SequencePlayers.Num(), 1);
	UAnimGraphNode_SequencePlayer* SequencePlayer = SequencePlayers.IsValidIndex(0) ? SequencePlayers[0] : nullptr;
	bOk &= TestTrue(TEXT("SequencePlayer references the animation"),
	                SequencePlayer && SequencePlayer->GetAnimationAsset() == Fixture.Animation);

	UEdGraphPin* UpstreamComponentPosePin = UpstreamKawaiiNode
		? UpstreamKawaiiNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_SkeletalControlBase, ComponentPose), EGPD_Input)
		: nullptr;
	UEdGraphNode* UpstreamInputNode = GetAnimGraphInputLinkedNode(UpstreamComponentPosePin);
	bOk &= TestTrue(TEXT("Most upstream KawaiiPhysics ComponentPose is fed by LocalToComponentSpace"),
	                UpstreamInputNode && UpstreamInputNode->IsA<UAnimGraphNode_LocalToComponentSpace>());
	bOk &= TestTrue(TEXT("LocalToComponentSpace is fed by the SequencePlayer"),
	                SequencePlayer &&
	                GetAnimGraphInputLinkedNode(FindAnimGraphInputFirstPosePin(UpstreamInputNode, EGPD_Input)) == SequencePlayer);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphInputReuseTest,
                                 "KawaiiPhysics.EditorScripting.AnimGraphInput.ReuseExistingPlayer",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphInputReuseTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsAnimGraphInputFixture Fixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(Fixture))
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("hair_01")));
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);
	UAnimGraphNode_KawaiiPhysics* KawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;

	UAnimSequence* SecondAnimation = NewObject<UAnimSequence>(
		Fixture.AnimBlueprint->GetOutermost(), TEXT("A_KawaiiPhysicsAnimGraphInputSecond"));
	SecondAnimation->SetSkeleton(Fixture.Skeleton);

	bool bOk = true;
	bOk &= TestTrue(TEXT("First SetAnimGraphInputAnimation succeeds"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, Fixture.Animation));
	const int32 NodeCountAfterFirstCall = Fixture.AnimGraph->Nodes.Num();
	bOk &= TestTrue(TEXT("Second SetAnimGraphInputAnimation succeeds"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, SecondAnimation));

	TArray<UAnimGraphNode_SequencePlayer*> SequencePlayers = CollectAnimGraphInputSequencePlayers(Fixture.AnimGraph);
	bOk &= TestEqual(TEXT("Calling twice keeps a single SequencePlayer"), SequencePlayers.Num(), 1);
	bOk &= TestEqual(TEXT("Calling twice does not add nodes"), Fixture.AnimGraph->Nodes.Num(), NodeCountAfterFirstCall);
	UAnimGraphNode_SequencePlayer* SequencePlayer = SequencePlayers.IsValidIndex(0) ? SequencePlayers[0] : nullptr;
	bOk &= TestTrue(TEXT("Existing SequencePlayer now references the second animation"),
	                SequencePlayer && SequencePlayer->GetAnimationAsset() == SecondAnimation);
	bOk &= TestAnimGraphInputPlayerFeedsKawaiiPhysics(
		*this, TEXT("ReuseExistingPlayer"), Fixture.AnimGraph, KawaiiNode, SequencePlayer);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphInputCompileMessagesTest,
                                 "KawaiiPhysics.EditorScripting.CompileAnimBlueprintWithMessages.Valid",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphInputCompileMessagesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsAnimGraphInputFixture Fixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(Fixture))
	{
		return false;
	}

	bool bOk = true;
	bOk &= TestTrue(TEXT("SetAnimGraphInputAnimation succeeds before compiling"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, Fixture.Animation));
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("hair_01")));
	bOk &= TestEqual(TEXT("One KawaiiPhysics node is added before compiling"),
	                 UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests).Num(), 1);

	TArray<FString> Messages;
	const int32 ErrorCount = UKawaiiPhysicsEditorLibrary::CompileAnimBlueprintWithMessages(Fixture.AnimBlueprint, Messages);
	bOk &= TestEqual(TEXT("Valid AnimBlueprint compiles without errors"), ErrorCount, 0);
	for (const FString& Message : Messages)
	{
		bOk &= TestFalse(*FString::Printf(TEXT("Compiler message is not an error: %s"), *Message),
		                 Message.StartsWith(TEXT("Error:")));
	}

	TArray<FString> NullMessages;
	bOk &= TestEqual(TEXT("Null AnimBlueprint returns INDEX_NONE"),
	                 UKawaiiPhysicsEditorLibrary::CompileAnimBlueprintWithMessages(nullptr, NullMessages),
	                 static_cast<int32>(INDEX_NONE));
	bOk &= TestTrue(TEXT("Null AnimBlueprint returns no messages"), NullMessages.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphInputPoseTest,
                                 "KawaiiPhysics.EditorScripting.AnimGraphInput.InputPose",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphInputPoseTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsAnimGraphInputFixture Fixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(Fixture))
	{
		return false;
	}

	bool bOk = true;
	FString Error;
	bOk &= TestEqual(TEXT("Input Pose is connected to a fresh AnimGraph"),
	                 UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(Fixture.AnimBlueprint, Error), 1);
	bOk &= TestTrue(TEXT("Fresh Input Pose reports no error"), Error.IsEmpty());
	UAnimGraphNode_LinkedInputPose* InputPose = Cast<UAnimGraphNode_LinkedInputPose>(
		GetAnimGraphInputLinkedNode(GetAnimGraphInputResultPin(Fixture.AnimGraph)));
	bOk &= TestNotNull(TEXT("Result is fed by Input Pose"), InputPose);
	if (InputPose)
	{
		bOk &= TestEqual(TEXT("Input Pose is named InPose"), InputPose->Node.Name,
		                 FAnimNode_LinkedInputPose::DefaultInputPoseName);
	}
	bOk &= TestTrue(TEXT("Connected InPose is reported as connected"),
	                UKawaiiPhysicsEditorLibrary::IsAnimGraphInputPoseConnected(Fixture.AnimBlueprint));

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAnimGraphInputAutoConnectRequest(TEXT("hair_01")));
	bOk &= TestEqual(TEXT("KawaiiPhysics node is added"),
	                 UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests).Num(), 1);
	TArray<FString> Messages;
	bOk &= TestEqual(TEXT("Input Pose chain compiles without errors"),
	                 UKawaiiPhysicsEditorLibrary::CompileAnimBlueprintWithMessages(Fixture.AnimBlueprint, Messages), 0);

	FKawaiiPhysicsAnimGraphInputFixture ReplacementFixture = MakeAnimGraphInputFixture(*this);
	if (!IsAnimGraphInputFixtureValid(ReplacementFixture))
	{
		return false;
	}
	bOk &= TestTrue(TEXT("SequencePlayer is connected before replacement"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(
		                ReplacementFixture.AnimBlueprint, ReplacementFixture.Animation));
	bOk &= TestEqual(TEXT("One SequencePlayer exists before replacement"),
	                 CollectAnimGraphInputSequencePlayers(ReplacementFixture.AnimGraph).Num(), 1);
	bOk &= TestFalse(TEXT("SequencePlayer input is not reported as a connected InPose"),
	                 UKawaiiPhysicsEditorLibrary::IsAnimGraphInputPoseConnected(ReplacementFixture.AnimBlueprint));
	bOk &= TestEqual(TEXT("SequencePlayer is replaced with a new Input Pose"),
	                 UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(ReplacementFixture.AnimBlueprint, Error), 1);
	bOk &= TestEqual(TEXT("SequencePlayer is removed"),
	                 CollectAnimGraphInputSequencePlayers(ReplacementFixture.AnimGraph).Num(), 0);
	UAnimGraphNode_LinkedInputPose* ReplacementPose = Cast<UAnimGraphNode_LinkedInputPose>(
		GetAnimGraphInputLinkedNode(GetAnimGraphInputResultPin(ReplacementFixture.AnimGraph)));
	bOk &= TestNotNull(TEXT("Replacement Input Pose feeds Result"), ReplacementPose);
	const int32 NodeCount = ReplacementFixture.AnimGraph->Nodes.Num();
	bOk &= TestEqual(TEXT("Calling Input Pose setter twice creates no node"),
	                 UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(ReplacementFixture.AnimBlueprint, Error), 0);
	bOk &= TestTrue(TEXT("Second call reports no error"), Error.IsEmpty());
	bOk &= TestEqual(TEXT("Calling twice does not add nodes"), ReplacementFixture.AnimGraph->Nodes.Num(), NodeCount);
	bOk &= TestTrue(TEXT("Calling twice keeps the same Input Pose"),
	                GetAnimGraphInputLinkedNode(GetAnimGraphInputResultPin(ReplacementFixture.AnimGraph)) == ReplacementPose);
	if (ReplacementPose)
	{
		ReplacementPose->Node.Name = TEXT("OtherPose");
		Error.Reset();
		bOk &= TestEqual(TEXT("Connected Input Pose with another name is rejected"),
		                 UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(ReplacementFixture.AnimBlueprint, Error), -1);
		bOk &= TestFalse(TEXT("Connected Input Pose with another name reports a reason"), Error.IsEmpty());
		bOk &= TestFalse(TEXT("Connected Input Pose with another name is not reported as InPose"),
		                 UKawaiiPhysicsEditorLibrary::IsAnimGraphInputPoseConnected(ReplacementFixture.AnimBlueprint));
	}
	Error.Reset();
	bOk &= TestEqual(TEXT("Null AnimBlueprint is rejected"),
	                 UKawaiiPhysicsEditorLibrary::SetAnimGraphInputPose(nullptr, Error), -1);
	bOk &= TestFalse(TEXT("Null AnimBlueprint reports a reason"), Error.IsEmpty());
	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
