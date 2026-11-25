// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "KawaiiPhysicsTestHarness.h"

namespace KawaiiPhysicsSubstepPlanTestPrivate
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubstepPlanArithmeticTest,
	"KawaiiPhysics.Simulation.SubstepPlan.Arithmetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSubstepPlanArithmeticTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const float FixedDt = 1.0f / 60.0f;
	const auto Zero = Accessor.ComputeSubstepPlanForTest(0.0f, FixedDt * 0.5f, 8, 60);
	TestEqual(TEXT("less than one fixed dt takes no step"), Zero.NumSteps, 0);
	TestEqual(TEXT("unconsumed half dt remains"), Zero.RemainingAccumulator, FixedDt * 0.5f);
	TestEqual(TEXT("no movement is consumed"), Zero.ConsumeFraction, 0.0f);

	const auto One = Accessor.ComputeSubstepPlanForTest(0.0f, FixedDt, 8, 60);
	TestEqual(TEXT("one fixed dt takes one step"), One.NumSteps, 1);
	TestEqual(TEXT("one step has no remainder"), One.RemainingAccumulator, 0.0f);
	TestEqual(TEXT("one step consumes all movement"), One.ConsumeFraction, 1.0f);

	const auto Multiple = Accessor.ComputeSubstepPlanForTest(0.0f, FixedDt * 3.0f, 8, 60);
	TestEqual(TEXT("three fixed dt take three steps"), Multiple.NumSteps, 3);
	TestEqual(TEXT("three steps have no dropped time"), Multiple.DroppedTime, 0.0f);

	// 固定 dt 5 回分が入り、上限 2 回だけ進めて残り 3 回分は捨てる。
	const auto Capped = Accessor.ComputeSubstepPlanForTest(0.0f, FixedDt * 5.0f, 2, 60);
	TestEqual(TEXT("step count is capped"), Capped.NumSteps, 2);
	TestEqual(TEXT("three fixed dt are dropped"), Capped.DroppedTime, FixedDt * 5.0f - FixedDt * 2.0f);
	TestEqual(TEXT("dropped time is consumed"), Capped.ConsumeFraction, 1.0f);

	// 前フレームの持ち越し 0.5 と今フレームの 0.75（固定 dt 単位）で 1.25 たまる。
	// 1 回進めて 0.25 を持ち越す。移動の分配は 1 / 1.25 = 0.8、消費率も 1 / 1.25 = 0.8。
	const auto Carried = Accessor.ComputeSubstepPlanForTest(FixedDt * 0.5f, FixedDt * 0.75f, 8, 60);
	TestEqual(TEXT("carried accumulator reaches one step"), Carried.NumSteps, 1);
	TestEqual(TEXT("carried accumulator keeps a quarter dt"), Carried.RemainingAccumulator, FixedDt * 0.25f);
	TestEqual(TEXT("move fraction is one step over the elapsed time"), Carried.MoveFrac, 0.8f);
	TestEqual(TEXT("consume fraction is one step over the elapsed time"), Carried.ConsumeFraction, 0.8f);
	TestEqual(TEXT("carried accumulator drops nothing"), Carried.DroppedTime, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSubstepPlanSwitchBitsTest,
	"KawaiiPhysics.Simulation.SubstepPlan.SwitchBits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSubstepPlanSwitchBitsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Switched;
	FKawaiiPhysicsTestAccessor Legacy;
	Switched.BuildVerticalChain(3, 10.0f);
	Legacy.BuildVerticalChain(3, 10.0f);
	Switched.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
	Legacy.SetGravityInSimSpace(FVector(0.0, 0.0, -980.0));
	const float FixedDt = 1.0f / 60.0f;
	const bool Modes[] = {true, false, true};
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		Switched.SetFixedSubstepping(Modes[Frame], 60);
		Legacy.SetFixedSubstepping(false, 60);
		Switched.StepFrame(FixedDt);
		Legacy.StepFrame(FixedDt);
		for (int32 BoneIndex = 0; BoneIndex < Switched.Num(); ++BoneIndex)
		{
			const FVector& A = Switched.Bone(BoneIndex).Location;
			const FVector& B = Legacy.Bone(BoneIndex).Location;
			// 固定サブステップのフレームはちょうど 1 ステップ・持ち越し 0・ポーズ一定なので、
			// dt と物理の入力が legacy の 1 ステップと同じになり、モードを切り替えてもビット一致する。
			TestTrue(TEXT("switched and legacy position bits match"),
				FMemory::Memcmp(&A.X, &B.X, sizeof(double) * 3) == 0);
		}
	}
	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
