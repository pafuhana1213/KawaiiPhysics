// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameplayTagsManager.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "NativeGameplayTags.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsPresetSource, "KawaiiPhysics.Test.PresetSource");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsPresetTarget, "KawaiiPhysics.Test.PresetTarget");

namespace
{
	constexpr uint32 KawaiiPhysicsPresetTestIdenticalPortFlags = PPF_DeepComparison | PPF_DeepCompareInstances;

	FKawaiiPhysicsRootBoneSetting MakeAdditionalRootBone(const FName RootBoneName, const FName ExcludeBoneName)
	{
		FKawaiiPhysicsRootBoneSetting Setting;
		Setting.RootBone = FBoneReference(RootBoneName);
		Setting.OverrideExcludeBones.Add(FBoneReference(ExcludeBoneName));
		Setting.bUseOverrideExcludeBones = true;
		return Setting;
	}

	FSphericalLimit MakeSphereLimit()
	{
		FSphericalLimit Limit;
		Limit.DrivingBone = FBoneReference(TEXT("spine_01"));
		Limit.Location = FVector(1.0f, 2.0f, 3.0f);
		Limit.Radius = 9.0f;
		Limit.LimitType = ESphericalLimitType::Inner;
		Limit.bEnable = true;
		return Limit;
	}

	FAnimNode_KawaiiPhysics MakePresetSourceNode()
	{
		FAnimNode_KawaiiPhysics Node;
		Node.RootBone = FBoneReference(TEXT("hair_01"));
		Node.ExcludeBones.Add(FBoneReference(TEXT("hair_skip_01")));
		Node.AdditionalRootBones.Add(MakeAdditionalRootBone(TEXT("hair_02"), TEXT("hair_skip_02")));
		Node.DummyBoneLength = 7.5f;
		Node.BoneSubdivisionCount = 2;
		Node.bBoneSubdivisionCollisionOnly = false;
		Node.BoneConstraintSubdivisionCount = 3;
		Node.BoneConstraintSubdivisionFeedbackScale = 0.5f;
		Node.TargetFramerate = 90;
		Node.bNeedWarmUp = true;
		Node.WarmUpFrames = 8;
		Node.TeleportDistanceThreshold = 123.0f;
		Node.TeleportRotationThreshold = 17.0f;
		Node.PlanarConstraint = EPlanarConstraint::X;
		Node.SkelCompMoveScale = FVector(0.5f, 0.75f, 1.25f);
		Node.PhysicsSettings.Damping = 0.42f;
		Node.PhysicsSettings.Stiffness = 0.73f;
		Node.SphericalLimits.Add(MakeSphereLimit());
		Node.bSharedCollisionSource = true;
		Node.BoneConstraintIterationCountBeforeCollision = 2;
		Node.BoneConstraintIterationCountAfterCollision = 4;
		Node.Gravity = FVector(1.0f, 2.0f, -980.0f);
		Node.bUseLegacyGravity = true;
		Node.bEnableWind = true;
		Node.WindScale = 2.5f;
		Node.SimpleExternalForce = FVector(11.0f, 12.0f, 13.0f);
		Node.bAllowWorldCollision = true;
		Node.SimpleWorldCollisionSource = EKawaiiPhysicsSimpleWorldCollisionSource::Shared;
		Node.SimpleWorldCollisionSharedTag = UGameplayTagsManager::Get().RequestGameplayTag(
			FName(TEXT("KawaiiPhysics.Shared.Default")), false);
		Node.bOverrideCollisionParams = true;
		Node.bIgnoreSelfComponent = false;
		Node.IgnoreBones.Add(FBoneReference(TEXT("pelvis")));
		Node.IgnoreBoneNamePrefix.Add(TEXT("ik_"));
		Node.KawaiiPhysicsTag = TAG_KawaiiPhysicsPresetSource;
		Node.Alpha = 0.93f;
		return Node;
	}

	const FProperty* FindNodeProperty(const FName PropertyName)
	{
		return FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), PropertyName);
	}

	bool IsNodePropertyIdentical(const FName PropertyName,
	                             const FAnimNode_KawaiiPhysics& A,
	                             const FAnimNode_KawaiiPhysics& B)
	{
		const FProperty* Property = FindNodeProperty(PropertyName);
		return Property && Property->Identical_InContainer(&A, &B, 0, KawaiiPhysicsPresetTestIdenticalPortFlags);
	}

	FGameplayTag RequestPresetGameplayTag(const TCHAR* TagName)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(TagName), false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRoundTripTest,
                                 "KawaiiPhysics.Preset.RoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetRoundTripTest::RunTest(const FString& Parameters)
{
	const FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	FAnimNode_KawaiiPhysics TargetNode;
	TargetNode.Alpha = 0.25f;

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);

	FKawaiiPhysicsPresetApplyOptions Options;
	Options.bApplyBoneAssignment = true;
	Options.bApplyTag = true;
	Preset->ApplyToNode(TargetNode, Options, Preset);

	bool bOk = true;
	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		if (UKawaiiPhysicsPresetDataAsset::ShouldApplyNodeProperty(Property, Options))
		{
			bOk &= TestTrue(FString::Printf(TEXT("%s round-trips"), *Property.GetName()),
			                Property.Identical_InContainer(&Preset->Node,
			                                               &TargetNode,
			                                      0,
			                                      KawaiiPhysicsPresetTestIdenticalPortFlags));
		}
	}
	bOk &= TestTrue(TEXT("Inherited Alpha is not copied"), FMath::IsNearlyEqual(TargetNode.Alpha, 0.25f));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetExclusionTest,
                                 "KawaiiPhysics.Preset.Exclusion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetExclusionTest::RunTest(const FString& Parameters)
{
	const FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	FAnimNode_KawaiiPhysics TargetNode;
	TargetNode.RootBone = FBoneReference(TEXT("keep_root"));
	TargetNode.ExcludeBones.Add(FBoneReference(TEXT("keep_exclude")));
	TargetNode.AdditionalRootBones.Add(MakeAdditionalRootBone(TEXT("keep_additional"), TEXT("keep_additional_exclude")));
	TargetNode.KawaiiPhysicsTag = TAG_KawaiiPhysicsPresetTarget;
	TargetNode.Alpha = 0.33f;

	const FAnimNode_KawaiiPhysics BeforeApply = TargetNode;
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);

	const FKawaiiPhysicsPresetApplyOptions Options;
	Preset->ApplyToNode(TargetNode, Options, Preset);

	bool bOk = true;
	bOk &= TestTrue(TEXT("RootBone is preserved"),
	                IsNodePropertyIdentical(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone),
	                                        BeforeApply, TargetNode));
	bOk &= TestTrue(TEXT("ExcludeBones is preserved"),
	                IsNodePropertyIdentical(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExcludeBones),
	                                        BeforeApply, TargetNode));
	bOk &= TestTrue(TEXT("AdditionalRootBones is preserved"),
	                IsNodePropertyIdentical(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, AdditionalRootBones),
	                                        BeforeApply, TargetNode));
	bOk &= TestTrue(TEXT("KawaiiPhysicsTag is preserved"),
	                IsNodePropertyIdentical(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag),
	                                        BeforeApply, TargetNode));
	bOk &= TestTrue(TEXT("Inherited Alpha is preserved"), FMath::IsNearlyEqual(TargetNode.Alpha, 0.33f));
	bOk &= TestTrue(TEXT("Regular preset property is applied"),
	                IsNodePropertyIdentical(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength),
	                                        Preset->Node, TargetNode));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetTargetTagsTest,
                                 "KawaiiPhysics.Preset.TargetTags",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetTargetTagsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag HairTag = RequestPresetGameplayTag(TEXT("KawaiiPhysics.Hair"));
	const FGameplayTag SkirtTag = RequestPresetGameplayTag(TEXT("KawaiiPhysics.Skirt"));
	const FGameplayTag HairLeftTag = RequestPresetGameplayTag(TEXT("Kawaii.Hair.L"));
	const FGameplayTag HairParentTag = HairLeftTag.RequestDirectParent();

	bool bOk = true;
	bOk &= TestTrue(TEXT("Hair tag is available"), HairTag.IsValid());
	bOk &= TestTrue(TEXT("Skirt tag is available"), SkirtTag.IsValid());
	bOk &= TestTrue(TEXT("Hair left tag is available"), HairLeftTag.IsValid());
	bOk &= TestTrue(TEXT("Hair parent tag is available"), HairParentTag.IsValid());
	if (!bOk)
	{
		return false;
	}

	FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	SourceNode.KawaiiPhysicsTag = HairTag;

	UKawaiiPhysicsPresetDataAsset* CopiedPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	CopiedPreset->CopyFromNode(SourceNode);
	bOk &= TestTrue(TEXT("CopyFromNode copies node tag to TargetTags"),
	                CopiedPreset->TargetTags.HasTagExact(HairTag));

	CopiedPreset->CopyFromNode(SourceNode);
	TArray<FGameplayTag> CopiedTags;
	CopiedPreset->TargetTags.GetGameplayTagArray(CopiedTags);
	bOk &= TestEqual(TEXT("CopyFromNode keeps TargetTags unique"),
	                 CopiedTags.Num(),
	                 1);

	UKawaiiPhysicsPresetDataAsset* PreservedPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	PreservedPreset->TargetTags.AddTag(SkirtTag);
	PreservedPreset->CopyFromNode(SourceNode);
	bOk &= TestTrue(TEXT("CopyFromNode preserves existing TargetTags"),
	                PreservedPreset->TargetTags.HasTagExact(SkirtTag));
	bOk &= TestTrue(TEXT("CopyFromNode appends source tag to existing TargetTags"),
	                PreservedPreset->TargetTags.HasTagExact(HairTag));

	FAnimNode_KawaiiPhysics InvalidTagSourceNode = MakePresetSourceNode();
	InvalidTagSourceNode.KawaiiPhysicsTag = FGameplayTag();
	UKawaiiPhysicsPresetDataAsset* InvalidTagPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	InvalidTagPreset->CopyFromNode(InvalidTagSourceNode);
	bOk &= TestTrue(TEXT("CopyFromNode ignores invalid node tags"),
	                InvalidTagPreset->TargetTags.IsEmpty());

	UKawaiiPhysicsPresetDataAsset* EmptyTargetPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	bOk &= TestFalse(TEXT("Empty TargetTags targets no nodes"),
	                 EmptyTargetPreset->TargetsNodeTag(HairTag));

	UKawaiiPhysicsPresetDataAsset* ExactTargetPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	ExactTargetPreset->TargetTags.AddTag(HairTag);
	ExactTargetPreset->bTargetTagsExactMatch = true;
	bOk &= TestTrue(TEXT("Exact TargetTags match exact node tag"),
	                ExactTargetPreset->TargetsNodeTag(HairTag));

	UKawaiiPhysicsPresetDataAsset* HierarchyTargetPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	HierarchyTargetPreset->TargetTags.AddTag(HairParentTag);
	bOk &= TestTrue(TEXT("TargetTags match child node tag hierarchically"),
	                HierarchyTargetPreset->TargetsNodeTag(HairLeftTag));

	HierarchyTargetPreset->bTargetTagsExactMatch = true;
	bOk &= TestFalse(TEXT("Exact TargetTags reject hierarchical-only matches"),
	                 HierarchyTargetPreset->TargetsNodeTag(HairLeftTag));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetTransientSkipTest,
                                 "KawaiiPhysics.Preset.TransientSkip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetTransientSkipTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	FKawaiiPhysicsModifyBone RuntimeBone;
	RuntimeBone.Location = FVector(10.0f, 20.0f, 30.0f);
	SourceNode.ModifyBones.Add(RuntimeBone);
	SourceNode.DeltaTime = 0.5f;

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);

	bool bOk = true;
	bOk &= TestTrue(TEXT("ModifyBones is not copied into preset"), Preset->Node.ModifyBones.IsEmpty());
	bOk &= TestTrue(TEXT("DeltaTime is not copied into preset"), FMath::IsNearlyZero(Preset->Node.DeltaTime));

	FAnimNode_KawaiiPhysics TargetNode;
	TargetNode.ModifyBones.Add(RuntimeBone);
	TargetNode.DeltaTime = 1.0f;
	Preset->ApplyToNode(TargetNode, FKawaiiPhysicsPresetApplyOptions(), Preset);

	bOk &= TestTrue(TEXT("ModifyBones is not applied"), TargetNode.ModifyBones.Num() == 1);
	bOk &= TestTrue(TEXT("DeltaTime is not applied"), FMath::IsNearlyEqual(TargetNode.DeltaTime, 1.0f));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetCustomExternalForcesSkipTest,
                                 "KawaiiPhysics.Preset.CustomExternalForcesSkip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetCustomExternalForcesSkipTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	SourceNode.ExternalForces.AddDefaulted();
	SourceNode.CustomExternalForces.Add(nullptr);

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);

	FAnimNode_KawaiiPhysics RuntimeTargetNode;
	RuntimeTargetNode.ExternalForces.AddDefaulted();
	RuntimeTargetNode.ExternalForces.AddDefaulted();
	RuntimeTargetNode.CustomExternalForces.Add(nullptr);
	RuntimeTargetNode.CustomExternalForces.Add(nullptr);
	Preset->ApplyToNode(RuntimeTargetNode, FKawaiiPhysicsPresetApplyOptions(), nullptr);

	bool bOk = true;
	bOk &= TestEqual(TEXT("ExternalForces are skipped without target outer"),
	                 RuntimeTargetNode.ExternalForces.Num(),
	                 2);
	bOk &= TestEqual(TEXT("CustomExternalForces are skipped without target outer"),
	                 RuntimeTargetNode.CustomExternalForces.Num(),
	                 2);

	FAnimNode_KawaiiPhysics EditorTargetNode;
	Preset->ApplyToNode(EditorTargetNode, FKawaiiPhysicsPresetApplyOptions(), Preset);
	bOk &= TestEqual(TEXT("ExternalForces are applied with target outer"),
	                 EditorTargetNode.ExternalForces.Num(),
	                 Preset->Node.ExternalForces.Num());
	bOk &= TestEqual(TEXT("CustomExternalForces are applied with target outer"),
	                 EditorTargetNode.CustomExternalForces.Num(),
	                 Preset->Node.CustomExternalForces.Num());
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetMatchTest,
                                 "KawaiiPhysics.Preset.Match",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetMatchTest::RunTest(const FString& Parameters)
{
	const FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);

	FKawaiiPhysicsPresetApplyOptions Options;
	Options.bApplyBoneAssignment = true;
	Options.bApplyTag = true;

	TArray<FName> DiffProperties;
	bool bOk = true;
	bOk &= TestTrue(TEXT("Copied node matches preset"),
	                Preset->MatchesNode(Preset->Node, Options, DiffProperties));
	bOk &= TestTrue(TEXT("No differences for matching node"), DiffProperties.IsEmpty());

	FAnimNode_KawaiiPhysics ChangedNode = Preset->Node;
	ChangedNode.DummyBoneLength += 1.0f;
	ChangedNode.RootBone = FBoneReference(TEXT("changed_root"));
	ChangedNode.KawaiiPhysicsTag = TAG_KawaiiPhysicsPresetTarget;

	bOk &= TestFalse(TEXT("Changed node does not match"),
	                 Preset->MatchesNode(ChangedNode, Options, DiffProperties));
	bOk &= TestTrue(TEXT("DummyBoneLength difference is reported"),
	                DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength)));
	bOk &= TestTrue(TEXT("RootBone difference is reported when bone assignment is applied"),
	                DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone)));
	bOk &= TestTrue(TEXT("KawaiiPhysicsTag difference is reported when tag is applied"),
	                DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag)));

	FKawaiiPhysicsPresetApplyOptions ProtectedOptions;
	bOk &= TestFalse(TEXT("Changed node still differs by regular property"),
	                 Preset->MatchesNode(ChangedNode, ProtectedOptions, DiffProperties));
	bOk &= TestFalse(TEXT("RootBone difference is hidden when bone assignment is protected"),
	                 DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone)));
	bOk &= TestFalse(TEXT("KawaiiPhysicsTag difference is hidden when tag is protected"),
	                 DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag)));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetPropertyClassificationTest,
                                 "KawaiiPhysics.Preset.PropertyClassification",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetPropertyClassificationTest::RunTest(const FString& Parameters)
{
	TArray<FName> UnclassifiedProperties;
	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		if (UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(Property) ==
			EKawaiiPhysicsPresetPropertyClass::Unknown)
		{
			UnclassifiedProperties.Add(Property.GetFName());
		}
	}

	FString UnclassifiedList;
	for (const FName PropertyName : UnclassifiedProperties)
	{
		if (!UnclassifiedList.IsEmpty())
		{
			UnclassifiedList += TEXT(", ");
		}
		UnclassifiedList += PropertyName.ToString();
	}

	TestTrue(FString::Printf(TEXT("All own properties are classified. Unclassified: %s"),
	                         *UnclassifiedList),
	         UnclassifiedProperties.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
