// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimationGraphSchema.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	// 同一モジュールの他テストとユニティビルドで衝突しないよう、ヘルパー名に Layout を付ける
	struct FKawaiiPhysicsLayoutTestFixture
	{
		USkeleton* Skeleton = nullptr;
		UAnimBlueprint* AnimBlueprint = nullptr;
		UEdGraph* AnimGraph = nullptr;
		UAnimSequence* Animation = nullptr;
	};

	// ライブラリ側の推定幅（KawaiiPhysics 400 / 空間変換 160 / その他 300）に合わせる
	constexpr int32 KawaiiPhysicsLayoutTestKawaiiPhysicsWidth = 400;
	constexpr int32 KawaiiPhysicsLayoutTestKawaiiPhysicsHeight = 260;
	constexpr int32 KawaiiPhysicsLayoutTestConversionWidth = 160;
	constexpr int32 KawaiiPhysicsLayoutTestOtherWidth = 300;
	constexpr int32 KawaiiPhysicsLayoutTestCommentTitleMargin = 80;

	FKawaiiPhysicsLayoutTestFixture MakeLayoutTestFixture(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsLayoutTestFixture Fixture;

		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(TEXT("/Temp/KawaiiPhysicsAnimGraphLayout_%s"), *UniqueSuffix);
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);

		const FName BlueprintName(*FString::Printf(TEXT("ABP_KawaiiPhysicsAnimGraphLayout_%s"), *UniqueSuffix));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass());
		Fixture.AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		Test.TestNotNull(TEXT("Transient AnimBlueprint is created"), Fixture.AnimBlueprint);

		Fixture.Skeleton = NewObject<USkeleton>(Package);
		{
			FReferenceSkeletonModifier Modifier(Fixture.Skeleton);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("pelvis"), TEXT("pelvis"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("hair_01"), TEXT("hair_01"), 1), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("hair_02"), TEXT("hair_02"), 2), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("tail_01"), TEXT("tail_01"), 1), FTransform::Identity);
		}
		if (Fixture.AnimBlueprint)
		{
			Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
		}

		Fixture.Animation = NewObject<UAnimSequence>(Package, TEXT("A_KawaiiPhysicsAnimGraphLayout"));
		if (Fixture.Animation)
		{
			Fixture.Animation->SetSkeleton(Fixture.Skeleton);
		}
		Test.TestNotNull(TEXT("Transient AnimSequence is created"), Fixture.Animation);

		if (Fixture.AnimBlueprint)
		{
			TArray<UEdGraph*> Graphs;
			Fixture.AnimBlueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
				{
					Fixture.AnimGraph = Graph;
					break;
				}
			}
		}
		Test.TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);
		return Fixture;
	}

	UAnimGraphNode_Root* FindLayoutTestRootNode(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
			{
				return RootNode;
			}
		}
		return nullptr;
	}

	// Result から先頭の接続済みポーズ入力を辿ったチェーン（下流→上流、先頭は Result）
	TArray<UEdGraphNode*> CollectLayoutTestChain(UEdGraph* Graph)
	{
		TArray<UEdGraphNode*> Chain;
		UEdGraphNode* CurrentNode = FindLayoutTestRootNode(Graph);
		while (CurrentNode && !Chain.Contains(CurrentNode))
		{
			Chain.Add(CurrentNode);
			UEdGraphNode* SourceNode = nullptr;
			for (UEdGraphPin* Pin : CurrentNode->Pins)
			{
				if (Pin &&
					Pin->Direction == EGPD_Input &&
					UAnimationGraphSchema::IsPosePin(Pin->PinType) &&
					!Pin->LinkedTo.IsEmpty() &&
					Pin->LinkedTo[0])
				{
					SourceNode = Pin->LinkedTo[0]->GetOwningNode();
					break;
				}
			}
			CurrentNode = SourceNode;
		}
		return Chain;
	}

	int32 GetLayoutTestExpectedWidth(const UEdGraphNode* Node)
	{
		if (Node && Node->IsA<UAnimGraphNode_KawaiiPhysics>())
		{
			return KawaiiPhysicsLayoutTestKawaiiPhysicsWidth;
		}
		if (Node && (Node->IsA<UAnimGraphNode_ComponentToLocalSpace>() || Node->IsA<UAnimGraphNode_LocalToComponentSpace>()))
		{
			return KawaiiPhysicsLayoutTestConversionWidth;
		}
		return KawaiiPhysicsLayoutTestOtherWidth;
	}

	// チェーンが Result の行に上流から下流へ左から右に並び、推定幅で重ならないことを確認する
	bool TestLayoutTestChainOnOneRow(FAutomationTestBase& Test,
	                                 const FString& Context,
	                                 const TArray<UEdGraphNode*>& Chain,
	                                 const FIntPoint& ExpectedRootPosition)
	{
		bool bOk = true;
		if (Chain.IsEmpty())
		{
			return Test.TestTrue(*FString::Printf(TEXT("%s: chain is not empty"), *Context), false);
		}

		bOk &= Test.TestEqual(*FString::Printf(TEXT("%s: Result NodePosX is kept"), *Context),
		                      Chain[0]->NodePosX, ExpectedRootPosition.X);
		bOk &= Test.TestEqual(*FString::Printf(TEXT("%s: Result NodePosY is kept"), *Context),
		                      Chain[0]->NodePosY, ExpectedRootPosition.Y);
		for (int32 ChainIndex = 1; ChainIndex < Chain.Num(); ++ChainIndex)
		{
			const UEdGraphNode* UpstreamNode = Chain[ChainIndex];
			const UEdGraphNode* DownstreamNode = Chain[ChainIndex - 1];
			bOk &= Test.TestEqual(
				*FString::Printf(TEXT("%s: chain node %d is on the Result row"), *Context, ChainIndex),
				UpstreamNode->NodePosY, ExpectedRootPosition.Y);
			bOk &= Test.TestTrue(
				*FString::Printf(TEXT("%s: chain node %d has a positive gap to its downstream node"), *Context, ChainIndex),
				UpstreamNode->NodePosX + GetLayoutTestExpectedWidth(UpstreamNode) < DownstreamNode->NodePosX);
			bOk &= Test.TestTrue(
				*FString::Printf(TEXT("%s: chain node %d does not overlap its downstream node"), *Context, ChainIndex),
				UpstreamNode->NodePosX + GetLayoutTestExpectedWidth(UpstreamNode) <= DownstreamNode->NodePosX);
		}
		return bOk;
	}

	const FKawaiiPhysicsAnimGraphCommentInfo* FindLayoutTestComment(
		const TArray<FKawaiiPhysicsAnimGraphCommentInfo>& CommentInfos,
		const FString& Title)
	{
		for (const FKawaiiPhysicsAnimGraphCommentInfo& CommentInfo : CommentInfos)
		{
			if (CommentInfo.Title == Title)
			{
				return &CommentInfo;
			}
		}
		return nullptr;
	}

	// コメント枠が KawaiiPhysics ノードをタイトル余白付きで囲み、チェーン上の他のノードには掛からないことを確認する
	bool TestLayoutTestCommentEncloses(FAutomationTestBase& Test,
	                                   const FString& Context,
	                                   UAnimBlueprint* AnimBlueprint,
	                                   const FString& Title,
	                                   const TArray<UEdGraphNode*>& KawaiiPhysicsNodes,
	                                   const TArray<UEdGraphNode*>& OtherChainNodes)
	{
		const TArray<FKawaiiPhysicsAnimGraphCommentInfo> CommentInfos =
			UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(AnimBlueprint);
		const FKawaiiPhysicsAnimGraphCommentInfo* CommentInfo = FindLayoutTestComment(CommentInfos, Title);
		bool bOk = Test.TestNotNull(*FString::Printf(TEXT("%s: MCP comment info is found"), *Context), CommentInfo);
		if (!CommentInfo)
		{
			return false;
		}

		const UEdGraphNode_Comment* CommentNode = CommentInfo->CommentNode;
		bOk &= Test.TestNotNull(*FString::Printf(TEXT("%s: comment info references the comment node"), *Context),
		                        CommentNode);
		if (CommentNode)
		{
			bOk &= Test.TestTrue(
				*FString::Printf(TEXT("%s: comment info position matches the node"), *Context),
				CommentInfo->NodePosition.Equals(FVector2D(static_cast<double>(CommentNode->NodePosX), static_cast<double>(CommentNode->NodePosY))));
			bOk &= Test.TestTrue(
				*FString::Printf(TEXT("%s: comment info size matches the node"), *Context),
				CommentInfo->NodeSize.Equals(FVector2D(static_cast<double>(CommentNode->NodeWidth), static_cast<double>(CommentNode->NodeHeight))));
		}

		const double CommentMinX = CommentInfo->NodePosition.X;
		const double CommentMinY = CommentInfo->NodePosition.Y;
		const double CommentMaxX = CommentMinX + CommentInfo->NodeSize.X;
		const double CommentMaxY = CommentMinY + CommentInfo->NodeSize.Y;
		for (int32 NodeIndex = 0; NodeIndex < KawaiiPhysicsNodes.Num(); ++NodeIndex)
		{
			const UEdGraphNode* Node = KawaiiPhysicsNodes[NodeIndex];
			bOk &= Test.TestTrue(
				*FString::Printf(TEXT("%s: comment encloses KawaiiPhysics node %d"), *Context, NodeIndex),
				Node &&
				CommentMinX <= Node->NodePosX &&
				CommentMinY <= Node->NodePosY - KawaiiPhysicsLayoutTestCommentTitleMargin &&
				CommentMaxX >= Node->NodePosX + KawaiiPhysicsLayoutTestKawaiiPhysicsWidth &&
				CommentMaxY >= Node->NodePosY + KawaiiPhysicsLayoutTestKawaiiPhysicsHeight);
		}

		for (int32 NodeIndex = 0; NodeIndex < OtherChainNodes.Num(); ++NodeIndex)
		{
			const UEdGraphNode* Node = OtherChainNodes[NodeIndex];
			const bool bOverlapsX = Node &&
				CommentMinX < Node->NodePosX + GetLayoutTestExpectedWidth(Node) &&
				CommentMaxX > Node->NodePosX;
			bOk &= Test.TestFalse(
				*FString::Printf(TEXT("%s: comment does not cover other chain node %d"), *Context, NodeIndex),
				bOverlapsX);
		}
		return bOk;
	}

	void SplitLayoutTestChain(const TArray<UEdGraphNode*>& Chain,
	                          TArray<UEdGraphNode*>& OutKawaiiPhysicsNodes,
	                          TArray<UEdGraphNode*>& OutOtherNodes)
	{
		OutKawaiiPhysicsNodes.Reset();
		OutOtherNodes.Reset();
		// 先頭の Result は枠との重なり判定から外す
		for (int32 ChainIndex = 1; ChainIndex < Chain.Num(); ++ChainIndex)
		{
			if (Chain[ChainIndex]->IsA<UAnimGraphNode_KawaiiPhysics>())
			{
				OutKawaiiPhysicsNodes.Add(Chain[ChainIndex]);
			}
			else
			{
				OutOtherNodes.Add(Chain[ChainIndex]);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphLayoutChainTest,
                                 "KawaiiPhysics.EditorScripting.Layout.Chain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphLayoutChainTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsLayoutTestFixture Fixture = MakeLayoutTestFixture(*this);
	UAnimGraphNode_Root* RootNode = FindLayoutTestRootNode(Fixture.AnimGraph);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph || !Fixture.Animation || !RootNode)
	{
		return false;
	}

	bool bOk = true;
	bOk &= TestTrue(TEXT("SetAnimGraphInputAnimation adds a SequencePlayer"),
	                UKawaiiPhysicsEditorLibrary::SetAnimGraphInputAnimation(Fixture.AnimBlueprint, Fixture.Animation));

	// チェーン外のノードはレイアウトで動かない
	FGraphNodeCreator<UAnimGraphNode_LocalRefPose> OffChainNodeCreator(*Fixture.AnimGraph);
	UAnimGraphNode_LocalRefPose* OffChainNode = OffChainNodeCreator.CreateNode(false);
	OffChainNode->NodePosX = -3000;
	OffChainNode->NodePosY = 1200;
	OffChainNodeCreator.Finalize();

	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const FString CommentText = TEXT("Layout chain");
	const FString CommentTitle = (Settings ? Settings->McpCommentPrefix : FString(TEXT("[MCP] "))) + CommentText;

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	for (const TCHAR* RootBoneName : {TEXT("hair_01"), TEXT("tail_01")})
	{
		FKawaiiPhysicsNodePlacementRequest& Request = Requests.AddDefaulted_GetRef();
		Request.RootBoneName = RootBoneName;
		Request.bAutoConnect = true;
	}
	const FIntPoint RootPosition(RootNode->NodePosX, RootNode->NodePosY);
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles = UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
		Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementMatchKey::None, NAME_None, CommentText);
	bOk &= TestEqual(TEXT("Two KawaiiPhysics nodes are added"), Handles.Num(), 2);
	if (Handles.Num() != 2 || !Handles[0].IsValid() || !Handles[1].IsValid())
	{
		return false;
	}

	// Result から上流へ、変換ノード・tail・hair・変換ノード・SequencePlayer の順に繋がる
	TArray<UEdGraphNode*> Chain = CollectLayoutTestChain(Fixture.AnimGraph);
	bOk &= TestEqual(TEXT("Chain has Result, two conversion nodes, two KawaiiPhysics nodes and the player"),
	                 Chain.Num(), 6);
	if (Chain.Num() != 6)
	{
		return false;
	}
	bOk &= TestTrue(TEXT("Chain[1] is ComponentToLocalSpace"), Chain[1]->IsA<UAnimGraphNode_ComponentToLocalSpace>());
	bOk &= TestTrue(TEXT("Chain[2] is the downstream KawaiiPhysics node"), Chain[2] == Handles[1].Node.Get());
	bOk &= TestTrue(TEXT("Chain[3] is the upstream KawaiiPhysics node"), Chain[3] == Handles[0].Node.Get());
	bOk &= TestTrue(TEXT("Chain[4] is LocalToComponentSpace"), Chain[4]->IsA<UAnimGraphNode_LocalToComponentSpace>());
	bOk &= TestTrue(TEXT("Chain[5] is the SequencePlayer"), Chain[5]->IsA<UAnimGraphNode_SequencePlayer>());

	TArray<UEdGraphNode*> KawaiiPhysicsNodes;
	TArray<UEdGraphNode*> OtherChainNodes;
	SplitLayoutTestChain(Chain, KawaiiPhysicsNodes, OtherChainNodes);

	// AddKawaiiPhysicsNodes は AutoConnect + AutoPosition の後に同じレイアウトを適用する
	bOk &= TestLayoutTestChainOnOneRow(*this, TEXT("After AddKawaiiPhysicsNodes"), Chain, RootPosition);
	bOk &= TestLayoutTestCommentEncloses(
		*this, TEXT("After AddKawaiiPhysicsNodes"), Fixture.AnimBlueprint, CommentTitle, KawaiiPhysicsNodes, OtherChainNodes);

	// 手で崩した配置を LayoutKawaiiPhysicsAnimGraph が並べ直し、枠内ノードの紐付けで枠も追従させる
	Chain[3]->NodePosX = 500;
	Chain[3]->NodePosY = 900;
	Chain[2]->NodePosX = -200;
	Chain[2]->NodePosY = -700;
	Chain[5]->NodePosX = 100;
	Chain[5]->NodePosY = 100;
	bOk &= TestTrue(TEXT("LayoutKawaiiPhysicsAnimGraph succeeds"),
	                UKawaiiPhysicsEditorLibrary::LayoutKawaiiPhysicsAnimGraph(Fixture.AnimBlueprint));
	bOk &= TestLayoutTestChainOnOneRow(*this, TEXT("After re-layout"), Chain, RootPosition);
	bOk &= TestLayoutTestCommentEncloses(
		*this, TEXT("After re-layout"), Fixture.AnimBlueprint, CommentTitle, KawaiiPhysicsNodes, OtherChainNodes);

	bOk &= TestEqual(TEXT("Off-chain node NodePosX is untouched"), OffChainNode->NodePosX, -3000);
	bOk &= TestEqual(TEXT("Off-chain node NodePosY is untouched"), OffChainNode->NodePosY, 1200);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAnimGraphLayoutCommentFallbackTest,
                                 "KawaiiPhysics.EditorScripting.Layout.CommentFallback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAnimGraphLayoutCommentFallbackTest::RunTest(const FString& Parameters)
{
	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const FString CommentPrefix = Settings ? Settings->McpCommentPrefix : FString(TEXT("[MCP] "));
	bool bOk = true;

	// 枠が1つだけで、紐付けが消えて枠がノードから遠く離れていても、グラフ内の全 KawaiiPhysics ノードを囲み直す
	{
		FKawaiiPhysicsLayoutTestFixture Fixture = MakeLayoutTestFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FString CommentText = TEXT("Layout far away");
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		for (const TCHAR* RootBoneName : {TEXT("hair_01"), TEXT("tail_01")})
		{
			FKawaiiPhysicsNodePlacementRequest& Request = Requests.AddDefaulted_GetRef();
			Request.RootBoneName = RootBoneName;
			Request.bAutoConnect = true;
		}
		TArray<FKawaiiPhysicsGraphNodeHandle> Handles = UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementMatchKey::None, NAME_None, CommentText);
		bOk &= TestEqual(TEXT("Far away: two KawaiiPhysics nodes are added"), Handles.Num(), 2);
		if (Handles.Num() != 2 || !Handles[0].IsValid() || !Handles[1].IsValid())
		{
			return false;
		}

		const TArray<FKawaiiPhysicsAnimGraphCommentInfo> CommentInfos =
			UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(Fixture.AnimBlueprint);
		const FKawaiiPhysicsAnimGraphCommentInfo* CommentInfo =
			FindLayoutTestComment(CommentInfos, CommentPrefix + CommentText);
		UEdGraphNode_Comment* CommentNode = CommentInfo ? CommentInfo->CommentNode.Get() : nullptr;
		bOk &= TestNotNull(TEXT("Far away: MCP comment node is found"), CommentNode);
		if (!CommentNode)
		{
			return false;
		}

		// 再ロード後相当に紐付けを消し、枠をノードと重ならない右下へ離す
		CommentNode->ClearNodesUnderComment();
		CommentNode->NodePosX += 5000;
		CommentNode->NodePosY += 3000;
		bOk &= TestTrue(TEXT("Far away: LayoutKawaiiPhysicsAnimGraph succeeds"),
		                UKawaiiPhysicsEditorLibrary::LayoutKawaiiPhysicsAnimGraph(Fixture.AnimBlueprint));

		TArray<UEdGraphNode*> KawaiiPhysicsNodes;
		TArray<UEdGraphNode*> OtherChainNodes;
		SplitLayoutTestChain(CollectLayoutTestChain(Fixture.AnimGraph), KawaiiPhysicsNodes, OtherChainNodes);
		bOk &= TestEqual(TEXT("Far away: both KawaiiPhysics nodes are on the chain"), KawaiiPhysicsNodes.Num(), 2);
		bOk &= TestLayoutTestCommentEncloses(
			*this, TEXT("Far away"), Fixture.AnimBlueprint, CommentPrefix + CommentText, KawaiiPhysicsNodes,
			OtherChainNodes);
	}

	// 枠が複数ある場合は、ノードの左上が枠外でも矩形が重なるノードだけを各枠の対象にする
	{
		FKawaiiPhysicsLayoutTestFixture Fixture = MakeLayoutTestFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FString CommentTexts[] = {TEXT("Layout hair"), TEXT("Layout tail")};
		const TCHAR* RootBoneNames[] = {TEXT("hair_01"), TEXT("tail_01")};
		UEdGraphNode* KawaiiPhysicsNodes[2] = {nullptr, nullptr};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
			FKawaiiPhysicsNodePlacementRequest& Request = Requests.AddDefaulted_GetRef();
			Request.RootBoneName = RootBoneNames[Index];
			Request.bAutoConnect = true;
			TArray<FKawaiiPhysicsGraphNodeHandle> Handles = UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
				Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementMatchKey::None, NAME_None, CommentTexts[Index]);
			KawaiiPhysicsNodes[Index] = Handles.Num() == 1 ? Handles[0].Node.Get() : nullptr;
			bOk &= TestNotNull(*FString::Printf(TEXT("Overlap: KawaiiPhysics node %d is added"), Index),
			                   KawaiiPhysicsNodes[Index]);
		}
		if (!KawaiiPhysicsNodes[0] || !KawaiiPhysicsNodes[1])
		{
			return false;
		}

		// 紐付けを消し、各枠を少し下へずらしてノードの左上を枠外に出す（矩形は自分のノードとだけ重なる）
		const TArray<FKawaiiPhysicsAnimGraphCommentInfo> CommentInfos =
			UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(Fixture.AnimBlueprint);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FKawaiiPhysicsAnimGraphCommentInfo* CommentInfo =
				FindLayoutTestComment(CommentInfos, CommentPrefix + CommentTexts[Index]);
			UEdGraphNode_Comment* CommentNode = CommentInfo ? CommentInfo->CommentNode.Get() : nullptr;
			bOk &= TestNotNull(*FString::Printf(TEXT("Overlap: MCP comment %d is found"), Index), CommentNode);
			if (!CommentNode)
			{
				return false;
			}
			CommentNode->ClearNodesUnderComment();
			CommentNode->NodePosY += 100;
			bOk &= TestTrue(*FString::Printf(TEXT("Overlap: node %d top-left is outside its shifted comment"), Index),
			                KawaiiPhysicsNodes[Index]->NodePosY < CommentNode->NodePosY);
		}

		bOk &= TestTrue(TEXT("Overlap: LayoutKawaiiPhysicsAnimGraph succeeds"),
		                UKawaiiPhysicsEditorLibrary::LayoutKawaiiPhysicsAnimGraph(Fixture.AnimBlueprint));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const TArray<UEdGraphNode*> OwnNodes = {KawaiiPhysicsNodes[Index]};
			const TArray<UEdGraphNode*> OtherNodes = {KawaiiPhysicsNodes[1 - Index]};
			bOk &= TestLayoutTestCommentEncloses(
				*this, FString::Printf(TEXT("Overlap comment %d"), Index), Fixture.AnimBlueprint,
				CommentPrefix + CommentTexts[Index], OwnNodes, OtherNodes);
		}
	}

	return bOk;
}

#endif
