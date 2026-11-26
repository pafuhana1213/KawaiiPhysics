// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsAngleLimit.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsTestHarness.h"

namespace KawaiiPhysicsControlSpecTestPrivate
{
	// SP-08・SP-06。2063: 差が MaxAge と同じ 121 で期限切れになり失敗する。2064: 巻き戻った 110 で別 provider を受理して失敗する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecPublisherAgeTest,
		"KawaiiPhysics.ControlSpec.Publisher.AgeBoundary",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecPublisherAgeTest::RunTest(const FString& Parameters)
	{
		FKawaiiPhysicsSharedPublisherEntry PublisherEntry;
		FKawaiiPhysicsSharedPublisherState FirstState;
		FKawaiiPhysicsSharedPublisherState OtherState;
		OtherState.bSimpleWorldEnabled = true;
		bool bOk = TestTrue(TEXT("最初の provider が公開できる"), PublisherEntry.PublishState(FirstState, 11, 111, 10));
		bOk &= TestEqual(TEXT("公開フレーム"), PublisherEntry.GetLastPublishFrame(), static_cast<uint64>(111));
		// 121−111=10 は生存、122−111=11 は期限切れ。巻き戻りは生存とする。
		bOk &= TestFalse(TEXT("期限ちょうどは生存"), PublisherEntry.IsExpired(121, 10));
		bOk &= TestTrue(TEXT("期限を一つ超えると失効"), PublisherEntry.IsExpired(122, 10));
		bOk &= TestFalse(TEXT("巻き戻りは生存"), PublisherEntry.IsExpired(110, 10));
		bOk &= TestFalse(TEXT("期限ちょうどで交代しない"), PublisherEntry.PublishState(OtherState, 22, 121, 10));
		bOk &= TestFalse(TEXT("巻き戻りで交代しない"), PublisherEntry.PublishState(OtherState, 22, 110, 10));
		bOk &= TestEqual(TEXT("元の provider が残る"), PublisherEntry.GetProviderID(), static_cast<uint64>(11));
		return bOk;
	}

	// LA-08・LA-12。2093: A 軸を 0 に戻さず、片側曲げの −30 と全軸ロックの 20 が残る。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecOneSidedSwingTest,
		"KawaiiPhysics.ControlSpec.LimitAngle.OneSidedSwing",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecOneSidedSwingTest::RunTest(const FString& Parameters)
	{
		KawaiiPhysics::AngleLimit::FSwingLimit OneSidedLimit;
		OneSidedLimit.MinA = 0.0f;
		OneSidedLimit.MaxA = 40.0f;
		OneSidedLimit.MinB = -20.0f;
		OneSidedLimit.MaxB = 30.0f;
		const FVector2f Inside(20.0f, 10.0f);
		bool bOk = TestTrue(TEXT("内側の対照は変わらない"), KawaiiPhysics::AngleLimit::ClampSwing(Inside, OneSidedLimit) == Inside);
		// A の負側は範囲 0、B の +10 は範囲内。float の角度演算の丸めに 1e-3 度を許す。
		bOk &= TestTrue(TEXT("A の負側だけをロック"),
			KawaiiPhysics::AngleLimit::ClampSwing(FVector2f(-30.0f, 10.0f), OneSidedLimit).Equals(FVector2f(0.0f, 10.0f), 0.001f));
		KawaiiPhysics::AngleLimit::FSwingLimit LockedLimit;
		LockedLimit.MinA = LockedLimit.MaxA = LockedLimit.MinB = LockedLimit.MaxB = 0.0f;
		bOk &= TestTrue(TEXT("二軸ロックはゼロ"),
			KawaiiPhysics::AngleLimit::ClampSwing(FVector2f(20.0f, 10.0f), LockedLimit).Equals(FVector2f(0.0f, 0.0f), 0.001f));
		return bOk;
	}

	// LA-01・LA-20・LA-U1。mutant なし。±180 に 0.5 を掛けると ±90、0 を掛けると全軸 0。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecAxisScaleTest,
		"KawaiiPhysics.ControlSpec.LimitAngle.AxisScale",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecAxisScaleTest::RunTest(const FString& Parameters)
	{
		const FKawaiiPhysicsSettings DefaultSettings;
		const FAnimNode_KawaiiPhysics DefaultNode;
		bool bOk = TestTrue(TEXT("設定の既定は Cone"), DefaultSettings.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::Cone);
		bOk &= TestTrue(TEXT("ノードの既定は Cone"), DefaultNode.PhysicsSettings.LimitAngleMode == EKawaiiPhysicsLimitAngleMode::Cone);
		FKawaiiPhysicsPerAxisLimitAngle WideLimit;
		WideLimit.X.Min = WideLimit.Y.Min = WideLimit.Z.Min = -180.0f;
		WideLimit.X.Max = WideLimit.Y.Max = WideLimit.Z.Max = 180.0f;
		const FKawaiiPhysicsPerAxisLimitAngle HalfLimit = KawaiiPhysics::AngleLimit::ScalePerAxisLimitAngle(WideLimit, 0.5f);
		const FKawaiiPhysicsPerAxisLimitAngle ZeroLimit = KawaiiPhysics::AngleLimit::ScalePerAxisLimitAngle(WideLimit, 0.0f);
		for (const auto* Axis : {&HalfLimit.X, &HalfLimit.Y, &HalfLimit.Z})
		{
			bOk &= TestEqual(TEXT("半分の下限"), Axis->Min, -90.0f);
			bOk &= TestEqual(TEXT("半分の上限"), Axis->Max, 90.0f);
		}
		for (const auto* Axis : {&ZeroLimit.X, &ZeroLimit.Y, &ZeroLimit.Z})
		{
			bOk &= TestEqual(TEXT("ゼロの下限"), Axis->Min, 0.0f);
			bOk &= TestEqual(TEXT("ゼロの上限"), Axis->Max, 0.0f);
		}
		return bOk;
	}

	// SM-02・SM-03・SM-U1。mutant なし。中間 α の 0.875 と、二項目の積 0.1 をリテラルで照合する。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecMultiplierProductTest,
		"KawaiiPhysics.ControlSpec.Multiplier.IntermediateProduct",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecMultiplierProductTest::RunTest(const FString& Parameters)
	{
		FKawaiiPhysicsTestAccessor MultiplierAccessor;
		FKawaiiPhysicsSettingsMultiplier HalfDamping;
		HalfDamping.Damping = 0.5f;
		MultiplierAccessor.Node.RequestPushPhysicsSettingsMultiplier(HalfDamping, 0.25f, 101);
		MultiplierAccessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bool bOk = TestEqual(TEXT("Push 項目が一件ある"),
			MultiplierAccessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		// 1−(1−0.5)×0.25 = 0.875。float の積の丸めに 1e-6。
		bOk &= TestTrue(TEXT("中間 α の実効倍率は 0.875"),
			FMath::Abs(MultiplierAccessor.CallComputeEffectiveSettingsMultiplierScale().Damping - 0.875f) <= 0.000001f);
		MultiplierAccessor.Node.RequestPushPhysicsSettingsMultiplier(HalfDamping, 0.0f, 101);
		MultiplierAccessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("α=0 は倍率 1"),
			MultiplierAccessor.CallComputeEffectiveSettingsMultiplierScale().Damping, 1.0f);
		MultiplierAccessor.Node.RequestPushPhysicsSettingsMultiplier(HalfDamping, 1.0f, 101);
		FKawaiiPhysicsSettingsMultiplier FifthDamping;
		FifthDamping.Damping = 0.2f;
		MultiplierAccessor.Node.RequestPushPhysicsSettingsMultiplier(FifthDamping, 1.0f, 202);
		MultiplierAccessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("別ハンドルが二件ある"),
			MultiplierAccessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 2);
		// 0.5×0.2 = 0.1。float の積の丸めに 1e-6。
		bOk &= TestTrue(TEXT("二項目の積は 0.1"),
			FMath::Abs(MultiplierAccessor.CallComputeEffectiveSettingsMultiplierScale().Damping - 0.1f) <= 0.000001f);
		return bOk;
	}

	// LA-02。mutant なし。Cone の LimitAngle=0 は 90°、179°、180° の方向をビット一致で通す。
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsControlSpecUnlimitedConeTest,
		"KawaiiPhysics.ControlSpec.LimitAngle.UnlimitedCone",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FKawaiiPhysicsControlSpecUnlimitedConeTest::RunTest(const FString& Parameters)
	{
		FKawaiiPhysicsTestAccessor AngleAccessor;
		FKawaiiPhysicsModifyBone ParentBone;
		ParentBone.Location = FVector::ZeroVector;
		ParentBone.PoseLocation = FVector::ZeroVector;
		ParentBone.PoseRotation = FQuat::Identity;
		bool bOk = true;
		for (const FVector& UnrestrictedLocation : {
			FVector(0.0, 10.0, 0.0),
			FVector(10.0 * FMath::Cos(FMath::DegreesToRadians(179.0)),
				10.0 * FMath::Sin(FMath::DegreesToRadians(179.0)), 0.0),
			FVector(-10.0, 0.0, 0.0)})
		{
			FKawaiiPhysicsModifyBone ChildBone;
			ChildBone.PoseLocation = FVector(10.0, 0.0, 0.0);
			ChildBone.Location = UnrestrictedLocation;
			ChildBone.PhysicsSettings.LimitAngleMode = EKawaiiPhysicsLimitAngleMode::Cone;
			ChildBone.PhysicsSettings.LimitAngle = 0.0f;
			AngleAccessor.CallAngleLimit(ChildBone, ParentBone);
			bOk &= TestTrue(TEXT("Cone のゼロは方向を変えない"), ChildBone.Location == UnrestrictedLocation);
		}
		return bOk;
	}
}

#endif
