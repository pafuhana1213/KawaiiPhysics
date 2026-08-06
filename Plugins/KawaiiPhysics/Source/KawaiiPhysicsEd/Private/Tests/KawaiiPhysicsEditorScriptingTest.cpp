// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
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
                                 "KawaiiPhysics.EditorScripting.Preset",
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
                                 "KawaiiPhysics.EditorScripting.Placement",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorScriptingPlacementValidationTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Validation",
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
                                 "KawaiiPhysics.EditorScripting.Placement.Pattern",
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

#endif // WITH_DEV_AUTOMATION_TESTS
