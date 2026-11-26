// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GameplayTagsManager.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTestGameplayTags.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsPresetTestPrivate
{
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsPresetSource, "KawaiiPhysics.Test.PresetSource");
KP_DEFINE_TEST_GAMEPLAY_TAG_STATIC(TAG_KawaiiPhysicsPresetTarget, "KawaiiPhysics.Test.PresetTarget");

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
		Node.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::PerAxis;
		Node.PhysicsSettings.PerAxisLimitAngle.Z.Min = -12.0f;
		Node.PhysicsSettings.PerAxisLimitAngle.Z.Max = 34.0f;
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

	TMap<FName, EKawaiiPhysicsPresetPropertyClass> GetExpectedPropertyClasses()
	{
		TMap<FName, EKawaiiPhysicsPresetPropertyClass> ExpectedClasses;
		auto AddNames = [&ExpectedClasses](const TCHAR* Names, const EKawaiiPhysicsPresetPropertyClass PropertyClass)
		{
			TArray<FString> PropertyNames;
			FString(Names).ParseIntoArray(PropertyNames, TEXT(","), true);
			for (FString& PropertyName : PropertyNames)
			{
				PropertyName.TrimStartAndEndInline();
				ExpectedClasses.Add(FName(*PropertyName), PropertyClass);
			}
		};
		// PR-U2 で承認された 87 件の固定表。分類関数から期待表を作らない。
		AddNames(TEXT("DummyBoneLength,BoneSubdivisionCount,bBoneSubdivisionCollisionOnly,bBoneSubdivisionDensifyByRadius,"
			"BoneConstraintSubdivisionCount,BoneConstraintSubdivisionFeedbackScale,BoneForwardAxis,PhysicsSettings,"
			"SimulationSpace,SimulationBaseBone,TargetFramerate,WarmUpFrames,bUseWarmUpWhenResetDynamics,bNeedWarmUp,"
			"TeleportDistanceThreshold,TeleportRotationThreshold,PlanarConstraint,SkelCompMoveScale,"
			"bUpdatePhysicsSettingsInGame,ResetBoneTransformWhenBoneNotFound,DampingCurveData,StiffnessCurveData,"
			"WorldDampingLocationCurveData,WorldDampingRotationCurveData,RadiusCurveData,LimitAngleCurveData,"
			"SphericalLimits,CapsuleLimits,TaperedCapsuleLimits,BoxLimits,PlanarLimits,LimitsDataAsset,"
			"PhysicsAssetForLimits,MirrorDataTableForLimits,bSkipMirroredBoneWithExistingCollision,"
			"bSharedCollisionSource,bUseSharedCollision,SharedCollisionGroupTag,BoneConstraintGlobalComplianceType,"
			"BoneConstraintIterationCountBeforeCollision,BoneConstraintIterationCountAfterCollision,"
			"bAutoAddChildDummyBoneConstraint,BoneConstraints,BoneConstraintsDataAsset,SyncBones,Gravity,bUseLegacyGravity,"
			"bUseDefaultGravityZProjectSetting,bUseWorldSpaceGravity,bEnableWind,WindScale,WindDirectionNoiseAngle,"
			"WindSampleMode,SimpleExternalForce,bUseWorldSpaceSimpleExternalForce,ExternalForces,CustomExternalForces,"
			"bAllowWorldCollision,bOverrideCollisionParams,CollisionChannelSettings,bIgnoreSelfComponent,IgnoreBones,"
			"IgnoreBoneNamePrefix,bUseSimpleWorldCollision,SimpleWorldCollisionGatherInterval,SimpleWorldCollisionObjectTypes,"
			"SimpleWorldCollisionConvexFallbackShape,bOverrideSimpleWorldCollisionGatherRadius,SimpleWorldCollisionGatherRadius,"
			"bSimpleWorldCollisionGroundCollision,SimpleWorldCollisionSkeletalMeshCollision,SimpleWorldCollisionSource,"
			"SimpleWorldCollisionSharedTag"), EKawaiiPhysicsPresetPropertyClass::CopyTarget);
		AddNames(TEXT("RootBone,ExcludeBones,AdditionalRootBones"), EKawaiiPhysicsPresetPropertyClass::BoneAssignment);
		AddNames(TEXT("KawaiiPhysicsTag"), EKawaiiPhysicsPresetPropertyClass::Tag);
		AddNames(TEXT("TimeScale"), EKawaiiPhysicsPresetPropertyClass::RuntimeOnly);
		AddNames(TEXT("SphericalLimitsData,CapsuleLimitsData,TaperedCapsuleLimitsData,BoxLimitsData,PlanarLimitsData,"
			"BoneConstraintsData,MergedBoneConstraints,ModifyBones,DeltaTime"), EKawaiiPhysicsPresetPropertyClass::Deny);
		return ExpectedClasses;
	}

// PR-02・PR-04・PR-17。mutant なし。元ノードの非既定値を、固定表の CopyTarget として比較する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRoundTripTest,
                                 "KawaiiPhysics.Preset.RoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetRoundTripTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	SourceNode.TimeScale = 0.25f;
	SourceNode.DeltaTime = 0.125f;

	UKawaiiPhysicsPresetDataAsset* Preset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	Preset->CopyFromNode(SourceNode);
	bool bOk = true;
	bOk &= TestEqual(TEXT("TimeScale は保存されない"), Preset->Node.TimeScale, 1.0f);
	const TMap<FName, EKawaiiPhysicsPresetPropertyClass> ExpectedClasses = GetExpectedPropertyClasses();
	const FAnimNode_KawaiiPhysics DefaultNode;
	int32 ChangedCopyTargets = 0;
	for (const TPair<FName, EKawaiiPhysicsPresetPropertyClass>& Expected : ExpectedClasses)
	{
		if (Expected.Value == EKawaiiPhysicsPresetPropertyClass::CopyTarget)
		{
			ChangedCopyTargets += !IsNodePropertyIdentical(Expected.Key, SourceNode, DefaultNode) ? 1 : 0;
			bOk &= TestTrue(FString::Printf(TEXT("%s は元ノードから保存される"), *Expected.Key.ToString()),
				IsNodePropertyIdentical(Expected.Key, SourceNode, Preset->Node));
		}
	}
	bOk &= TestTrue(TEXT("既定値だけの往復ではない"), ChangedCopyTargets >= 20);
	for (const bool bApplyBoneAssignment : {false, true})
	{
		for (const bool bApplyTag : {false, true})
		{
			FAnimNode_KawaiiPhysics TargetNode;
			TargetNode.TimeScale = 2.0f;
			TargetNode.DeltaTime = 0.5f;
			TargetNode.RootBone = FBoneReference(TEXT("untouched_root"));
			TargetNode.KawaiiPhysicsTag = TAG_KawaiiPhysicsPresetTarget;
			const FAnimNode_KawaiiPhysics BeforeApply = TargetNode;
			FKawaiiPhysicsPresetApplyOptions ApplyOptions;
			ApplyOptions.bApplyBoneAssignment = bApplyBoneAssignment;
			ApplyOptions.bApplyTag = bApplyTag;
			Preset->ApplyToNode(TargetNode, ApplyOptions, Preset);
			for (const TPair<FName, EKawaiiPhysicsPresetPropertyClass>& Expected : ExpectedClasses)
			{
				const bool bShouldCopy = Expected.Value == EKawaiiPhysicsPresetPropertyClass::CopyTarget ||
					(Expected.Value == EKawaiiPhysicsPresetPropertyClass::BoneAssignment && bApplyBoneAssignment) ||
					(Expected.Value == EKawaiiPhysicsPresetPropertyClass::Tag && bApplyTag);
				const FAnimNode_KawaiiPhysics& ReferenceNode = bShouldCopy ? SourceNode : BeforeApply;
				bOk &= TestTrue(FString::Printf(TEXT("%s のフラグ %d/%d"), *Expected.Key.ToString(), bApplyBoneAssignment, bApplyTag),
					IsNodePropertyIdentical(Expected.Key, ReferenceNode, TargetNode));
			}
		}
	}
	return bOk;
}

// 保護プロパティ、実行時状態、外部力の適用条件を実際の ApplyToNode で守る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetApplyExclusionsTest,
                                 "KawaiiPhysics.Preset.ApplyExclusions",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetApplyExclusionsTest::RunTest(const FString& Parameters)
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
	bOk &= TestTrue(TEXT("TransientForceStore is not a reflected preset property"),
	                FAnimNode_KawaiiPhysics::StaticStruct()->FindPropertyByName(
		                GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TransientForceStore)) == nullptr);

	// 実行時配列はプリセットへのコピーにもノードへの適用にも含めない。
	{
		FAnimNode_KawaiiPhysics TransientSourceNode = MakePresetSourceNode();
		FKawaiiPhysicsModifyBone RuntimeBone;
		RuntimeBone.Location = FVector(10.0f, 20.0f, 30.0f);
		TransientSourceNode.ModifyBones.Add(RuntimeBone);
		TransientSourceNode.DeltaTime = 0.5f;
		TransientSourceNode.TimeScale = 0.25f;

		UKawaiiPhysicsPresetDataAsset* TransientPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
		TransientPreset->CopyFromNode(TransientSourceNode);
		bOk &= TestTrue(TEXT("ModifyBones is not copied into preset"), TransientPreset->Node.ModifyBones.IsEmpty());
		bOk &= TestTrue(TEXT("DeltaTime is not copied into preset"), FMath::IsNearlyZero(TransientPreset->Node.DeltaTime));
		bOk &= TestTrue(TEXT("TimeScale is not copied into preset"), FMath::IsNearlyEqual(TransientPreset->Node.TimeScale, 1.0f));

		FAnimNode_KawaiiPhysics TransientTargetNode;
		TransientTargetNode.ModifyBones.Add(RuntimeBone);
		TransientTargetNode.DeltaTime = 1.0f;
		TransientTargetNode.TimeScale = 0.5f;
		TransientPreset->ApplyToNode(TransientTargetNode, FKawaiiPhysicsPresetApplyOptions(), TransientPreset);
		bOk &= TestTrue(TEXT("ModifyBones is not applied"), TransientTargetNode.ModifyBones.Num() == 1);
		bOk &= TestTrue(TEXT("DeltaTime is not applied"), FMath::IsNearlyEqual(TransientTargetNode.DeltaTime, 1.0f));
		bOk &= TestTrue(TEXT("TimeScale is not applied"), FMath::IsNearlyEqual(TransientTargetNode.TimeScale, 0.5f));
	}

	// outer がない実行時ノードでは外部力を保護し、outer がある場合だけ適用する。
	{
		FAnimNode_KawaiiPhysics ForceSourceNode = MakePresetSourceNode();
		ForceSourceNode.ExternalForces.AddDefaulted();
		ForceSourceNode.CustomExternalForces.Add(nullptr);

		UKawaiiPhysicsPresetDataAsset* ForcePreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
		ForcePreset->CopyFromNode(ForceSourceNode);

		FAnimNode_KawaiiPhysics RuntimeTargetNode;
		RuntimeTargetNode.ExternalForces.AddDefaulted();
		RuntimeTargetNode.ExternalForces.AddDefaulted();
		RuntimeTargetNode.CustomExternalForces.Add(nullptr);
		RuntimeTargetNode.CustomExternalForces.Add(nullptr);
		ForcePreset->ApplyToNode(RuntimeTargetNode, FKawaiiPhysicsPresetApplyOptions(), nullptr);
		bOk &= TestEqual(TEXT("ExternalForces are skipped without target outer"),
		                 RuntimeTargetNode.ExternalForces.Num(), 2);
		bOk &= TestEqual(TEXT("CustomExternalForces are skipped without target outer"),
		                 RuntimeTargetNode.CustomExternalForces.Num(), 2);

		FAnimNode_KawaiiPhysics EditorTargetNode;
		ForcePreset->ApplyToNode(EditorTargetNode, FKawaiiPhysicsPresetApplyOptions(), ForcePreset);
		bOk &= TestEqual(TEXT("ExternalForces are applied with target outer"),
		                 EditorTargetNode.ExternalForces.Num(), ForcePreset->Node.ExternalForces.Num());
		bOk &= TestEqual(TEXT("CustomExternalForces are applied with target outer"),
		                 EditorTargetNode.CustomExternalForces.Num(), ForcePreset->Node.CustomExternalForces.Num());
	}
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

	FAnimNode_KawaiiPhysics SourceNode = MakePresetSourceNode();
	SourceNode.KawaiiPhysicsTag = HairTag;

	UKawaiiPhysicsPresetDataAsset* CopiedPreset = NewObject<UKawaiiPhysicsPresetDataAsset>();
	CopiedPreset->CopyFromNode(SourceNode);
	bOk &= TestTrue(TEXT("CopyFromNode copies node tag to TargetTags"),
	                CopiedPreset->TargetTags.HasTagExact(HairTag));

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

// PR-03・PR-05・PR-U3。mutant なし。適用先の別ノードと比較し、TimeScale を比較対象から除く。
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

	FAnimNode_KawaiiPhysics AppliedNode;
	Preset->ApplyToNode(AppliedNode, Options, Preset);
	TArray<FName> DiffProperties;
	TArray<FName> ComparedProperties;
	bool bOk = true;
	bOk &= TestTrue(TEXT("別ノードへ適用した値は一致"),
	                Preset->MatchesNode(AppliedNode, Options, DiffProperties, &ComparedProperties));
	bOk &= TestEqual(TEXT("一致すると差分は空"), DiffProperties.Num(), 0);
	FAnimNode_KawaiiPhysics RuntimeOnlyNode = AppliedNode;
	RuntimeOnlyNode.TimeScale = 0.25f;
	bOk &= TestTrue(TEXT("TimeScale だけの差は一致"),
		Preset->MatchesNode(RuntimeOnlyNode, Options, DiffProperties, &ComparedProperties));
	bOk &= TestEqual(TEXT("TimeScale は差分に出ない"), DiffProperties.Num(), 0);
	bOk &= TestFalse(TEXT("TimeScale は比較項目に出ない"),
		ComparedProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TimeScale)));
	FAnimNode_KawaiiPhysics ChangedNode = AppliedNode;
	ChangedNode.DummyBoneLength += 1.0f;
	bOk &= TestFalse(TEXT("CopyTarget を一つ変えると不一致"),
		Preset->MatchesNode(ChangedNode, Options, DiffProperties, &ComparedProperties));
	bOk &= TestEqual(TEXT("差分は一件だけ"), DiffProperties.Num(), 1);
	bOk &= TestTrue(TEXT("差分名は DummyBoneLength"),
		DiffProperties.Contains(GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength)));
	return bOk;
}

// PR-U2。mutant なし。固定表との照合により分類漏れと誤分類を個別の名前で報告する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetPropertyClassificationTest,
                                 "KawaiiPhysics.Preset.PropertyClassification",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsPresetPropertyClassificationTest::RunTest(const FString& Parameters)
{
	TMap<FName, EKawaiiPhysicsPresetPropertyClass> ExpectedClasses = GetExpectedPropertyClasses();
	bool bOk = TestEqual(TEXT("固定表は 87 件"), ExpectedClasses.Num(), 87);
	int32 SeenCount = 0;
	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		const EKawaiiPhysicsPresetPropertyClass* ExpectedClass = ExpectedClasses.Find(Property.GetFName());
		const EKawaiiPhysicsPresetPropertyClass ActualClass = UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(Property);
		if (!ExpectedClass || *ExpectedClass != ActualClass)
		{
			AddError(FString::Printf(TEXT("%s: 期待分類 %d、実際 %d"), *Property.GetName(),
				ExpectedClass ? static_cast<int32>(*ExpectedClass) : -1, static_cast<int32>(ActualClass)));
			bOk = false;
		}
		ExpectedClasses.Remove(Property.GetFName());
		++SeenCount;
	}
	for (const TPair<FName, EKawaiiPhysicsPresetPropertyClass>& Missing : ExpectedClasses)
	{
		AddError(FString::Printf(TEXT("固定表の %s がノードに無い。期待分類 %d"), *Missing.Key.ToString(),
			static_cast<int32>(Missing.Value)));
		bOk = false;
	}
	bOk &= TestEqual(TEXT("ノード自身の UPROPERTY は 87 件"), SeenCount, 87);
	return bOk;
}

} // namespace KawaiiPhysicsPresetTestPrivate

#endif // WITH_DEV_AUTOMATION_TESTS
