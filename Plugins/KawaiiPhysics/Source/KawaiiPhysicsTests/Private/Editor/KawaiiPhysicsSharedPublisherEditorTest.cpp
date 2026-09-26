// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.h"
#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimationGraphSchema.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_Root.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsEdUtils.h"
#include "KawaiiPhysicsEditorCategoryNames.h"
#include "KawaiiPhysicsSharedPublisherTypes.h"
#include "KawaiiPhysicsWindPresetDataAsset.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "UObject/FieldIterator.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealType.h"

namespace
{
	// UncookedOnly モジュールでは Native Gameplay Tag を定義できない（エンジンが起動時に Error を出す）ため、
	// Runtime 側のテスト（KawaiiPhysicsSimpleWorldCollisionTest.cpp）が登録しているテスト用タグを名前で借りる。
	FGameplayTag GetEditorSharedPublisherTagA()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.SimpleWorld.Registry.X")), false);
	}

	FGameplayTag GetEditorSharedPublisherTagB()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("KawaiiPhysics.Test.SimpleWorld.Registry.Y")), false);
	}

	struct FKawaiiPhysicsSharedPublisherEditorFixture
	{
		UPackage* Package = nullptr;
		USkeleton* Skeleton = nullptr;
		UAnimBlueprint* AnimBlueprint = nullptr;
		UEdGraph* AnimGraph = nullptr;
	};

	USkeleton* CreateSharedPublisherTestSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("pelvis"), TEXT("pelvis"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("hair_01"), TEXT("hair_01"), 1), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("tail_01"), TEXT("tail_01"), 1), FTransform::Identity);
		return Skeleton;
	}

	UAnimBlueprint* CreateSharedPublisherTransientAnimBlueprint(FAutomationTestBase& Test)
	{
		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(TEXT("/Temp/KawaiiPhysicsSharedPublisher_%s"), *UniqueSuffix);
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			FName(*FString::Printf(TEXT("ABP_KawaiiPhysicsSharedPublisher_%s"), *UniqueSuffix)),
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass());

		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);
		Test.TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
		return AnimBlueprint;
	}

	UEdGraph* FindSharedPublisherAnimGraph(UAnimBlueprint* AnimBlueprint)
	{
		TArray<UAnimationGraph*> Graphs;
		KawaiiPhysicsEdUtils::CollectAnimGraphs(AnimBlueprint, Graphs);
		return Graphs.IsEmpty() ? nullptr : Graphs[0];
	}

	FKawaiiPhysicsSharedPublisherEditorFixture MakeSharedPublisherFixture(FAutomationTestBase& Test)
	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture;
		Fixture.AnimBlueprint = CreateSharedPublisherTransientAnimBlueprint(Test);
		Fixture.Package = Fixture.AnimBlueprint ? Fixture.AnimBlueprint->GetOutermost() : nullptr;
		Fixture.Skeleton = CreateSharedPublisherTestSkeleton(Fixture.Package);
		if (Fixture.AnimBlueprint)
		{
			Fixture.AnimBlueprint->TargetSkeleton = Fixture.Skeleton;
		}
		Fixture.AnimGraph = FindSharedPublisherAnimGraph(Fixture.AnimBlueprint);
		Test.TestNotNull(TEXT("Default AnimGraph is found"), Fixture.AnimGraph);
		Test.TestTrue(TEXT("Runtime test gameplay tags are registered"),
		              GetEditorSharedPublisherTagA().IsValid() && GetEditorSharedPublisherTagB().IsValid());
		return Fixture;
	}

	UAnimGraphNode_Root* FindSharedPublisherResultRootNode(UEdGraph* Graph)
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

	UEdGraphPin* FindSharedPublisherPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* GetSharedPublisherResultPin(UEdGraph* Graph)
	{
		UAnimGraphNode_Root* RootNode = FindSharedPublisherResultRootNode(Graph);
		return RootNode ? RootNode->FindPin(GET_MEMBER_NAME_CHECKED(FAnimNode_Root, Result), EGPD_Input) : nullptr;
	}

	UAnimGraphNode_KawaiiPhysicsSharedPublisher* AddSharedPublisherGraphNode(
		UEdGraph* Graph,
		FGameplayTag Tag,
		FVector2D NodePosition = FVector2D(-300.0, 0.0))
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysicsSharedPublisher> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->Node.SharedGroupTag = Tag;
		GraphNode->NodePosX = static_cast<int32>(NodePosition.X);
		GraphNode->NodePosY = static_cast<int32>(NodePosition.Y);
		NodeCreator.Finalize();
		return GraphNode;
	}

	UAnimGraphNode_KawaiiPhysics* AddSharedPublisherConsumerGraphNode(
		UEdGraph* Graph,
		FGameplayTag Tag,
		EKawaiiPhysicsSimpleWorldCollisionSource Source)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->Node.RootBone = FBoneReference(TEXT("hair_01"));
		GraphNode->Node.bUseSimpleWorldCollision = true;
		GraphNode->Node.SimpleWorldCollisionSource = Source;
		GraphNode->Node.SimpleWorldCollisionSharedTag = Tag;
		NodeCreator.Finalize();
		return GraphNode;
	}

	UAnimGraphNode_KawaiiPhysics* AddSharedPublisherWindConsumerGraphNode(
		UEdGraph* Graph,
		EKawaiiPhysicsProceduralWindSource WindSource,
		const FGameplayTag& SharedWindTag,
		bool bAddWind = true)
	{
		if (!Graph)
		{
			return nullptr;
		}

		FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> NodeCreator(*Graph);
		UAnimGraphNode_KawaiiPhysics* GraphNode = NodeCreator.CreateNode(false);
		GraphNode->Node.RootBone = FBoneReference(TEXT("hair_01"));
		if (bAddWind)
		{
			GraphNode->Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
			if (FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
				GraphNode->Node.ExternalForces[0].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>())
			{
				Wind->WindSource = WindSource;
				Wind->SharedWindTag = SharedWindTag;
			}
		}
		NodeCreator.Finalize();
		return GraphNode;
	}

	int32 CountSharedPublisherNodes(UEdGraph* Graph)
	{
		int32 Count = 0;
		if (!Graph)
		{
			return Count;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->IsA<UAnimGraphNode_KawaiiPhysicsSharedPublisher>())
			{
				++Count;
			}
		}
		return Count;
	}

	FKawaiiPhysicsSharedPublisherGraphNodeHandle MakeSharedPublisherTestHandle(
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode)
	{
		FKawaiiPhysicsSharedPublisherGraphNodeHandle Handle;
		Handle.Node = GraphNode;
		Handle.AnimBlueprint = GraphNode ? GraphNode->GetAnimBlueprint() : nullptr;
		Handle.NodeGuid = GraphNode ? GraphNode->NodeGuid : FGuid();
		Handle.SharedGroupTag = GraphNode ? GraphNode->Node.SharedGroupTag : FGameplayTag();
		Handle.GraphName = GraphNode && GraphNode->GetGraph() ? GraphNode->GetGraph()->GetFName() : NAME_None;
		return Handle;
	}

	bool ContainsCompilerMessage(
		const FCompilerResultsLog& MessageLog,
		EMessageSeverity::Type Severity,
		const FString& Text)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : MessageLog.Messages)
		{
			if (Message->GetSeverity() == Severity &&
				Message->ToText().ToString().Contains(Text))
			{
				return true;
			}
		}
		return false;
	}

	// 表示言語に依存しないよう、本体と同じキーの訳文から @@（ノード名に置換される部分）を除いて照合に使う
	FString MakeSharedPublisherCompilerMessageNeedle(const FText& LocalizedFormat)
	{
		FString Needle = LocalizedFormat.ToString();
		Needle.ReplaceInline(TEXT("@@"), TEXT(""));
		Needle.TrimStartAndEndInline();
		return Needle;
	}

	UKawaiiPhysicsWindPresetDataAsset* CreateSharedPublisherWindPresetAsset(
		UObject* Outer,
		const FGameplayTag& PresetTag,
		float PresetConstantForce,
		float PresetSwayForce)
	{
		UKawaiiPhysicsWindPresetDataAsset* PresetAsset =
			NewObject<UKawaiiPhysicsWindPresetDataAsset>(Outer ? Outer : GetTransientPackage());
		FKawaiiProceduralWindPreset Preset;
		Preset.PresetTag = PresetTag;
		Preset.ConstantForce = PresetConstantForce;
		Preset.SwayForce = PresetSwayForce;
		PresetAsset->Presets.Add(Preset);
		return PresetAsset;
	}

	void CollectPropertyCategories(const UStruct* Struct, TArray<FName>& OutCategories)
	{
		const FName CategoryKey(TEXT("Category"));
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (Property && Property->HasMetaData(CategoryKey))
			{
				OutCategories.Add(FName(*Property->GetMetaData(CategoryKey)));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSharedPublisherCategoryConsistencyTest,
                                 "KawaiiPhysics.Editor.SharedPublisher.CategoryConsistency",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSharedPublisherCategoryConsistencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FName>& SortOrderNames =
		KawaiiPhysicsEditorCategoryNames::GetSharedPublisherCategorySortOrderNames();
	TSet<FName> AllowedCategories;
	AllowedCategories.Append(SortOrderNames);
	bool bOk = true;

	TArray<FName> NodeCategories;
	CollectPropertyCategories(FAnimNode_KawaiiPhysicsSharedPublisher::StaticStruct(), NodeCategories);
	for (const FName& Category : NodeCategories)
	{
		if (Category == FName(TEXT("Links")))
		{
			continue;
		}

		bOk &= TestTrue(
			FString::Printf(TEXT("Publisher category is declared in sort order: %s"), *Category.ToString()),
			AllowedCategories.Contains(Category));
	}

	TArray<FName> SettingsCategories;
	CollectPropertyCategories(FKawaiiPhysicsSimpleWorldCollisionSettings::StaticStruct(), SettingsCategories);
	bOk &= TestEqual(TEXT("SimpleWorldCollision settings property count"), SettingsCategories.Num(), 12);
	for (const FName& Category : SettingsCategories)
	{
		bOk &= TestEqual(
			TEXT("SimpleWorldCollision settings category matches constant"),
			Category,
			KawaiiPhysicsEditorCategoryNames::SharedPublisherSimpleWorldCollision);
		bOk &= TestTrue(
			FString::Printf(TEXT("SimpleWorldCollision category is declared in sort order: %s"), *Category.ToString()),
			AllowedCategories.Contains(Category));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingSharedPublisherCollectTest,
                                 "KawaiiPhysics.EditorScripting.SharedPublisher.Collect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingSharedPublisherCollectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
	AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
	AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagB(), FVector2D(-300.0, 160.0));

	const TArray<FKawaiiPhysicsSharedPublisherGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::CollectKawaiiPhysicsSharedPublisherGraphNodes(Fixture.AnimBlueprint);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Collect returns two Shared Publisher nodes"), Handles.Num(), 2);
	TSet<FGameplayTag> Tags;
	for (const FKawaiiPhysicsSharedPublisherGraphNodeHandle& Handle : Handles)
	{
		Tags.Add(Handle.SharedGroupTag);
	}
	bOk &= TestTrue(TEXT("Collected Tag A"), Tags.Contains(GetEditorSharedPublisherTagA()));
	bOk &= TestTrue(TEXT("Collected Tag B"), Tags.Contains(GetEditorSharedPublisherTagB()));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingSharedPublisherPropertyAccessTest,
                                 "KawaiiPhysics.EditorScripting.SharedPublisher.PropertyAccess",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingSharedPublisherPropertyAccessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
	UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
		AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
	FKawaiiPhysicsSharedPublisherGraphNodeHandle Handle = MakeSharedPublisherTestHandle(Publisher);

	bool bOk = true;
	bOk &= TestTrue(TEXT("Set SharedGroupTag by string"),
	                UKawaiiPhysicsEditorLibrary::SetSharedPublisherNodePropertyFromString(
		                Handle,
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, SharedGroupTag),
		                TEXT("(TagName=\"KawaiiPhysics.Shared.Default\")")));
	bOk &= TestTrue(TEXT("SharedGroupTag round-trips"),
	                UKawaiiPhysicsEditorLibrary::GetSharedPublisherNodePropertyAsString(
		                Handle,
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, SharedGroupTag)).
	                Contains(TEXT("KawaiiPhysics.Shared.Default")));
	bOk &= TestTrue(TEXT("Set nested GatherInterval by string"),
	                UKawaiiPhysicsEditorLibrary::SetSharedPublisherNodePropertyFromString(
		                Handle,
		                TEXT("SimpleWorldCollision.GatherInterval"),
		                TEXT("0.5")));
	bOk &= TestTrue(TEXT("GatherInterval round-trips"),
	                UKawaiiPhysicsEditorLibrary::GetSharedPublisherNodePropertyAsString(
		                Handle,
		                TEXT("SimpleWorldCollision.GatherInterval")).Contains(TEXT("0.5")));
	bOk &= TestTrue(TEXT("Set bEnabled by string"),
	                UKawaiiPhysicsEditorLibrary::SetSharedPublisherNodePropertyFromString(
		                Handle,
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, bEnabled),
		                TEXT("False")));
	bOk &= TestTrue(TEXT("bEnabled round-trips"),
	                UKawaiiPhysicsEditorLibrary::GetSharedPublisherNodePropertyAsString(
		                Handle,
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, bEnabled)).
	                Equals(TEXT("False"), ESearchCase::IgnoreCase));
	bOk &= TestFalse(TEXT("Invalid property is rejected"),
	                 UKawaiiPhysicsEditorLibrary::SetSharedPublisherNodePropertyFromString(
		                 Handle,
		                 TEXT("NoSuchProperty"),
		                 TEXT("1")));
	bOk &= TestTrue(TEXT("Invalid property get returns empty"),
	                UKawaiiPhysicsEditorLibrary::GetSharedPublisherNodePropertyAsString(
		                Handle,
		                TEXT("NoSuchProperty")).IsEmpty());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingSharedPublisherPlacementAutoConnectTest,
                                 "KawaiiPhysics.EditorScripting.SharedPublisher.Placement.AutoConnect",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorScriptingSharedPublisherPlacementAutoConnectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;
	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		FKawaiiPhysicsSharedPublisherGraphNodeHandle Handle =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsSharedPublisherNode(
				Fixture.AnimBlueprint,
				GetEditorSharedPublisherTagA(),
				true,
				true);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher = Handle.Node.Get();
		UEdGraphPin* ResultPin = GetSharedPublisherResultPin(Fixture.AnimGraph);
		UEdGraphPin* PublisherPosePin = FindSharedPublisherPosePin(Publisher, EGPD_Output);
		UEdGraphPin* PublisherSourcePin = Publisher
			                                   ? Publisher->FindPin(
				                                   GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, Source),
				                                   EGPD_Input)
			                                   : nullptr;

		bOk &= TestTrue(TEXT("Empty graph Result input is Publisher Pose"),
		                ResultPin && ResultPin->LinkedTo.Num() == 1 && ResultPin->LinkedTo[0] == PublisherPosePin);
		bOk &= TestTrue(TEXT("Empty graph Publisher Source is unconnected"),
		                PublisherSourcePin && PublisherSourcePin->LinkedTo.IsEmpty());

		const int32 NodeCountBeforeReuse = CountSharedPublisherNodes(Fixture.AnimGraph);
		FKawaiiPhysicsSharedPublisherGraphNodeHandle ReusedHandle =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsSharedPublisherNode(
				Fixture.AnimBlueprint,
				GetEditorSharedPublisherTagA(),
				true,
				true);
		bOk &= TestEqual(TEXT("Reuse returns the same NodeGuid"), ReusedHandle.NodeGuid, Handle.NodeGuid);
		bOk &= TestEqual(TEXT("Reuse does not add a node"),
		                 CountSharedPublisherNodes(Fixture.AnimGraph),
		                 NodeCountBeforeReuse);
	}

	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		FGraphNodeCreator<UAnimGraphNode_LocalRefPose> LocalRefCreator(*Fixture.AnimGraph);
		UAnimGraphNode_LocalRefPose* LocalRefNode = LocalRefCreator.CreateNode(false);
		LocalRefCreator.Finalize();

		const UAnimationGraphSchema* Schema = CastChecked<UAnimationGraphSchema>(Fixture.AnimGraph->GetSchema());
		UEdGraphPin* LocalRefPosePin = FindSharedPublisherPosePin(LocalRefNode, EGPD_Output);
		UEdGraphPin* ResultPin = GetSharedPublisherResultPin(Fixture.AnimGraph);
		bOk &= TestTrue(TEXT("LocalRef connects to Result"),
		                LocalRefPosePin && ResultPin && Schema->TryCreateConnection(LocalRefPosePin, ResultPin));

		FKawaiiPhysicsSharedPublisherGraphNodeHandle Handle =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsSharedPublisherNode(
				Fixture.AnimBlueprint,
				GetEditorSharedPublisherTagB(),
				true,
				true);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher = Handle.Node.Get();
		UEdGraphPin* PublisherPosePin = FindSharedPublisherPosePin(Publisher, EGPD_Output);
		UEdGraphPin* PublisherSourcePin = Publisher
			                                   ? Publisher->FindPin(
				                                   GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysicsSharedPublisher, Source),
				                                   EGPD_Input)
			                                   : nullptr;
		ResultPin = GetSharedPublisherResultPin(Fixture.AnimGraph);

		bOk &= TestTrue(TEXT("Connected graph Result input is Publisher Pose"),
		                ResultPin && ResultPin->LinkedTo.Num() == 1 && ResultPin->LinkedTo[0] == PublisherPosePin);
		bOk &= TestTrue(TEXT("Publisher Source is previous node"),
		                PublisherSourcePin &&
		                PublisherSourcePin->LinkedTo.Num() == 1 &&
		                PublisherSourcePin->LinkedTo[0] == LocalRefPosePin);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSharedPublisherConsumerTraversalTest,
                                 "KawaiiPhysics.Editor.SharedPublisher.ConsumerTraversal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSharedPublisherConsumerTraversalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
	UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
		AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
	AddSharedPublisherConsumerGraphNode(
		Fixture.AnimGraph,
		GetEditorSharedPublisherTagA(),
		EKawaiiPhysicsSimpleWorldCollisionSource::Shared);
	AddSharedPublisherConsumerGraphNode(
		Fixture.AnimGraph,
		GetEditorSharedPublisherTagA(),
		EKawaiiPhysicsSimpleWorldCollisionSource::Auto);
	AddSharedPublisherConsumerGraphNode(
		Fixture.AnimGraph,
		GetEditorSharedPublisherTagA(),
		EKawaiiPhysicsSimpleWorldCollisionSource::Local);

	TArray<UAnimGraphNode_KawaiiPhysics*> Consumers;
	KawaiiPhysicsEdUtils::FindKawaiiPhysicsConsumerGraphNodes(
		Fixture.AnimBlueprint,
		GetEditorSharedPublisherTagA(),
		Consumers);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Shared and Auto consumers are found"), Consumers.Num(), 2);
	bOk &= TestTrue(TEXT("Publisher is found by Tag A"),
	                KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(
		                Fixture.AnimBlueprint,
		                GetEditorSharedPublisherTagA()) == Publisher);
	bOk &= TestNull(TEXT("Publisher is not found by Tag B"),
	                KawaiiPhysicsEdUtils::FindSharedPublisherGraphNodeByTag(
		                Fixture.AnimBlueprint,
		                GetEditorSharedPublisherTagB()));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSharedPublisherWindConsumerTraversalTest,
                                 "KawaiiPhysics.Editor.SharedPublisher.WindConsumerTraversal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSharedPublisherWindConsumerTraversalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
	const FGameplayTag TagX = GetEditorSharedPublisherTagA();
	const FGameplayTag TagY = GetEditorSharedPublisherTagB();

	UAnimGraphNode_KawaiiPhysics* SharedWindNode =
		AddSharedPublisherWindConsumerGraphNode(
			Fixture.AnimGraph,
			EKawaiiPhysicsProceduralWindSource::Shared,
			TagX);
	UAnimGraphNode_KawaiiPhysics* AutoWindNode =
		AddSharedPublisherWindConsumerGraphNode(
			Fixture.AnimGraph,
			EKawaiiPhysicsProceduralWindSource::Auto,
			TagX);
	UAnimGraphNode_KawaiiPhysics* LocalWindNode =
		AddSharedPublisherWindConsumerGraphNode(
			Fixture.AnimGraph,
			EKawaiiPhysicsProceduralWindSource::Local,
			TagX);
	UAnimGraphNode_KawaiiPhysics* NoWindNode =
		AddSharedPublisherWindConsumerGraphNode(
			Fixture.AnimGraph,
			EKawaiiPhysicsProceduralWindSource::Shared,
			TagX,
			false);
	UAnimGraphNode_KawaiiPhysics* MixedWindNode =
		AddSharedPublisherWindConsumerGraphNode(
			Fixture.AnimGraph,
			EKawaiiPhysicsProceduralWindSource::Shared,
			TagX);
	MixedWindNode->Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>());
	if (FKawaiiPhysics_ExternalForce_ProceduralWind* SecondWind =
		MixedWindNode->Node.ExternalForces[1].GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>())
	{
		SecondWind->WindSource = EKawaiiPhysicsProceduralWindSource::Shared;
		SecondWind->SharedWindTag = TagY;
	}

	FGameplayTag FoundTag;
	bool bAuto = false;
	bool bOk = true;
	bOk &= TestTrue(TEXT("Shared wind consumer is detected"),
	                KawaiiPhysicsEdUtils::IsUsingSharedWindPublisher(SharedWindNode->Node, FoundTag, bAuto));
	bOk &= TestTrue(TEXT("Shared wind tag is Tag X"), FoundTag == TagX);
	bOk &= TestFalse(TEXT("Shared wind is not Auto"), bAuto);

	FoundTag = FGameplayTag();
	bAuto = false;
	bOk &= TestTrue(TEXT("Auto wind consumer is detected"),
	                KawaiiPhysicsEdUtils::IsUsingSharedWindPublisher(AutoWindNode->Node, FoundTag, bAuto));
	bOk &= TestTrue(TEXT("Auto wind tag is Tag X"), FoundTag == TagX);
	bOk &= TestTrue(TEXT("Auto wind reports Auto"), bAuto);

	FoundTag = FGameplayTag();
	bAuto = false;
	bOk &= TestFalse(TEXT("Local wind consumer is ignored"),
	                 KawaiiPhysicsEdUtils::IsUsingSharedWindPublisher(LocalWindNode->Node, FoundTag, bAuto));
	bOk &= TestFalse(TEXT("No wind consumer is ignored"),
	                 KawaiiPhysicsEdUtils::IsUsingSharedWindPublisher(NoWindNode->Node, FoundTag, bAuto));

	FoundTag = FGameplayTag();
	bAuto = false;
	bOk &= TestTrue(TEXT("Mixed wind consumer is detected"),
	                KawaiiPhysicsEdUtils::IsUsingSharedWindPublisher(MixedWindNode->Node, FoundTag, bAuto));
	bOk &= TestTrue(TEXT("Mixed wind returns first Tag X"), FoundTag == TagX);

	TArray<UAnimGraphNode_KawaiiPhysics*> Consumers;
	KawaiiPhysicsEdUtils::FindKawaiiPhysicsConsumerGraphNodes(Fixture.AnimBlueprint, TagX, Consumers);
	bOk &= TestTrue(TEXT("Shared wind node is a Tag X consumer"), Consumers.Contains(SharedWindNode));
	bOk &= TestTrue(TEXT("Auto wind node is a Tag X consumer"), Consumers.Contains(AutoWindNode));
	bOk &= TestTrue(TEXT("Mixed wind node is a Tag X consumer"), Consumers.Contains(MixedWindNode));
	bOk &= TestFalse(TEXT("Local wind node is not a Tag X consumer"), Consumers.Contains(LocalWindNode));
	bOk &= TestFalse(TEXT("No wind node is not a Tag X consumer"), Consumers.Contains(NoWindNode));

	TArray<UAnimGraphNode_KawaiiPhysics*> ConsumersY;
	KawaiiPhysicsEdUtils::FindKawaiiPhysicsConsumerGraphNodes(Fixture.AnimBlueprint, TagY, ConsumersY);
	bOk &= TestTrue(TEXT("Mixed wind node is a Tag Y consumer"), ConsumersY.Contains(MixedWindNode));
	bOk &= TestFalse(TEXT("Shared wind node is not a Tag Y consumer"), ConsumersY.Contains(SharedWindNode));
	bOk &= TestFalse(TEXT("Auto wind node is not a Tag Y consumer"), ConsumersY.Contains(AutoWindNode));
	bOk &= TestFalse(TEXT("Local wind node is not a Tag Y consumer"), ConsumersY.Contains(LocalWindNode));
	bOk &= TestFalse(TEXT("No wind node is not a Tag Y consumer"), ConsumersY.Contains(NoWindNode));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSharedPublisherCompileWarningsTest,
                                 "KawaiiPhysics.Editor.SharedPublisher.CompileWarnings",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSharedPublisherCompileWarningsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;
	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
			AddSharedPublisherGraphNode(Fixture.AnimGraph, FGameplayTag());
		FCompilerResultsLog MessageLog;
		Publisher->ValidateAnimNodeDuringCompilation(Fixture.Skeleton, MessageLog);
		bOk &= TestTrue(TEXT("Invalid tag emits warning"),
		                ContainsCompilerMessage(
			                MessageLog,
			                EMessageSeverity::Warning,
			                MakeSharedPublisherCompilerMessageNeedle(NSLOCTEXT(
				                "KawaiiPhysics", "SharedPublisherNoTag",
				                "@@ has no Shared Group Tag. Consumers cannot find it."))));
	}

	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
			AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
		AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA(), FVector2D(-300.0, 160.0));
		FCompilerResultsLog MessageLog;
		Publisher->ValidateAnimNodeDuringCompilation(Fixture.Skeleton, MessageLog);
		bOk &= TestTrue(TEXT("Duplicate tag emits warning"),
		                ContainsCompilerMessage(
			                MessageLog,
			                EMessageSeverity::Warning,
			                MakeSharedPublisherCompilerMessageNeedle(NSLOCTEXT(
				                "KawaiiPhysics", "SharedPublisherDuplicateTag",
				                "@@ shares its tag with another Shared Publisher in this Animation Blueprint. Only one publisher per tag per actor family is used."))));
	}

	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* Publisher =
			AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
		FCompilerResultsLog MessageLog;
		Publisher->ValidateAnimNodeDuringCompilation(Fixture.Skeleton, MessageLog);
		bOk &= TestTrue(TEXT("No consumer emits note"),
		                ContainsCompilerMessage(
			                MessageLog,
			                EMessageSeverity::Info,
			                MakeSharedPublisherCompilerMessageNeedle(NSLOCTEXT(
				                "KawaiiPhysics", "SharedPublisherNoConsumers",
				                "@@ has no consumer in this Animation Blueprint. Consumers in other Animation Blueprints (Post Process, Linked Layers, child actors) can still read it."))));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorSharedPublisherPreviewSyncKeepsWindPresetTest,
                                 "KawaiiPhysics.Editor.SharedPublisher.PreviewSyncKeepsWindPreset",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorSharedPublisherPreviewSyncKeepsWindPresetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	bool bOk = true;

	// プリセット選択中は、authored の SharedWind を丸ごと同期しても退避を捨てて reinit するのでプリセット値へ戻せる
	{
		FKawaiiPhysicsSharedPublisherEditorFixture Fixture = MakeSharedPublisherFixture(*this);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* GraphNode =
			AddSharedPublisherGraphNode(Fixture.AnimGraph, GetEditorSharedPublisherTagA());
		if (!GraphNode)
		{
			return false;
		}

		const FGameplayTag PresetTag = GetEditorSharedPublisherTagB();
		GraphNode->Node.WindPresetDataAsset =
			CreateSharedPublisherWindPresetAsset(Fixture.Package, PresetTag, 3.5f, 4.5f);
		GraphNode->Node.WindPresetTag = PresetTag;
		GraphNode->Node.SharedWind.ConstantForce = 77.0f;

		FAnimNode_KawaiiPhysicsSharedPublisher Preview = GraphNode->Node;
		Preview.ApplySharedWindPreset();
		bOk &= TestEqual(TEXT("Preset ConstantForce is applied to the preview node"),
		                 Preview.SharedWind.ConstantForce, 3.5f);
		bOk &= TestTrue(TEXT("Pre-preset snapshot is held after apply"), Preview.HasSharedWindBeforePreset());

		// authored 側の別項目を Details で編集した状況
		Preview.ClearReinitRequestForTest();
		GraphNode->Node.SharedWind.SwayForce = 9.0f;
		GraphNode->CopyNodeDataToPreviewNode(&Preview);
		bOk &= TestFalse(TEXT("Preview sync drops the pre-preset snapshot"), Preview.HasSharedWindBeforePreset());
		bOk &= TestTrue(TEXT("Preview sync requests reinit while a preset is selected"),
		                Preview.IsReinitRequestedForTest());

		// reinit の PreUpdate 経路が行う再適用
		Preview.ApplySharedWindPreset();
		bOk &= TestEqual(TEXT("Preset ConstantForce is re-applied after the sync"),
		                 Preview.SharedWind.ConstantForce, 3.5f);
		bOk &= TestEqual(TEXT("Preset SwayForce is re-applied after the sync"),
		                 Preview.SharedWind.SwayForce, 4.5f);
		bOk &= TestTrue(TEXT("Pre-preset snapshot is retaken from the new authored values"),
		                Preview.HasSharedWindBeforePreset());

		Preview.WindPresetDataAsset = nullptr;
		Preview.ApplySharedWindPreset();
		bOk &= TestEqual(TEXT("Removing the preset restores the authored ConstantForce"),
		                 Preview.SharedWind.ConstantForce, 77.0f);
		bOk &= TestEqual(TEXT("Removing the preset restores the edited authored SwayForce"),
		                 Preview.SharedWind.SwayForce, 9.0f);
	}

	// プリセット未選択なら退避にも reinit 要求にも触れない（既存の差分条件だけが効く）
	{
		FKawaiiPhysicsSharedPublisherEditorFixture PlainFixture = MakeSharedPublisherFixture(*this);
		UAnimGraphNode_KawaiiPhysicsSharedPublisher* PlainGraphNode =
			AddSharedPublisherGraphNode(PlainFixture.AnimGraph, GetEditorSharedPublisherTagA());
		if (!PlainGraphNode)
		{
			return false;
		}

		PlainGraphNode->Node.SharedWind.ConstantForce = 55.0f;

		FAnimNode_KawaiiPhysicsSharedPublisher PlainPreview = PlainGraphNode->Node;
		PlainPreview.ApplySharedWindPreset();
		PlainPreview.ClearReinitRequestForTest();
		PlainGraphNode->Node.SharedWind.SwayForce = 9.0f;
		PlainGraphNode->CopyNodeDataToPreviewNode(&PlainPreview);

		bOk &= TestFalse(TEXT("No preset leaves the pre-preset snapshot untouched"),
		                 PlainPreview.HasSharedWindBeforePreset());
		bOk &= TestFalse(TEXT("No preset does not request reinit"), PlainPreview.IsReinitRequestedForTest());
		bOk &= TestEqual(TEXT("Authored SwayForce edit still reaches the preview node"),
		                 PlainPreview.SharedWind.SwayForce, 9.0f);
		bOk &= TestEqual(TEXT("Authored ConstantForce is kept without a preset"),
		                 PlainPreview.SharedWind.ConstantForce, 55.0f);
	}

	return bOk;
}

#endif
