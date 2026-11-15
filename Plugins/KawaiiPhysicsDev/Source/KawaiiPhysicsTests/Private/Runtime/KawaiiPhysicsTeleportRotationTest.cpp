// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"

namespace KawaiiPhysicsTeleportRotationTestPrivate
{
// ヨーを StartYaw から 1 フレーム DegreesPerFrame ずつ Frames フレーム回し、テレポートと判定した回数を返す。
// FRotator から Quat を作るので、ヨーが ±180 度をまたぐと Character の旋回と同じように Quat の符号が反転する
int32 CountTeleportsForYawTurn(const float StartYaw, const float DegreesPerFrame, const int32 Frames)
{
	FKawaiiPhysicsTestAccessor A;
	A.BuildVerticalChain(4, 10.0f);
	A.SetSimulationSpace(EKawaiiPhysicsSimulationSpace::ComponentSpace);
	A.SetGravityInSimSpace(FVector::ZeroVector);
	A.Node.TeleportDistanceThreshold = 0.0f;
	A.Node.TeleportRotationThreshold = 10.0f;
	A.SetPreSkelCompTransform(FTransform(FRotator(0.0f, StartYaw, 0.0f)));

	FAnimInstanceProxy Proxy;
	FComponentSpacePoseContext Output(&Proxy);

	int32 Teleports = 0;
	for (int32 Frame = 1; Frame <= Frames; ++Frame)
	{
		const float Yaw = FRotator::NormalizeAxis(StartYaw + DegreesPerFrame * Frame);
		const FTransform Component(FRotator(0.0f, Yaw, 0.0f));
		Teleports += A.StepFrameWithComponentTransform(Output, 1.0f / 60.0f, Component) ? 1 : 0;
	}
	return Teleports;
}

// ヨーが ±180 度をまたいで Quat の符号が反転しても、1 フレームの回転が閾値未満ならテレポートにしない
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTeleportRotationSignFlipTest,
                                 "KawaiiPhysics.Teleport.RotationAcrossYaw180",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTeleportRotationSignFlipTest::RunTest(const FString& Parameters)
{
	// 前提：このヨーの並びでは、境界をまたぐフレームで Quat の符号が実際に反転する
	const FQuat Before = FRotator(0.0f, 179.0f, 0.0f).Quaternion();
	const FQuat After = FRotator(0.0f, -179.0f, 0.0f).Quaternion();
	bool bOk = TestTrue(TEXT("crossing yaw 180 flips the quaternion sign"),
		(Before.Inverse() * After).W < 0.0);

	// 1 フレーム 2 度で 170 度から -170 度まで回す（境界を 1 回またぐ）
	bOk &= TestEqual(TEXT("a slow turn across yaw 180 is not a teleport"), CountTeleportsForYawTurn(170.0f, 2.0f, 10), 0);
	// 逆回り
	bOk &= TestEqual(TEXT("a slow reverse turn across yaw -180 is not a teleport"),
		CountTeleportsForYawTurn(-170.0f, -2.0f, 10), 0);
	// 境界をまたいでも、1 フレームで閾値を超える回転は従来どおりテレポートになる
	bOk &= TestEqual(TEXT("a 25 degree snap across yaw 180 is one teleport"), CountTeleportsForYawTurn(170.0f, 25.0f, 1), 1);
	return bOk;
}

} // namespace KawaiiPhysicsTeleportRotationTestPrivate

#endif
