// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsTestHarness.h"

namespace
{
constexpr float GSettingsMultiplierTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSettingsMultiplierTol));
}

int32 GetPendingOverrideCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultipliers.Num();
}

int64 GetPendingOverrideHandle(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultipliers.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsMultipliers[Index].HandleId
		       : 0;
}

int32 GetPendingOverrideStopCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultiplierStops.Num();
}

int32 GetPendingOverrideSetCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultiplierPushes.Num();
}

// 検証しやすいよう全項目に異なる値を入れたベース設定
FKawaiiPhysicsSettings MakeBaseSettings()
{
	FKawaiiPhysicsSettings Settings;
	Settings.Damping = 0.4f;
	Settings.Stiffness = 0.2f;
	Settings.WorldDampingLocation = 0.8f;
	Settings.WorldDampingRotation = 0.6f;
	Settings.Radius = 3.0f;
	Settings.LimitAngle = 20.0f;
	return Settings;
}

void SetupChainWithBaseSettings(FKawaiiPhysicsTestAccessor& Accessor)
{
	Accessor.BuildVerticalChain(3, 10.0f);
	Accessor.Node.PhysicsSettings = MakeBaseSettings();
}

FKawaiiPhysicsSettingsMultiplier MakeScale(const float Damping, const float Stiffness, const float WorldDampingLocation,
                                      const float WorldDampingRotation, const float Radius, const float LimitAngle)
{
	FKawaiiPhysicsSettingsMultiplier Scale;
	Scale.Damping = Damping;
	Scale.Stiffness = Stiffness;
	Scale.WorldDampingLocation = WorldDampingLocation;
	Scale.WorldDampingRotation = WorldDampingRotation;
	Scale.Radius = Radius;
	Scale.LimitAngle = LimitAngle;
	return Scale;
}

bool ContainsSettingsMultiplierHandle(const FAnimNode_KawaiiPhysics& Node, const int64 HandleId)
{
	return Node.TransientForceStore.SettingsMultiplierItems.ContainsByPredicate(
		[HandleId](const FKawaiiPhysicsActiveSettingsMultiplier& Item)
		{
			return Item.HandleId == HandleId;
		});
}

bool TestBoneSettings(FAutomationTestBase& Test, const TCHAR* Context, const FKawaiiPhysicsModifyBone& Bone,
                      const FKawaiiPhysicsSettings& Expected)
{
	bool bOk = true;
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s Damping"), Context), Bone.PhysicsSettings.Damping,
	                     Expected.Damping);
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s Stiffness"), Context), Bone.PhysicsSettings.Stiffness,
	                     Expected.Stiffness);
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s WorldDampingLocation"), Context),
	                     Bone.PhysicsSettings.WorldDampingLocation, Expected.WorldDampingLocation);
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s WorldDampingRotation"), Context),
	                     Bone.PhysicsSettings.WorldDampingRotation, Expected.WorldDampingRotation);
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s Radius"), Context), Bone.PhysicsSettings.Radius,
	                     Expected.Radius);
	bOk &= TestFloatNear(Test, *FString::Printf(TEXT("%s LimitAngle"), Context), Bone.PhysicsSettings.LimitAngle,
	                     Expected.LimitAngle);
	return bOk;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierEnvelopeAlphaTest,
                                 "KawaiiPhysics.SettingsMultiplier.EnvelopeAlpha",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierEnvelopeAlphaTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	// 台形（rise 0.2 / hold 0.6 / decay 0.2）
	bOk &= TestFloatNear(*this, TEXT("Rise start"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.0f), 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Rise mid"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.1f), 0.5f);
	bOk &= TestFloatNear(*this, TEXT("Rise end"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.2f), 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Hold mid"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.5f), 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Decay start"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.8f), 1.0f);
	bOk &= TestFloatNear(*this, TEXT("Decay mid"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 0.9f), 0.5f);
	bOk &= TestFloatNear(*this, TEXT("Decay end"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 1.0f), 0.0f);
	bOk &= TestFloatNear(*this, TEXT("After end"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, 5.0f), 0.0f);

	// 負の経過時間は 0
	bOk &= TestFloatNear(*this, TEXT("Negative elapsed"),
	                     KawaiiPhysics::EvaluateEnvelopeAlpha01(0.2f, 0.6f, 0.2f, -1.0f), 0.0f);

	// 全区間 0 は常に 0（Duration<=0 相当）
	bOk &= TestFloatNear(*this, TEXT("Zero envelope"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.0f, 0.0f, 0.0f, 0.0f),
	                     0.0f);

	// 負の区間長は 0 として扱う
	bOk &= TestFloatNear(*this, TEXT("Negative rise/decay"),
	                     KawaiiPhysics::EvaluateEnvelopeAlpha01(-1.0f, 1.0f, -1.0f, 0.5f), 1.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierRequestConsumeTest,
                                 "KawaiiPhysics.SettingsMultiplier.RequestConsume",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierRequestConsumeTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 0.25f, 0.5f, 0.5f, 2.0f, 0.0f);
		const int64 Handle = Node.RequestStartPhysicsSettingsMultiplier(Scale, 0.2f, 1.0f, 0.5f);

		bOk &= TestTrue(TEXT("Generated handle"), Handle > 0);
		bOk &= TestEqual(TEXT("Items before consume"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);

		bOk &= TestTrue(TEXT("Consume reports active"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		bOk &= TestEqual(TEXT("Items after consume"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);

		if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
		{
			const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
			bOk &= TestEqual(TEXT("Stamped handle"), Item.HandleId, Handle);
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("Scale.Damping"), Item.Scale.Damping, 0.5f);
			bOk &= TestFloatNear(*this, TEXT("Scale.LimitAngle"), Item.Scale.LimitAngle, 0.0f);
		}
	}

	{
		// 明示ハンドルはそのまま採用される
		FAnimNode_KawaiiPhysics Node;
		const int64 Returned = Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 1.0f, 0.0f, 4242);
		bOk &= TestEqual(TEXT("Explicit handle returned"), Returned, static_cast<int64>(4242));
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("Explicit handle stamped"),
		                 Node.TransientForceStore.SettingsMultiplierItems[0].HandleId, static_cast<int64>(4242));
	}

	{
		// Duration<=0 相当（全区間0）は初回consumeで即消滅する
		FAnimNode_KawaiiPhysics Node;
		Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f);
		bOk &= TestFalse(TEXT("Zero envelope is inactive"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		bOk &= TestEqual(TEXT("Zero envelope leaves no item"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierApplyScaleTest,
                                 "KawaiiPhysics.SettingsMultiplier.ApplyScale",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierApplyScaleTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 0.25f), 0.0f, 1.0f, 0.0f);
		bOk &= TestTrue(TEXT("Active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		Accessor.CallUpdatePhysicsSettings();

		FKawaiiPhysicsSettings Expected;
		Expected.Damping = 0.2f;
		Expected.Stiffness = 0.1f;
		Expected.WorldDampingLocation = 0.4f;
		Expected.WorldDampingRotation = 0.3f;
		Expected.Radius = 6.0f;
		Expected.LimitAngle = 5.0f;
		for (int32 Index = 0; Index < Accessor.Num(); ++Index)
		{
			bOk &= TestBoneSettings(*this, *FString::Printf(TEXT("Scaled bone %d"), Index), Accessor.Bone(Index),
			                        Expected);
		}
	}

	{
		// rise 中は α に比例して倍率が 1.0 から補間される
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Rise start Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Rise mid Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.75f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Hold Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierClampRulesTest,
                                 "KawaiiPhysics.SettingsMultiplier.ClampRules",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierClampRulesTest::RunTest(const FString& Parameters)
{
	// 各設定の上限と下限、および LimitAngle の無制限と極小値を守る。
	bool bOk = true;

	{
		// 0..1 クランプ対象は倍率で 1.0 を超えない
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.Stiffness = 0.9f;

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 1.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestFloatNear(*this, TEXT("Damping clamped"), Accessor.Bone(1).PhysicsSettings.Damping, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Stiffness clamped"), Accessor.Bone(1).PhysicsSettings.Stiffness, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingLocation clamped"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingLocation, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingRotation clamped"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingRotation, 1.0f);
		// Radius は上限クランプ無し
		bOk &= TestFloatNear(*this, TEXT("Radius unclamped"), Accessor.Bone(1).PhysicsSettings.Radius, 15.0f);
	}

	{
		// 倍率 0 は下限側へ落ちる（LimitAngle のみ極小値で止まる）
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestFloatNear(*this, TEXT("Damping zero"), Accessor.Bone(1).PhysicsSettings.Damping, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Stiffness zero"), Accessor.Bone(1).PhysicsSettings.Stiffness, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingLocation zero"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingLocation, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingRotation zero"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingRotation, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Radius zero"), Accessor.Bone(1).PhysicsSettings.Radius, 0.0f);
		bOk &= TestTrue(TEXT("LimitAngle stays positive"), Accessor.Bone(1).PhysicsSettings.LimitAngle > 0.0f);
	}

	{
		// ベース 0（制限なし）は倍率に関わらず 0 のまま
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 0.0f;

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 5.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestTrue(TEXT("Unlimited stays exactly zero"),
		                Accessor.Bone(1).PhysicsSettings.LimitAngle == 0.0f);
	}

	{
		// ベース > 0 は倍率 0 でも 0 へ反転しない
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 30.0f;

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestTrue(TEXT("Limited never becomes unlimited"),
		                Accessor.Bone(1).PhysicsSettings.LimitAngle != 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Clamped to tiny value"), Accessor.Bone(1).PhysicsSettings.LimitAngle,
		                     KINDA_SMALL_NUMBER);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierRestoreTest,
                                 "KawaiiPhysics.SettingsMultiplier.Restore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierRestoreTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	Accessor.CallUpdatePhysicsSettings();
	bool bOk = TestBoneSettings(*this, TEXT("Before override"), Accessor.Bone(1), Base);

	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f, 0.5f, 0.0f);

	bOk &= TestTrue(TEXT("Active at start"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestTrue(TEXT("Active mid"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.25f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping mid"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestFalse(TEXT("Expired"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.25f));
	bOk &= TestEqual(TEXT("No items left"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("After override"), Accessor.Bone(1), Base);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStackingTest,
                                 "KawaiiPhysics.SettingsMultiplier.Stacking",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStackingTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	// α=1 で維持される側
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f);
	bool bOk = TestTrue(TEXT("First active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));

	// rise 途中で α=0.5 になる側
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 10.0f, 0.0f);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
	bOk &= TestEqual(TEXT("Both active"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 2);

	const FKawaiiPhysicsSettingsMultiplier Effective = Accessor.CallComputeEffectiveSettingsMultiplierScale();
	// Damping: Lerp(1, 0.5, 1) * Lerp(1, 0, 0.5) = 0.5 * 0.5
	bOk &= TestFloatNear(*this, TEXT("Effective Damping"), Effective.Damping, 0.25f);
	// Stiffness: Lerp(1, 0.5, 1) * Lerp(1, 0.5, 0.5) = 0.5 * 0.75
	bOk &= TestFloatNear(*this, TEXT("Effective Stiffness"), Effective.Stiffness, 0.375f);
	bOk &= TestFloatNear(*this, TEXT("Effective Radius"), Effective.Radius, 1.0f);

	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Stacked Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.25f);
	bOk &= TestFloatNear(*this, TEXT("Stacked Stiffness"), Accessor.Bone(1).PhysicsSettings.Stiffness, 0.2f * 0.375f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierPendingBoundedTest,
                                 "KawaiiPhysics.SettingsMultiplier.PendingBounded",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierPendingBoundedTest::RunTest(const FString& Parameters)
{
	// 未消費の要求と取り込み済みの倍率に上限を適用し、種別をまたぐ最古の項目を破棄する。
	// 無期限保持の破棄では警告を一度だけ出す。
	bool bOk = true;
	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 12; ++Index)
		{
			Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, Index);
		}
		bOk &= TestEqual(TEXT("Pending starts bounded"), GetPendingOverrideCount(Node),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		bOk &= TestEqual(TEXT("Oldest pending start dropped"), GetPendingOverrideHandle(Node, 0), static_cast<int64>(5));
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("Consumed pending starts"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
	}
	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 100 + Index);
			Node.RequestStopPhysicsSettingsMultiplier(1000 + Index, 0.1f);
		}
		bOk &= TestTrue(TEXT("Pending pushes bounded"),
		                GetPendingOverrideSetCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		bOk &= TestTrue(TEXT("Pending stops bounded"),
		                GetPendingOverrideStopCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
	}
	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 0; Index < FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers; ++Index)
		{
			Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 100 + Index);
		}
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, 999);
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("Mixed item cap"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		bOk &= TestFalse(TEXT("Oldest driven item evicted"), ContainsSettingsMultiplierHandle(Node, 100));
		bOk &= TestTrue(TEXT("Timed item kept"), ContainsSettingsMultiplierHandle(Node, 999));
	}
	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 0; Index < FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers; ++Index)
		{
			Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 0.0f, 0.5f,
			                                           100 + Index, true);
		}
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		AddExpectedError(TEXT("Infinite-hold physics settings multiplier cap exceeded"), EAutomationExpectedErrorFlags::Contains, 1);
		Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 0.0f, 0.5f, 999, true);
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("Infinite item cap"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		bOk &= TestFalse(TEXT("Oldest infinite evicted"), ContainsSettingsMultiplierHandle(Node, 100));
		bOk &= TestTrue(TEXT("Newest infinite kept"), ContainsSettingsMultiplierHandle(Node, 999));
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStopImmediateTest,
                                 "KawaiiPhysics.SettingsMultiplier.StopImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStopImmediateTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f, 10.0f, 0.0f, 111);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Accessor.CallUpdatePhysicsSettings();
	bool bOk = TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	Accessor.Node.RequestStopPhysicsSettingsMultiplier(111, 0.0f);
	bOk &= TestEqual(TEXT("Pending stop queued"), GetPendingOverrideStopCount(Accessor.Node), 1);

	bOk &= TestFalse(TEXT("Inactive after stop"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Item removed"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);

	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("Restored"), Accessor.Bone(1), MakeBaseSettings());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStopBlendOutTest,
                                 "KawaiiPhysics.SettingsMultiplier.StopBlendOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStopBlendOutTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// α=1 から線形にフェードアウトする
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f,
		                                             222);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

		Accessor.Node.RequestStopPhysicsSettingsMultiplier(222, 1.0f);
		bOk &= TestTrue(TEXT("Still active on stop frame"),
		                Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Fade start Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Fade mid Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.75f);

		bOk &= TestFalse(TEXT("Fade finished"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Fade end Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);
	}

	{
		// rise 途中で停止した場合は、その時点の適用率を起点にフェードする（跳ね上がらない）
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 10.0f, 0.0f,
		                                             333);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);

		Accessor.Node.RequestStopPhysicsSettingsMultiplier(333, 1.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
		{
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha from rise"),
			                     Accessor.Node.TransientForceStore.SettingsMultiplierItems[0].PeakAlpha, 0.5f);
		}
		Accessor.CallUpdatePhysicsSettings();
		// 停止直前と同じ Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Continuous at stop"), Accessor.Bone(1).PhysicsSettings.Damping,
		                     0.4f * 0.75f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// α = 0.5 * (1 - 0.5) = 0.25 → Lerp(1.0, 0.5, 0.25) = 0.875
		bOk &= TestFloatNear(*this, TEXT("Fade from peak"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.875f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierGatingRestoreTest,
                                 "KawaiiPhysics.SettingsMultiplier.GatingRestore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierGatingRestoreTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.bUpdatePhysicsSettingsInGame = false;
	Accessor.SetInitPhysicsSettings(false);

	// 初回だけは未初期化なので走る
	bool bOk = TestTrue(TEXT("First update runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestFloatNear(*this, TEXT("Base Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);

	// 倍率が無ければ以降は走らない（外部から書き換えた値が残ることで確認する）
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Idle update skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));
	bOk &= TestFloatNear(*this, TEXT("Idle value untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0.1f, 0.0f);

	bOk &= TestTrue(TEXT("Override frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestTrue(TEXT("Applied flag set"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestTrue(TEXT("Override mid frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping mid"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	// 期限切れフレームは consume が false でも1回だけ走ってベース値へ戻る
	bOk &= TestTrue(TEXT("Restore frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFalse(TEXT("Applied flag cleared"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Restored"), Accessor.Bone(1), MakeBaseSettings());

	// 復元後は再び走らない
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Post-restore update skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFloatNear(*this, TEXT("Post-restore value untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenSetCreateAndUpdateTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenSetCreateAndUpdate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenSetCreateAndUpdateTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	const FKawaiiPhysicsSettingsMultiplier Scale1 = MakeScale(0.5f, 0.25f, 0.5f, 0.5f, 2.0f, 0.5f);
	const FKawaiiPhysicsSettingsMultiplier Scale2 = MakeScale(0.25f, 0.5f, 0.75f, 0.8f, 1.5f, 0.75f);

	bool bOk = TestTrue(TEXT("Request set"), Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale1, 0.5f, 7));
	bOk &= TestTrue(TEXT("Consume set"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Item count"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestTrue(TEXT("Driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("DrivenAlpha"), Item.DrivenAlpha, 0.5f);
		bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
	}
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Damping half alpha"), Accessor.Bone(1).PhysicsSettings.Damping,
	                     Base.Damping * FMath::Lerp(1.0f, Scale1.Damping, 0.5f));

	bOk &= TestTrue(TEXT("Request update"), Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale2, 1.0f, 7));
	bOk &= TestTrue(TEXT("Consume update"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Updated item count"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFloatNear(*this, TEXT("Updated Damping scale"), Item.Scale.Damping, Scale2.Damping);
		bOk &= TestFloatNear(*this, TEXT("Updated alpha"), Item.DrivenAlpha, 1.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenAlphaClampTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenAlphaClamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenAlphaClampTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 0.5f);

	bool bOk = TestTrue(TEXT("Request alpha high"), Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale, 2.0f, 7));
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	bOk &= TestFloatNear(*this, TEXT("Alpha clamped high"),
	                     Accessor.Node.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha, 1.0f);

	bOk &= TestTrue(TEXT("Request alpha low"), Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale, -1.0f, 7));
	bOk &= TestTrue(TEXT("Alpha zero remains active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestFloatNear(*this, TEXT("Alpha clamped low"),
	                     Accessor.Node.TransientForceStore.SettingsMultiplierItems[0].DrivenAlpha, 0.0f);
	const FKawaiiPhysicsSettingsMultiplier Effective = Accessor.CallComputeEffectiveSettingsMultiplierScale();
	bOk &= TestFloatNear(*this, TEXT("Effective Damping identity"), Effective.Damping, 1.0f);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("Alpha zero base"), Accessor.Bone(1), MakeBaseSettings());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenNoExpiryTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenNoExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenNoExpiryTest::RunTest(const FString& Parameters)
{
	// 既定の lease=0 では再 Push がなくても driven 状態を保つ。
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);

	bool bOk = true;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		bOk &= TestTrue(*FString::Printf(TEXT("Consume %d active"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsMultipliers(100.0f));
		bOk &= TestEqual(*FString::Printf(TEXT("Consume %d item count"), Index),
		                 Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	}
	bOk &= TestEqual(TEXT("Default lease evaluations"),
	                 Node.TransientForceStore.SettingsMultiplierItems[0].LeaseEvaluations, 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenStopBlendOutTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenStopBlendOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenStopBlendOutTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.6f, 7);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(7, 1.0f);

	bool bOk = TestTrue(TEXT("Stop consume active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("No longer driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Peak from driven alpha"), Item.PeakAlpha, 0.6f);
		bOk &= TestFloatNear(*this, TEXT("Rise zero"), Item.RiseTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Hold zero"), Item.HoldTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Decay one"), Item.DecayTime, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Elapsed zero"), Item.ElapsedTime, 0.0f);
	}
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Damping at stop"), Accessor.Bone(1).PhysicsSettings.Damping,
	                     Base.Damping * FMath::Lerp(1.0f, Scale.Damping, 0.6f));

	bOk &= TestTrue(TEXT("Half fade active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Damping half fade"), Accessor.Bone(1).PhysicsSettings.Damping,
	                     Base.Damping * FMath::Lerp(1.0f, Scale.Damping, 0.3f));

	bOk &= TestFalse(TEXT("Fade completed"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	bOk &= TestEqual(TEXT("Item removed"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenSetSupersedesPendingStopTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenSetSupersedesPendingStop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenSetSupersedesPendingStopTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestStopPhysicsSettingsMultiplier(7, 1.0f);
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);

	bool bOk = TestTrue(TEXT("Consume driven"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Driven item"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierSetRemovesPendingStartTest,
                                 "KawaiiPhysics.SettingsMultiplier.SetRemovesPendingStart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierSetRemovesPendingStartTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, 7);
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);

	bool bOk = TestTrue(TEXT("Consume driven after pending start"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("One driven item after pending start"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		bOk &= TestTrue(TEXT("Pending start replaced by driven item"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenLeaseTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenLease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenLeaseTest::RunTest(const FString& Parameters)
{
	// 再 Push で期限を延ばし、Push が止まった後は現在の強さからフェードする。
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	FAnimNode_KawaiiPhysics& Node = Accessor.Node;
	const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
	Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.6f, 7, 2, 1.0f);
	bool bOk = TestTrue(TEXT("Lease first consume"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Lease initially driven"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	bOk &= TestTrue(TEXT("Lease survives one evaluation"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.6f, 7, 2, 1.0f);
	bOk &= TestTrue(TEXT("Lease refresh consume"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Lease refresh remains driven"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	bOk &= TestTrue(TEXT("Lease survives after refresh"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Lease still driven after one missed push"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	bOk &= TestTrue(TEXT("Lease expiry fades"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestFalse(TEXT("Lease no longer driven"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Lease fade starts continuously"), Accessor.Bone(1).PhysicsSettings.Damping, 0.28f);
	bOk &= TestTrue(TEXT("Lease fade mid active"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Lease fade mid damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.34f);
	bOk &= TestFalse(TEXT("Lease fade completes"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenReinitClearsTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenReinitClears",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenReinitClearsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Accessor.Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 8);
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, 9);
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(10, 0.5f);
	Accessor.SetPhysicsSettingsMultiplierAppliedLastUpdate(true);

	Accessor.CallResetTransientRuntimeState();

	bool bOk = TestEqual(TEXT("Reinit items clear"), Accessor.Node.TransientForceStore.Items.Num(), 0);
	bOk &= TestEqual(TEXT("Reinit settings items clear"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	bOk &= TestFalse(TEXT("Reinit consumes no pending multiplier"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Reinit leaves no settings items after consume"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	bOk &= TestFalse(TEXT("Reinit applied flag clear"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierHandleMismatchNoopTest,
                                 "KawaiiPhysics.SettingsMultiplier.HandleMismatchNoop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierHandleMismatchNoopTest::RunTest(const FString& Parameters)
{
	// 異なる handle と 0 の Push/Stop が既存倍率を変えないことを守る。
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 100);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	// 失効ハンドルへの停止は何もしない
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(999, 0.5f);
	bool bOk = TestTrue(TEXT("Still active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	bOk &= TestEqual(TEXT("Item kept"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Still scaled"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	// ハンドル 0 の Push と Stop はどちらも無視する。
	bOk &= TestFalse(TEXT("Zero handle push rejected"),
	                 Accessor.Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 0));
	bOk &= TestEqual(TEXT("Zero handle push not queued"), GetPendingOverrideSetCount(Accessor.Node), 0);
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(0, 0.5f);
	bOk &= TestEqual(TEXT("Zero handle ignored"), GetPendingOverrideStopCount(Accessor.Node), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStartRequestBuilderTest,
                                 "KawaiiPhysics.SettingsMultiplier.StartRequestBuilder",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStartRequestBuilderTest::RunTest(const FString& Parameters)
{
	// 負の継続時間、ゼロ、正の継続時間で生成結果を確認する。
	bool bOk = true;
	{
		const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 0.25f, 0.75f, 0.9f, 2.0f, 0.5f);
		FKawaiiPhysicsSettingsMultiplierRequest Request;
		bOk &= TestTrue(TEXT("Negative duration built"),
		                UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(Scale, -1.0f, 0.2f, 0.5f, Request));
		bOk &= TestTrue(TEXT("Negative duration infinite hold"), Request.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Negative duration rise"), Request.RiseTime, 0.2f);
		bOk &= TestFloatNear(*this, TEXT("Negative duration hold"), Request.HoldTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Negative duration decay"), Request.DecayTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Negative duration scale copied"), Request.Scale.Damping, 0.5f);
		bOk &= TestEqual(TEXT("Negative duration handle untouched"), Request.HandleId, static_cast<int64>(0));
	}
	{
		FKawaiiPhysicsSettingsMultiplierRequest Request;
		bOk &= TestFalse(TEXT("Zero duration rejected"),
		                 UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(FKawaiiPhysicsSettingsMultiplier(),
		                                                                            0.0f, 0.2f, 0.5f, Request));
	}
	{
		FKawaiiPhysicsSettingsMultiplierRequest Request;
		bOk &= TestTrue(TEXT("Positive duration built"),
		                UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(FKawaiiPhysicsSettingsMultiplier(),
		                                                                           2.0f, 0.2f, 0.5f, Request));
		bOk &= TestFalse(TEXT("Positive duration finite"), Request.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Positive duration rise"), Request.RiseTime, 0.2f);
		bOk &= TestFloatNear(*this, TEXT("Positive duration hold"), Request.HoldTime, 1.3f);
		bOk &= TestFloatNear(*this, TEXT("Positive duration decay"), Request.DecayTime, 0.5f);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldPersistsTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldPersists",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldPersistsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f),
	                                                     0.2f, 0.0f, 0.5f, 7, true);

	bool bOk = TestTrue(TEXT("Initial active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	const bool bStillActive = Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(100.0f);

	bOk &= TestTrue(TEXT("Still active after long run"), bStillActive);
	bOk &= TestEqual(TEXT("Item count"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		const FKawaiiPhysicsSettingsMultiplier Effective = Accessor.CallComputeEffectiveSettingsMultiplierScale();
		bOk &= TestTrue(TEXT("Infinite flag"), Item.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Held alpha"), 1.0f - Effective.Damping, Item.PeakAlpha);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopUsesStopBlendOutTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldStopUsesStopBlendOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopUsesStopBlendOutTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	bool bOk = true;

	FKawaiiPhysicsSettingsMultiplierRequest Request;
	bOk &= TestTrue(TEXT("Built"),
	                UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(
		                MakeScale(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), -1.0f, 0.0f, 2.0f, Request));
	Request.HandleId = 7;

	const int64 ReturnedHandle = Accessor.Node.RequestStartPhysicsSettingsMultiplier(
		Request.Scale, Request.RiseTime, Request.HoldTime, Request.DecayTime, Request.HandleId, Request.bInfiniteHold);
	bOk &= TestEqual(TEXT("Handle returned"), ReturnedHandle, static_cast<int64>(7));
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f / 60.0f);
	}

	Accessor.Node.RequestStopPhysicsSettingsMultiplier(Request.HandleId, 0.5f);
	bOk &= TestTrue(TEXT("Stop converted"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Decay mid active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.25f));
	const FKawaiiPhysicsSettingsMultiplier EffectiveMid = Accessor.CallComputeEffectiveSettingsMultiplierScale();
	bOk &= TestTrue(TEXT("Mid alpha uses Stop BlendOutTime"),
	                FMath::IsNearlyEqual(1.0f - EffectiveMid.Damping, 0.5f, 0.05f));
	bOk &= TestFalse(TEXT("Decay finished"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.3f));
	bOk &= TestEqual(TEXT("Removed"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopMidRiseCapturesPeakTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldStopMidRiseCapturesPeak",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopMidRiseCapturesPeakTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	bool bOk = true;

	FKawaiiPhysicsSettingsMultiplierRequest Request;
	bOk &= TestTrue(TEXT("Built"),
	                UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(
		                MakeScale(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), -1.0f, 1.0f, 1.0f, Request));
	Request.HandleId = 7;

	const int64 ReturnedHandle = Accessor.Node.RequestStartPhysicsSettingsMultiplier(
		Request.Scale, Request.RiseTime, Request.HoldTime, Request.DecayTime, Request.HandleId, Request.bInfiniteHold);
	bOk &= TestEqual(TEXT("Handle returned"), ReturnedHandle, static_cast<int64>(7));
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);

	Accessor.Node.RequestStopPhysicsSettingsMultiplier(Request.HandleId, 1.0f);
	bOk &= TestTrue(TEXT("Stop converted"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Infinite cleared"), Item.bInfiniteHold);
		bOk &= TestTrue(TEXT("Peak captured"),
		                FMath::IsNearlyEqual(Item.PeakAlpha, 0.5f, 0.0001f));
		bOk &= TestFloatNear(*this, TEXT("Rise"), Item.RiseTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Hold"), Item.HoldTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Decay"), Item.DecayTime, 1.0f);
	}

	bOk &= TestFalse(TEXT("Decay finished"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f));
	bOk &= TestEqual(TEXT("Removed"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldReplacedByPushTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldReplacedByPush",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldReplacedByPushTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(Scale, 0.0f, 0.0f, 0.5f, 7, true);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Accessor.Node.RequestPushPhysicsSettingsMultiplier(Scale, 0.3f, 7);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	bool bOk = TestEqual(TEXT("One item"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		const FKawaiiPhysicsSettingsMultiplier Effective = Accessor.CallComputeEffectiveSettingsMultiplierScale();
		bOk &= TestTrue(TEXT("Driven"), Item.bExternallyDriven);
		bOk &= TestFalse(TEXT("Infinite cleared"), Item.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Driven alpha"), Item.DrivenAlpha, 0.3f);
		bOk &= TestFloatNear(*this, TEXT("Effective alpha"), 1.0f - Effective.Damping, 0.3f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldReplacedByFiniteStartTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldReplacedByFiniteStart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldReplacedByFiniteStartTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 0.0f, 0.5f, 7, true);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	const KawaiiPhysics::FWindGustEnvelope Envelope = KawaiiPhysics::ResolveWindGustEnvelope(1.0f, 0.2f, 0.3f);
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), Envelope.RiseTime, Envelope.HoldTime,
	                                           Envelope.DecayTime, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	bool bOk = TestEqual(TEXT("One item"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Infinite cleared"), Item.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Rise"), Item.RiseTime, Envelope.RiseTime);
		bOk &= TestFloatNear(*this, TEXT("Hold"), Item.HoldTime, Envelope.HoldTime);
		bOk &= TestFloatNear(*this, TEXT("Decay"), Item.DecayTime, Envelope.DecayTime);
	}

	bOk &= TestFalse(TEXT("Expired after finite duration"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f));
	bOk &= TestEqual(TEXT("Removed"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	return bOk;
}

#endif
