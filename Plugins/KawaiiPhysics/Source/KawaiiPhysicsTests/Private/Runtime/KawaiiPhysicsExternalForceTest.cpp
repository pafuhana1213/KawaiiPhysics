// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Curve.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"

namespace
{
constexpr float GExternalForceSpaceTestDt = 1.0f / 30.0f;
constexpr float GExternalForceSpaceTestTol = KINDA_SMALL_NUMBER;

// 本番の公開関数を増やさず、キャッシュと変換ヘルパーをテストから検証する
template <typename ForceType>
struct TKawaiiPhysicsExternalForceSpaceTestAccessor : ForceType
{
	TKawaiiPhysicsExternalForceSpaceTestAccessor()
	{
		this->bSupportsRandomForceScaleRange = false;
		this->RandomizedForceScale = 1.0f;
	}

	using ForceType::Force;
	using ForceType::ConvertExternalForceToSimulationSpace;
};

template <typename ForceType>
bool RunExternalForceSingleTransformTest(FAutomationTestBase& Test, ForceType& ExternalForce)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.BuildVerticalChain(2, 10.0f);
	Accessor.SetTimeState(GExternalForceSpaceTestDt, GExternalForceSpaceTestDt);
	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);
	const FTransform ComponentToWorld(FRotator(0.0f, 90.0f, 0.0f));
	// 実コンポーネントの代わりに評価キャッシュへ回転を注入し、Apply の BoneTM は合成する
	Accessor.SetWorldSpaceTransformForTest(ComponentToWorld);
	bool bOk = true;
	for (const EKawaiiPhysicsSimulationSpace SimSpace :
		{EKawaiiPhysicsSimulationSpace::ComponentSpace, EKawaiiPhysicsSimulationSpace::WorldSpace})
	{
		Accessor.SetSimulationSpace(SimSpace);
		ExternalForce.ExternalForceSpace = EExternalForceSpace::BoneSpace;
		ExternalForce.PreApply(Accessor.Node, PoseContext);
		bOk &= Test.TestTrue(TEXT("BoneSpace cache remains local"),
			ExternalForce.Force.Equals(FVector::ForwardVector, GExternalForceSpaceTestTol));
		// スケールを含む TransformVector の大きさも維持する
		for (const float Scale : {1.0f, 2.0f})
		{
			FTransform BoneTM = ComponentToWorld;
			BoneTM.SetScale3D(FVector(Scale, 1.0f, 1.0f));
			const FVector InitialLocation = Accessor.Bone(1).Location;
			ExternalForce.Apply(Accessor.Bone(1), Accessor.Node, PoseContext, BoneTM);
			const FVector Displacement = Accessor.Bone(1).Location - InitialLocation;
			bOk &= Test.TestTrue(TEXT("BoneSpace displacement is positive Y with one scaled transform"),
				Displacement.Equals(FVector::RightVector * Scale * GExternalForceSpaceTestDt,
					GExternalForceSpaceTestTol));
#if ENABLE_ANIM_DEBUG
			const FVector* DebugForce = ExternalForce.BoneForceMap.Find(Accessor.Bone(1).BoneRef.BoneName);
			bOk &= Test.TestTrue(TEXT("Debug map stores the transformed force"),
				DebugForce && DebugForce->Equals(BoneTM.TransformVector(FVector::ForwardVector),
					GExternalForceSpaceTestTol));
#endif
		}

		ExternalForce.ExternalForceSpace = EExternalForceSpace::ComponentSpace;
		ExternalForce.PreApply(Accessor.Node, PoseContext);
		const FVector ExpectedForce = Accessor.Node.ConvertSimulationSpaceVector(PoseContext,
			EKawaiiPhysicsSimulationSpace::ComponentSpace, SimSpace, FVector::ForwardVector);
		bOk &= Test.TestTrue(TEXT("ComponentSpace cache keeps the existing conversion"),
			ExternalForce.Force.Equals(ExpectedForce, GExternalForceSpaceTestTol));
		const FVector InitialLocation = Accessor.Bone(1).Location;
		ExternalForce.Apply(Accessor.Bone(1), Accessor.Node, PoseContext, ComponentToWorld);
		bOk &= Test.TestTrue(TEXT("ComponentSpace displacement keeps the existing conversion"),
			(Accessor.Bone(1).Location - InitialLocation).Equals(ExpectedForce * GExternalForceSpaceTestDt,
				GExternalForceSpaceTestTol));
	}
	return bOk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsBasicBoneSpaceSingleTransformTest,
                                 "KawaiiPhysics.ExternalForce.BasicBoneSpaceSingleTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsBasicBoneSpaceSingleTransformTest::RunTest(const FString& Parameters)
{
	TKawaiiPhysicsExternalForceSpaceTestAccessor<FKawaiiPhysics_ExternalForce_Basic> Basic;
	Basic.ForceDir = FVector::ForwardVector;
	Basic.Interval = 0.0f;
	return RunExternalForceSingleTransformTest(*this, Basic);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsCurveBoneSpaceSingleTransformTest,
                                 "KawaiiPhysics.ExternalForce.CurveBoneSpaceSingleTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsCurveBoneSpaceSingleTransformTest::RunTest(const FString& Parameters)
{
	TKawaiiPhysicsExternalForceSpaceTestAccessor<FKawaiiPhysics_ExternalForce_Curve> Curve;
	Curve.CurveEvaluateType = EExternalForceCurveEvaluateType::Single;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		// 両端と外挿区間を含めて全時刻で (1, 0, 0) を返す定数カーブ
		FRichCurve* AxisCurve = Curve.ForceCurve.GetRichCurve(Axis);
		const float Value = Axis == 0 ? 1.0f : 0.0f;
		AxisCurve->AddKey(0.0f, Value);
		AxisCurve->AddKey(1.0f, Value);
	}
	Curve.InitMaxCurveTime();
	return RunExternalForceSingleTransformTest(*this, Curve);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsConvertToSimulationSpacePassesBoneSpaceThroughTest,
                                 "KawaiiPhysics.ExternalForce.ConvertToSimulationSpacePassesBoneSpaceThrough",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsConvertToSimulationSpacePassesBoneSpaceThroughTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);
	TKawaiiPhysicsExternalForceSpaceTestAccessor<FKawaiiPhysics_ExternalForce> ExternalForce;
	const FVector Input(2.0f, -3.0f, 4.0f);
	bool bOk = true;
	for (const FTransform& ComponentToWorld : {FTransform::Identity, FTransform(FRotator(0.0f, 90.0f, 0.0f))})
	{
		Accessor.SetWorldSpaceTransformForTest(ComponentToWorld);
		for (const EKawaiiPhysicsSimulationSpace SimSpace :
			{EKawaiiPhysicsSimulationSpace::ComponentSpace, EKawaiiPhysicsSimulationSpace::WorldSpace})
		{
			Accessor.SetSimulationSpace(SimSpace);
			ExternalForce.ExternalForceSpace = EExternalForceSpace::BoneSpace;
			bOk &= TestTrue(TEXT("BoneSpace helper returns the exact input"),
				ExternalForce.ConvertExternalForceToSimulationSpace(Accessor.Node, PoseContext, Input) == Input);
			for (const EExternalForceSpace ForceSpace :
				{EExternalForceSpace::ComponentSpace, EExternalForceSpace::WorldSpace})
			{
				ExternalForce.ExternalForceSpace = ForceSpace;
				const EKawaiiPhysicsSimulationSpace From = ForceSpace == EExternalForceSpace::WorldSpace
					? EKawaiiPhysicsSimulationSpace::WorldSpace : EKawaiiPhysicsSimulationSpace::ComponentSpace;
				const FVector Expected = Accessor.Node.ConvertSimulationSpaceVector(PoseContext, From, SimSpace, Input);
				bOk &= TestTrue(TEXT("WorldSpace and ComponentSpace keep the existing conversion"),
					ExternalForce.ConvertExternalForceToSimulationSpace(Accessor.Node, PoseContext, Input)
						.Equals(Expected, GExternalForceSpaceTestTol));
			}
		}
	}
	// StepFrame は呼ばず、BaseBoneSpace の同一空間契約も直接確認する
	Accessor.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::BaseBoneSpace);
	ExternalForce.ExternalForceSpace = EExternalForceSpace::BoneSpace;
	bOk &= TestTrue(TEXT("BoneSpace remains unchanged in BaseBoneSpace simulation"),
		ExternalForce.ConvertExternalForceToSimulationSpace(Accessor.Node, PoseContext, Input) == Input);
	return bOk;
}

#endif
