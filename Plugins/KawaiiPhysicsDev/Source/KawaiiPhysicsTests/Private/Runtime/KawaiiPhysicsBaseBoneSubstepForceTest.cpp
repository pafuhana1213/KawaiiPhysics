// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsNodeFixture.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Curve.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Gravity.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"
#include "Misc/AutomationTest.h"
#include <type_traits>
#include <new>

namespace KawaiiPhysicsBaseBoneSubstepForceTestPrivate
{

enum class EForceCase : uint8
{
	None,
	Gravity,
	ComponentGravity,
	SimpleWorld,
	BasicWorld,
	BasicComponent,
	GravityExternal,
	NodeWind,
	CurveWorld,
	WindExternal,
	ProceduralWorld,
	TransientBasicWorld,
};

const EForceCase ForceCases[] = {
	EForceCase::Gravity, EForceCase::ComponentGravity, EForceCase::SimpleWorld,
	EForceCase::BasicWorld, EForceCase::BasicComponent, EForceCase::GravityExternal,
	EForceCase::NodeWind, EForceCase::CurveWorld, EForceCase::WindExternal,
	EForceCase::ProceduralWorld, EForceCase::TransientBasicWorld
};

void ConfigureForce(FAnimNode_KawaiiPhysics& Node, const EForceCase ForceCase,
	const FVector& WorldDirection, const float Acceleration, const float OffsetSpeed)
{
	Node.Gravity = FVector::ZeroVector;
	Node.bUseDefaultGravityZProjectSetting = false;
	Node.bUseWorldSpaceGravity = true;
	Node.SimpleExternalForce = FVector::ZeroVector;
	Node.bUseWorldSpaceSimpleExternalForce = true;
	Node.bEnableWind = ForceCase == EForceCase::NodeWind;
	Node.WindScale = 1.0f;
	Node.WindDirectionNoiseAngle = 0.0f;
	if (ForceCase == EForceCase::Gravity || ForceCase == EForceCase::ComponentGravity)
	{
		Node.bUseWorldSpaceGravity = ForceCase == EForceCase::Gravity;
		Node.Gravity = (Node.bUseWorldSpaceGravity ? WorldDirection : FVector::XAxisVector) * Acceleration;
	}
	else if (ForceCase == EForceCase::SimpleWorld)
	{
		Node.SimpleExternalForce = WorldDirection * OffsetSpeed;
	}
	else if (ForceCase == EForceCase::BasicWorld || ForceCase == EForceCase::BasicComponent)
	{
		auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Basic>(Node);
		Force.ExternalForceSpace = ForceCase == EForceCase::BasicWorld
			? EExternalForceSpace::WorldSpace : EExternalForceSpace::ComponentSpace;
		Force.ForceDir = (ForceCase == EForceCase::BasicWorld ? WorldDirection : FVector::XAxisVector) * OffsetSpeed;
		Force.Interval = 0.0f;
	}
	else if (ForceCase == EForceCase::GravityExternal)
	{
		auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Gravity>(Node);
		Force.bUseOverrideGravityDirection = true;
		Force.OverrideGravityDirection = WorldDirection;
		Force.RandomForceScaleRange = FFloatInterval(Acceleration, Acceleration);
	}
	else if (ForceCase == EForceCase::CurveWorld)
	{
		auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Curve>(Node);
		Force.ExternalForceSpace = EExternalForceSpace::WorldSpace;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Force.ForceCurve.GetRichCurve(Axis)->AddKey(0.0f, WorldDirection[Axis] * OffsetSpeed);
			Force.ForceCurve.GetRichCurve(Axis)->AddKey(2.0f, WorldDirection[Axis] * OffsetSpeed);
		}
	}
	else if (ForceCase == EForceCase::WindExternal)
	{
		auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Wind>(Node);
		Force.WindDirectionNoiseAngle = 0.0f;
	}
	else if (ForceCase == EForceCase::ProceduralWorld)
	{
		auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_ProceduralWind>(Node);
		Force.ExternalForceSpace = EExternalForceSpace::WorldSpace;
		Force.WindDirection = WorldDirection;
		Force.WindDirectionNoiseAngle = 0.0f;
		Force.ConstantForce = OffsetSpeed;
		Force.SwayForce = 0.0f;
		Force.RippleForce = 0.0f;
		Force.RandomForce = 0.0f;
		Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	}
}

KawaiiPhysicsNodeFixture::FOptions MakeOptions(const EForceCase ForceCase, const int32 Rate,
	const EKawaiiPhysicsSimulationSpace Space, const bool bRotateWorld = false,
	const float Acceleration = 980.0f, const float OffsetSpeed = 20.0f)
{
	KawaiiPhysicsNodeFixture::FOptions Options;
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(5);
	Options.FrameDt = 1.0f / Rate;
	Options.SimulationSpace = Space;
	Options.BaseBoneName = TEXT("chain_1");
	Options.ConfigureDeveloperSettings = [](UKawaiiPhysicsDeveloperSettings& Settings)
	{
		Settings.bUseFixedSubstepping = true;
		Settings.MaxSubsteps = 4;
	};
	Options.ConfigureNode = [=](FAnimNode_KawaiiPhysics& Node)
	{
		Node.RootBone.BoneName = TEXT("root");
		Node.DummyBoneLength = 0.0f;
		Node.WarmUpFrames = 0;
		Node.TargetFramerate = 60;
		Node.bAllowWorldCollision = false;
		Node.bUseSimpleWorldCollision = false;
		ConfigureForce(Node, ForceCase, bRotateWorld ? FVector::YAxisVector : FVector::XAxisVector,
			Acceleration, OffsetSpeed);
	};
	Options.SetInputPose = [Rate](FCompactPose& Pose, const int32 Frame)
	{
		const float Angle = 180.0f * (Frame + 1) / Rate;
		Pose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Angle)));
		Pose[FCompactPoseBoneIndex(2)].SetRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(30.0f)));
	};
	return Options;
}

bool AddTransientBasic(FAutomationTestBase& Test, FAnimNode_KawaiiPhysics& Node,
	const FVector& Direction, const float OffsetSpeed)
{
	FInstancedStruct Force = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Basic>();
	Force.GetMutable<FKawaiiPhysics_ExternalForce_Basic>().ForceDir = Direction * OffsetSpeed;
	return Test.TestTrue(TEXT("一時外力を要求できる"), Node.RequestTransientExternalForce(MoveTemp(Force), 2.0f) > 0);
}

TArray<FVector> RunCase(FAutomationTestBase& Test, const EForceCase ForceCase,
	const int32 Rate, const EKawaiiPhysicsSimulationSpace Space, const bool bRotateWorld = false)
{
	KawaiiPhysicsNodeFixture::FFixture Fixture(MakeOptions(ForceCase, Rate, Space, bRotateWorld));
	const FVector WorldDirection = bRotateWorld ? FVector::YAxisVector : FVector::XAxisVector;
	if (bRotateWorld)
	{
		// Component とワールドの力をともに Yaw 90° 回す。Component 内では元の +X の物理と等価。
		Fixture.Component->SetWorldTransform(FTransform(FRotator(0.0f, 90.0f, 0.0f)));
		Fixture.Proxy->PreUpdate(Fixture.Anim, Fixture.FrameDt);
		Fixture.Node.PreUpdate(Fixture.Anim);
	}
	if (ForceCase == EForceCase::TransientBasicWorld && !AddTransientBasic(Test, Fixture.Node, WorldDirection, 20.0f))
	{
		return {};
	}
	TArray<FVector> Samples;
	for (int32 Frame = 0; Frame < Rate; ++Frame)
	{
		Fixture.Accessor.SetWindCenterSample(WorldDirection, ForceCase == EForceCase::WindExternal ? 20.0f : 20.0f / 60.0f);
		// ノード風の突風倍率も全セル・全レートで同一にし、変換以外の乱数差を除く。
		++GFrameCounter;
		FMath::RandInit(1000);
		Fixture.EvaluateFrame();
		if (!Test.TestEqual(TEXT("実ボーンが五本ある"), Fixture.Node.ModifyBones.Num(), 5))
		{
			return {};
		}
		if ((Frame + 1) % (Rate / 30) == 0)
		{
			Samples.Add(Fixture.Node.ModifyBones.Last().Location);
		}
	}
	return Samples;
}

double ComputeP95Distance(const TArray<FVector>& First, const TArray<FVector>& Second)
{
	TArray<double> Distances;
	for (int32 Index = 0; Index < FMath::Min(First.Num(), Second.Num()); ++Index)
	{
		Distances.Add(FVector::Dist(First[Index], Second[Index]));
	}
	Distances.Sort();
	return Distances.IsEmpty() ? 0.0 : Distances[FMath::Min(28, Distances.Num() - 1)];
}

// FX-24・FX-U4 (b)。修正前は重力の 30/60fps が 0.82cm > 0.664cm となる主張が落ちる。
// 他のワールド外力も同じ基準で全 fps 組を比較する。FX-U4 (a) の Component 対照は世界全体を 90° 回した別入力で作る。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneWorldForcesAcrossRatesTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneWorldForcesAcrossRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneWorldForcesAcrossRatesTest::RunTest(const FString& Parameters)
{
	const int32 Rates[] = {30, 60, 120};
	TArray<FVector> NoForce[2][3];
	for (int32 SpaceIndex = 0; SpaceIndex < 2; ++SpaceIndex)
	{
		for (int32 RateIndex = 0; RateIndex < 3; ++RateIndex)
		{
			NoForce[SpaceIndex][RateIndex] = RunCase(*this, EForceCase::None, Rates[RateIndex],
				SpaceIndex == 0 ? EKawaiiPhysicsSimulationSpace::ComponentSpace : EKawaiiPhysicsSimulationSpace::BaseBoneSpace);
		}
	}
	bool bOk = true;
	for (const EForceCase ForceCase : ForceCases)
	{
		TArray<FVector> Locations[2][3];
		for (int32 RateIndex = 0; RateIndex < 3; ++RateIndex)
		{
			const int32 Rate = Rates[RateIndex];
			for (int32 SpaceIndex = 0; SpaceIndex < 2; ++SpaceIndex)
			{
				Locations[SpaceIndex][RateIndex] = RunCase(*this, ForceCase, Rate,
					SpaceIndex == 0 ? EKawaiiPhysicsSimulationSpace::ComponentSpace : EKawaiiPhysicsSimulationSpace::BaseBoneSpace);
				const auto& Samples = Locations[SpaceIndex][RateIndex];
				const auto& Control = NoForce[SpaceIndex][RateIndex];
				if (!TestEqual(TEXT("力ありの 30 サンプル"), Samples.Num(), 30) ||
					!TestEqual(TEXT("力なしの 30 サンプル"), Control.Num(), 30)) return false;
				double MaxForceEffect = 0.0;
				for (int32 Index = 0; Index < Samples.Num(); ++Index)
				{
					MaxForceEffect = FMath::Max(MaxForceEffect, FVector::Dist(Samples[Index], Control[Index]));
				}
				bOk &= TestTrue(FString::Printf(TEXT("外力 %d 空間 %d %dfps の力が効く"),
					static_cast<int32>(ForceCase), SpaceIndex, Rate), MaxForceEffect > 0.1);
			}
			const TArray<FVector> RotatedWorld = RunCase(*this, ForceCase, Rate,
				EKawaiiPhysicsSimulationSpace::ComponentSpace, true);
			if (!TestEqual(TEXT("回した Component 対照の 30 サンプル"), RotatedWorld.Num(), 30)) return false;
			for (int32 Index = 0; Index < 30; ++Index)
			{
				// 同じ物理を別のワールドの向きで作るため、FX-U4 (a) の位置許容値 0.01cm。
				bOk &= TestTrue(TEXT("Component 出力は世界全体の回転で変わらない"),
					Locations[0][RateIndex][Index].Equals(RotatedWorld[Index], 0.01));
			}
		}
		for (int32 First = 0; First < 3; ++First)
		{
			for (int32 Second = First + 1; Second < 3; ++Second)
			{
				const double BaseP95 = ComputeP95Distance(Locations[1][First], Locations[1][Second]);
				const double ComponentP95 = ComputeP95Distance(Locations[0][First], Locations[0][Second]);
				// 別 fps の積分・入力補間の差は対照で正規化し、丸めの床に 0.001cm を許す（FX-U4 (b)）。
				bOk &= TestTrue(FString::Printf(TEXT("外力 %d %d/%dfps: Base %.4f <= Component %.4f × 1.5 + 0.001"),
					static_cast<int32>(ForceCase), Rates[First], Rates[Second], BaseP95, ComponentP95),
					BaseP95 <= ComponentP95 * 1.5 + 0.001);
			}
		}
	}
	return bOk;
}

// FX-24・FX-U4 (a)。45° と 90° の二ステップを解析的に作る。修正前は全外力が終端の -Y だけへ進み、
// 最初のステップの +X 寄与（約 0.7cm）が消えて 0.01cm の主張が落ちる。Gravity 外力の速度経路も含む。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneWorldForceStepDirectionsTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneWorldForceStepDirections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneWorldForceStepDirectionsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	FMath::RandInit(1000);
	const float GustFactor = FMath::FRandRange(0.0f, 2.0f);
	if (!TestTrue(TEXT("固定した突風倍率が非零"), GustFactor > KINDA_SMALL_NUMBER)) return false;
	const double HalfSqrt = FMath::Sqrt(0.5);
	FVector Expected(0.0, 0.0, -10.0);
	// Damping=1、Stiffness=0。各ステップで長さ 10cm の制約だけを戻す。
	Expected = (Expected + FVector(HalfSqrt, -HalfSqrt, 0.0)).GetSafeNormal() * 10.0;
	Expected = (Expected + FVector(0.0, -1.0, 0.0)).GetSafeNormal() * 10.0;
	for (const EForceCase ForceCase : ForceCases)
	{
		auto Options = MakeOptions(ForceCase, 30, EKawaiiPhysicsSimulationSpace::BaseBoneSpace, false, 3600.0f, 60.0f);
		Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
		Options.BaseBoneName = TEXT("root");
		const auto Configure = Options.ConfigureNode;
		Options.ConfigureNode = [Configure](FAnimNode_KawaiiPhysics& Node)
		{
			Configure(Node);
			Node.PhysicsSettings.Damping = 1.0f;
			Node.PhysicsSettings.Stiffness = 0.0f;
			Node.PhysicsSettings.WorldDampingLocation = 1.0f;
			Node.PhysicsSettings.WorldDampingRotation = 1.0f;
		};
		Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
		{
			Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.0f * Frame)));
		};
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
		const FVector Gravity = Fixture.Node.Gravity;
		const FVector SimpleForce = Fixture.Node.SimpleExternalForce;
		Fixture.Node.Gravity = FVector::ZeroVector;
		Fixture.Node.SimpleExternalForce = FVector::ZeroVector;
		Fixture.Node.bEnableWind = false;
		for (auto& Force : Fixture.Node.ExternalForces)
		{
			Force.GetMutablePtr<FKawaiiPhysics_ExternalForce>()->bIsEnabled = false;
		}
		Fixture.EvaluateFrame();
		Fixture.Node.Gravity = Gravity;
		Fixture.Node.SimpleExternalForce = SimpleForce;
		Fixture.Node.bEnableWind = ForceCase == EForceCase::NodeWind;
		for (auto& Force : Fixture.Node.ExternalForces)
		{
			Force.GetMutablePtr<FKawaiiPhysics_ExternalForce>()->bIsEnabled = true;
		}
		if (ForceCase == EForceCase::TransientBasicWorld)
		{
			bOk &= AddTransientBasic(*this, Fixture.Node, FVector::XAxisVector, 60.0f);
		}
		Fixture.Accessor.SetWindCenterSample(FVector::XAxisVector,
			ForceCase == EForceCase::NodeWind ? 1.0f / GustFactor : 60.0f);
		++GFrameCounter;
		FMath::RandInit(1000);
		Fixture.EvaluateFrame();
		if (!TestEqual(TEXT("解析セルの二本の実ボーン"), Fixture.Node.ModifyBones.Num(), 2)) return false;
		bOk &= TestTrue(FString::Printf(TEXT("外力 %d は各ステップの向きで作用する"), static_cast<int32>(ForceCase)),
			Fixture.Node.ModifyBones[1].Location.Equals(Expected, 0.01));
	}
	return bOk;
}

// FX-24・FX-U4 (a)。Component の -2cm の移動を -45° / -90° に分ける。
// 修正前は両方とも終端で変換し、期待する -X 寄与（約 0.7cm）が無くなるので 0.01cm の主張が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneComponentTranslationTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneComponentTranslation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneComponentTranslationTest::RunTest(const FString& Parameters)
{
	auto Options = MakeOptions(EForceCase::None, 30, EKawaiiPhysicsSimulationSpace::BaseBoneSpace);
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	Options.BaseBoneName = TEXT("root");
	const auto Configure = Options.ConfigureNode;
	Options.ConfigureNode = [Configure](FAnimNode_KawaiiPhysics& Node)
	{
		Configure(Node);
		Node.PhysicsSettings.Damping = 1.0f;
		Node.PhysicsSettings.Stiffness = 0.0f;
		Node.PhysicsSettings.WorldDampingLocation = 0.0f;
		Node.PhysicsSettings.WorldDampingRotation = 1.0f;
	};
	Options.GetComponentTransform = [](const int32 Frame) { return FTransform(FVector(2.0 * Frame, 0.0, 0.0)); };
	Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
	{
		Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.0f * Frame)));
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	Fixture.EvaluateFrame();
	Fixture.EvaluateFrame();
	if (!TestEqual(TEXT("移動セルの二本の実ボーン"), Fixture.Node.ModifyBones.Num(), 2)) return false;
	const double HalfSqrt = FMath::Sqrt(0.5);
	FVector Expected(0.0, 0.0, -10.0);
	Expected = (Expected + FVector(-HalfSqrt, HalfSqrt, 0.0)).GetSafeNormal() * 10.0;
	Expected = (Expected + FVector(0.0, 1.0, 0.0)).GetSafeNormal() * 10.0;
	return TestTrue(TEXT("Component 移動も各ステップの基準ボーンで変換する"),
		Fixture.Node.ModifyBones[1].Location.Equals(Expected, 0.01));
}

// TW-WS-15/16・FX-U4 (a)。BoneSpace Basic と、三角関数で一回だけ回したローカル Simple Force を比較する。
// 四構成・全レートで従来の単一 BoneTM を守る。修正前も通り、StepCorrection の誤適用や BoneTM の二重適用で落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneBoneSpaceForceOnceTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneBoneSpaceForceOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneBoneSpaceForceOnceTest::RunTest(const FString& Parameters)
{
	const EKawaiiPhysicsSimulationSpace Spaces[] = {
		EKawaiiPhysicsSimulationSpace::ComponentSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		EKawaiiPhysicsSimulationSpace::BaseBoneSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace
	};
	bool bOk = true;
	for (int32 SpaceIndex = 0; SpaceIndex < 4; ++SpaceIndex)
	{
		for (const int32 Rate : {30, 60, 120})
		{
			auto Options = MakeOptions(EForceCase::None, Rate, Spaces[SpaceIndex]);
			Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
			Options.BaseBoneName = SpaceIndex >= 2 ? FName(TEXT("root")) : NAME_None;
			Options.GetComponentTransform = [](const int32 Frame) { return FTransform(FRotator(0.0f, 90.0f, 0.0f)); };
			const auto Configure = Options.ConfigureNode;
			Options.ConfigureNode = [Configure](FAnimNode_KawaiiPhysics& Node)
			{
				Configure(Node);
				Node.PhysicsSettings.WorldDampingLocation = 1.0f;
				Node.PhysicsSettings.WorldDampingRotation = 1.0f;
				Node.bUseWorldSpaceSimpleExternalForce = false;
			};
			Options.SetInputPose = [Rate](FCompactPose& Pose, const int32 Frame)
			{
				Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector,
					FMath::DegreesToRadians(180.0f * Frame / Rate)));
				Pose[FCompactPoseBoneIndex(1)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0f)));
			};
			auto BoneOptions = Options;
			BoneOptions.ConfigureNode = [Configure = Options.ConfigureNode](FAnimNode_KawaiiPhysics& Node)
			{
				Configure(Node);
				auto& Force = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Basic>(Node);
				Force.ExternalForceSpace = EExternalForceSpace::BoneSpace;
				Force.ForceDir = FVector(20.0, 0.0, 0.0);
			};
			KawaiiPhysicsNodeFixture::FFixture BoneForce(BoneOptions);
			KawaiiPhysicsNodeFixture::FFixture LocalForce(Options);
			KawaiiPhysicsNodeFixture::FFixture NoForce(Options);
			double MaxForceEffect = 0.0;
			for (int32 Frame = 0; Frame < Rate; ++Frame)
			{
				const double Degrees = 30.0 + (Spaces[SpaceIndex] == EKawaiiPhysicsSimulationSpace::BaseBoneSpace
					? 0.0 : 180.0 * Frame / Rate) + (Spaces[SpaceIndex] == EKawaiiPhysicsSimulationSpace::WorldSpace ? 90.0 : 0.0);
				const double Radians = FMath::DegreesToRadians(Degrees);
				LocalForce.Node.SimpleExternalForce = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0) * 20.0;
				BoneForce.EvaluateFrame();
				LocalForce.EvaluateFrame();
				NoForce.EvaluateFrame();
				if (BoneForce.Node.ModifyBones.Num() != 2 || LocalForce.Node.ModifyBones.Num() != 2 || NoForce.Node.ModifyBones.Num() != 2)
				{
					AddError(TEXT("BoneSpace セルの実ボーンが二本ではない"));
					return false;
				}
				const FVector Actual = BoneForce.Node.ModifyBones[1].Location;
				bOk &= TestTrue(FString::Printf(TEXT("構成 %d %dfps フレーム %d の BoneTM は一回"), SpaceIndex, Rate, Frame),
					Actual.Equals(LocalForce.Node.ModifyBones[1].Location, 0.01));
				MaxForceEffect = FMath::Max(MaxForceEffect, FVector::Dist(Actual, NoForce.Node.ModifyBones[1].Location));
			}
			bOk &= TestTrue(TEXT("BoneSpace の力が効く"), MaxForceEffect > 0.1);
		}
	}
	return bOk;
}

// 前後の外力を別々に呼び、ノードがコールバックから戻った後の値を観測する。
// コールバック自身の中だけの検査では、戻った直後の差分回転を検出できない。
struct FForceTrace
{
	TArray<FVector> BeforeVelocity;
	TArray<FVector> AfterVelocity;
	TArray<FVector> BeforeLocation;
	TArray<FVector> AfterLocation;
	TArray<FVector> BeforePrevLocation;
	TArray<FVector> AfterPrevLocation;
	TArray<bool> StepTransforms;
	bool bInjectVelocity = false;
	int32 PreApplyCount = 0;
};

thread_local FForceTrace* ActiveForceTrace = nullptr;

struct FBeforeForceProbe : FKawaiiPhysics_ExternalForce
{
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, FVector& Velocity) override
	{
		if (ActiveForceTrace->bInjectVelocity) Velocity += FVector(60.0, 30.0, 0.0);
		ActiveForceTrace->BeforeVelocity.Add(Velocity);
		ActiveForceTrace->StepTransforms.Add(Node.IsUsingBaseBoneStepTransforms());
	}
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM) override
	{
		ActiveForceTrace->BeforeLocation.Add(Bone.Location);
		ActiveForceTrace->BeforePrevLocation.Add(Bone.PrevLocation);
	}
};

struct FAfterForceProbe : FKawaiiPhysics_ExternalForce
{
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, FVector& Velocity) override
	{
		ActiveForceTrace->AfterVelocity.Add(Velocity);
	}
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM) override
	{
		ActiveForceTrace->AfterLocation.Add(Bone.Location);
		ActiveForceTrace->AfterPrevLocation.Add(Bone.PrevLocation);
	}
};

struct FCallbackForce : FKawaiiPhysics_ExternalForce
{
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
	{
		FKawaiiPhysics_ExternalForce::PreApply(Node, PoseContext);
		++ActiveForceTrace->PreApplyCount;
	}
};

struct FVelocityDampingForce : FCallbackForce
{
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, FVector& Velocity) override
	{
		Velocity *= 0.5;
	}
};

struct FVelocityZeroForce : FCallbackForce
{
	virtual void ApplyToVelocity(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, FVector& Velocity) override
	{
		Velocity = FVector::ZeroVector;
	}
};

struct FLocationAndPrevLocationForce : FCallbackForce
{
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
		FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM) override
	{
		const FVector Offset(0.1, 0.2, 0.0);
		Bone.Location += Offset;
		Bone.PrevLocation += Offset;
	}
};

// この .cpp だけの状態を持たない派生型を、基底と同じサイズの保存領域に構築する。
// 配列の配置を先に確定し、構築後はコピーしない。破棄は基底の仮想デストラクタを通る。
// 反射用ヘッダや本番のテスト専用フックを追加せず、実際の仮想コールバック経路を通す。
template <typename TForce, typename TBaseForce = FKawaiiPhysics_ExternalForce>
void InitializePlainForce(FInstancedStruct& Slot)
{
	static_assert(sizeof(TForce) == sizeof(TBaseForce));
	static_assert(alignof(TForce) == alignof(TBaseForce));
	TBaseForce* Storage = Slot.GetMutablePtr<TBaseForce>();
	Storage->~TBaseForce();
	new (Storage) TForce();
}

KawaiiPhysicsNodeFixture::FOptions MakeAnalyticalOptions(const int32 Rate)
{
	auto Options = MakeOptions(EForceCase::None, Rate, EKawaiiPhysicsSimulationSpace::BaseBoneSpace);
	Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(2);
	Options.BaseBoneName = TEXT("root");
	const auto Configure = Options.ConfigureNode;
	Options.ConfigureNode = [Configure](FAnimNode_KawaiiPhysics& Node)
	{
		Configure(Node);
		Node.PhysicsSettings.Damping = 1.0f;
		Node.PhysicsSettings.Stiffness = 0.0f;
		Node.PhysicsSettings.WorldDampingLocation = 1.0f;
		Node.PhysicsSettings.WorldDampingRotation = 1.0f;
	};
	return Options;
}

// F1・FX-24。修正前の差分回転は、減衰・ゼロ置換の後の速度の式と向き、Location/PrevLocation の同一移動を壊す。
// 30fps の回転する二ステップと、60fps の変換を使わない一ステップの両方で独立した式を検査する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneDerivedForceStateTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneDerivedForceState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneDerivedForceStateTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	for (const int32 Rate : {30, 60})
	{
		for (int32 CallbackIndex = 0; CallbackIndex < 3; ++CallbackIndex)
		{
			auto Options = MakeAnalyticalOptions(Rate);
			Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
			{
				Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector,
					FMath::DegreesToRadians(90.0f * Frame)));
			};
			KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
			Fixture.EvaluateFrame();
			FForceTrace Trace;
			Trace.bInjectVelocity = true;
			TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
			Fixture.Node.ExternalForces.Reserve(3);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
			}
			InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
			if (CallbackIndex == 0) InitializePlainForce<FVelocityDampingForce>(Fixture.Node.ExternalForces[1]);
			if (CallbackIndex == 1) InitializePlainForce<FVelocityZeroForce>(Fixture.Node.ExternalForces[1]);
			if (CallbackIndex == 2) InitializePlainForce<FLocationAndPrevLocationForce>(Fixture.Node.ExternalForces[1]);
			InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces[2]);
			Fixture.EvaluateFrame();
			const int32 NumSteps = 60 / Rate;
			if (!TestEqual(TEXT("速度の前後を全ステップ観測"), Trace.BeforeVelocity.Num(), NumSteps) ||
				!TestEqual(TEXT("速度コールバックから戻った値を全ステップ観測"), Trace.AfterVelocity.Num(), NumSteps) ||
				!TestEqual(TEXT("位置の前後を全ステップ観測"), Trace.BeforeLocation.Num(), NumSteps) ||
				!TestEqual(TEXT("位置コールバックから戻った値を全ステップ観測"), Trace.AfterLocation.Num(), NumSteps)) return false;
			bOk &= TestEqual(TEXT("PreApply はフレームに一回"), Trace.PreApplyCount, 1);
			for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
			{
				bOk &= TestEqual(TEXT("二ステップ時だけ変換を使う"), Trace.StepTransforms[StepIndex], Rate == 30);
				const double Multiplier = CallbackIndex == 0 ? 0.5 : (CallbackIndex == 1 ? 0.0 : 1.0);
				// 速度・位置の四則演算だけの比較なので、丸め分の 1e-6 を許す。
				bOk &= TestTrue(TEXT("速度は減衰・ゼロ置換・無変更の式を保ち、横方向を生まない"),
					Trace.AfterVelocity[StepIndex].Equals(Trace.BeforeVelocity[StepIndex] * Multiplier, 1.e-6));
				const FVector ExpectedOffset = CallbackIndex == 2 ? FVector(0.1, 0.2, 0.0) : FVector::ZeroVector;
				bOk &= TestTrue(TEXT("Location はコールバックの移動量を保つ"),
					(Trace.AfterLocation[StepIndex] - Trace.BeforeLocation[StepIndex]).Equals(ExpectedOffset, 1.e-6));
				bOk &= TestTrue(TEXT("PrevLocation も同じ移動量を保つ"),
					(Trace.AfterPrevLocation[StepIndex] - Trace.BeforePrevLocation[StepIndex]).Equals(ExpectedOffset, 1.e-6));
			}
		}
	}
	return bOk;
}

// F2・FX-24・FX-U4。修正前の回転だけの補正では、最初のステップが (2/3,1,0) でなく (1/2,1,0) となる。
// ノード重力と各組み込み外力をコールバックの前後で観測し、非一様・負スケールも解析値と照合する。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneForceStepScalesTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneForceStepScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneForceStepScalesTest::RunTest(const FString& Parameters)
{
	struct FScaleCase
	{
		FVector Start;
		FVector End;
		FVector Expected[2];
	};
	const FScaleCase ScaleCases[] = {
		{FVector(1, 1, 1), FVector(2, 1, 1), {FVector(2.0 / 3.0, 1, 0), FVector(0.5, 1, 0)}},
		{FVector(1, 2, 1), FVector(2, 4, 1), {FVector(2.0 / 3.0, 1.0 / 3.0, 0), FVector(0.5, 0.25, 0)}},
		{FVector(-1, 1, 1), FVector(-2, 3, 1), {FVector(-2.0 / 3.0, 0.5, 0), FVector(-0.5, 1.0 / 3.0, 0)}}
	};
	const EForceCase ScaleForceCases[] = {EForceCase::Gravity, EForceCase::GravityExternal,
		EForceCase::BasicWorld, EForceCase::BasicComponent, EForceCase::CurveWorld,
		EForceCase::WindExternal, EForceCase::ProceduralWorld};
	bool bOk = true;
	for (const FScaleCase& ScaleCase : ScaleCases)
	{
		for (const EForceCase ForceCase : ScaleForceCases)
		{
			auto Options = MakeAnalyticalOptions(30);
			Options.SetInputPose = [ScaleCase](FCompactPose& Pose, const int32 Frame)
			{
				Pose[FCompactPoseBoneIndex(0)].SetScale3D(Frame == 0 ? ScaleCase.Start : ScaleCase.End);
			};
			KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
			Fixture.EvaluateFrame();
			// 加速度 (1,1,0) と、同じステップ変位になる速度 (1,1,0)/60 を別経路で作る。
			const float Magnitude = FMath::Sqrt(2.0f);
			ConfigureForce(Fixture.Node, ForceCase, FVector(1, 1, 0).GetSafeNormal(), Magnitude, Magnitude / 60.0f);
			if (ForceCase == EForceCase::Gravity) Fixture.Node.Gravity = FVector(1, 1, 0);
			if (ForceCase == EForceCase::BasicComponent)
			{
				Fixture.Node.ExternalForces[0].GetMutable<FKawaiiPhysics_ExternalForce_Basic>().ForceDir = FVector(1, 1, 0) / 60.0;
			}
			Fixture.Accessor.SetWindCenterSample(FVector(1, 1, 0), 1.0f / 60.0f);
			Fixture.Node.ExternalForces.Reserve(3);
			Fixture.Node.ExternalForces.Insert(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>(), 0);
			Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
			InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
			InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces.Last());
			FForceTrace Trace;
			TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
			Fixture.EvaluateFrame();
			if (!TestEqual(TEXT("スケールの二ステップを観測"), Trace.BeforeVelocity.Num(), 2) ||
				!TestEqual(TEXT("外力後の二ステップを観測"), Trace.AfterVelocity.Num(), 2) ||
				!TestEqual(TEXT("位置外力前の二ステップを観測"), Trace.BeforeLocation.Num(), 2) ||
				!TestEqual(TEXT("位置外力後の二ステップを観測"), Trace.AfterLocation.Num(), 2)) return false;
			for (int32 StepIndex = 0; StepIndex < 2; ++StepIndex)
			{
				FVector ActualForce;
				if (ForceCase == EForceCase::Gravity)
				{
					ActualForce = Trace.BeforeVelocity[StepIndex] * 60.0;
				}
				else if (ForceCase == EForceCase::GravityExternal)
				{
					ActualForce = (Trace.AfterVelocity[StepIndex] - Trace.BeforeVelocity[StepIndex]) * 60.0;
				}
				else
				{
					ActualForce = (Trace.AfterLocation[StepIndex] - Trace.BeforeLocation[StepIndex]) * 3600.0;
				}
				// float の dt・強度の正規化と、変位からの逆算の丸めを合わせて 1e-5 の力を許す。
				bOk &= TestTrue(FString::Printf(TEXT("外力 %d ステップ %d は補間スケールの解析値"),
					static_cast<int32>(ForceCase), StepIndex), ActualForce.Equals(ScaleCase.Expected[StepIndex], 1.e-5));
				bOk &= TestTrue(TEXT("スケール観測はステップ変換中"), Trace.StepTransforms[StepIndex]);
			}
		}
	}
	return bOk;
}

template <typename TBuiltInForce, bool bCallSuper>
struct FOverrideBuiltInForce : TBuiltInForce
{
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
	{
		if constexpr (bCallSuper) TBuiltInForce::PreApply(Node, PoseContext);
		this->Force = bCallSuper ? FVector::ZeroVector : FVector(6.0, -3.0, 0.0);
		++ActiveForceTrace->PreApplyCount;
	}
};

template <bool bCallSuper>
struct FOverrideProceduralWindForce : FKawaiiPhysics_ExternalForce_ProceduralWind
{
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
	{
		if constexpr (bCallSuper) FKawaiiPhysics_ExternalForce_ProceduralWind::PreApply(Node, PoseContext);
		RuntimeState->CachedWindVector = bCallSuper ? FVector::ZeroVector : FVector(6.0, -3.0, 0.0);
		if constexpr (!bCallSuper)
		{
			RuntimeState->CachedSinesWithoutRipple = 1.0f;
			RuntimeState->CachedStrengthCycle = 1.0f;
		}
		++ActiveForceTrace->PreApplyCount;
	}
};

template <typename TBuiltInForce>
bool CheckBuiltInForceOverride(FAutomationTestBase& Test)
{
	bool bOk = true;
	for (const bool bCallSuper : {true, false})
	{
		auto Options = MakeAnalyticalOptions(30);
		Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
		{
			Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector,
				FMath::DegreesToRadians(90.0f * Frame)));
		};
		KawaiiPhysicsNodeFixture::FFixture NoForce(Options);
		KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
		NoForce.EvaluateFrame();
		Fixture.EvaluateFrame();
		Fixture.Node.ExternalForces.Reserve(3);
		Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
		Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<TBuiltInForce>());
		Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
		InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
		InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces[2]);
		if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_ProceduralWind>)
		{
			if (bCallSuper) InitializePlainForce<FOverrideProceduralWindForce<true>, TBuiltInForce>(Fixture.Node.ExternalForces[1]);
			else InitializePlainForce<FOverrideProceduralWindForce<false>, TBuiltInForce>(Fixture.Node.ExternalForces[1]);
		}
		else
		{
			if (bCallSuper) InitializePlainForce<FOverrideBuiltInForce<TBuiltInForce, true>, TBuiltInForce>(Fixture.Node.ExternalForces[1]);
			else InitializePlainForce<FOverrideBuiltInForce<TBuiltInForce, false>, TBuiltInForce>(Fixture.Node.ExternalForces[1]);
		}
		auto& Force = Fixture.Node.ExternalForces[1].GetMutable<TBuiltInForce>();
		Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.0f, 0.25f);
		Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(1.0f, 0.25f);
		if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Basic>)
		{
			Force.ForceDir = FVector(60.0, 0.0, 0.0);
		}
		else if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Curve>)
		{
			Force.ForceCurve.GetRichCurve(0)->AddKey(0.0f, 60.0f);
			Force.ForceCurve.GetRichCurve(0)->AddKey(1.0f, 60.0f);
		}
		else if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Gravity>)
		{
			Force.bUseOverrideGravityDirection = true;
			Force.OverrideGravityDirection = FVector::XAxisVector;
			Force.RandomForceScaleRange = FFloatInterval(60.0f, 60.0f);
		}
		else
		{
			Force.WindDirection = FVector::XAxisVector;
			Force.ConstantForce = 1.0f;
			Force.SwayForce = Force.RippleForce = Force.RandomForce = 0.0f;
			Force.WindDirectionNoiseAngle = 0.0f;
			Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
		}
		NoForce.EvaluateFrame();
		FForceTrace Trace;
		TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
		Fixture.EvaluateFrame();
		const FString Cell = FString::Printf(TEXT("%s Super=%d"), *TBuiltInForce::StaticStruct()->GetName(), bCallSuper);
		if (!Test.TestEqual(Cell + TEXT(" 位置の前を二ステップ観測"), Trace.BeforeLocation.Num(), 2) ||
			!Test.TestEqual(Cell + TEXT(" 位置の後を二ステップ観測"), Trace.AfterLocation.Num(), 2) ||
			!Test.TestEqual(Cell + TEXT(" 速度の前を二ステップ観測"), Trace.BeforeVelocity.Num(), 2) ||
			!Test.TestEqual(Cell + TEXT(" 速度の後を二ステップ観測"), Trace.AfterVelocity.Num(), 2)) return false;
		bOk &= Test.TestEqual(Cell + TEXT(" PreApply は一回"), Trace.PreApplyCount, 1);
		const FVector Expected = (bCallSuper ? FVector::ZeroVector : FVector(6.0, -3.0, 0.0)) * 0.25f * (1.0f / 60.0f);
		for (int32 StepIndex = 0; StepIndex < 2; ++StepIndex)
		{
			bOk &= Test.TestTrue(Cell + TEXT(" ステップ変換中"), Trace.StepTransforms[StepIndex]);
			const FVector LocationDelta = Trace.AfterLocation[StepIndex] - Trace.BeforeLocation[StepIndex];
			const FVector VelocityDelta = Trace.AfterVelocity[StepIndex] - Trace.BeforeVelocity[StepIndex];
			const bool bGravity = std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Gravity>;
			// 四則演算と float の dt の丸めのみを許し、派生の sim 空間の値は回転しない。
			bOk &= Test.TestTrue(Cell + TEXT(" 派生の値 × ForceRate × dt で位置に作用する"),
				LocationDelta.Equals(bGravity ? FVector::ZeroVector : Expected, 1.e-6));
			bOk &= Test.TestTrue(Cell + TEXT(" Gravity だけが同じ式で速度に作用する"),
				VelocityDelta.Equals(bGravity ? Expected : FVector::ZeroVector, 1.e-6));
		}
		if (bCallSuper)
		{
			if (!Test.TestEqual(Cell + TEXT(" 力ありの実ボーン数"), Fixture.Node.ModifyBones.Num(), 2) ||
				!Test.TestEqual(Cell + TEXT(" 力なしの実ボーン数"), NoForce.Node.ModifyBones.Num(), 2)) return false;
			bOk &= Test.TestTrue(Cell + TEXT(" Super 後のゼロ置換は力なしセルと一致する"),
				Fixture.Node.ModifyBones[1].Location.Equals(NoForce.Node.ModifyBones[1].Location, 1.e-6));
		}
	}
	return bOk;
}

// F3・FX-24。Basic / Curve / Gravity / ProceduralWind の継承された Apply 系を、回転する 30fps の二ステップで通す。
// 修正前は Super 後のゼロ置換でも非ゼロの Component キャッシュが作用し、ゼロ変位と力なしセルとの一致が落ちる。
// Super を呼ばないセルはゼロの Component キャッシュを読み、派生の値 × 0.25 × dt の主張が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneDerivedBuiltInForceStateTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneDerivedBuiltInForceState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneDerivedBuiltInForceStateTest::RunTest(const FString& Parameters)
{
	bool bOk = CheckBuiltInForceOverride<FKawaiiPhysics_ExternalForce_Basic>(*this);
	bOk &= CheckBuiltInForceOverride<FKawaiiPhysics_ExternalForce_Curve>(*this);
	bOk &= CheckBuiltInForceOverride<FKawaiiPhysics_ExternalForce_Gravity>(*this);
	bOk &= CheckBuiltInForceOverride<FKawaiiPhysics_ExternalForce_ProceduralWind>(*this);
	return bOk;
}

// 最初の二評価だけ組み込みの PreApply を通し、その後は同じ sim 空間の値を保持する。
// 状態は既存の観測領域に置き、反射された基底と同じサイズの保存領域で使う。
template <typename TBuiltInForce>
struct FRetainBuiltInForce : TBuiltInForce
{
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, FComponentSpacePoseContext& PoseContext) override
	{
		if (ActiveForceTrace->PreApplyCount < 2) TBuiltInForce::PreApply(Node, PoseContext);
		++ActiveForceTrace->PreApplyCount;
	}

	// 基底の protected な Force をテストから読む口。
	FVector GetRetainedForce() const
	{
		return this->Force;
	}
};

template <typename TBuiltInForce>
bool CheckBuiltInForcePreparation(FAutomationTestBase& Test)
{
	auto Options = MakeAnalyticalOptions(30);
	Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
	{
		// 初期化と最初の二評価は Identity。その後、各フレームで 90 度ずつ回す。
		Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector,
			FMath::DegreesToRadians(90.0f * FMath::Max(Frame - 2, 0))));
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	Fixture.EvaluateFrame();
	Fixture.Node.ExternalForces.Reserve(3);
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<TBuiltInForce>());
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
	InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
	InitializePlainForce<FRetainBuiltInForce<TBuiltInForce>, TBuiltInForce>(Fixture.Node.ExternalForces[1]);
	InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces[2]);

	auto& Force = Fixture.Node.ExternalForces[1].GetMutable<TBuiltInForce>();
	Force.ExternalForceSpace = EExternalForceSpace::WorldSpace;
	Force.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
	Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.0f, 0.25f);
	Force.ForceRateByBoneLengthRate.GetRichCurve()->AddKey(1.0f, 0.25f);
	if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Basic>)
	{
		Force.ForceDir = FVector::XAxisVector;
	}
	else if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Curve>)
	{
		Force.ForceCurve.GetRichCurve(0)->AddKey(0.0f, 1.0f);
		Force.ForceCurve.GetRichCurve(0)->AddKey(1.0f, 1.0f);
	}
	else if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Gravity>)
	{
		Force.bUseOverrideGravityDirection = true;
		Force.OverrideGravityDirection = FVector::XAxisVector;
	}
	else
	{
		Force.WindDirection = FVector::XAxisVector;
		Force.ConstantForce = 1.0f;
		Force.SwayForce = Force.RippleForce = Force.RandomForce = 0.0f;
		Force.WindDirectionNoiseAngle = 0.0f;
		Force.StrengthCycleRange = FFloatInterval(1.0f, 1.0f);
	}
	const auto GetSimSpaceForce = [&Force]()
	{
		if constexpr (std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_ProceduralWind>)
		{
			return Force.RuntimeState->CachedWindVector;
		}
		else
		{
			// 保存領域の中身は InitializePlainForce で構築した FRetainBuiltInForce。
			return static_cast<const FRetainBuiltInForce<TBuiltInForce>&>(Force).GetRetainedForce();
		}
	};
	FForceTrace Trace;
	TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
	FVector RetainedForce = FVector::ZeroVector;
	bool bOk = true;
	for (int32 Frame = 1; Frame <= 4; ++Frame)
	{
		Fixture.EvaluateFrame();
		const FString Cell = FString::Printf(TEXT("%s フレーム %d"), *TBuiltInForce::StaticStruct()->GetName(), Frame);
		if (!Test.TestEqual(Cell + TEXT(" 位置の前を全ステップ観測"), Trace.BeforeLocation.Num(), Frame * 2) ||
			!Test.TestEqual(Cell + TEXT(" 位置の後を全ステップ観測"), Trace.AfterLocation.Num(), Frame * 2) ||
			!Test.TestEqual(Cell + TEXT(" 速度の前を全ステップ観測"), Trace.BeforeVelocity.Num(), Frame * 2) ||
			!Test.TestEqual(Cell + TEXT(" 速度の後を全ステップ観測"), Trace.AfterVelocity.Num(), Frame * 2) ||
			!Test.TestEqual(Cell + TEXT(" 変換の使用状態を全ステップ観測"), Trace.StepTransforms.Num(), Frame * 2)) return false;
		bOk &= Test.TestEqual(Cell + TEXT(" 派生の PreApply は評価ごとに一回"), Trace.PreApplyCount, Frame);
		if (Frame == 2)
		{
			RetainedForce = GetSimSpaceForce();
			bOk &= Test.TestTrue(Cell + TEXT(" Super が非ゼロの +X を準備した"), RetainedForce.Equals(FVector::XAxisVector, 1.e-6));
		}
		if (Frame <= 2) continue;
		const FVector CurrentForce = GetSimSpaceForce();
		bOk &= Test.TestTrue(Cell + TEXT(" Super を呼ばず前の sim 空間の値をビット単位で保持する"),
			FMemory::Memcmp(&CurrentForce, &RetainedForce, sizeof(FVector)) == 0);
		const FVector Expected = RetainedForce * 0.25f * (1.0f / 60.0f);
		for (int32 StepIndex = (Frame - 1) * 2; StepIndex < Frame * 2; ++StepIndex)
		{
			bOk &= Test.TestTrue(Cell + TEXT(" ステップ変換中"), Trace.StepTransforms[StepIndex]);
			const FVector LocationDelta = Trace.AfterLocation[StepIndex] - Trace.BeforeLocation[StepIndex];
			const FVector VelocityDelta = Trace.AfterVelocity[StepIndex] - Trace.BeforeVelocity[StepIndex];
			const bool bGravity = std::is_same_v<TBuiltInForce, FKawaiiPhysics_ExternalForce_Gravity>;
			// float の dt と変位の差し引きの丸めのみを許す。誤った回転による差は約 0.003 以上になる。
			bOk &= Test.TestTrue(Cell + TEXT(" 保持した値 × ForceRate × dt で位置に作用する"),
				LocationDelta.Equals(bGravity ? FVector::ZeroVector : Expected, 1.e-6));
			bOk &= Test.TestTrue(Cell + TEXT(" Gravity だけが保持した値 × ForceRate × dt で速度に作用する"),
				VelocityDelta.Equals(bGravity ? Expected : FVector::ZeroVector, 1.e-6));
		}
	}
	return bOk;
}

// F3（評価ごとの準備）・FX-24。四種とも最初の二フレームは Super、その後の二フレームは値を保持する。
// bool の印を持つ修正前は古い Component キャッシュを回転し、各ステップの「保持値 × 0.25 × dt」が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneDerivedBuiltInForcePreparationTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneDerivedBuiltInForcePreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneDerivedBuiltInForcePreparationTest::RunTest(const FString& Parameters)
{
	bool bOk = CheckBuiltInForcePreparation<FKawaiiPhysics_ExternalForce_Basic>(*this);
	bOk &= CheckBuiltInForcePreparation<FKawaiiPhysics_ExternalForce_Curve>(*this);
	bOk &= CheckBuiltInForcePreparation<FKawaiiPhysics_ExternalForce_Gravity>(*this);
	bOk &= CheckBuiltInForcePreparation<FKawaiiPhysics_ExternalForce_ProceduralWind>(*this);
	return bOk;
}

// 最終検収 C1・FX-24。Wind の派生が最初の二フレームだけ Super::PreApply を呼び、その後は呼ばずに基準ボーンを回す。
// Wind の方向のキャッシュは private で、従来は sim 空間の方向をそのまま使い続ける。各ステップの変位は、
// 基準ボーンが Identity の二フレーム目のステップの変位とビット近くで一致する（float の丸めのみ 1e-6）。
// 評価の番号で確かめない版は、古い Component 空間の方向を 45° / 90° 回すので、この一致が 0.01 以上ずれて落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneDerivedWindPreparationTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneDerivedWindPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneDerivedWindPreparationTest::RunTest(const FString& Parameters)
{
	auto Options = MakeAnalyticalOptions(30);
	Options.SetInputPose = [](FCompactPose& Pose, const int32 Frame)
	{
		// 初期化と最初の二評価は Identity。その後、各フレームで 90 度ずつ回す。
		Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector,
			FMath::DegreesToRadians(90.0f * FMath::Max(Frame - 2, 0))));
	};
	KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
	Fixture.EvaluateFrame();
	Fixture.Node.ExternalForces.Reserve(3);
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_Wind>());
	Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
	InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
	InitializePlainForce<FRetainBuiltInForce<FKawaiiPhysics_ExternalForce_Wind>, FKawaiiPhysics_ExternalForce_Wind>(
		Fixture.Node.ExternalForces[1]);
	InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces[2]);
	auto& Wind = Fixture.Node.ExternalForces[1].GetMutable<FKawaiiPhysics_ExternalForce_Wind>();
	Wind.ExternalForceSpace = EExternalForceSpace::WorldSpace;
	Wind.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
	Wind.WindDirectionNoiseAngle = 0.0f;

	FForceTrace Trace;
	TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
	FVector ReferenceDelta = FVector::ZeroVector;
	bool bOk = true;
	for (int32 Frame = 1; Frame <= 4; ++Frame)
	{
		Fixture.Accessor.SetWindCenterSample(FVector::XAxisVector, 60.0f);
		Fixture.EvaluateFrame();
		const FString Cell = FString::Printf(TEXT("Wind フレーム %d"), Frame);
		if (!TestEqual(Cell + TEXT(" 位置の前を全ステップ観測"), Trace.BeforeLocation.Num(), Frame * 2) ||
			!TestEqual(Cell + TEXT(" 位置の後を全ステップ観測"), Trace.AfterLocation.Num(), Frame * 2) ||
			!TestEqual(Cell + TEXT(" 変換の使用状態を全ステップ観測"), Trace.StepTransforms.Num(), Frame * 2)) return false;
		bOk &= TestEqual(Cell + TEXT(" 派生の PreApply は評価ごとに一回"), Trace.PreApplyCount, Frame);
		if (Frame == 2)
		{
			ReferenceDelta = Trace.AfterLocation[3] - Trace.BeforeLocation[3];
			bOk &= TestTrue(Cell + TEXT(" 対照: Super が準備した風が作用する"), ReferenceDelta.Size() > 0.1);
		}
		if (Frame <= 2) continue;
		for (int32 StepIndex = (Frame - 1) * 2; StepIndex < Frame * 2; ++StepIndex)
		{
			bOk &= TestTrue(Cell + TEXT(" ステップ変換中"), Trace.StepTransforms[StepIndex]);
			const FVector LocationDelta = Trace.AfterLocation[StepIndex] - Trace.BeforeLocation[StepIndex];
			bOk &= TestTrue(FString::Printf(TEXT("%s ステップ %d は保持した sim 空間の方向で作用する %s / %s"), *Cell, StepIndex,
				*LocationDelta.ToString(), *ReferenceDelta.ToString()), LocationDelta.Equals(ReferenceDelta, 1.e-6));
		}
	}
	return bOk;
}

// F4・FX-24。静止した Yaw 90° と非一様・負スケールで、同じ Seed のノイズを一ステップと二ステップで比較する。
// 修正前は FTransform の往復で XY が伸縮・反転し、各ボーン・各ステップの方向一致（1e-6）が落ちる。
// F6: スケール (2,0,1) は写像に逆が無い。逆が無いときに従来の標本へ戻さないと、30fps の方向一致が落ちる。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseBoneNoisyWindStepScalesTest,
	"KawaiiPhysics.SolverSpec.Substep.BaseBoneNoisyWindStepScales",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBaseBoneNoisyWindStepScalesTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	// (2,0,1) は写像に逆が無い場合（F6）。逆行列の代わりに単位行列を使うと、30fps だけ方向と強さが変わる。
	for (const FVector& Scale : {FVector(2, 1, 1), FVector(-1, 1, 1), FVector(2, 0, 1)})
	{
		const bool bSingularScale = Scale.Y == 0.0;
		TArray<FVector> ReferenceDirections;
		int32 ReferenceNextRandom = 0;
		for (const int32 Rate : {60, 30})
		{
			auto Options = MakeAnalyticalOptions(Rate);
			Options.Bones = KawaiiPhysicsNodeFixture::BuildVerticalChain(3);
			Options.SetInputPose = [Scale](FCompactPose& Pose, const int32 Frame)
			{
				Pose[FCompactPoseBoneIndex(0)].SetRotation(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(90.0f)));
				Pose[FCompactPoseBoneIndex(0)].SetScale3D(Scale);
			};
			KawaiiPhysicsNodeFixture::FFixture Fixture(Options);
			Fixture.EvaluateFrame();
			Fixture.Node.ExternalForces.Reserve(3);
			Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
			auto& Wind = KawaiiPhysicsNodeFixture::AddExternalForce<FKawaiiPhysics_ExternalForce_Wind>(Fixture.Node);
			Wind.WindDirectionNoiseAngle = 30.0f;
			Wind.RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);
			Fixture.Node.ExternalForces.Add(FInstancedStruct::Make<FKawaiiPhysics_ExternalForce>());
			InitializePlainForce<FBeforeForceProbe>(Fixture.Node.ExternalForces[0]);
			InitializePlainForce<FAfterForceProbe>(Fixture.Node.ExternalForces[2]);
			Fixture.Accessor.SetWindCenterSample(FVector(1, 1, 0).GetSafeNormal(), 1.0f);
			FForceTrace Trace;
			TGuardValue<FForceTrace*> TraceScope(ActiveForceTrace, &Trace);
			// 同数のボーンに同じ順で PreApply を一回呼び、各ボーンの VRandCone 標本を揃える。
			FMath::RandInit(173);
			Fixture.EvaluateFrame();
			const int32 NextRandom = FMath::Rand();
			const int32 NumSteps = 60 / Rate;
			if (!TestEqual(TEXT("二本の動くボーンの位置前を全ステップ観測"), Trace.BeforeLocation.Num(), NumSteps * 2) ||
				!TestEqual(TEXT("二本の動くボーンの位置後を全ステップ観測"), Trace.AfterLocation.Num(), NumSteps * 2) ||
				!TestEqual(TEXT("ステップ変換の使用を全ボーンで観測"), Trace.StepTransforms.Num(), NumSteps * 2)) return false;
			if (Rate == 60) ReferenceNextRandom = NextRandom;
			else bOk &= TestEqual(TEXT("一ステップと二ステップで乱数の消費数が同じ"), NextRandom, ReferenceNextRandom);
			for (int32 Index = 0; Index < NumSteps * 2; ++Index)
			{
				bOk &= TestEqual(TEXT("30fps のセルだけステップ変換を使う"), Trace.StepTransforms[Index], Rate == 30);
				const FVector Direction = (Trace.AfterLocation[Index] - Trace.BeforeLocation[Index]) / (1.0f / 60.0f);
				// 0 の軸があるスケールでは sim 空間の風が縮むので、作用の有無だけを見る下限を下げる。
				bOk &= TestTrue(TEXT("ノイズありの風が非ゼロで作用する"), Direction.Size() > (bSingularScale ? 0.05 : 0.5));
				if (Rate == 60) ReferenceDirections.Add(Direction);
				// float の dt、行列の逆写像と変位からの逆算の丸めだけを許すため、方向の許容誤差は 1e-6。
				else bOk &= TestTrue(FString::Printf(TEXT("スケール %s ステップ %d ボーン %d の方向は終端キャッシュと一致"),
					*Scale.ToString(), Index / 2, Index % 2), Direction.Equals(ReferenceDirections[Index % 2], 1.e-6));
			}
			if (Rate == 60)
			{
				bOk &= TestTrue(TEXT("ボーンごとに別のノイズ標本を保つ"), !ReferenceDirections[0].Equals(ReferenceDirections[1], 1.e-4));
			}
		}
	}
	return bOk;
}

}

#endif
