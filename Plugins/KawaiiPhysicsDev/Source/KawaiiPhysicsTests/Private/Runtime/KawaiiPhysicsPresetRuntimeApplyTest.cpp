// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsNodeFixture.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "../Editor/KawaiiPhysicsAnimBlueprintFixture.h"
#endif

namespace KawaiiPhysicsPresetRuntimeApplyTestPrivate
{

static KawaiiPhysicsNodeFixture::FOptions MakeOptions()
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
	Options.bPreUpdateEveryFrame = true;
	Options.GetComponentTransform = [](int32 Frame)
	{
		return FTransform(FVector(0.0, FMath::Sin(Frame * 0.18) * 8.0, 0.0));
	};
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.bNeedWarmUp = false;
		Node.PhysicsSettings.Damping = 0.1f;
	};
	return Options;
}

static TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> MakePreset(const FAnimNode_KawaiiPhysics& Node)
{
	TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset> Preset(NewObject<UKawaiiPhysicsPresetDataAsset>());
	Preset->CopyFromNode(Node);
	return Preset;
}

static bool ArePositionsIdentical(const FAnimNode_KawaiiPhysics& A, const FAnimNode_KawaiiPhysics& B)
{
	if (A.ModifyBones.Num() != B.ModifyBones.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.ModifyBones.Num(); ++Index)
	{
		if (A.ModifyBones[Index].Location != B.ModifyBones[Index].Location)
		{
			return false;
		}
	}
	return true;
}

static TArray<FVector> CopyPositions(const FAnimNode_KawaiiPhysics& Node)
{
	TArray<FVector> Positions;
	Positions.Reserve(Node.ModifyBones.Num());
	for (const FKawaiiPhysicsModifyBone& Bone : Node.ModifyBones)
	{
		Positions.Add(Bone.Location);
	}
	return Positions;
}

static double GetMaximumDisplacement(const TArray<FVector>& Before, const TArray<FVector>& After)
{
	double Maximum = 0.0;
	for (int32 Index = 0; Index < Before.Num() && Index < After.Num(); ++Index)
	{
		Maximum = FMath::Max(Maximum, static_cast<double>(FVector::Dist(Before[Index], After[Index])));
	}
	return Maximum;
}

static bool HasNoPendingRequests(const FKawaiiPhysicsTestAccessor& Accessor)
{
	return !Accessor.IsModifyBonesReinitPending() && !Accessor.IsBoneReferencesReinitPending() &&
		!Accessor.IsLimitAssetsReapplyPending() && !Accessor.IsSyncBonesReinitPending() &&
		!Accessor.IsSharedCollisionReinitPending() && !Accessor.IsPhysicsSettingsRefreshPending();
}

// PR-11 / FX-34: 修正前は同じプリセットでも ModifyBones が作り直され、位置が対照とずれる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplySameTest,
	"KawaiiPhysics.Preset.RuntimeApplySame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplySameTest::RunTest(const FString& Parameters)
{
	const KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
	KawaiiPhysicsNodeFixture::FFixture Target(Options);
	KawaiiPhysicsNodeFixture::FFixture Control(Options);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Target.EvaluateFrame();
		Control.EvaluateFrame();
	}
	const auto Preset = MakePreset(Target.Node);
	bool bOk = true;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		bOk &= TestEqual(TEXT("same preset changes no properties"),
			UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Target.Node, *Preset, {}), 0);
		bOk &= TestTrue(TEXT("same preset leaves requests clear"), HasNoPendingRequests(Target.Accessor));
		Target.EvaluateFrame();
		Control.EvaluateFrame();
		bOk &= TestTrue(TEXT("positions match untouched control exactly"), ArePositionsIdentical(Target.Node, Control.Node));
	}
	return bOk;
}

// PR-09 / PR-U4: 修正前は LimitsDataAsset だけでも ModifyBones を作り直す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyLimitsTest,
	"KawaiiPhysics.Preset.RuntimeApplyLimits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyLimitsTest::RunTest(const FString& Parameters)
{
	const KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
	KawaiiPhysicsNodeFixture::FFixture Target(Options);
	KawaiiPhysicsNodeFixture::FFixture Control(Options);
	for (int32 Frame = 0; Frame < 12; ++Frame) { Target.EvaluateFrame(); Control.EvaluateFrame(); }
	const auto Preset = MakePreset(Target.Node);
	TStrongObjectPtr<UKawaiiPhysicsLimitsDataAsset> Limits(NewObject<UKawaiiPhysicsLimitsDataAsset>());
	FSphericalLimit& Sphere = Limits->SphericalLimits.AddDefaulted_GetRef();
	Sphere.OffsetLocation = FVector(100000.0, 0.0, 0.0);
	Sphere.Radius = 1.0f;
	Preset->Node.LimitsDataAsset = Limits.Get();
	bool bOk = TestEqual(TEXT("only asset changes"),
		UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Target.Node, *Preset, {}), 1);
	bOk &= TestTrue(TEXT("limit reapply requested"), Target.Accessor.IsLimitAssetsReapplyPending());
	bOk &= TestTrue(TEXT("bone references requested"), Target.Accessor.IsBoneReferencesReinitPending());
	bOk &= TestFalse(TEXT("bones stay built"), Target.Accessor.IsModifyBonesReinitPending());
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Target.EvaluateFrame(); Control.EvaluateFrame();
		bOk &= TestTrue(TEXT("distant limit preserves positions"), ArePositionsIdentical(Target.Node, Control.Node));
	}
	return bOk;
}

// PR-10: 修正前は物理設定の変更で ModifyBones が作り直され、位置がポーズへ戻る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyPhysicsTest,
	"KawaiiPhysics.Preset.RuntimeApplyPhysics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyPhysicsTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	double BaselineMaximum = 0.0;
	TArray<FVector> PreviousPositions;
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Fixture.EvaluateFrame();
		const TArray<FVector> CurrentPositions = CopyPositions(Fixture.Node);
		if (Frame >= 2)
		{
			BaselineMaximum = FMath::Max(BaselineMaximum,
				GetMaximumDisplacement(PreviousPositions, CurrentPositions));
		}
		PreviousPositions = CurrentPositions;
	}
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.PhysicsSettings.Damping = 0.85f;
	Preset->Node.PhysicsSettings.Stiffness = 0.9f;
	Preset->Node.PhysicsSettings.Radius = 2.0f;
	Preset->Node.PhysicsSettings.LimitAngle = 45.0f;
	const int32 BoneCount = Fixture.Node.ModifyBones.Num();
	// 適用直後は強い Stiffness と新しい LimitAngle でポーズ側へ引き戻されるのが設定どおりの動き。
	// 弾けていないことは「ポーズとのずれを越えて飛ばない」で見る（適用前のポーズとの最大のずれ＋通常の変位の 3 倍）。
	double PoseDeviationMaximum = 0.0;
	for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
	{
		PoseDeviationMaximum = FMath::Max(PoseDeviationMaximum, static_cast<double>(FVector::Dist(Bone.Location, Bone.PoseLocation)));
	}
	bool bOk = TestEqual(TEXT("physics settings count as one property"),
		UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {}), 1);
	bOk &= TestFalse(TEXT("settings do not request bone rebuild"), Fixture.Accessor.IsModifyBonesReinitPending());
	bOk &= TestFalse(TEXT("settings do not request warm-up"), Fixture.Node.bNeedWarmUp);
	bOk &= TestTrue(TEXT("settings request refresh"), Fixture.Accessor.IsPhysicsSettingsRefreshPending());
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Fixture.EvaluateFrame();
		bOk &= TestEqual(TEXT("bone count is stable"), Fixture.Node.ModifyBones.Num(), BoneCount);
		const TArray<FVector> CurrentPositions = CopyPositions(Fixture.Node);
		// 比較基準は適用前 10 フレームの最大変位。固定の小さな許容値は静止に近い場合の丸め差を吸収する。
		// 適用直後のフレームだけは、ポーズへの引き戻し分（適用前のポーズとの最大のずれ）を上乗せする。
		const double Displacement = GetMaximumDisplacement(PreviousPositions, CurrentPositions);
		const double Limit = 3.0 * BaselineMaximum + 0.01 + (Frame == 0 ? PoseDeviationMaximum : 0.0);
		bOk &= TestTrue(FString::Printf(TEXT("one-frame displacement stays bounded (frame %d: %.4f, limit %.4f, baseline %.4f, pose deviation %.4f)"),
			Frame, Displacement, Limit, BaselineMaximum, PoseDeviationMaximum), Displacement <= Limit);
		PreviousPositions = CurrentPositions;
		for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
		{
			bOk &= TestTrue(TEXT("bone position is finite"), !Bone.Location.ContainsNaN());
			bOk &= TestEqual(TEXT("bone damping is refreshed"), Bone.PhysicsSettings.Damping, 0.85f);
			if (Fixture.Node.ModifyBones.IsValidIndex(Bone.ParentIndex))
			{
				const double LinkLength = FVector::Dist(Bone.Location,
					Fixture.Node.ModifyBones[Bone.ParentIndex].Location);
				// 10 cm リンクの 5% を数値許容差とする。
				bOk &= TestTrue(TEXT("link length remains constrained"),
					FMath::Abs(LinkLength - static_cast<double>(Bone.BoneLength)) <= 0.5);
			}
		}
	}
	return bOk;
}

// PR-U4: 修正前後とも構成変更は再構築する。変更範囲の取り違えを検出する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyTopologyTest,
	"KawaiiPhysics.Preset.RuntimeApplyTopology", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyTopologyTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	Fixture.EvaluateFrame();
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.DummyBoneLength += 5.0f;
	const int32 Changed = UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {});
	return TestEqual(TEXT("one topology property changed"), Changed, 1) &&
		TestTrue(TEXT("topology requests rebuild"), Fixture.Accessor.IsModifyBonesReinitPending());
}

// PR-U4: BoneConstraint を分割しているときは、bridge ダミーの要否が構築時の Radius で決まるので作り直す。
// 分割なしでは Radius だけの変更で作り直さない（物理状態を保つ）。修正前は分割の有無にかかわらず作り直す。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyConstraintRadiusTest,
	"KawaiiPhysics.Preset.RuntimeApplyConstraintRadius", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyConstraintRadiusTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const int32 SubdivisionCount : {0, 1})
	{
		KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
		Options.ConfigureNode = [SubdivisionCount](FAnimNode_KawaiiPhysics& Node)
		{
			Node.RootBone.BoneName = TEXT("root");
			Node.bNeedWarmUp = false;
			Node.PhysicsSettings.Radius = 1.0f;
			Node.BoneConstraintSubdivisionCount = SubdivisionCount;
		};
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
		Fixture.EvaluateFrame();
		const auto Preset = MakePreset(Fixture.Node);
		Preset->Node.PhysicsSettings.Radius = 6.0f;
		bOk &= TestEqual(TEXT("radius changes one property"),
			UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {}), 1);
		bOk &= TestEqual(FString::Printf(TEXT("rebuild requested only with constraint subdivision (count %d)"), SubdivisionCount),
			Fixture.Accessor.IsModifyBonesReinitPending(), SubdivisionCount > 0);
	}
	return bOk;
}

// PR-U4: 修正前は bNeedWarmUp をコピーして次の評価で warm-up を再実行する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyWarmUpTest,
	"KawaiiPhysics.Preset.RuntimeApplyWarmUp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyWarmUpTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	Fixture.EvaluateFrame();
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.bNeedWarmUp = true;
	const int32 Changed = UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {});
	bool bOk = TestEqual(TEXT("warm-up request is not copied"), Changed, 0);
	bOk &= TestFalse(TEXT("warm-up stays disabled"), Fixture.Node.bNeedWarmUp);
	Fixture.EvaluateFrame();
	bOk &= TestFalse(TEXT("warm-up stays disabled after evaluation"), Fixture.Node.bNeedWarmUp);
	return bOk;
}

// PR-U4: 修正前は実行時の物理設定更新が gate を通らず、Damping が古いままになる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyRefreshTest,
	"KawaiiPhysics.Preset.RuntimeApplyRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyRefreshTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FOptions Options = MakeOptions();
	Options.ConfigureNode = [](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.bUpdatePhysicsSettingsInGame = false;
		Node.PhysicsSettings.Damping = 0.1f;
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	for (int32 Frame = 0; Frame < 12; ++Frame) { Fixture.EvaluateFrame(); }
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.PhysicsSettings.Damping = 0.75f;
	const int32 Changed = UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {});
	bool bOk = TestEqual(TEXT("settings changed"), Changed, 1);
	bOk &= TestFalse(TEXT("no rebuild requested"), Fixture.Accessor.IsModifyBonesReinitPending());
	Fixture.EvaluateFrame();
	for (const FKawaiiPhysicsModifyBone& Bone : Fixture.Node.ModifyBones)
	{
		bOk &= TestEqual(TEXT("damping was refreshed"), Bone.PhysicsSettings.Damping, 0.75f);
	}
	bOk &= TestFalse(TEXT("refresh was consumed"), Fixture.Accessor.IsPhysicsSettingsRefreshPending());
	return bOk;
}

// PR-08: 修正前はプリセット適用で SimpleWorld の変更要求が出ない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplySharedTest,
	"KawaiiPhysics.Preset.RuntimeApplyShared", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplySharedTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	Fixture.EvaluateFrame();
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.bUseSharedCollision = !Fixture.Node.bUseSharedCollision;
	bool bOk = TestEqual(TEXT("shared collision changed"),
		UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {}), 1);
	bOk &= TestTrue(TEXT("shared collision reinit requested"), Fixture.Accessor.IsSharedCollisionReinitPending());
	bOk &= TestEqual(TEXT("same value makes no new change"),
		UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {}), 0);
	return bOk;
}

// PR-06: 修正前の実行時適用でも外力を除外する。比較付きの新経路で退行しないことを検出する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyForcesTest,
	"KawaiiPhysics.Preset.RuntimeApplyForces", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyForcesTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions());
	Fixture.Node.ExternalForces.AddDefaulted();
	Fixture.Node.CustomExternalForces.Add(nullptr);
	Fixture.Node.CustomExternalForces.Add(nullptr);
	const auto Preset = MakePreset(Fixture.Node);
	Preset->Node.ExternalForces.Reset();
	Preset->Node.CustomExternalForces.Reset();
	const int32 Changed = UKawaiiPhysicsLibrary::ApplyPresetDataAssetToNode(Fixture.Node, *Preset, {});
	return TestEqual(TEXT("external force is excluded"), Changed, 0) &&
		TestEqual(TEXT("external force remains"), Fixture.Node.ExternalForces.Num(), 1) &&
		TestEqual(TEXT("custom force pointers remain"), Fixture.Node.CustomExternalForces.Num(), 2) &&
		TestTrue(TEXT("custom force pointer identity remains"),
			Fixture.Node.CustomExternalForces[0] == nullptr && Fixture.Node.CustomExternalForces[1] == nullptr);
}

// PR-U4: 修正前は実行時適用が分類を振り分けず、追加された CopyTarget の要求漏れを検出できない。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyCoverageTest,
	"KawaiiPhysics.Preset.RuntimeApplyCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyCoverageTest::RunTest(const FString& Parameters)
{
	// 評価中に直接読む設定。要求表の期待値を本番の表から作らない。
	const TSet<FName> Direct = {
		TEXT("bBoneSubdivisionCollisionOnly"), TEXT("PhysicsSettings"),
		TEXT("SimulationSpace"), TEXT("TargetFramerate"), TEXT("WarmUpFrames"),
		TEXT("bUseWarmUpWhenResetDynamics"), TEXT("bNeedWarmUp"), TEXT("TeleportDistanceThreshold"),
		TEXT("TeleportRotationThreshold"), TEXT("PlanarConstraint"), TEXT("SkelCompMoveScale"),
		TEXT("bUpdatePhysicsSettingsInGame"), TEXT("ResetBoneTransformWhenBoneNotFound"),
		TEXT("DampingCurveData"), TEXT("StiffnessCurveData"), TEXT("WorldDampingLocationCurveData"),
		TEXT("WorldDampingRotationCurveData"), TEXT("LimitAngleCurveData"),
		TEXT("BoneConstraintSubdivisionFeedbackScale"), TEXT("BoneConstraintGlobalComplianceType"),
		TEXT("BoneConstraintIterationCountBeforeCollision"), TEXT("BoneConstraintIterationCountAfterCollision"),
		TEXT("Gravity"), TEXT("bUseLegacyGravity"), TEXT("bUseDefaultGravityZProjectSetting"),
		TEXT("bUseWorldSpaceGravity"), TEXT("bEnableWind"), TEXT("WindScale"),
		TEXT("WindDirectionNoiseAngle"), TEXT("WindSampleMode"), TEXT("SimpleExternalForce"),
		TEXT("bUseWorldSpaceSimpleExternalForce"), TEXT("ExternalForces"), TEXT("CustomExternalForces"),
		TEXT("bAllowWorldCollision"), TEXT("SimpleWorldCollisionGatherInterval"),
		TEXT("SimpleWorldCollisionObjectTypes"), TEXT("SimpleWorldCollisionConvexFallbackShape"),
		TEXT("bOverrideSimpleWorldCollisionGatherRadius"), TEXT("SimpleWorldCollisionGatherRadius"),
		TEXT("bSimpleWorldCollisionGroundCollision"), TEXT("SimpleWorldCollisionSkeletalMeshCollision"),
		TEXT("bOverrideCollisionParams"), TEXT("CollisionChannelSettings"),
		TEXT("bIgnoreSelfComponent"), TEXT("IgnoreBones"), TEXT("IgnoreBoneNamePrefix")
	};
	bool bOk = true;
	for (TFieldIterator<FProperty> It(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(**It) != EKawaiiPhysicsPresetPropertyClass::CopyTarget)
		{
			continue;
		}
		const FName Name = It->GetFName();
		const bool bCovered = Direct.Contains(Name) ||
			UKawaiiPhysicsLibrary::DoesNodePropertyRequireModifyBonesReinit(Name) ||
			UKawaiiPhysicsLibrary::DoesNodePropertyRequireBoneReferencesReinit(Name) ||
			UKawaiiPhysicsLibrary::DoesNodePropertyRequireLimitAssetsReapply(Name) ||
			UKawaiiPhysicsLibrary::DoesNodePropertyRequireSharedCollisionReinit(Name) ||
			UKawaiiPhysicsLibrary::DoesNodePropertyRequireSimpleWorldCollisionReinit(Name) ||
			Name == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SyncBones);
		bOk &= TestTrue(FString::Printf(TEXT("CopyTarget classified: %s"), *Name.ToString()), bCovered);
	}
	return bOk;
}

#if WITH_EDITOR
// PR-11: 修正前は UFUNCTION 経由の同値適用でも再初期化要求が立つ。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsPresetRuntimeApplyReferenceTest,
	"KawaiiPhysics.Preset.RuntimeApplyReference", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKawaiiPhysicsPresetRuntimeApplyReferenceTest::RunTest(const FString& Parameters)
{
	KawaiiPhysicsAnimBlueprintFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(4);
	KawaiiPhysicsAnimBlueprintFixture::FFixture Fixture(Options);
	if (!TestTrue(TEXT("AnimBlueprint fixture is ready"), Fixture.IsReady()))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		if (!TestTrue(TEXT("component evaluates"), Fixture.StepFrame()))
		{
			return false;
		}
	}
	TArray<FKawaiiPhysicsReference> References = Fixture.GetNodes();
	if (!TestEqual(TEXT("one runtime node is found"), References.Num(), 1))
	{
		return false;
	}
	const auto Preset = MakePreset(References[0].GetAnimNode<FAnimNode_KawaiiPhysics>());
	bool bOk = true;
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		EKawaiiPhysicsAccessResult Result = EKawaiiPhysicsAccessResult::NotValid;
		UKawaiiPhysicsLibrary::ApplyPresetDataAsset(Result, References[0], Preset.Get(), {});
		bOk &= TestTrue(TEXT("UFUNCTION finds the node"), Result == EKawaiiPhysicsAccessResult::Valid);
		bOk &= TestFalse(TEXT("same preset leaves all requests clear"),
			FKawaiiPhysicsTestAccessor::HasPendingRuntimeApplyRequest(
				References[0].GetAnimNode<FAnimNode_KawaiiPhysics>()));
		bOk &= TestTrue(TEXT("component keeps evaluating"), Fixture.StepFrame());
	}
	return bOk;
}
#endif

}

#endif
