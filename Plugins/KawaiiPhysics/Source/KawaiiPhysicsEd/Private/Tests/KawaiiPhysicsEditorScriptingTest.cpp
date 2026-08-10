// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimationGraphSchema.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNode_Root.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EdGraphNode_Comment.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsSettings.h"
#include "KawaiiPhysicsDeveloperSettings.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsMcpCommentNode.h"
#include "K2Node_Knot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "UObject/NameTypes.h"

namespace
{
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
	using FKawaiiMcpCommentNode = UKawaiiPhysicsMcpCommentNode;
#else
	using FKawaiiMcpCommentNode = UEdGraphNode_Comment;
#endif

	const FGameplayTag& GetKawaiiPhysicsEditorScriptingTagA()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.PresetSource")));
		return Tag;
	}

	const FGameplayTag& GetKawaiiPhysicsEditorScriptingTagB()
	{
		static const FGameplayTag Tag =
			FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.PresetTarget")));
		return Tag;
	}

	struct FKawaiiPhysicsEditorScriptingFixture
	{
		UPackage* Package = nullptr;
		USkeleton* Skeleton = nullptr;
		UAnimBlueprint* AnimBlueprint = nullptr;
		UEdGraph* AnimGraph = nullptr;
		TArray<UAnimGraphNode_KawaiiPhysics*> Nodes;
	};

	struct FScopedMcpPlacementSettings
	{
		UKawaiiPhysicsDeveloperSettings* Settings = nullptr;
		EKawaiiPhysicsMcpNodePlacementDirection PreviousDirection =
			EKawaiiPhysicsMcpNodePlacementDirection::Auto;
		int32 PreviousWrapCount = 0;
		int32 PreviousSpacingX = 420;
		int32 PreviousSpacingY = 260;

		// SpacingX/Yの既定値500/300は旧固定オフセット定数と一致させ、既存テストの座標期待値をそのまま成立させる
		FScopedMcpPlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection Direction,
			int32 WrapCount,
			int32 SpacingX = 500,
			int32 SpacingY = 300)
			: Settings(GetMutableDefault<UKawaiiPhysicsDeveloperSettings>())
		{
			if (Settings)
			{
				PreviousDirection = Settings->McpNodePlacementDirection;
				PreviousWrapCount = Settings->McpNodePlacementWrapCount;
				PreviousSpacingX = Settings->McpNodePlacementSpacingX;
				PreviousSpacingY = Settings->McpNodePlacementSpacingY;
				Settings->McpNodePlacementDirection = Direction;
				Settings->McpNodePlacementWrapCount = WrapCount;
				Settings->McpNodePlacementSpacingX = SpacingX;
				Settings->McpNodePlacementSpacingY = SpacingY;
			}
		}

		~FScopedMcpPlacementSettings()
		{
			if (Settings)
			{
				Settings->McpNodePlacementDirection = PreviousDirection;
				Settings->McpNodePlacementWrapCount = PreviousWrapCount;
				Settings->McpNodePlacementSpacingX = PreviousSpacingX;
				Settings->McpNodePlacementSpacingY = PreviousSpacingY;
			}
		}
	};

	struct FScopedGameplayTagRedirects
	{
		UGameplayTagsSettings* Settings = nullptr;
		TArray<FGameplayTagRedirect> PreviousRedirects;

		explicit FScopedGameplayTagRedirects(UGameplayTagsSettings* InSettings)
			: Settings(InSettings)
		{
			if (Settings)
			{
				PreviousRedirects = Settings->GameplayTagRedirects;
			}
		}

		~FScopedGameplayTagRedirects()
		{
			if (Settings)
			{
				Settings->GameplayTagRedirects = PreviousRedirects;
			}
		}
	};

	USkeleton* CreateTestSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("pelvis"), TEXT("pelvis"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("spine_01"), TEXT("spine_01"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_01"), TEXT("hair_01"), 2), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_02"), TEXT("hair_02"), 3), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_skip_01"), TEXT("hair_skip_01"), 3), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("tail_01"), TEXT("tail_01"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("tail_02"), TEXT("tail_02"), 6), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("bang_01"), TEXT("bang_01"), 2), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("preset_root"), TEXT("preset_root"), 2), FTransform::Identity);
		return Skeleton;
	}

	USkeleton* CreateLongBoneListSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);

		int32 BoneListStringLength = 0;
		int32 ParentIndex = INDEX_NONE;
		for (int32 BoneIndex = 0; BoneListStringLength < NAME_SIZE + 128; ++BoneIndex)
		{
			const FString BoneNameString = FString::Printf(TEXT("long_regex_bone_%03d"), BoneIndex);
			const FName BoneName(*BoneNameString);
			Modifier.Add(FMeshBoneInfo(BoneName, BoneNameString, ParentIndex), FTransform::Identity);
			ParentIndex = BoneIndex;
			BoneListStringLength += BoneNameString.Len() + 2;
		}

		return Skeleton;
	}

	USkeleton* CreateNestedRootWarningSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Object008"), TEXT("Object008"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Object009"), TEXT("Object009"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Object010"), TEXT("Object010"), 2), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Object011"), TEXT("Object011"), 3), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Object012"), TEXT("Object012"), 4), FTransform::Identity);
		return Skeleton;
	}

	UAnimBlueprint* CreateTransientAnimBlueprint(FAutomationTestBase& Test)
	{
		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(TEXT("/Temp/KawaiiPhysicsEditorScripting_%s"), *UniqueSuffix);
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);

		const FName BlueprintName(*FString::Printf(TEXT("ABP_KawaiiPhysicsEditorScripting_%s"), *UniqueSuffix));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass());

		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		Test.TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
		return AnimBlueprint;
	}

	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

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

	UAnimGraphNode_KawaiiPhysics* AddKawaiiPhysicsNode(
		UEdGraph* Graph,
		const FGameplayTag& Tag,
		FName RootBoneName,
		FVector2D NodePosition)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->NodePosX = static_cast<int32>(NodePosition.X);
		GraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
		GraphNode->Node.KawaiiPhysicsTag = Tag;
		GraphNode->Node.RootBone = FBoneReference(RootBoneName);
		NodeCreator.Finalize();
		return GraphNode;
	}

	FKawaiiPhysicsEditorScriptingFixture MakeFixture(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsEditorScriptingFixture Fixture;
		Fixture.AnimBlueprint = CreateTransientAnimBlueprint(Test);
		Fixture.Package = Fixture.AnimBlueprint ? Fixture.AnimBlueprint->GetOutermost() : nullptr;
		Fixture.Skeleton = CreateTestSkeleton(Fixture.Package);
		if (Fixture.AnimBlueprint)
		{
			Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
		}
		Fixture.AnimGraph = FindAnimGraph(Fixture.AnimBlueprint);
		Test.TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);

		if (Fixture.AnimGraph)
		{
			Fixture.Nodes.Add(AddKawaiiPhysicsNode(
				Fixture.AnimGraph, GetKawaiiPhysicsEditorScriptingTagA(), TEXT("hair_01"), FVector2D(-300.0f, 0.0f)));
			Fixture.Nodes.Add(AddKawaiiPhysicsNode(
				Fixture.AnimGraph, GetKawaiiPhysicsEditorScriptingTagB(), TEXT("tail_01"), FVector2D(-300.0f, 160.0f)));
		}

		Test.TestEqual(TEXT("Two KawaiiPhysics nodes are added"), Fixture.Nodes.Num(), 2);
		return Fixture;
	}

	FKawaiiPhysicsEditorScriptingFixture MakeEmptyFixture(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsEditorScriptingFixture Fixture;
		Fixture.AnimBlueprint = CreateTransientAnimBlueprint(Test);
		Fixture.Package = Fixture.AnimBlueprint ? Fixture.AnimBlueprint->GetOutermost() : nullptr;
		Fixture.Skeleton = CreateTestSkeleton(Fixture.Package);
		if (Fixture.AnimBlueprint)
		{
			Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
		}
		Fixture.AnimGraph = FindAnimGraph(Fixture.AnimBlueprint);
		Test.TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);
		return Fixture;
	}

	FKawaiiPhysicsGraphNodeHandle MakeHandle(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		FKawaiiPhysicsGraphNodeHandle Handle;
		Handle.Node = GraphNode;
		return Handle;
	}

	UAnimGraphNode_Root* FindResultRootNode(UEdGraph* Graph)
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

	FVector2D GetExpectedAutoPlacementBasePosition(
		FAutomationTestBase& Test, UEdGraph* Graph, int32 SpacingX = 500, bool bAutoConnect = false)
	{
		const UAnimGraphNode_Root* RootNode = FindResultRootNode(Graph);
		Test.TestNotNull(TEXT("Result root node is found for auto placement"), RootNode);
		if (!RootNode)
		{
			return FVector2D::ZeroVector;
		}

		// AutoConnect時はコメント枠と変換ノードスロットが重ならないための予約幅(280)だけさらに左へずれる
		const int32 BaseReserveX = bAutoConnect ? 280 : 0;
		return FVector2D(
			static_cast<double>(RootNode->NodePosX - SpacingX - BaseReserveX),
			static_cast<double>(RootNode->NodePosY));
	}

	UEdGraphPin* FindFirstPosePin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
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

	UEdGraphPin* FindFirstPin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Dir)
			{
				return Pin;
			}
		}

		return nullptr;
	}

	UEdGraphPin* GetResultPin(UEdGraph* Graph)
	{
		UAnimGraphNode_Root* RootNode = FindResultRootNode(Graph);
		return RootNode ? RootNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_Root, Result), EGPD_Input) : nullptr;
	}

	UEdGraphPin* GetKawaiiComponentPosePin(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		return GraphNode
			? GraphNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_SkeletalControlBase, ComponentPose), EGPD_Input)
			: nullptr;
	}

	UEdGraphPin* GetKawaiiPosePin(UAnimGraphNode_KawaiiPhysics* GraphNode)
	{
		return GraphNode ? GraphNode->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
	}

	int32 CountNodesOfClass(UEdGraph* Graph, UClass* NodeClass)
	{
		int32 Count = 0;
		if (!Graph || !NodeClass)
		{
			return Count;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA(NodeClass))
			{
				++Count;
			}
		}

		return Count;
	}

	int32 CountExactNodesOfClass(UEdGraph* Graph, UClass* NodeClass)
	{
		int32 Count = 0;
		if (!Graph || !NodeClass)
		{
			return Count;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetClass() == NodeClass)
			{
				++Count;
			}
		}

		return Count;
	}

	FKawaiiMcpCommentNode* FindMcpCommentNode(UEdGraph* Graph, const FString& CommentText)
	{
		if (!Graph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			FKawaiiMcpCommentNode* CommentNode = Cast<FKawaiiMcpCommentNode>(Node);
			if (CommentNode &&
				CommentNode->GetClass() == FKawaiiMcpCommentNode::StaticClass() &&
				CommentNode->NodeComment == CommentText)
			{
				return CommentNode;
			}
		}

		return nullptr;
	}

	UEdGraphNode_Comment* AddManualCommentNode(UEdGraph* Graph, const FString& CommentText, FVector2D NodePosition)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UEdGraphNode_Comment> NodeCreator(*Graph);
		UEdGraphNode_Comment* CommentNode = NodeCreator.CreateNode(false);
		NodeCreator.Finalize();
		CommentNode->NodeComment = CommentText;
		CommentNode->NodePosX = static_cast<int32>(NodePosition.X);
		CommentNode->NodePosY = static_cast<int32>(NodePosition.Y);
		CommentNode->NodeWidth = 320;
		CommentNode->NodeHeight = 180;
		return CommentNode;
	}

	FKawaiiPhysicsNodePlacementRequest MakeAutoConnectRequest(FName RootBoneName, const FGameplayTag& Tag)
	{
		FKawaiiPhysicsNodePlacementRequest Request;
		Request.RootBoneName = RootBoneName;
		Request.KawaiiPhysicsTag = Tag;
		Request.bAutoConnect = true;
		return Request;
	}

	FKawaiiPhysicsNodePlacementRequest MakePlacementRequest(FName RootBoneName, const FGameplayTag& Tag)
	{
		FKawaiiPhysicsNodePlacementRequest Request;
		Request.RootBoneName = RootBoneName;
		Request.KawaiiPhysicsTag = Tag;
		return Request;
	}

	bool TestNodePosition(
		FAutomationTestBase& Test,
		const FString& Context,
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		const FVector2D& ExpectedPosition)
	{
		UAnimGraphNode_KawaiiPhysics* Node = Handle.IsValid() ? Handle.Node.Get() : nullptr;
		bool bOk = true;
		bOk &= Test.TestNotNull(*FString::Printf(TEXT("%s node is valid"), *Context), Node);
		if (Node)
		{
			bOk &= Test.TestEqual(
				*FString::Printf(TEXT("%s NodePosX"), *Context),
				Node->NodePosX,
				static_cast<int32>(ExpectedPosition.X));
			bOk &= Test.TestEqual(
				*FString::Printf(TEXT("%s NodePosY"), *Context),
				Node->NodePosY,
				static_cast<int32>(ExpectedPosition.Y));
		}
		return bOk;
	}

	UAnimGraphNode_LocalRefPose* AddLocalRefPoseNode(UEdGraph* Graph, FVector2D NodePosition)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_LocalRefPose> NodeCreator(*Graph);
		UAnimGraphNode_LocalRefPose* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->NodePosX = static_cast<int32>(NodePosition.X);
		GraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
		NodeCreator.Finalize();
		return GraphNode;
	}

	UK2Node_Knot* AddKnotNode(UEdGraph* Graph, FVector2D NodePosition)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UK2Node_Knot> NodeCreator(*Graph);
		UK2Node_Knot* KnotNode = NodeCreator.CreateNode(false);
		KnotNode->NodePosX = static_cast<int32>(NodePosition.X);
		KnotNode->NodePosY = static_cast<int32>(NodePosition.Y);
		NodeCreator.Finalize();
		return KnotNode;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingCollectTest,
                                 "KawaiiPhysics.EditorScripting.Collect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingCollectTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	bool bOk = true;
	TArray<FKawaiiPhysicsGraphNodeHandle> AllHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, FGameplayTagContainer(), false);
	bOk &= TestEqual(TEXT("Empty tag filter collects all nodes"), AllHandles.Num(), 2);

	FGameplayTagContainer FilterTags;
	FilterTags.AddTag(GetKawaiiPhysicsEditorScriptingTagA());
	TArray<FKawaiiPhysicsGraphNodeHandle> FilteredHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, FilterTags, false);
	bOk &= TestEqual(TEXT("Tag filter collects matching node"), FilteredHandles.Num(), 1);
	bOk &= TestTrue(TEXT("Collected handle is valid"), FilteredHandles.Num() == 1 && FilteredHandles[0].IsValid());

	TArray<FKawaiiPhysicsGraphNodeHandle> ExactHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, FilterTags, true);
	bOk &= TestEqual(TEXT("Exact tag filter collects exact matching node"), ExactHandles.Num(), 1);

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	Preset->TargetTags.AddTag(GetKawaiiPhysicsEditorScriptingTagB());
	Preset->bTargetTagsExactMatch = false;
	TArray<FKawaiiPhysicsGraphNodeHandle> PresetTargetHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, Preset->TargetTags, Preset->bTargetTagsExactMatch);
	bOk &= TestEqual(TEXT("Preset TargetTags collect matching node"), PresetTargetHandles.Num(), 1);
	bOk &= TestTrue(TEXT("Preset TargetTags collect tag-matched node"),
	                PresetTargetHandles.Num() == 1 && PresetTargetHandles[0].Node.Get() == Fixture.Nodes[1]);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPropertyAccessTest,
                                 "KawaiiPhysics.EditorScripting.PropertyAccess",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPropertyAccessTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeFixture(*this);
	if (Fixture.Nodes.IsEmpty() || !Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsGraphNodeHandle Handle = MakeHandle(Fixture.Nodes[0]);
	Fixture.AnimBlueprint->Status = BS_UpToDate;

	bool bOk = true;
	bOk &= TestTrue(TEXT("Set WindScale by string"),
	                UKawaiiPhysicsEditorLibrary::SetGraphNodePropertyByString(
		                Handle, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale), TEXT("3.25")));
	FString OutValue;
	bOk &= TestTrue(TEXT("Get WindScale as string"),
	                UKawaiiPhysicsEditorLibrary::GetGraphNodePropertyAsString(
		                Handle, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale), OutValue));
	bOk &= TestTrue(TEXT("WindScale round-trips"),
	                FMath::IsNearlyEqual(Fixture.Nodes[0]->Node.WindScale, 3.25f));
	bOk &= TestFalse(TEXT("Denied ExternalForces property is rejected"),
	                 UKawaiiPhysicsEditorLibrary::SetGraphNodePropertyByString(
		                 Handle, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces), TEXT("()")));
	bOk &= TestEqual(TEXT("Blueprint is marked as modified"), Fixture.AnimBlueprint->Status, BS_Dirty);

	FGameplayTag OutTag;
	bOk &= TestTrue(TEXT("Set tag shortcut"),
	                UKawaiiPhysicsEditorLibrary::SetGraphNodeTag(Handle, GetKawaiiPhysicsEditorScriptingTagB()));
	bOk &= TestTrue(TEXT("Get tag shortcut"),
	                UKawaiiPhysicsEditorLibrary::GetGraphNodeTag(Handle, OutTag));
	bOk &= TestTrue(TEXT("Tag shortcut round-trips"), OutTag == GetKawaiiPhysicsEditorScriptingTagB());

	FName OutRootBoneName;
	bOk &= TestTrue(TEXT("Set RootBoneName shortcut"),
	                UKawaiiPhysicsEditorLibrary::SetGraphNodeRootBoneName(Handle, TEXT("bang_01")));
	bOk &= TestTrue(TEXT("Get RootBoneName shortcut"),
	                UKawaiiPhysicsEditorLibrary::GetGraphNodeRootBoneName(Handle, OutRootBoneName));
	bOk &= TestEqual(TEXT("RootBoneName shortcut round-trips"), OutRootBoneName, FName(TEXT("bang_01")));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPresetStringAccessTest,
                                 "KawaiiPhysics.EditorScripting.PresetStringAccess",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPresetStringAccessTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	UKawaiiPhysicsPresetDataAsset* RoundTripPreset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	if (!Preset || !RoundTripPreset)
	{
		return false;
	}

	const FString PhysicsSettingsValue =
		TEXT("(Damping=0.33,Stiffness=0.44,WorldDampingLocation=0.55,WorldDampingRotation=0.66,Radius=7.0,LimitAngle=45.0)");
	const FString TagValue = FString::Printf(
		TEXT("(TagName=\"%s\")"),
		*GetKawaiiPhysicsEditorScriptingTagB().ToString());

	bool bOk = true;
	bOk &= TestTrue(TEXT("Set preset PhysicsSettings by string"),
	                UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
		                Preset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings), PhysicsSettingsValue));
	bOk &= TestTrue(TEXT("Preset Damping is updated"),
	                FMath::IsNearlyEqual(Preset->Node.PhysicsSettings.Damping, 0.33f));
	bOk &= TestTrue(TEXT("Preset Radius is updated"),
	                FMath::IsNearlyEqual(Preset->Node.PhysicsSettings.Radius, 7.0f));
	bOk &= TestTrue(TEXT("Set preset KawaiiPhysicsTag by string"),
	                UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
		                Preset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag), TagValue));
	bOk &= TestTrue(TEXT("Preset KawaiiPhysicsTag is updated"),
	                Preset->Node.KawaiiPhysicsTag == GetKawaiiPhysicsEditorScriptingTagB());

	FString OutPhysicsSettingsValue;
	bOk &= TestTrue(TEXT("Get preset PhysicsSettings as string"),
	                UKawaiiPhysicsEditorLibrary::GetPresetNodePropertyAsString(
		                Preset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings), OutPhysicsSettingsValue));
	bOk &= TestTrue(TEXT("Round-trip preset PhysicsSettings string"),
	                UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
		                RoundTripPreset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings),
		                OutPhysicsSettingsValue));
	bOk &= TestTrue(TEXT("Round-tripped PhysicsSettings Damping matches"),
	                FMath::IsNearlyEqual(
		                RoundTripPreset->Node.PhysicsSettings.Damping,
		                Preset->Node.PhysicsSettings.Damping));
	bOk &= TestTrue(TEXT("Round-tripped PhysicsSettings LimitAngle matches"),
	                FMath::IsNearlyEqual(
		                RoundTripPreset->Node.PhysicsSettings.LimitAngle,
		                Preset->Node.PhysicsSettings.LimitAngle));

	FString OutTagValue;
	bOk &= TestTrue(TEXT("Get preset KawaiiPhysicsTag as string"),
	                UKawaiiPhysicsEditorLibrary::GetPresetNodePropertyAsString(
		                Preset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag), OutTagValue));
	bOk &= TestTrue(TEXT("Round-trip preset KawaiiPhysicsTag string"),
	                UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
		                RoundTripPreset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag),
		                OutTagValue));
	bOk &= TestTrue(TEXT("Round-tripped KawaiiPhysicsTag matches"),
	                RoundTripPreset->Node.KawaiiPhysicsTag == Preset->Node.KawaiiPhysicsTag);

	bOk &= TestFalse(TEXT("Denied ExternalForces preset property is rejected"),
	                 UKawaiiPhysicsEditorLibrary::SetPresetNodePropertyByString(
		                 Preset, GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces), TEXT("()")));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPresetTargetTagsTest,
                                 "KawaiiPhysics.EditorScripting.Preset.TargetTags",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPresetTargetTagsTest::RunTest(const FString& Parameters)
{
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	if (!Preset)
	{
		return false;
	}

	TArray<FName> TagNames;
	TagNames.Add(GetKawaiiPhysicsEditorScriptingTagA().GetTagName());
	TagNames.Add(FName(TEXT("KawaiiPhysics.Test.UnregisteredTargetTag")));

	bool bOk = true;
	FGameplayTagContainer ResolvedTags;
	bOk &= TestTrue(TEXT("MakeGameplayTagContainerFromNames resolves valid tag"),
	                UKawaiiPhysicsEditorLibrary::MakeGameplayTagContainerFromNames(TagNames, ResolvedTags));
	bOk &= TestTrue(TEXT("MakeGameplayTagContainerFromNames stores valid tag"),
	                ResolvedTags.HasTagExact(GetKawaiiPhysicsEditorScriptingTagA()));
	bOk &= TestEqual(TEXT("MakeGameplayTagContainerFromNames skips unregistered tag"), ResolvedTags.Num(), 1);

	TArray<FName> InvalidTagNames;
	InvalidTagNames.Add(FName(TEXT("KawaiiPhysics.Test.UnregisteredTargetTag")));
	FGameplayTagContainer EmptyResolvedTags;
	bOk &= TestFalse(TEXT("MakeGameplayTagContainerFromNames fails when all tags are unresolved"),
	                 UKawaiiPhysicsEditorLibrary::MakeGameplayTagContainerFromNames(
		                 InvalidTagNames, EmptyResolvedTags));
	bOk &= TestTrue(TEXT("MakeGameplayTagContainerFromNames leaves no unresolved tags"),
	                EmptyResolvedTags.IsEmpty());

	bOk &= TestTrue(TEXT("SetPresetTargetTags succeeds with valid tag"),
	                UKawaiiPhysicsEditorLibrary::SetPresetTargetTags(Preset, TagNames, true));
	bOk &= TestTrue(TEXT("SetPresetTargetTags stores valid tag"),
	                Preset->TargetTags.HasTagExact(GetKawaiiPhysicsEditorScriptingTagA()));
	bOk &= TestTrue(TEXT("SetPresetTargetTags stores exact-match flag"),
	                Preset->bTargetTagsExactMatch);

	TArray<FGameplayTag> TargetTags;
	Preset->TargetTags.GetGameplayTagArray(TargetTags);
	bOk &= TestEqual(TEXT("SetPresetTargetTags skips unregistered tag"), TargetTags.Num(), 1);

	const FGameplayTagContainer PreviousTargetTags = Preset->TargetTags;
	const bool bPreviousExactMatch = Preset->bTargetTagsExactMatch;
	bOk &= TestFalse(TEXT("SetPresetTargetTags fails when all tags are unresolved"),
	                 UKawaiiPhysicsEditorLibrary::SetPresetTargetTags(Preset, InvalidTagNames, false));
	bOk &= TestTrue(TEXT("SetPresetTargetTags keeps TargetTags on all-unresolved input"),
	                Preset->TargetTags.HasAllExact(PreviousTargetTags) &&
	                PreviousTargetTags.HasAllExact(Preset->TargetTags));
	bOk &= TestEqual(TEXT("SetPresetTargetTags keeps exact-match flag on all-unresolved input"),
	                  Preset->bTargetTagsExactMatch, bPreviousExactMatch);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPublishedEditorApisTest,
                                 "KawaiiPhysics.EditorScripting.PublishedEditorApis",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPublishedEditorApisTest::RunTest(const FString& Parameters)
{
	TArray<UKawaiiPhysicsPresetDataAsset*> Presets =
		UKawaiiPhysicsEditorLibrary::FindAllPresetAssets();

	bool bOk = true;
	bOk &= TestFalse(TEXT("FindAllPresetAssets returns preset assets"), Presets.IsEmpty());
	for (UKawaiiPhysicsPresetDataAsset* Preset : Presets)
	{
		bOk &= TestTrue(TEXT("FindAllPresetAssets elements are preset data assets"),
		                Preset && Preset->IsA<UKawaiiPhysicsPresetDataAsset>());
	}

	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game/KawaiiPhysicsSample/Chain"));
	TArray<FSoftObjectPath> AnimBlueprintPaths =
		UKawaiiPhysicsEditorLibrary::FindAnimBlueprintAssets(ContentPaths);
	bOk &= TestFalse(TEXT("FindAnimBlueprintAssets returns results for narrowed /Game path"),
	                 AnimBlueprintPaths.IsEmpty());

	const bool bHasNarrowedPathResult = AnimBlueprintPaths.ContainsByPredicate(
		[](const FSoftObjectPath& AssetPath)
		{
			return AssetPath.ToString().StartsWith(TEXT("/Game/KawaiiPhysicsSample/Chain/"));
		});
	bOk &= TestTrue(TEXT("FindAnimBlueprintAssets results stay under narrowed path"),
	                bHasNarrowedPathResult);

	UKawaiiPhysicsPresetDataAsset* DescriptionPreset =
		NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	const FText ExpectedDescription =
		FText::FromString(TEXT("KawaiiPhysics editor scripting description round trip"));
	bOk &= TestTrue(TEXT("SetPresetDescription succeeds"),
	                UKawaiiPhysicsEditorLibrary::SetPresetDescription(
		                DescriptionPreset,
		                ExpectedDescription));
	bOk &= TestEqual(TEXT("GetPresetDescription round-trips"),
	                 UKawaiiPhysicsEditorLibrary::GetPresetDescription(DescriptionPreset).ToString(),
	                 ExpectedDescription.ToString());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPresetTest,
                                 "KawaiiPhysics.EditorScripting.Preset.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPresetTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeFixture(*this);
	if (Fixture.Nodes.IsEmpty())
	{
		return false;
	}

	FKawaiiPhysicsGraphNodeHandle Handle = MakeHandle(Fixture.Nodes[0]);

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	Preset->Node = Fixture.Nodes[1]->Node;
	Preset->Node.WindScale = 7.0f;
	Preset->Node.RootBone = FBoneReference(TEXT("preset_root"));
	Preset->Node.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagB();

	FKawaiiPhysicsPresetApplyOptions Options;
	Options.bApplyBoneAssignment = true;
	Options.bApplyTag = true;

	bool bOk = true;
	bOk &= TestTrue(TEXT("Apply preset to graph node"),
	                UKawaiiPhysicsEditorLibrary::ApplyPresetToGraphNode(Handle, Preset, Options));

	TArray<FName> DiffProperties =
		UKawaiiPhysicsEditorLibrary::GetGraphNodePresetDiffProperties(Handle, Preset, Options);
	bOk &= TestTrue(TEXT("No diff after preset apply"), DiffProperties.IsEmpty());

	UKawaiiPhysicsPresetDataAsset* ExportTarget = NewObject<UKawaiiPhysicsPresetDataAsset>(GetTransientPackage());
	bOk &= TestTrue(TEXT("Export graph node to preset"),
	                UKawaiiPhysicsEditorLibrary::ExportGraphNodeToPreset(Handle, ExportTarget));
	bOk &= TestTrue(TEXT("Exported preset matches graph node"),
	                ExportTarget->MatchesNode(Fixture.Nodes[0]->Node, Options, DiffProperties));
	bOk &= TestTrue(TEXT("Exported preset TargetTags include graph node tag"),
	                ExportTarget->TargetTags.HasTagExact(Fixture.Nodes[0]->Node.KawaiiPhysicsTag));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(Fixture.Package);
#if WITH_EDITORONLY_DATA
	Preset->Skeleton = Fixture.Skeleton;
#endif
	Preset->Node.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagB();
	Preset->Node.WindScale = 7.0f;

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	FKawaiiPhysicsNodePlacementRequest PresetRequest;
	PresetRequest.Preset = Preset;
	PresetRequest.RootBoneName = TEXT("hair_01");
	Requests.Add(PresetRequest);

	FKawaiiPhysicsNodePlacementRequest DefaultRequest;
	DefaultRequest.RootBoneName = TEXT("tail_01");
	DefaultRequest.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagA();
	DefaultRequest.bAutoPosition = false;
	DefaultRequest.NodePosition = FVector2D(-900.0f, 120.0f);
	Requests.Add(DefaultRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Two placement handles are returned"), Handles.Num(), 2);
	bOk &= TestTrue(TEXT("First placement handle is valid"), Handles.IsValidIndex(0) && Handles[0].IsValid());
	bOk &= TestTrue(TEXT("Second placement handle is valid"), Handles.IsValidIndex(1) && Handles[1].IsValid());

	TArray<FKawaiiPhysicsGraphNodeHandle> CollectedHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, FGameplayTagContainer(), false);
	bOk &= TestEqual(TEXT("Two placed nodes are collected"), CollectedHandles.Num(), 2);

	if (Handles.Num() == 2 && Handles[0].IsValid() && Handles[1].IsValid())
	{
		UAnimGraphNode_KawaiiPhysics* FirstNode = Handles[0].Node.Get();
		UAnimGraphNode_KawaiiPhysics* SecondNode = Handles[1].Node.Get();
		bOk &= TestEqual(TEXT("First RootBone is applied"),
		                  FirstNode->Node.RootBone.BoneName, FName(TEXT("hair_01")));
		bOk &= TestTrue(TEXT("Preset tag fallback is applied"),
		                 FirstNode->Node.KawaiiPhysicsTag == GetKawaiiPhysicsEditorScriptingTagB());
		bOk &= TestTrue(TEXT("Preset value is applied"),
		                 FMath::IsNearlyEqual(FirstNode->Node.WindScale, 7.0f));
		bOk &= TestEqual(TEXT("Second RootBone is applied"),
		                  SecondNode->Node.RootBone.BoneName, FName(TEXT("tail_01")));
		bOk &= TestTrue(TEXT("Explicit tag is applied"),
		                 SecondNode->Node.KawaiiPhysicsTag == GetKawaiiPhysicsEditorScriptingTagA());
		bOk &= TestEqual(TEXT("Manual NodePosX is applied"), SecondNode->NodePosX, -900);
		bOk &= TestEqual(TEXT("Manual NodePosY is applied"), SecondNode->NodePosY, 120);
	}

	TArray<FKawaiiPhysicsGraphNodeHandle> MismatchHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::None, TEXT("NoSuchGraph"));
	bOk &= TestTrue(TEXT("GraphName mismatch returns empty handles"), MismatchHandles.IsEmpty());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoPositionStackingTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoPositionStacking",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoPositionStackingTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest FirstRequest;
	FirstRequest.RootBoneName = TEXT("hair_01");
	FirstRequest.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagA();
	TArray<FKawaiiPhysicsNodePlacementRequest> FirstRequests;
	FirstRequests.Add(FirstRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> FirstHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, FirstRequests);

	FKawaiiPhysicsNodePlacementRequest SecondRequest;
	SecondRequest.RootBoneName = TEXT("tail_01");
	SecondRequest.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagB();
	TArray<FKawaiiPhysicsNodePlacementRequest> SecondRequests;
	SecondRequests.Add(SecondRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> SecondHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, SecondRequests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("First auto placement creates one node"), FirstHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Second auto placement creates one node"), SecondHandles.Num(), 1);
	if (FirstHandles.IsValidIndex(0) && FirstHandles[0].IsValid() &&
		SecondHandles.IsValidIndex(0) && SecondHandles[0].IsValid())
	{
		const UAnimGraphNode_KawaiiPhysics* FirstNode = FirstHandles[0].Node.Get();
		const UAnimGraphNode_KawaiiPhysics* SecondNode = SecondHandles[0].Node.Get();
		bOk &= TestFalse(TEXT("Second auto placement does not reuse first coordinates"),
		                 FirstNode->NodePosX == SecondNode->NodePosX &&
		                 FirstNode->NodePosY == SecondNode->NodePosY);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementDirectionHorizontalSettingTest,
                                 "KawaiiPhysics.EditorScripting.Placement.DirectionHorizontalSetting",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementDirectionHorizontalSettingTest::RunTest(const FString& Parameters)
{
	FScopedMcpPlacementSettings PlacementSettings(
		EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
		0);
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph);
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	Requests.Add(MakePlacementRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Horizontal setting placement creates three nodes"), Handles.Num(), 3);
	// リクエスト順に左から右へ並ぶため、最後のリクエスト（末尾ノード）が基準位置になる
	for (int32 NodeIndex = 0; NodeIndex < Handles.Num(); ++NodeIndex)
	{
		bOk &= TestNodePosition(
			*this,
			FString::Printf(TEXT("Horizontal setting node %d"), NodeIndex),
			Handles[NodeIndex],
			FVector2D(BasePosition.X + static_cast<double>((NodeIndex - (Handles.Num() - 1)) * 500), BasePosition.Y));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementDirectionVerticalWithAutoConnectTest,
                                 "KawaiiPhysics.EditorScripting.Placement.DirectionVerticalWithAutoConnect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementDirectionVerticalWithAutoConnectTest::RunTest(const FString& Parameters)
{
	FScopedMcpPlacementSettings PlacementSettings(
		EKawaiiPhysicsMcpNodePlacementDirection::Vertical,
		0);
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	const FVector2D BasePosition =
		GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 500, true);
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Vertical AutoConnect placement creates two nodes"), Handles.Num(), 2);
	if (Handles.IsValidIndex(0))
	{
		bOk &= TestNodePosition(
			*this,
			TEXT("Vertical AutoConnect first node"),
			Handles[0],
			BasePosition);
	}
	if (Handles.IsValidIndex(1))
	{
		bOk &= TestNodePosition(
			*this,
			TEXT("Vertical AutoConnect second node"),
			Handles[1],
			FVector2D(BasePosition.X, BasePosition.Y + 300.0));
	}

	UAnimGraphNode_KawaiiPhysics* FirstNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;
	UAnimGraphNode_KawaiiPhysics* SecondNode =
		Handles.IsValidIndex(1) && Handles[1].IsValid() ? Handles[1].Node.Get() : nullptr;
	UEdGraphPin* FirstPosePin = GetKawaiiPosePin(FirstNode);
	UEdGraphPin* SecondComponentPosePin = GetKawaiiComponentPosePin(SecondNode);
	UEdGraphPin* SecondPosePin = GetKawaiiPosePin(SecondNode);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1
			? Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode())
			: nullptr;
	UEdGraphPin* ComponentToLocalSpaceInputPin =
		FindFirstPosePin(ComponentToLocalSpaceNode, EGPD_Input);

	bOk &= TestTrue(TEXT("Vertical AutoConnect keeps first Pose connected to second ComponentPose"),
	                FirstPosePin &&
	                SecondComponentPosePin &&
	                FirstPosePin->LinkedTo.Num() == 1 &&
	                FirstPosePin->LinkedTo[0] == SecondComponentPosePin &&
	                SecondComponentPosePin->LinkedTo.Num() == 1 &&
	                SecondComponentPosePin->LinkedTo[0] == FirstPosePin);
	bOk &= TestTrue(TEXT("Vertical AutoConnect keeps second Pose connected toward Result"),
	                SecondPosePin &&
	                ComponentToLocalSpaceInputPin &&
	                SecondPosePin->LinkedTo.Num() == 1 &&
	                SecondPosePin->LinkedTo[0] == ComponentToLocalSpaceInputPin);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectConversionNodeTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnectConversionNode",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectConversionNodeTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	Requests[0].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;
	Requests[1].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnectConversionNode placement creates two nodes"), Handles.Num(), 2);

	UAnimGraphNode_Root* RootNode = FindResultRootNode(Fixture.AnimGraph);
	bOk &= TestNotNull(TEXT("AutoConnectConversionNode Result root node is found"), RootNode);

	UAnimGraphNode_ComponentToLocalSpace* ConversionNode = nullptr;
	for (UEdGraphNode* Node : Fixture.AnimGraph->Nodes)
	{
		if ((ConversionNode = Cast<UAnimGraphNode_ComponentToLocalSpace>(Node)) != nullptr)
		{
			break;
		}
	}
	bOk &= TestNotNull(TEXT("AutoConnectConversionNode finds a spawned ComponentToLocalSpace node"), ConversionNode);

	if (ConversionNode && RootNode)
	{
		// 変換ノードはResultの左側にKawaiiPhysicsノード用の予約幅(220)を空けて明示配置される
		bOk &= TestEqual(TEXT("Conversion node NodePosX reserves space left of Result"),
		                  ConversionNode->NodePosX, RootNode->NodePosX - 220);
		bOk &= TestEqual(TEXT("Conversion node NodePosY aligns with Result"),
		                  ConversionNode->NodePosY, RootNode->NodePosY);
	}

	if (ConversionNode && Handles.Num() == 2 && Handles[0].IsValid() && Handles[1].IsValid())
	{
		const FIntRect ConversionRect(
			ConversionNode->NodePosX,
			ConversionNode->NodePosY,
			ConversionNode->NodePosX + 250,
			ConversionNode->NodePosY + 120);

		for (int32 NodeIndex = 0; NodeIndex < Handles.Num(); ++NodeIndex)
		{
			const UAnimGraphNode_KawaiiPhysics* KawaiiNode = Handles[NodeIndex].Node.Get();
			const FIntRect KawaiiRect(
				KawaiiNode->NodePosX,
				KawaiiNode->NodePosY,
				KawaiiNode->NodePosX + 400,
				KawaiiNode->NodePosY + 260);
			const bool bOverlaps =
				ConversionRect.Min.X < KawaiiRect.Max.X &&
				ConversionRect.Max.X > KawaiiRect.Min.X &&
				ConversionRect.Min.Y < KawaiiRect.Max.Y &&
				ConversionRect.Max.Y > KawaiiRect.Min.Y;
			bOk &= TestFalse(
				*FString::Printf(TEXT("Conversion node rect does not overlap KP node %d rect"), NodeIndex),
				bOverlaps);
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectCommentFrameTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnectCommentFrame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectCommentFrameTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const FString CommentText = TEXT("Hair and tail physics with AutoConnect");
	const FString ExpectedTitle = (Settings ? Settings->McpCommentPrefix : FString(TEXT("[MCP] "))) + CommentText;

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	Requests[0].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;
	Requests[1].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint,
			Requests,
			EKawaiiPhysicsPlacementUpsertKey::TagAndRootBone,
			NAME_None,
			CommentText);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnectCommentFrame placement creates two nodes"), Handles.Num(), 2);

	UAnimGraphNode_Root* RootNode = FindResultRootNode(Fixture.AnimGraph);
	bOk &= TestNotNull(TEXT("AutoConnectCommentFrame Result root node is found"), RootNode);

	UAnimGraphNode_ComponentToLocalSpace* ConversionNode = nullptr;
	for (UEdGraphNode* Node : Fixture.AnimGraph->Nodes)
	{
		if ((ConversionNode = Cast<UAnimGraphNode_ComponentToLocalSpace>(Node)) != nullptr)
		{
			break;
		}
	}
	bOk &= TestNotNull(TEXT("AutoConnectCommentFrame finds a spawned ComponentToLocalSpace node"), ConversionNode);

	FKawaiiMcpCommentNode* McpCommentNode = FindMcpCommentNode(Fixture.AnimGraph, ExpectedTitle);
	bOk &= TestNotNull(TEXT("AutoConnectCommentFrame finds the MCP comment node"), McpCommentNode);

	if (ConversionNode && RootNode)
	{
		// 変換ノードスロット幅(220)自体は本修正の対象外。ここでは変化していないことを確認する
		bOk &= TestEqual(TEXT("AutoConnectCommentFrame conversion node NodePosX reserves space left of Result"),
		                  ConversionNode->NodePosX, RootNode->NodePosX - 220);
		bOk &= TestEqual(TEXT("AutoConnectCommentFrame conversion node NodePosY aligns with Result"),
		                  ConversionNode->NodePosY, RootNode->NodePosY);
	}

	if (McpCommentNode && ConversionNode)
	{
		const int32 CommentMaxX = McpCommentNode->NodePosX + McpCommentNode->NodeWidth;
		// コメント枠の右端が変換ノードの左端へ食い込まないこと（AutoConnect基準余白分離の検証）
		bOk &= TestTrue(TEXT("AutoConnectCommentFrame comment frame right edge does not overlap conversion node"),
		                CommentMaxX <= ConversionNode->NodePosX);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementWrapCountTest,
                                 "KawaiiPhysics.EditorScripting.Placement.WrapCount",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementWrapCountTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
			2);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests.Add(MakePlacementRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("preset_root"), GetKawaiiPhysicsEditorScriptingTagB()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Horizontal wrap placement creates four nodes"), Handles.Num(), 4);
		// BlockCols=2でリクエスト順に左から右へ並ぶため、各段の右端（段の最終リクエスト）が基準位置になる
		const FVector2D ExpectedPositions[] =
		{
			FVector2D(BasePosition.X - 500.0, BasePosition.Y),
			BasePosition,
			FVector2D(BasePosition.X - 500.0, BasePosition.Y + 300.0),
			FVector2D(BasePosition.X, BasePosition.Y + 300.0),
		};
		const int32 ExpectedPositionCount = UE_ARRAY_COUNT(ExpectedPositions);
		for (int32 NodeIndex = 0; NodeIndex < ExpectedPositionCount && Handles.IsValidIndex(NodeIndex); ++NodeIndex)
		{
			bOk &= TestNodePosition(
				*this,
				FString::Printf(TEXT("Horizontal wrap node %d"), NodeIndex),
				Handles[NodeIndex],
				ExpectedPositions[NodeIndex]);
		}
	}

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Vertical,
			2);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests.Add(MakePlacementRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("preset_root"), GetKawaiiPhysicsEditorScriptingTagB()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Vertical wrap placement creates four nodes"), Handles.Num(), 4);
		// BlockColsV=2で折り返し列がリクエスト順に左から右へ並ぶため、最後の折り返し列が基準位置になる
		const FVector2D ExpectedPositions[] =
		{
			FVector2D(BasePosition.X - 500.0, BasePosition.Y),
			FVector2D(BasePosition.X - 500.0, BasePosition.Y + 300.0),
			BasePosition,
			FVector2D(BasePosition.X, BasePosition.Y + 300.0),
		};
		const int32 ExpectedPositionCount = UE_ARRAY_COUNT(ExpectedPositions);
		for (int32 NodeIndex = 0; NodeIndex < ExpectedPositionCount && Handles.IsValidIndex(NodeIndex); ++NodeIndex)
		{
			bOk &= TestNodePosition(
				*this,
				FString::Printf(TEXT("Vertical wrap node %d"), NodeIndex),
				Handles[NodeIndex],
				ExpectedPositions[NodeIndex]);
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementSpacingSettingTest,
                                 "KawaiiPhysics.EditorScripting.Placement.SpacingSetting",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementSpacingSettingTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
			0,
			450,
			270);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 450);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests.Add(MakePlacementRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Horizontal custom spacing placement creates three nodes"), Handles.Num(), 3);
		// リクエスト順に左から右へ並ぶため、最後のリクエストが基準位置になる
		const FVector2D ExpectedPositions[] =
		{
			FVector2D(BasePosition.X - 900.0, BasePosition.Y),
			FVector2D(BasePosition.X - 450.0, BasePosition.Y),
			BasePosition,
		};
		const int32 ExpectedPositionCount = UE_ARRAY_COUNT(ExpectedPositions);
		for (int32 NodeIndex = 0; NodeIndex < ExpectedPositionCount && Handles.IsValidIndex(NodeIndex); ++NodeIndex)
		{
			bOk &= TestNodePosition(
				*this,
				FString::Printf(TEXT("Horizontal custom spacing node %d"), NodeIndex),
				Handles[NodeIndex],
				ExpectedPositions[NodeIndex]);
		}
	}

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Vertical,
			0,
			450,
			270);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 450);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests.Add(MakePlacementRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Vertical custom spacing placement creates three nodes"), Handles.Num(), 3);
		const FVector2D ExpectedPositions[] =
		{
			BasePosition,
			FVector2D(BasePosition.X, BasePosition.Y + 270.0),
			FVector2D(BasePosition.X, BasePosition.Y + 540.0),
		};
		const int32 ExpectedPositionCount = UE_ARRAY_COUNT(ExpectedPositions);
		for (int32 NodeIndex = 0; NodeIndex < ExpectedPositionCount && Handles.IsValidIndex(NodeIndex); ++NodeIndex)
		{
			bOk &= TestNodePosition(
				*this,
				FString::Printf(TEXT("Vertical custom spacing node %d"), NodeIndex),
				Handles[NodeIndex],
				ExpectedPositions[NodeIndex]);
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementSpacingClampTest,
                                 "KawaiiPhysics.EditorScripting.Placement.SpacingClamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementSpacingClampTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	// GetMutableDefaultへの直接代入はUPROPERTYのClampMin metaを通らないため、下限未満の値がそのまま入る
	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
			0,
			100,
			100);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 400);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Horizontal clamped spacing placement creates two nodes"), Handles.Num(), 2);
		// リクエスト順に左から右へ並ぶため、最後のリクエストが基準位置になる
		if (Handles.IsValidIndex(0))
		{
			bOk &= TestNodePosition(
				*this,
				TEXT("Horizontal clamped spacing first node"),
				Handles[0],
				FVector2D(BasePosition.X - 400.0, BasePosition.Y));
		}
		if (Handles.IsValidIndex(1))
		{
			bOk &= TestNodePosition(*this, TEXT("Horizontal clamped spacing second node"), Handles[1], BasePosition);
		}
	}

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Vertical,
			0,
			100,
			100);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 400);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Vertical clamped spacing placement creates two nodes"), Handles.Num(), 2);
		if (Handles.IsValidIndex(0))
		{
			bOk &= TestNodePosition(*this, TEXT("Vertical clamped spacing first node"), Handles[0], BasePosition);
		}
		if (Handles.IsValidIndex(1))
		{
			bOk &= TestNodePosition(
				*this,
				TEXT("Vertical clamped spacing second node"),
				Handles[1],
				FVector2D(BasePosition.X, BasePosition.Y + 260.0));
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementDirectionRequestOverrideTest,
                                 "KawaiiPhysics.EditorScripting.Placement.DirectionRequestOverride",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementDirectionRequestOverrideTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Auto,
			0);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests[0].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;
		Requests[1].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Horizontal;

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Horizontal override placement creates two nodes"), Handles.Num(), 2);
		// リクエスト順に左から右へ並ぶため、最後のリクエストが基準位置になる
		if (Handles.IsValidIndex(0))
		{
			bOk &= TestNodePosition(
				*this,
				TEXT("Horizontal override first node"),
				Handles[0],
				FVector2D(BasePosition.X - 500.0, BasePosition.Y));
		}
		if (Handles.IsValidIndex(1))
		{
			bOk &= TestNodePosition(*this, TEXT("Horizontal override second node"), Handles[1], BasePosition);
		}
	}

	{
		FScopedMcpPlacementSettings PlacementSettings(
			EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
			0);
		FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
		if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
		{
			return false;
		}

		const FVector2D BasePosition = GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph);
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakePlacementRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
		Requests.Add(MakePlacementRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
		Requests[0].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Vertical;
		Requests[1].PlacementDirection = EKawaiiPhysicsNodePlacementDirectionOverride::Vertical;

		TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

		bOk &= TestEqual(TEXT("Vertical override placement creates two nodes"), Handles.Num(), 2);
		if (Handles.IsValidIndex(0))
		{
			bOk &= TestNodePosition(*this, TEXT("Vertical override first node"), Handles[0], BasePosition);
		}
		if (Handles.IsValidIndex(1))
		{
			bOk &= TestNodePosition(
				*this,
				TEXT("Vertical override second node"),
				Handles[1],
				FVector2D(BasePosition.X, BasePosition.Y + 300.0));
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementHorizontalRequestOrderTest,
                                 "KawaiiPhysics.EditorScripting.Placement.HorizontalRequestOrder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementHorizontalRequestOrderTest::RunTest(const FString& Parameters)
{
	FScopedMcpPlacementSettings PlacementSettings(
		EKawaiiPhysicsMcpNodePlacementDirection::Horizontal,
		0);
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	// AutoConnectのチェーンはリクエスト順=上流→下流のため、横配置もリクエスト順に左から右へ
	// 並ぶことを確認する（最後のリクエストがResult直前=基準位置）
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	Requests.Add(MakeAutoConnectRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("HorizontalRequestOrder placement creates three nodes"), Handles.Num(), 3);
	if (Handles.Num() != 3 || !Handles[0].IsValid() || !Handles[1].IsValid() || !Handles[2].IsValid())
	{
		return bOk;
	}

	const FVector2D BasePosition =
		GetExpectedAutoPlacementBasePosition(*this, Fixture.AnimGraph, 500, true);
	UAnimGraphNode_KawaiiPhysics* FirstNode = Handles[0].Node.Get();
	UAnimGraphNode_KawaiiPhysics* SecondNode = Handles[1].Node.Get();
	UAnimGraphNode_KawaiiPhysics* ThirdNode = Handles[2].Node.Get();

	bOk &= TestEqual(TEXT("HorizontalRequestOrder last node X is the base position"),
	                  ThirdNode->NodePosX, static_cast<int32>(BasePosition.X));
	bOk &= TestTrue(TEXT("HorizontalRequestOrder nodes are ordered left to right by request order"),
	                FirstNode->NodePosX < SecondNode->NodePosX &&
	                SecondNode->NodePosX < ThirdNode->NodePosX);

	UEdGraphPin* FirstPosePin = GetKawaiiPosePin(FirstNode);
	UEdGraphPin* SecondComponentPosePin = GetKawaiiComponentPosePin(SecondNode);
	UEdGraphPin* SecondPosePin = GetKawaiiPosePin(SecondNode);
	UEdGraphPin* ThirdComponentPosePin = GetKawaiiComponentPosePin(ThirdNode);
	UEdGraphPin* ThirdPosePin = GetKawaiiPosePin(ThirdNode);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1
			? Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode())
			: nullptr;
	UEdGraphPin* ComponentToLocalSpaceInputPin =
		FindFirstPosePin(ComponentToLocalSpaceNode, EGPD_Input);

	bOk &= TestTrue(TEXT("HorizontalRequestOrder keeps first Pose connected to second ComponentPose"),
	                FirstPosePin &&
	                SecondComponentPosePin &&
	                FirstPosePin->LinkedTo.Num() == 1 &&
	                FirstPosePin->LinkedTo[0] == SecondComponentPosePin);
	bOk &= TestTrue(TEXT("HorizontalRequestOrder keeps second Pose connected to third ComponentPose"),
	                SecondPosePin &&
	                ThirdComponentPosePin &&
	                SecondPosePin->LinkedTo.Num() == 1 &&
	                SecondPosePin->LinkedTo[0] == ThirdComponentPosePin);
	bOk &= TestTrue(TEXT("HorizontalRequestOrder keeps third Pose connected toward Result"),
	                ThirdPosePin &&
	                ComponentToLocalSpaceInputPin &&
	                ThirdPosePin->LinkedTo.Num() == 1 &&
	                ThirdPosePin->LinkedTo[0] == ComponentToLocalSpaceInputPin);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementCommentTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Comment",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementCommentTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	const UKawaiiPhysicsDeveloperSettings* Settings = GetDefault<UKawaiiPhysicsDeveloperSettings>();
	const FString CommentText = TEXT("Hair and tail physics");
	const FString InitialPrompt = TEXT("Add KawaiiPhysics to hair and tail from MCP.");
	const FString UpdatedPrompt = TEXT("Add KawaiiPhysics to hair and tail with the latest MCP instruction.");
	const FString ExpectedTitle = (Settings ? Settings->McpCommentPrefix : FString(TEXT("[MCP] "))) + CommentText;
	const FLinearColor ExpectedColor =
		Settings ? Settings->McpCommentColor : FLinearColor(0.55f, 0.45f, 0.85f);
	UEdGraphNode_Comment* ManualCommentNode =
		AddManualCommentNode(Fixture.AnimGraph, ExpectedTitle, FVector2D(-1400.0f, -300.0f));

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	FKawaiiPhysicsNodePlacementRequest FirstRequest;
	FirstRequest.RootBoneName = TEXT("hair_01");
	FirstRequest.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagA();
	FirstRequest.bAutoPosition = false;
	FirstRequest.NodePosition = FVector2D(-900.0f, 120.0f);
	Requests.Add(FirstRequest);

	FKawaiiPhysicsNodePlacementRequest SecondRequest;
	SecondRequest.RootBoneName = TEXT("tail_01");
	SecondRequest.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagB();
	SecondRequest.bAutoPosition = false;
	SecondRequest.NodePosition = FVector2D(-400.0f, 420.0f);
	Requests.Add(SecondRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> FirstHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint,
			Requests,
			EKawaiiPhysicsPlacementUpsertKey::TagAndRootBone,
			NAME_None,
			CommentText,
			InitialPrompt);
	FKawaiiMcpCommentNode* McpCommentNode = FindMcpCommentNode(Fixture.AnimGraph, ExpectedTitle);
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
	FDateTime InitialCommentCreatedAt;
	FDateTime InitialCommentUpdatedAt;
	const int32 ExpectedCommentNodeCount = 2;
#else
	const int32 ExpectedCommentNodeCount = 1;
#endif

	bool bOk = true;
	bOk &= TestNotNull(TEXT("Manual comment setup is created"), ManualCommentNode);
	bOk &= TestEqual(TEXT("Manual comment remains a plain comment node"),
	                 ManualCommentNode ? ManualCommentNode->GetClass() : nullptr,
	                 UEdGraphNode_Comment::StaticClass());
	bOk &= TestEqual(TEXT("Comment placement returns two handles"), FirstHandles.Num(), 2);
	bOk &= TestTrue(TEXT("First comment handle is valid"),
	                FirstHandles.IsValidIndex(0) && FirstHandles[0].IsValid());
	bOk &= TestTrue(TEXT("Second comment handle is valid"),
	                FirstHandles.IsValidIndex(1) && FirstHandles[1].IsValid());
	bOk &= TestEqual(TEXT("Exactly one MCP comment node is created"),
	                 CountExactNodesOfClass(Fixture.AnimGraph, FKawaiiMcpCommentNode::StaticClass()), 1);
	bOk &= TestEqual(TEXT("Comment frame count matches supported node mode"),
	                 CountNodesOfClass(Fixture.AnimGraph, UEdGraphNode_Comment::StaticClass()), ExpectedCommentNodeCount);
	bOk &= TestNotNull(TEXT("MCP comment node is found by title"), McpCommentNode);

	if (McpCommentNode &&
		FirstHandles.Num() == 2 &&
		FirstHandles[0].IsValid() &&
		FirstHandles[1].IsValid())
	{
		UAnimGraphNode_KawaiiPhysics* FirstNode = FirstHandles[0].Node.Get();
		UAnimGraphNode_KawaiiPhysics* SecondNode = FirstHandles[1].Node.Get();
		const int32 MinNodeX = FMath::Min(FirstNode->NodePosX, SecondNode->NodePosX);
		const int32 MinNodeY = FMath::Min(FirstNode->NodePosY, SecondNode->NodePosY);
		const int32 MaxNodeX = FMath::Max(FirstNode->NodePosX, SecondNode->NodePosX);
		const int32 MaxNodeY = FMath::Max(FirstNode->NodePosY, SecondNode->NodePosY);
		const int32 CommentMaxX = McpCommentNode->NodePosX + McpCommentNode->NodeWidth;
		const int32 CommentMaxY = McpCommentNode->NodePosY + McpCommentNode->NodeHeight;
		const FCommentNodeSet& NodesUnderComment = McpCommentNode->GetNodesUnderComment();

		bOk &= TestEqual(TEXT("MCP comment title includes configured prefix"),
		                 McpCommentNode->NodeComment, ExpectedTitle);
		bOk &= TestTrue(TEXT("MCP comment color matches configured color"),
		                McpCommentNode->CommentColor.Equals(ExpectedColor));
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
		bOk &= TestEqual(TEXT("MCP comment stores prompt"),
		                  McpCommentNode->Prompt, InitialPrompt);
		bOk &= TestTrue(TEXT("MCP comment CreatedAt is valid"),
		                McpCommentNode->CreatedAt.GetTicks() > 0);
		bOk &= TestTrue(TEXT("MCP comment UpdatedAt is valid"),
		                McpCommentNode->UpdatedAt.GetTicks() > 0);
		InitialCommentCreatedAt = McpCommentNode->CreatedAt;
		InitialCommentUpdatedAt = McpCommentNode->UpdatedAt;
#endif
		bOk &= TestTrue(TEXT("MCP comment includes placed node positions"),
		                McpCommentNode->NodePosX <= MinNodeX &&
		                McpCommentNode->NodePosY <= MinNodeY &&
		                CommentMaxX >= MaxNodeX &&
		                CommentMaxY >= MaxNodeY);
		bOk &= TestTrue(TEXT("MCP comment covers expected node width and horizontal padding"),
		                McpCommentNode->NodePosX <= MinNodeX - 50 &&
		                CommentMaxX >= MaxNodeX + 450);
		bOk &= TestTrue(TEXT("MCP comment covers expected node height and vertical padding"),
		                McpCommentNode->NodePosY <= MinNodeY - 80 &&
		                CommentMaxY >= MaxNodeY + 310);
		bOk &= TestEqual(TEXT("MCP comment tracks two nodes"), NodesUnderComment.Num(), 2);
		bOk &= TestTrue(TEXT("MCP comment tracks first node"), NodesUnderComment.Contains(FirstNode));
		bOk &= TestTrue(TEXT("MCP comment tracks second node"), NodesUnderComment.Contains(SecondNode));
	}

	TArray<FKawaiiPhysicsGraphNodeHandle> SecondHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint,
			Requests,
			EKawaiiPhysicsPlacementUpsertKey::TagAndRootBone,
			NAME_None,
			CommentText,
			UpdatedPrompt);
	McpCommentNode = FindMcpCommentNode(Fixture.AnimGraph, ExpectedTitle);
	bOk &= TestEqual(TEXT("Comment upsert returns two handles"), SecondHandles.Num(), 2);
	bOk &= TestTrue(TEXT("First comment upsert handle is valid"),
	                SecondHandles.IsValidIndex(0) && SecondHandles[0].IsValid());
	bOk &= TestTrue(TEXT("Second comment upsert handle is valid"),
	                SecondHandles.IsValidIndex(1) && SecondHandles[1].IsValid());
	bOk &= TestEqual(TEXT("Comment upsert keeps one MCP comment"),
	                 CountExactNodesOfClass(Fixture.AnimGraph, FKawaiiMcpCommentNode::StaticClass()), 1);
	bOk &= TestEqual(TEXT("Comment upsert keeps expected comment frame count"),
	                 CountNodesOfClass(Fixture.AnimGraph, UEdGraphNode_Comment::StaticClass()), ExpectedCommentNodeCount);
	bOk &= TestEqual(TEXT("Comment upsert keeps KawaiiPhysics node count unchanged"),
	                 CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_KawaiiPhysics::StaticClass()), 2);
	if (McpCommentNode)
	{
		const FCommentNodeSet& NodesUnderComment = McpCommentNode->GetNodesUnderComment();
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
		bOk &= TestEqual(TEXT("Comment upsert updates prompt"),
		                  McpCommentNode->Prompt, UpdatedPrompt);
		bOk &= TestTrue(TEXT("Comment upsert keeps CreatedAt valid"),
		                McpCommentNode->CreatedAt.GetTicks() > 0);
		bOk &= TestTrue(TEXT("Comment upsert keeps UpdatedAt valid"),
		                McpCommentNode->UpdatedAt.GetTicks() > 0);
		bOk &= TestEqual(TEXT("Comment upsert keeps CreatedAt unchanged"),
		                  McpCommentNode->CreatedAt, InitialCommentCreatedAt);
		bOk &= TestTrue(TEXT("Comment upsert does not regress UpdatedAt"),
		                McpCommentNode->UpdatedAt >= InitialCommentUpdatedAt);
#endif
		bOk &= TestEqual(TEXT("Comment upsert keeps two nodes under comment"), NodesUnderComment.Num(), 2);
		if (SecondHandles.Num() == 2 && SecondHandles[0].IsValid() && SecondHandles[1].IsValid())
		{
			bOk &= TestTrue(TEXT("Comment upsert tracks first upserted node"),
			                NodesUnderComment.Contains(SecondHandles[0].Node.Get()));
			bOk &= TestTrue(TEXT("Comment upsert tracks second upserted node"),
			                NodesUnderComment.Contains(SecondHandles[1].Node.Get()));
		}
	}

	TArray<FKawaiiPhysicsAnimGraphCommentInfo> CommentInfos =
		UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(Fixture.AnimBlueprint);
	bOk &= TestEqual(TEXT("GetAnimGraphComments returns expected comment count"), CommentInfos.Num(), ExpectedCommentNodeCount);
	int32 McpInfoCount = 0;
	int32 ManualInfoCount = 0;
	for (const FKawaiiPhysicsAnimGraphCommentInfo& CommentInfo : CommentInfos)
	{
		if (CommentInfo.Title != ExpectedTitle)
		{
			continue;
		}

		if (CommentInfo.bMcpComment)
		{
			++McpInfoCount;
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
			bOk &= TestEqual(TEXT("GetAnimGraphComments returns MCP prompt"),
			                  CommentInfo.Prompt, UpdatedPrompt);
			bOk &= TestTrue(TEXT("GetAnimGraphComments returns MCP CreatedAt"),
			                CommentInfo.CreatedAt.GetTicks() > 0);
			bOk &= TestTrue(TEXT("GetAnimGraphComments returns MCP UpdatedAt"),
			                CommentInfo.UpdatedAt.GetTicks() > 0);
#endif
		}
		else
		{
			++ManualInfoCount;
			bOk &= TestTrue(TEXT("GetAnimGraphComments returns empty prompt for manual comment"),
			                CommentInfo.Prompt.IsEmpty());
		}
	}
#if KAWAII_PHYSICS_MCP_COMMENT_NODE_SUPPORTED
	bOk &= TestEqual(TEXT("GetAnimGraphComments returns one MCP comment"), McpInfoCount, 1);
	bOk &= TestEqual(TEXT("GetAnimGraphComments returns one manual comment"), ManualInfoCount, 1);
#else
	bOk &= TestEqual(TEXT("GetAnimGraphComments returns no MCP metadata comments"), McpInfoCount, 0);
	bOk &= TestEqual(TEXT("GetAnimGraphComments returns one plain comment"), ManualInfoCount, 1);
#endif

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementCommentEmptyTest,
                                 "KawaiiPhysics.EditorScripting.Placement.CommentEmpty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementCommentEmptyTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture OmittedCommentFixture = MakeEmptyFixture(*this);
	FKawaiiPhysicsEditorScriptingFixture WhitespaceCommentFixture = MakeEmptyFixture(*this);
	if (!OmittedCommentFixture.AnimBlueprint || !OmittedCommentFixture.AnimGraph ||
		!WhitespaceCommentFixture.AnimBlueprint || !WhitespaceCommentFixture.AnimGraph)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest OmittedCommentRequest;
	OmittedCommentRequest.RootBoneName = TEXT("hair_01");
	TArray<FKawaiiPhysicsNodePlacementRequest> OmittedCommentRequests;
	OmittedCommentRequests.Add(OmittedCommentRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> OmittedCommentHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			OmittedCommentFixture.AnimBlueprint, OmittedCommentRequests);

	FKawaiiPhysicsNodePlacementRequest WhitespaceCommentRequest;
	WhitespaceCommentRequest.RootBoneName = TEXT("tail_01");
	TArray<FKawaiiPhysicsNodePlacementRequest> WhitespaceCommentRequests;
	WhitespaceCommentRequests.Add(WhitespaceCommentRequest);

	TArray<FKawaiiPhysicsGraphNodeHandle> WhitespaceCommentHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			WhitespaceCommentFixture.AnimBlueprint,
			WhitespaceCommentRequests,
			EKawaiiPhysicsPlacementUpsertKey::None,
			NAME_None,
			TEXT("   \t  "),
			TEXT("Prompt should not be stored without a comment."));

	bool bOk = true;
	bOk &= TestEqual(TEXT("Omitted comment placement creates one node"), OmittedCommentHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Omitted comment creates no comments"),
	                 CountNodesOfClass(OmittedCommentFixture.AnimGraph, UEdGraphNode_Comment::StaticClass()), 0);
	bOk &= TestTrue(TEXT("Omitted comment API returns no comments"),
	                UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(OmittedCommentFixture.AnimBlueprint).IsEmpty());
	bOk &= TestEqual(TEXT("Whitespace comment placement creates one node"), WhitespaceCommentHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Whitespace-only comment creates no comments"),
	                 CountNodesOfClass(WhitespaceCommentFixture.AnimGraph, UEdGraphNode_Comment::StaticClass()), 0);
	bOk &= TestTrue(TEXT("Whitespace-only comment API returns no comments"),
	                UKawaiiPhysicsEditorLibrary::GetAnimGraphComments(WhitespaceCommentFixture.AnimBlueprint).IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementUpsertTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Upsert",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementUpsertTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBoneName = TEXT("hair_01");
	Request.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagA();
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FKawaiiPhysicsGraphNodeHandle> FirstHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);

	Requests[0].RootBoneName = TEXT("tail_01");
	TArray<FKawaiiPhysicsGraphNodeHandle> SecondHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);

	TArray<FKawaiiPhysicsGraphNodeHandle> CollectedHandles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(
			Fixture.AnimBlueprint, FGameplayTagContainer(), false);

	bool bOk = true;
	bOk &= TestEqual(TEXT("First upsert creates one node"), FirstHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Second upsert returns one node"), SecondHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Upsert keeps node count unchanged"), CollectedHandles.Num(), 1);
	if (!SecondHandles.IsEmpty() && SecondHandles[0].IsValid())
	{
		bOk &= TestEqual(TEXT("Upsert updates RootBone"),
		                  SecondHandles[0].Node->Node.RootBone.BoneName, FName(TEXT("tail_01")));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementUpsertKeepsPositionTest,
                                 "KawaiiPhysics.EditorScripting.Placement.UpsertKeepsPosition",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementUpsertKeepsPositionTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBoneName = TEXT("hair_01");
	Request.KawaiiPhysicsTag = GetKawaiiPhysicsEditorScriptingTagA();
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FKawaiiPhysicsGraphNodeHandle> FirstHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);
	if (FirstHandles.IsValidIndex(0) && FirstHandles[0].IsValid())
	{
		FirstHandles[0].Node->NodePosX = -1234;
		FirstHandles[0].Node->NodePosY = 567;
	}

	Requests[0].RootBoneName = TEXT("tail_01");
	TArray<FKawaiiPhysicsGraphNodeHandle> SecondHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);

	bool bOk = true;
	bOk &= TestEqual(TEXT("First upsert placement creates one node"), FirstHandles.Num(), 1);
	bOk &= TestEqual(TEXT("Second upsert placement returns one node"), SecondHandles.Num(), 1);
	if (SecondHandles.IsValidIndex(0) && SecondHandles[0].IsValid())
	{
		bOk &= TestEqual(TEXT("Auto-position upsert keeps NodePosX"), SecondHandles[0].Node->NodePosX, -1234);
		bOk &= TestEqual(TEXT("Auto-position upsert keeps NodePosY"), SecondHandles[0].Node->NodePosY, 567);
		bOk &= TestEqual(TEXT("Auto-position upsert still updates RootBone"),
		                  SecondHandles[0].Node->Node.RootBone.BoneName, FName(TEXT("tail_01")));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectBasicTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectBasicTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnect basic creates one node"), Handles.Num(), 1);
	UAnimGraphNode_KawaiiPhysics* KawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UEdGraphPin* KawaiiComponentPosePin = GetKawaiiComponentPosePin(KawaiiNode);
	UEdGraphPin* KawaiiPosePin = GetKawaiiPosePin(KawaiiNode);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1
			? Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode())
			: nullptr;
	UEdGraphPin* ComponentToLocalSpaceInputPin =
		FindFirstPosePin(ComponentToLocalSpaceNode, EGPD_Input);

	bOk &= TestNotNull(TEXT("Result is linked through ComponentToLocalSpace"), ComponentToLocalSpaceNode);
	bOk &= TestTrue(TEXT("ComponentToLocalSpace input is linked to Kawaii Pose"),
	                ComponentToLocalSpaceInputPin &&
	                ComponentToLocalSpaceInputPin->LinkedTo.Num() == 1 &&
	                ComponentToLocalSpaceInputPin->LinkedTo[0] == KawaiiPosePin);
	bOk &= TestTrue(TEXT("Kawaii Pose is linked to ComponentToLocalSpace input"),
	                KawaiiPosePin &&
	                KawaiiPosePin->LinkedTo.Num() == 1 &&
	                KawaiiPosePin->LinkedTo[0] == ComponentToLocalSpaceInputPin);
	bOk &= TestTrue(TEXT("Kawaii ComponentPose remains unconnected"),
	                KawaiiComponentPosePin && KawaiiComponentPosePin->LinkedTo.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectSerialTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.Serial",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectSerialTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnect serial creates two nodes"), Handles.Num(), 2);
	UAnimGraphNode_KawaiiPhysics* FirstNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;
	UAnimGraphNode_KawaiiPhysics* SecondNode =
		Handles.IsValidIndex(1) && Handles[1].IsValid() ? Handles[1].Node.Get() : nullptr;
	UEdGraphPin* FirstPosePin = GetKawaiiPosePin(FirstNode);
	UEdGraphPin* SecondComponentPosePin = GetKawaiiComponentPosePin(SecondNode);
	UEdGraphPin* SecondPosePin = GetKawaiiPosePin(SecondNode);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1
			? Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode())
			: nullptr;
	UEdGraphPin* ComponentToLocalSpaceInputPin =
		FindFirstPosePin(ComponentToLocalSpaceNode, EGPD_Input);

	bOk &= TestTrue(TEXT("First Pose connects directly to second ComponentPose"),
	                FirstPosePin &&
	                SecondComponentPosePin &&
	                FirstPosePin->LinkedTo.Num() == 1 &&
	                FirstPosePin->LinkedTo[0] == SecondComponentPosePin &&
	                SecondComponentPosePin->LinkedTo.Num() == 1 &&
	                SecondComponentPosePin->LinkedTo[0] == FirstPosePin);
	bOk &= TestTrue(TEXT("Second Pose connects through ComponentToLocalSpace to Result"),
	                SecondPosePin &&
	                ComponentToLocalSpaceInputPin &&
	                SecondPosePin->LinkedTo.Num() == 1 &&
	                SecondPosePin->LinkedTo[0] == ComponentToLocalSpaceInputPin);
	bOk &= TestEqual(TEXT("Serial graph has exactly one ComponentToLocalSpace node"),
	                 CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_ComponentToLocalSpace::StaticClass()), 1);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectAppendTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.Append",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectAppendTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> SerialRequests;
	SerialRequests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	SerialRequests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	TArray<FKawaiiPhysicsGraphNodeHandle> SerialHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, SerialRequests);
	const int32 ComponentToLocalSpaceCountBefore =
		CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_ComponentToLocalSpace::StaticClass());

	TArray<FKawaiiPhysicsNodePlacementRequest> AppendRequests;
	AppendRequests.Add(MakeAutoConnectRequest(TEXT("bang_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	TArray<FKawaiiPhysicsGraphNodeHandle> AppendHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, AppendRequests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnect append setup creates two nodes"), SerialHandles.Num(), 2);
	bOk &= TestEqual(TEXT("AutoConnect append creates one node"), AppendHandles.Num(), 1);
	UAnimGraphNode_KawaiiPhysics* SecondNode =
		SerialHandles.IsValidIndex(1) && SerialHandles[1].IsValid() ? SerialHandles[1].Node.Get() : nullptr;
	UAnimGraphNode_KawaiiPhysics* ThirdNode =
		AppendHandles.IsValidIndex(0) && AppendHandles[0].IsValid() ? AppendHandles[0].Node.Get() : nullptr;
	UEdGraphPin* SecondPosePin = GetKawaiiPosePin(SecondNode);
	UEdGraphPin* ThirdComponentPosePin = GetKawaiiComponentPosePin(ThirdNode);
	UEdGraphPin* ThirdPosePin = GetKawaiiPosePin(ThirdNode);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalSpaceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1
			? Cast<UAnimGraphNode_ComponentToLocalSpace>(ResultPin->LinkedTo[0]->GetOwningNode())
			: nullptr;
	UEdGraphPin* ComponentToLocalSpaceInputPin =
		FindFirstPosePin(ComponentToLocalSpaceNode, EGPD_Input);

	bOk &= TestEqual(TEXT("Append reuses the existing ComponentToLocalSpace node"),
	                 CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_ComponentToLocalSpace::StaticClass()),
	                 ComponentToLocalSpaceCountBefore);
	bOk &= TestEqual(TEXT("Append keeps exactly one ComponentToLocalSpace node"),
	                 ComponentToLocalSpaceCountBefore, 1);
	bOk &= TestTrue(TEXT("Second Pose connects directly to appended ComponentPose"),
	                SecondPosePin &&
	                ThirdComponentPosePin &&
	                SecondPosePin->LinkedTo.Num() == 1 &&
	                SecondPosePin->LinkedTo[0] == ThirdComponentPosePin);
	bOk &= TestTrue(TEXT("Appended Pose connects to reused ComponentToLocalSpace input"),
	                ThirdPosePin &&
	                ComponentToLocalSpaceInputPin &&
	                ThirdPosePin->LinkedTo.Num() == 1 &&
	                ThirdPosePin->LinkedTo[0] == ComponentToLocalSpaceInputPin);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectSpaceConversionTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.SpaceConversion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectSpaceConversionTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	UAnimGraphNode_LocalRefPose* LocalRefPoseNode = AddLocalRefPoseNode(Fixture.AnimGraph, FVector2D(-600.0f, 0.0f));
	UEdGraphPin* LocalRefPosePin = FindFirstPosePin(LocalRefPoseNode, EGPD_Output);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	const UAnimationGraphSchema* Schema = CastChecked<UAnimationGraphSchema>(Fixture.AnimGraph->GetSchema());
	const bool bConnectedLocalRefPose = LocalRefPosePin && ResultPin && Schema->TryCreateConnection(LocalRefPosePin, ResultPin);
	TestTrue(TEXT("LocalRefPose setup connects directly to Result"), bConnectedLocalRefPose);
	if (!bConnectedLocalRefPose)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnect space conversion creates one node"), Handles.Num(), 1);
	UAnimGraphNode_KawaiiPhysics* KawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;
	UEdGraphPin* KawaiiComponentPosePin = GetKawaiiComponentPosePin(KawaiiNode);
	UEdGraphPin* KawaiiPosePin = GetKawaiiPosePin(KawaiiNode);
	ResultPin = GetResultPin(Fixture.AnimGraph);

	bOk &= TestEqual(TEXT("Space conversion inserts one LocalToComponentSpace node"),
	                 CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_LocalToComponentSpace::StaticClass()), 1);
	bOk &= TestTrue(TEXT("Kawaii ComponentPose is connected through LocalToComponentSpace"),
	                KawaiiComponentPosePin &&
	                KawaiiComponentPosePin->LinkedTo.Num() == 1 &&
	                KawaiiComponentPosePin->LinkedTo[0]->GetOwningNode()->IsA<UAnimGraphNode_LocalToComponentSpace>());
	bOk &= TestTrue(TEXT("Kawaii Pose remains connected toward Result"),
	                KawaiiPosePin && !KawaiiPosePin->LinkedTo.IsEmpty());
	bOk &= TestTrue(TEXT("Result remains connected after auto-wiring"),
	                ResultPin && !ResultPin->LinkedTo.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectUpsertKeepsWiringTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.UpsertKeepsWiring",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectUpsertKeepsWiringTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	TArray<FKawaiiPhysicsGraphNodeHandle> FirstHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);
	UAnimGraphNode_KawaiiPhysics* FirstNode =
		FirstHandles.IsValidIndex(0) && FirstHandles[0].IsValid() ? FirstHandles[0].Node.Get() : nullptr;
	UEdGraphPin* ComponentPosePinBefore = GetKawaiiComponentPosePin(FirstNode);
	UEdGraphPin* PosePinBefore = GetKawaiiPosePin(FirstNode);
	const int32 ComponentPoseLinkCountBefore = ComponentPosePinBefore ? ComponentPosePinBefore->LinkedTo.Num() : INDEX_NONE;
	const int32 PoseLinkCountBefore = PosePinBefore ? PosePinBefore->LinkedTo.Num() : INDEX_NONE;
	UEdGraphNode* ComponentPosePeerNodeBefore =
		ComponentPosePinBefore && ComponentPosePinBefore->LinkedTo.Num() == 1
			? ComponentPosePinBefore->LinkedTo[0]->GetOwningNode()
			: nullptr;
	UEdGraphNode* PosePeerNodeBefore =
		PosePinBefore && PosePinBefore->LinkedTo.Num() == 1
			? PosePinBefore->LinkedTo[0]->GetOwningNode()
			: nullptr;

	Requests[0].RootBoneName = TEXT("tail_01");
	TArray<FKawaiiPhysicsGraphNodeHandle> SecondHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(
			Fixture.AnimBlueprint, Requests, EKawaiiPhysicsPlacementUpsertKey::Tag);
	UAnimGraphNode_KawaiiPhysics* SecondNode =
		SecondHandles.IsValidIndex(0) && SecondHandles[0].IsValid() ? SecondHandles[0].Node.Get() : nullptr;
	UEdGraphPin* ComponentPosePinAfter = GetKawaiiComponentPosePin(SecondNode);
	UEdGraphPin* PosePinAfter = GetKawaiiPosePin(SecondNode);
	UEdGraphNode* ComponentPosePeerNodeAfter =
		ComponentPosePinAfter && ComponentPosePinAfter->LinkedTo.Num() == 1
			? ComponentPosePinAfter->LinkedTo[0]->GetOwningNode()
			: nullptr;
	UEdGraphNode* PosePeerNodeAfter =
		PosePinAfter && PosePinAfter->LinkedTo.Num() == 1
			? PosePinAfter->LinkedTo[0]->GetOwningNode()
			: nullptr;

	bool bOk = true;
	bOk &= TestEqual(TEXT("AutoConnect upsert returns the same node"), SecondNode, FirstNode);
	bOk &= TestEqual(TEXT("ComponentPose link count is unchanged"), ComponentPosePinAfter ? ComponentPosePinAfter->LinkedTo.Num() : INDEX_NONE, ComponentPoseLinkCountBefore);
	bOk &= TestEqual(TEXT("Pose link count is unchanged"), PosePinAfter ? PosePinAfter->LinkedTo.Num() : INDEX_NONE, PoseLinkCountBefore);
	bOk &= TestEqual(TEXT("ComponentPose peer identity is unchanged"), ComponentPosePeerNodeAfter, ComponentPosePeerNodeBefore);
	bOk &= TestEqual(TEXT("Pose peer identity is unchanged"), PosePeerNodeAfter, PosePeerNodeBefore);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectBackCompatTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.BackCompat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectBackCompatTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBoneName = TEXT("hair_01");
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("BackCompat placement creates one node"), Handles.Num(), 1);
	UAnimGraphNode_KawaiiPhysics* KawaiiNode =
		Handles.IsValidIndex(0) && Handles[0].IsValid() ? Handles[0].Node.Get() : nullptr;
	UEdGraphPin* ComponentPosePin = GetKawaiiComponentPosePin(KawaiiNode);
	UEdGraphPin* PosePin = GetKawaiiPosePin(KawaiiNode);
	bOk &= TestTrue(TEXT("BackCompat ComponentPose remains unconnected"),
	                ComponentPosePin && ComponentPosePin->LinkedTo.IsEmpty());
	bOk &= TestTrue(TEXT("BackCompat Pose remains unconnected"),
	                PosePin && PosePin->LinkedTo.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementAutoConnectKnotSkipTest,
                                 "KawaiiPhysics.EditorScripting.Placement.AutoConnect.KnotSkip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementAutoConnectKnotSkipTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint || !Fixture.AnimGraph)
	{
		return false;
	}

	TArray<FKawaiiPhysicsNodePlacementRequest> InitialRequests;
	InitialRequests.Add(MakeAutoConnectRequest(TEXT("hair_01"), GetKawaiiPhysicsEditorScriptingTagA()));
	TArray<FKawaiiPhysicsGraphNodeHandle> InitialHandles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, InitialRequests);
	UEdGraphPin* ResultPin = GetResultPin(Fixture.AnimGraph);
	UEdGraphPin* ExistingConversionOutputPin =
		ResultPin && ResultPin->LinkedTo.Num() == 1 ? ResultPin->LinkedTo[0] : nullptr;
	UK2Node_Knot* KnotNode = AddKnotNode(Fixture.AnimGraph, FVector2D(-250.0f, -150.0f));
	UEdGraphPin* KnotInputPin = FindFirstPin(KnotNode, EGPD_Input);
	UEdGraphPin* KnotOutputPin = FindFirstPin(KnotNode, EGPD_Output);
	const UAnimationGraphSchema* Schema = CastChecked<UAnimationGraphSchema>(Fixture.AnimGraph->GetSchema());
	const bool bConnectedExistingConversionToKnot =
		ExistingConversionOutputPin && KnotInputPin && Schema->TryCreateConnection(ExistingConversionOutputPin, KnotInputPin);
	const bool bConnectedKnotToResult =
		KnotOutputPin && ResultPin && Schema->TryCreateConnection(KnotOutputPin, ResultPin);
	UEdGraphNode* ResultSourceNodeBefore =
		ResultPin && ResultPin->LinkedTo.Num() == 1 ? ResultPin->LinkedTo[0]->GetOwningNode() : nullptr;
	TestTrue(TEXT("Knot setup connects existing conversion to knot"), bConnectedExistingConversionToKnot);
	TestTrue(TEXT("Knot setup connects knot to Result"), bConnectedKnotToResult);
	TestTrue(TEXT("Knot is immediately upstream of Result before auto-wiring"),
	         ResultSourceNodeBefore && ResultSourceNodeBefore->IsA<UK2Node_Knot>());
	if (InitialHandles.Num() != 1 ||
		!bConnectedExistingConversionToKnot ||
		!bConnectedKnotToResult ||
		!ResultSourceNodeBefore ||
		!ResultSourceNodeBefore->IsA<UK2Node_Knot>())
	{
		return false;
	}

	const int32 ComponentToLocalSpaceCountBefore =
		CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_ComponentToLocalSpace::StaticClass());
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(MakeAutoConnectRequest(TEXT("tail_01"), GetKawaiiPhysicsEditorScriptingTagB()));
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("KnotSkip placement creates one node"), Handles.Num(), 1);
	bOk &= TestEqual(TEXT("Knot setup has one initial ComponentToLocalSpace"),
	                 ComponentToLocalSpaceCountBefore, 1);
	bOk &= TestEqual(TEXT("KnotSkip does not reuse conversion hidden behind knot"),
	                 CountNodesOfClass(Fixture.AnimGraph, UAnimGraphNode_ComponentToLocalSpace::StaticClass()),
	                 ComponentToLocalSpaceCountBefore + 1);
	ResultPin = GetResultPin(Fixture.AnimGraph);
	UEdGraphNode* ResultSourceNode =
		ResultPin && ResultPin->LinkedTo.Num() == 1 ? ResultPin->LinkedTo[0]->GetOwningNode() : nullptr;
	bOk &= TestFalse(TEXT("Knot is not treated as Result conversion source"),
	                 ResultSourceNode && ResultSourceNode->IsA<UK2Node_Knot>());
	bOk &= TestTrue(TEXT("Result is linked through a direct ComponentToLocalSpace after KnotSkip"),
	                ResultSourceNode && ResultSourceNode->IsA<UAnimGraphNode_ComponentToLocalSpace>());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementValidationTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Validation.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementValidationTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBoneName = TEXT("missing_bone");
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FString> Errors =
		UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(Fixture.AnimBlueprint, Requests);
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestFalse(TEXT("Missing bone reports errors"), Errors.IsEmpty());
	bOk &= TestTrue(TEXT("Invalid placement is skipped"), Handles.IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementValidationEmptyRootTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Validation.EmptyRoot",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementValidationEmptyRootTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FString> Errors =
		UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Empty root reports exactly one error"), Errors.Num(), 1);
	if (Errors.IsValidIndex(0))
	{
		bOk &= TestEqual(
			TEXT("Empty root reports only root specification error"),
			Errors[0],
			FString(TEXT("Request[0]: RootBoneName or RootBonePattern must be specified.")));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementValidationNestedRootWarningTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Validation.NestedRootWarning",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementValidationNestedRootWarningTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture;
	Fixture.AnimBlueprint = CreateTransientAnimBlueprint(*this);
	Fixture.Package = Fixture.AnimBlueprint ? Fixture.AnimBlueprint->GetOutermost() : nullptr;
	Fixture.Skeleton = CreateNestedRootWarningSkeleton(Fixture.Package);
	if (Fixture.AnimBlueprint)
	{
		Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
	}
	Fixture.AnimGraph = FindAnimGraph(Fixture.AnimBlueprint);
	TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBonePattern = TEXT("Object0[0-9]+");
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FString> ValidationMessages =
		UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(Fixture.AnimBlueprint, Requests);
	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	const bool bHasNestedRootWarning = ValidationMessages.ContainsByPredicate(
		[](const FString& Message)
		{
			return Message.StartsWith(TEXT("Warning:")) && Message.Contains(TEXT("descendant"));
		});
	const bool bHasBlockingValidationMessage = ValidationMessages.ContainsByPredicate(
		[](const FString& Message)
		{
			return !Message.StartsWith(TEXT("Warning:"));
		});

	bool bOk = true;
	bOk &= TestTrue(TEXT("Nested root validation reports warning"), bHasNestedRootWarning);
	bOk &= TestFalse(TEXT("Nested root validation has no blocking errors"), bHasBlockingValidationMessage);
	bOk &= TestEqual(TEXT("Nested root warning does not block placement"), Handles.Num(), 1);
	bOk &= TestTrue(TEXT("Nested root placement handle is valid"), Handles.IsValidIndex(0) && Handles[0].IsValid());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementPatternTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Pattern.Basic",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementPatternTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	TArray<FName> ResolvedBones =
		UKawaiiPhysicsEditorLibrary::ResolveBonesByPattern(Fixture.Skeleton, TEXT("hair_[0-9]+"));

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBonePattern = TEXT("hair_[0-9]+");
	Request.ExcludeBonePattern = TEXT("tail_[0-9]+");
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Pattern resolves two hair bones"), ResolvedBones.Num(), 2);
	const FName FirstResolvedBone = ResolvedBones.IsValidIndex(0) ? ResolvedBones[0] : FName(NAME_None);
	bOk &= TestEqual(TEXT("Pattern first match is hair_01"), FirstResolvedBone,
	                  FName(TEXT("hair_01")));
	bOk &= TestEqual(TEXT("Pattern placement creates one node"), Handles.Num(), 1);
	if (!Handles.IsEmpty() && Handles[0].IsValid())
	{
		UAnimGraphNode_KawaiiPhysics* GraphNode = Handles[0].Node.Get();
		bOk &= TestEqual(TEXT("Pattern first match becomes RootBone"),
		                  GraphNode->Node.RootBone.BoneName, FName(TEXT("hair_01")));
		bOk &= TestEqual(TEXT("Pattern remaining match becomes AdditionalRootBones"),
		                  GraphNode->Node.AdditionalRootBones.Num(), 1);
		bOk &= TestEqual(TEXT("Pattern AdditionalRootBone is hair_02"),
		                  GraphNode->Node.AdditionalRootBones[0].RootBone.BoneName, FName(TEXT("hair_02")));
		bOk &= TestEqual(TEXT("Exclude pattern expands into ExcludeBones"),
		                  GraphNode->Node.ExcludeBones.Num(), 2);
		bOk &= TestEqual(TEXT("First exclude bone is tail_01"),
		                  GraphNode->Node.ExcludeBones[0].BoneName, FName(TEXT("tail_01")));
		bOk &= TestEqual(TEXT("Second exclude bone is tail_02"),
		                  GraphNode->Node.ExcludeBones[1].BoneName, FName(TEXT("tail_02")));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementGreedyPatternLongMatchTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Pattern.GreedyLongMatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementGreedyPatternLongMatchTest::RunTest(const FString& Parameters)
{
	USkeleton* Skeleton = CreateLongBoneListSkeleton(GetTransientPackage());
	if (!Skeleton)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 BoneListStringLength = 0;
	for (const FMeshBoneInfo& BoneInfo : RefSkeleton.GetRefBoneInfo())
	{
		BoneListStringLength += BoneInfo.Name.ToString().Len() + 2;
	}

	TArray<FName> ResolvedBones =
		UKawaiiPhysicsEditorLibrary::ResolveBonesByPattern(Skeleton, TEXT(".*"));

	bool bOk = true;
	bOk &= TestTrue(TEXT("Greedy pattern test skeleton exceeds FName limit"),
	                BoneListStringLength >= NAME_SIZE);
	for (const FName& BoneName : ResolvedBones)
	{
		bOk &= TestTrue(TEXT("Greedy pattern result length is valid"),
		                BoneName.ToString().Len() < NAME_SIZE);
		bOk &= TestTrue(TEXT("Greedy pattern result exists in skeleton"),
		                !BoneName.IsNone() && RefSkeleton.FindBoneIndex(BoneName) != INDEX_NONE);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementLimitsPinRegressionTest,
                                 "KawaiiPhysics.EditorScripting.Placement.LimitsDataAssetPinHidden",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingPlacementLimitsPinRegressionTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsEditorScriptingFixture Fixture = MakeEmptyFixture(*this);
	if (!Fixture.AnimBlueprint)
	{
		return false;
	}

	UKawaiiPhysicsLimitsDataAsset* LimitsDataAsset = NewObject<UKawaiiPhysicsLimitsDataAsset>(Fixture.Package);
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>(Fixture.Package);
	Preset->Node.LimitsDataAsset = LimitsDataAsset;

	FKawaiiPhysicsNodePlacementRequest Request;
	Request.Preset = Preset;
	Request.RootBoneName = TEXT("hair_01");
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);

	TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(Fixture.AnimBlueprint, Requests);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Limits pin regression placement creates one node"), Handles.Num(), 1);
	if (!Handles.IsEmpty() && Handles[0].IsValid())
	{
		UAnimGraphNode_KawaiiPhysics* GraphNode = Handles[0].Node.Get();
		const FName LimitsDataAssetPropertyName =
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset);

		// PinHiddenByDefault のプロパティは UEdGraphPin が生成されず、ShowPinForProperties で非露出として管理される。
		UEdGraphPin* LimitsPin = GraphNode->FindPin(LimitsDataAssetPropertyName);
		bOk &= TestNull(TEXT("LimitsDataAsset pin is not exposed"), LimitsPin);

		const FOptionalPinFromProperty* LimitsOptionalPin = GraphNode->ShowPinForProperties.FindByPredicate(
			[LimitsDataAssetPropertyName](const FOptionalPinFromProperty& OptionalPin)
			{
				return OptionalPin.PropertyName == LimitsDataAssetPropertyName;
			});
		bOk &= TestNotNull(TEXT("LimitsDataAsset optional pin entry exists"), LimitsOptionalPin);
		if (LimitsOptionalPin)
		{
			bOk &= TestFalse(TEXT("LimitsDataAsset optional pin remains unexposed"), LimitsOptionalPin->bShowPin);
		}
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingTagPrefilterDirtyBypassTest,
                                 "KawaiiPhysics.EditorScripting.TagPrefilter.DirtyBypass",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingTagPrefilterDirtyBypassTest::RunTest(const FString& Parameters)
{
	// MakeEmptyFixture等が作る/Temp配下のtransientパッケージはAsset RegistryのGetAssets候補列挙
	// （ScanPathsSynchronous経由）に載らないため、dirtyバイパスの検証は保存済みアセットを使い、
	// そのdirtyフラグを操作して確認する。
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game/Test/MCPSetup2"));
	TArray<FAssetData> CandidateAssets;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(ContentPaths, CandidateAssets);

	const FAssetData* FoundAssetData = CandidateAssets.FindByPredicate(
		[](const FAssetData& AssetData)
		{
			return AssetData.AssetName == FName(TEXT("ABP_MCP_Retest"));
		});
	if (!FoundAssetData)
	{
		// プラグイン単体配布などこのローカル専用アセットが存在しない環境ではskipする。
		AddInfo(TEXT("/Game/Test/MCPSetup2/ABP_MCP_Retest was not found. Skipping tag prefilter dirty bypass test."));
		return true;
	}
	const FAssetData AssetData = *FoundAssetData;
	const FName TargetPackageName = AssetData.PackageName;

	// 絶対にマッチしないタグ（KawaiiPhysics.Test.PresetTarget）を、コントロール検証とdirtyバイパス検証の両方で使う。
	FGameplayTagContainer NonMatchingTags;
	NonMatchingTags.AddTag(GetKawaiiPhysicsEditorScriptingTagB());

	// (a) コントロール検証: dirtyでない状態では、非マッチタグの候補に含まれないことを確認する。
	//     既にロード済みかつdirtyな場合は、並行編集中の環境を壊さないためskipする。
	UPackage* LoadedPackage = FindPackage(nullptr, *TargetPackageName.ToString());
	if (LoadedPackage && LoadedPackage->IsDirty())
	{
		AddInfo(TEXT("ABP_MCP_Retest package is already loaded and dirty in this session. Skipping tag prefilter dirty bypass test."));
		return true;
	}

	TArray<FAssetData> ControlResults;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
		NonMatchingTags, false, ContentPaths, ControlResults);
	bool bOk = TestFalse(TEXT("Non-matching tag filter excludes ABP_MCP_Retest while not dirty"),
	                     ControlResults.ContainsByPredicate([TargetPackageName](const FAssetData& Candidate)
	                     {
		                     return Candidate.PackageName == TargetPackageName;
	                     }));

	// (b) dirtyバイパス検証: アセットをロードしdirty化すると、非マッチタグでも常に候補へ含まれることを確認する。
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
	bOk &= TestNotNull(TEXT("ABP_MCP_Retest loads as an AnimBlueprint"), AnimBlueprint);

	UPackage* Package = AnimBlueprint ? AnimBlueprint->GetOutermost() : nullptr;
	bOk &= TestNotNull(TEXT("ABP_MCP_Retest package is resolved"), Package);

	bool bDirtyBypassIncludesTarget = false;
	if (Package)
	{
		Package->SetDirtyFlag(true);

		TArray<FAssetData> DirtyBypassResults;
		UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
			NonMatchingTags, false, ContentPaths, DirtyBypassResults);
		bDirtyBypassIncludesTarget =
			DirtyBypassResults.ContainsByPredicate([TargetPackageName](const FAssetData& Candidate)
			{
				return Candidate.PackageName == TargetPackageName;
			});

		// 検証結果に関わらず必ずdirtyフラグを元に戻すため、アサート前に結果をboolで受けておく。
		Package->SetDirtyFlag(false);
	}
	bOk &= TestTrue(TEXT("Non-matching tag filter still includes dirty ABP_MCP_Retest (dirty bypass)"),
	                bDirtyBypassIncludesTarget);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingTagPrefilterSavedAssetsTest,
                                 "KawaiiPhysics.EditorScripting.TagPrefilter.SavedAssets",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingTagPrefilterSavedAssetsTest::RunTest(const FString& Parameters)
{
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game/Test/MCPSetup2"));
	TArray<FAssetData> CandidateAssets;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(ContentPaths, CandidateAssets);

	const FAssetData* TargetAssetData = CandidateAssets.FindByPredicate(
		[](const FAssetData& AssetData)
		{
			return AssetData.AssetName == FName(TEXT("ABP_MCP_Retest"));
		});
	if (!TargetAssetData)
	{
		// プラグイン単体配布などこのローカル専用アセットが存在しない環境ではskipする。
		AddInfo(TEXT("/Game/Test/MCPSetup2/ABP_MCP_Retest was not found. Skipping saved-asset tag prefilter test."));
		return true;
	}
	const FName TargetPackageName = TargetAssetData->PackageName;

	// 既にエディタ上でロード済みかつdirtyだと、(c)のExact除外検証がdirtyバイパスにより偽陽性になるためskipする。
	UPackage* LoadedPackage = FindPackage(nullptr, *TargetPackageName.ToString());
	if (LoadedPackage && LoadedPackage->IsDirty())
	{
		AddInfo(TEXT("ABP_MCP_Retest package is loaded and dirty in this session. Skipping saved-asset tag prefilter test."));
		return true;
	}

	const FGameplayTag HairTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Hair")), false);
	const FGameplayTag RootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics")), false);
	if (!HairTag.IsValid() || !RootTag.IsValid())
	{
		AddInfo(TEXT("KawaiiPhysics.Hair / KawaiiPhysics tags are not registered. Skipping saved-asset tag prefilter test."));
		return true;
	}

	bool bOk = true;

	// (a) KawaiiPhysics.Hairの完全一致(非Exact)で、保存済みSearchableName依存経由でヒットする。
	FGameplayTagContainer HairFilter;
	HairFilter.AddTag(HairTag);
	TArray<FAssetData> HairResults;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(HairFilter, false, ContentPaths, HairResults);
	bOk &= TestTrue(TEXT("KawaiiPhysics.Hair non-exact filter includes ABP_MCP_Retest"),
	                HairResults.ContainsByPredicate([TargetPackageName](const FAssetData& AssetData)
	                {
		                return AssetData.PackageName == TargetPackageName;
	                }));

	// (b) 親タグKawaiiPhysicsを非Exactで指定すると、子タグ(KawaiiPhysics.Hair等)経由でヒットする。
	FGameplayTagContainer RootFilter;
	RootFilter.AddTag(RootTag);
	TArray<FAssetData> RootNonExactResults;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
		RootFilter, false, ContentPaths, RootNonExactResults);
	bOk &= TestTrue(TEXT("KawaiiPhysics parent tag non-exact filter includes ABP_MCP_Retest via child tag"),
	                RootNonExactResults.ContainsByPredicate([TargetPackageName](const FAssetData& AssetData)
	                {
		                return AssetData.PackageName == TargetPackageName;
	                }));

	// (c) 親タグKawaiiPhysicsをExactで指定すると、ノード側はKawaiiPhysics.Hair等の子タグしか持たないため含まれない。
	TArray<FAssetData> RootExactResults;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
		RootFilter, true, ContentPaths, RootExactResults);
	bOk &= TestFalse(TEXT("KawaiiPhysics parent tag exact filter excludes ABP_MCP_Retest"),
	                 RootExactResults.ContainsByPredicate([TargetPackageName](const FAssetData& AssetData)
	                 {
		                 return AssetData.PackageName == TargetPackageName;
	                 }));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingTagPrefilterRedirectChainTest,
                                 "KawaiiPhysics.EditorScripting.TagPrefilter.RedirectChain",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingTagPrefilterRedirectChainTest::RunTest(const FString& Parameters)
{
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game/Test/MCPSetup2"));
	TArray<FAssetData> CandidateAssets;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(ContentPaths, CandidateAssets);

	const FAssetData* TargetAssetData = CandidateAssets.FindByPredicate(
		[](const FAssetData& AssetData)
		{
			return AssetData.AssetName == FName(TEXT("ABP_MCP_Retest"));
		});
	if (!TargetAssetData)
	{
		// プラグイン単体配布などこのローカル専用アセットが存在しない環境ではskipする。
		AddInfo(TEXT("/Game/Test/MCPSetup2/ABP_MCP_Retest was not found. Skipping tag prefilter redirect-chain test."));
		return true;
	}
	const FName TargetPackageName = TargetAssetData->PackageName;

	UPackage* LoadedPackage = FindPackage(nullptr, *TargetPackageName.ToString());
	if (LoadedPackage && LoadedPackage->IsDirty())
	{
		AddInfo(TEXT("ABP_MCP_Retest package is loaded and dirty in this session. Skipping tag prefilter redirect-chain test."));
		return true;
	}

	UGameplayTagsSettings* GameplayTagsSettings = GetMutableDefault<UGameplayTagsSettings>();
	if (!GameplayTagsSettings)
	{
		AddInfo(TEXT("GameplayTagsSettings was not available. Skipping tag prefilter redirect-chain test."));
		return true;
	}

	FGameplayTagContainer TargetFilter;
	TargetFilter.AddTag(GetKawaiiPhysicsEditorScriptingTagB());

	TArray<FAssetData> ControlResults;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
		TargetFilter, false, ContentPaths, ControlResults);
	bool bOk = TestFalse(TEXT("Target tag filter excludes ABP_MCP_Retest before temporary redirects"),
	                     ControlResults.ContainsByPredicate([TargetPackageName](const FAssetData& AssetData)
	                     {
		                     return AssetData.PackageName == TargetPackageName;
	                     }));

	{
		FScopedGameplayTagRedirects ScopedRedirects(GameplayTagsSettings);

		FGameplayTagRedirect ParentToMiddleRedirect;
		ParentToMiddleRedirect.OldTagName = FName(TEXT("KawaiiPhysics"));
		ParentToMiddleRedirect.NewTagName = FName(TEXT("KawaiiPhysics.Test.RedirectMiddle"));
		GameplayTagsSettings->GameplayTagRedirects.Add(ParentToMiddleRedirect);

		FGameplayTagRedirect MiddleToTargetRedirect;
		MiddleToTargetRedirect.OldTagName = ParentToMiddleRedirect.NewTagName;
		MiddleToTargetRedirect.NewTagName = GetKawaiiPhysicsEditorScriptingTagB().GetTagName();
		GameplayTagsSettings->GameplayTagRedirects.Add(MiddleToTargetRedirect);

		TArray<FAssetData> RedirectResults;
		UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
			TargetFilter, false, ContentPaths, RedirectResults);
		bOk &= TestTrue(TEXT("Multi-step redirected parent tag non-exact filter includes ABP_MCP_Retest via old child tag"),
		                RedirectResults.ContainsByPredicate([TargetPackageName](const FAssetData& AssetData)
		                {
			                return AssetData.PackageName == TargetPackageName;
		                }));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingTagPrefilterSupersetTest,
                                 "KawaiiPhysics.EditorScripting.TagPrefilter.Superset",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingTagPrefilterSupersetTest::RunTest(const FString& Parameters)
{
	const FGameplayTag RootTag = FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics")), false);
	if (!RootTag.IsValid())
	{
		AddInfo(TEXT("KawaiiPhysics tag is not registered. Skipping tag prefilter superset test."));
		return true;
	}

	// /Game/Test直下にはSkeleton欠落の壊れアセット（ABP_MCP_FromScratch等）があり、ロード時の
	// コンパイルエラーログでテストが自動失敗するため、正常アセットのみのMCPSetup2に限定する。
	TArray<FString> ContentPaths;
	ContentPaths.Add(TEXT("/Game/Test/MCPSetup2"));

	TArray<FAssetData> AllAnimBlueprintAssets;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssets(ContentPaths, AllAnimBlueprintAssets);
	if (AllAnimBlueprintAssets.IsEmpty())
	{
		AddInfo(TEXT("No AnimBlueprint assets under /Game/Test/MCPSetup2. Skipping tag prefilter superset test."));
		return true;
	}

	FGameplayTagContainer RootFilter;
	RootFilter.AddTag(RootTag);
	TArray<FAssetData> PrefilteredAssets;
	UKawaiiPhysicsEditorLibrary::GetAnimBlueprintAssetsReferencingTags(
		RootFilter, false, ContentPaths, PrefilteredAssets);

	TSet<FName> AllPackageNames;
	for (const FAssetData& AssetData : AllAnimBlueprintAssets)
	{
		AllPackageNames.Add(AssetData.PackageName);
	}

	// (1) プレフィルタ結果は必ず全件集合の部分集合である。
	bool bOk = true;
	TSet<FName> PrefilteredPackageNames;
	for (const FAssetData& AssetData : PrefilteredAssets)
	{
		PrefilteredPackageNames.Add(AssetData.PackageName);
		bOk &= TestTrue(
			*FString::Printf(TEXT("Prefiltered asset '%s' is a subset of all AnimBlueprint assets"),
			                 *AssetData.PackageName.ToString()),
			AllPackageNames.Contains(AssetData.PackageName));
	}

	// (2) プレフィルタが実マッチを取りこぼしていないか、全ABPをロードして実際のノードタグで検証する。
	for (const FAssetData& AssetData : AllAnimBlueprintAssets)
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (!AnimBlueprint)
		{
			continue;
		}

		TArray<FKawaiiPhysicsGraphNodeHandle> MatchedHandles =
			UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsGraphNodes(AnimBlueprint, RootFilter, false);
		if (!MatchedHandles.IsEmpty())
		{
			bOk &= TestTrue(
				*FString::Printf(TEXT("Actual tag match '%s' is included in the tag prefilter result"),
				                 *AssetData.PackageName.ToString()),
				PrefilteredPackageNames.Contains(AssetData.PackageName));
		}
	}

	return bOk;
}

#endif // WITH_DEV_AUTOMATION_TESTS
