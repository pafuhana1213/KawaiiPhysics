// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsTestHarness.h"

#include "UObject/UnrealType.h"

namespace
{
constexpr float GSettingsOverrideTol = 0.000001f;

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GSettingsOverrideTol));
}

int32 GetPendingOverrideCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrides.Num();
}

int64 GetPendingOverrideHandle(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrides.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsOverrides[Index].HandleId
		       : 0;
}

int32 GetPendingOverrideStopCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrideStops.Num();
}

float GetPendingOverrideStopBlendOutTime(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0.0f;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrideStops.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsOverrideStops[Index].BlendOutTime
		       : 0.0f;
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

FKawaiiPhysicsSettingsScale MakeScale(const float Damping, const float Stiffness, const float WorldDampingLocation,
                                      const float WorldDampingRotation, const float Radius, const float LimitAngle)
{
	FKawaiiPhysicsSettingsScale Scale;
	Scale.Damping = Damping;
	Scale.Stiffness = Stiffness;
	Scale.WorldDampingLocation = WorldDampingLocation;
	Scale.WorldDampingRotation = WorldDampingRotation;
	Scale.Radius = Radius;
	Scale.LimitAngle = LimitAngle;
	return Scale;
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

// プリセット適用と同じ経路でノードのUPROPERTYを一括コピーする（非UPROPERTYの一時状態が巻き込まれないかの確認用）
void ApplyDefaultPresetStyleCopy(FAnimNode_KawaiiPhysics& TargetNode)
{
	FAnimNode_KawaiiPhysics DefaultNode;
	const FKawaiiPhysicsPresetApplyOptions Options;

	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		if (!UKawaiiPhysicsPresetDataAsset::ShouldApplyNodeProperty(Property, Options))
		{
			continue;
		}

		const FName PropertyName = Property.GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces))
		{
			continue;
		}

		Property.CopyCompleteValue_InContainer(&TargetNode, &DefaultNode);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideEnvelopeAlphaTest,
                                 "KawaiiPhysics.SettingsOverride.EnvelopeAlpha",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideEnvelopeAlphaTest::RunTest(const FString& Parameters)
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

	// rise のみ / hold のみ / decay のみ
	bOk &= TestFloatNear(*this, TEXT("Rise only mid"), KawaiiPhysics::EvaluateEnvelopeAlpha01(1.0f, 0.0f, 0.0f, 0.5f),
	                     0.5f);
	bOk &= TestFloatNear(*this, TEXT("Rise only end"), KawaiiPhysics::EvaluateEnvelopeAlpha01(1.0f, 0.0f, 0.0f, 1.0f),
	                     0.0f);
	bOk &= TestFloatNear(*this, TEXT("Hold only start"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.0f, 1.0f, 0.0f, 0.0f),
	                     1.0f);
	bOk &= TestFloatNear(*this, TEXT("Hold only end"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.0f, 1.0f, 0.0f, 1.0f),
	                     0.0f);
	bOk &= TestFloatNear(*this, TEXT("Decay only start"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.0f, 0.0f, 1.0f, 0.0f),
	                     1.0f);
	bOk &= TestFloatNear(*this, TEXT("Decay only mid"), KawaiiPhysics::EvaluateEnvelopeAlpha01(0.0f, 0.0f, 1.0f, 0.25f),
	                     0.75f);

	// 負の区間長は 0 として扱う
	bOk &= TestFloatNear(*this, TEXT("Negative rise/decay"),
	                     KawaiiPhysics::EvaluateEnvelopeAlpha01(-1.0f, 1.0f, -1.0f, 0.5f), 1.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideRequestConsumeTest,
                                 "KawaiiPhysics.SettingsOverride.RequestConsume",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideRequestConsumeTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		const FKawaiiPhysicsSettingsScale Scale = MakeScale(0.5f, 0.25f, 0.5f, 0.5f, 2.0f, 0.0f);
		const int64 Handle = Node.RequestPhysicsSettingsOverride(Scale, 0.2f, 1.0f, 0.5f);

		bOk &= TestTrue(TEXT("Generated handle"), Handle > 0);
		bOk &= TestEqual(TEXT("Pending before consume"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Items before consume"), Node.TransientForceStore.SettingsOverrideItems.Num(), 0);

		bOk &= TestTrue(TEXT("Consume reports active"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
		bOk &= TestEqual(TEXT("Pending after consume"), GetPendingOverrideCount(Node), 0);
		bOk &= TestEqual(TEXT("Items after consume"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);

		if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
		{
			const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
			bOk &= TestEqual(TEXT("Stamped handle"), Item.HandleId, Handle);
			bOk &= TestFloatNear(*this, TEXT("RiseTime"), Item.RiseTime, 0.2f);
			bOk &= TestFloatNear(*this, TEXT("HoldTime"), Item.HoldTime, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("DecayTime"), Item.DecayTime, 0.5f);
			bOk &= TestFloatNear(*this, TEXT("ElapsedTime"), Item.ElapsedTime, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("Scale.Damping"), Item.Scale.Damping, 0.5f);
			bOk &= TestFloatNear(*this, TEXT("Scale.LimitAngle"), Item.Scale.LimitAngle, 0.0f);
		}
	}

	{
		// 明示ハンドルはそのまま採用される
		FAnimNode_KawaiiPhysics Node;
		const int64 Returned = Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 1.0f, 0.0f, 4242);
		bOk &= TestEqual(TEXT("Explicit handle returned"), Returned, static_cast<int64>(4242));
		Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		bOk &= TestEqual(TEXT("Explicit handle stamped"),
		                 Node.TransientForceStore.SettingsOverrideItems[0].HandleId, static_cast<int64>(4242));
	}

	{
		// Duration<=0 相当（全区間0）は初回consumeで即消滅する
		FAnimNode_KawaiiPhysics Node;
		Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0.0f, 0.0f);
		bOk &= TestFalse(TEXT("Zero envelope is inactive"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
		bOk &= TestEqual(TEXT("Zero envelope leaves no item"), Node.TransientForceStore.SettingsOverrideItems.Num(), 0);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideApplyScaleTest,
                                 "KawaiiPhysics.SettingsOverride.ApplyScale",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideApplyScaleTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 0.25f), 0.0f, 1.0f, 0.0f);
		bOk &= TestTrue(TEXT("Active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
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

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Rise start Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Rise mid Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.75f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Hold Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideClampRulesTest,
                                 "KawaiiPhysics.SettingsOverride.ClampRules",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideClampRulesTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// 0..1 クランプ対象は倍率で 1.0 を超えない
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.Stiffness = 0.9f;

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(5.0f, 5.0f, 5.0f, 5.0f, 1.0f, 1.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestFloatNear(*this, TEXT("Damping clamped"), Accessor.Bone(1).PhysicsSettings.Damping, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Stiffness clamped"), Accessor.Bone(1).PhysicsSettings.Stiffness, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingLocation clamped"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingLocation, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("WorldDampingRotation clamped"),
		                     Accessor.Bone(1).PhysicsSettings.WorldDampingRotation, 1.0f);
		// Radius は上限クランプ無し
		bOk &= TestFloatNear(*this, TEXT("Radius unclamped"), Accessor.Bone(1).PhysicsSettings.Radius, 3.0f);
	}

	{
		// 倍率 0 は下限側へ落ちる（LimitAngle のみ極小値で止まる）
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
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

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideRestoreTest,
                                 "KawaiiPhysics.SettingsOverride.Restore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideRestoreTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	Accessor.CallUpdatePhysicsSettings();
	bool bOk = TestBoneSettings(*this, TEXT("Before override"), Accessor.Bone(1), Base);

	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f, 0.5f, 0.0f);

	bOk &= TestTrue(TEXT("Active at start"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestTrue(TEXT("Active mid"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.25f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping mid"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestFalse(TEXT("Expired"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.25f));
	bOk &= TestEqual(TEXT("No items left"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 0);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("After override"), Accessor.Bone(1), Base);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideStackingTest,
                                 "KawaiiPhysics.SettingsOverride.Stacking",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideStackingTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	// α=1 で維持される側
	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f);
	bool bOk = TestTrue(TEXT("First active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));

	// rise 途中で α=0.5 になる側
	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 10.0f, 0.0f);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
	bOk &= TestEqual(TEXT("Both active"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 2);

	const FKawaiiPhysicsSettingsScale Effective = Accessor.CallComputeEffectiveSettingsOverrideScale();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideCapEvictionTest,
                                 "KawaiiPhysics.SettingsOverride.CapEviction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideCapEvictionTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// 取り込み済みが上限を超えたら最古から破棄する
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 5; ++Index)
		{
			Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, Index);
		}
		Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		bOk &= TestEqual(TEXT("First batch"), Node.TransientForceStore.SettingsOverrideItems.Num(), 5);

		for (int32 Index = 6; Index <= 9; ++Index)
		{
			Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, Index);
		}
		Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

		bOk &= TestEqual(TEXT("Capped item count"), Node.TransientForceStore.SettingsOverrideItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
		for (int32 Index = 0; Index < Node.TransientForceStore.SettingsOverrideItems.Num(); ++Index)
		{
			bOk &= TestEqual(FString::Printf(TEXT("Remaining handle %d"), Index),
			                 Node.TransientForceStore.SettingsOverrideItems[Index].HandleId,
			                 static_cast<int64>(Index + 2));
		}
	}

	{
		// 評価が走らない間の連打でも pending が上限を超えない
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 12; ++Index)
		{
			Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, Index);
		}

		bOk &= TestEqual(TEXT("Pending bounded"), GetPendingOverrideCount(Node),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
		bOk &= TestEqual(TEXT("Oldest pending dropped"), GetPendingOverrideHandle(Node, 0), static_cast<int64>(5));

		Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		bOk &= TestEqual(TEXT("Consumed pending"), Node.TransientForceStore.SettingsOverrideItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideStopImmediateTest,
                                 "KawaiiPhysics.SettingsOverride.StopImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideStopImmediateTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f), 0.0f, 10.0f, 0.0f, 111);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Accessor.CallUpdatePhysicsSettings();
	bool bOk = TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	Accessor.Node.RequestStopPhysicsSettingsOverride(111, 0.0f);
	bOk &= TestEqual(TEXT("Pending stop queued"), GetPendingOverrideStopCount(Accessor.Node), 1);

	bOk &= TestFalse(TEXT("Inactive after stop"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Item removed"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 0);

	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("Restored"), Accessor.Bone(1), MakeBaseSettings());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideStopBlendOutTest,
                                 "KawaiiPhysics.SettingsOverride.StopBlendOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideStopBlendOutTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// α=1 から線形にフェードアウトする
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f,
		                                             222);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

		Accessor.Node.RequestStopPhysicsSettingsOverride(222, 1.0f);
		bOk &= TestTrue(TEXT("Still active on stop frame"),
		                Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
		if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
		{
			const FKawaiiPhysicsActiveSettingsOverride& Item = Accessor.Node.TransientForceStore.SettingsOverrideItems[0];
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("RiseTime cleared"), Item.RiseTime, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("HoldTime cleared"), Item.HoldTime, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("DecayTime replaced"), Item.DecayTime, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("ElapsedTime reset"), Item.ElapsedTime, 0.0f);
		}
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Fade start Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Fade mid Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.75f);

		bOk &= TestFalse(TEXT("Fade finished"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f));
		Accessor.CallUpdatePhysicsSettings();
		bOk &= TestFloatNear(*this, TEXT("Fade end Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);
	}

	{
		// rise 途中で停止した場合は、その時点の適用率を起点にフェードする（跳ね上がらない）
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 10.0f, 0.0f,
		                                             333);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);

		Accessor.Node.RequestStopPhysicsSettingsOverride(333, 1.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
		{
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha from rise"),
			                     Accessor.Node.TransientForceStore.SettingsOverrideItems[0].PeakAlpha, 0.5f);
		}
		Accessor.CallUpdatePhysicsSettings();
		// 停止直前と同じ Lerp(1.0, 0.5, 0.5) = 0.75
		bOk &= TestFloatNear(*this, TEXT("Continuous at stop"), Accessor.Bone(1).PhysicsSettings.Damping,
		                     0.4f * 0.75f);

		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);
		Accessor.CallUpdatePhysicsSettings();
		// α = 0.5 * (1 - 0.5) = 0.25 → Lerp(1.0, 0.5, 0.25) = 0.875
		bOk &= TestFloatNear(*this, TEXT("Fade from peak"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f * 0.875f);
	}

	{
		// 同一ハンドルへの停止要求は pending 内で BlendOutTime を上書きする
		FAnimNode_KawaiiPhysics Node;
		Node.RequestStopPhysicsSettingsOverride(444, 0.1f);
		Node.RequestStopPhysicsSettingsOverride(444, 0.2f);
		Node.RequestStopPhysicsSettingsOverride(444, 0.3f);

		bOk &= TestEqual(TEXT("Coalesced stops"), GetPendingOverrideStopCount(Node), 1);
		bOk &= TestFloatNear(*this, TEXT("Coalesced BlendOutTime"), GetPendingOverrideStopBlendOutTime(Node, 0), 0.3f);

		for (int32 Index = 0; Index < 12; ++Index)
		{
			Node.RequestStopPhysicsSettingsOverride(1000 + Index, 0.1f);
		}
		bOk &= TestTrue(TEXT("Pending stops bounded"),
		                GetPendingOverrideStopCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideGatingRestoreTest,
                                 "KawaiiPhysics.SettingsOverride.GatingRestore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideGatingRestoreTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.bUpdatePhysicsSettingsInGame = false;
	Accessor.SetInitPhysicsSettings(false);

	// 初回だけは未初期化なので走る
	bool bOk = TestTrue(TEXT("First update runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestFloatNear(*this, TEXT("Base Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.4f);

	// オーバーライドが無ければ以降は走らない（外部から書き換えた値が残ることで確認する）
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Idle update skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));
	bOk &= TestFloatNear(*this, TEXT("Idle value untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 0.1f, 0.0f);

	bOk &= TestTrue(TEXT("Override frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestTrue(TEXT("Applied flag set"), Accessor.IsPhysicsSettingsOverrideAppliedLastUpdate());
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	bOk &= TestTrue(TEXT("Override mid frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFloatNear(*this, TEXT("Scaled Damping mid"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	// 期限切れフレームは consume が false でも1回だけ走ってベース値へ戻る
	bOk &= TestTrue(TEXT("Restore frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFalse(TEXT("Applied flag cleared"), Accessor.IsPhysicsSettingsOverrideAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Restored"), Accessor.Bone(1), MakeBaseSettings());

	// 復元後は再び走らない
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Post-restore update skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.05f));
	bOk &= TestFloatNear(*this, TEXT("Post-restore value untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideLimitAngleZeroSemanticsTest,
                                 "KawaiiPhysics.SettingsOverride.LimitAngleZeroSemantics",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideLimitAngleZeroSemanticsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// ベース 0（制限なし）は倍率に関わらず 0 のまま
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 0.0f;

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 5.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestTrue(TEXT("Unlimited stays exactly zero"),
		                Accessor.Bone(1).PhysicsSettings.LimitAngle == 0.0f);
	}

	{
		// ベース > 0 は倍率 0 でも 0 へ反転しない
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 30.0f;

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestTrue(TEXT("Limited never becomes unlimited"),
		                Accessor.Bone(1).PhysicsSettings.LimitAngle != 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Clamped to tiny value"), Accessor.Bone(1).PhysicsSettings.LimitAngle,
		                     KINDA_SMALL_NUMBER);
	}

	{
		// 通常倍率は素直に乗算される
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 30.0f;

		Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestFloatNear(*this, TEXT("Scaled LimitAngle"), Accessor.Bone(1).PhysicsSettings.LimitAngle, 15.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideHandleMismatchNoopTest,
                                 "KawaiiPhysics.SettingsOverride.HandleMismatchNoop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideHandleMismatchNoopTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	Accessor.Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 100);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

	// 失効ハンドルへの停止は何もしない
	Accessor.Node.RequestStopPhysicsSettingsOverride(999, 0.5f);
	bool bOk = TestTrue(TEXT("Still active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f));
	bOk &= TestEqual(TEXT("Item kept"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Accessor.Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestFloatNear(*this, TEXT("HoldTime untouched"), Item.HoldTime, 10.0f);
		bOk &= TestFloatNear(*this, TEXT("DecayTime untouched"), Item.DecayTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("PeakAlpha untouched"), Item.PeakAlpha, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("ElapsedTime advanced"), Item.ElapsedTime, 0.5f);
	}

	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Still scaled"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	// ハンドル 0 はキューにも積まれない
	Accessor.Node.RequestStopPhysicsSettingsOverride(0, 0.5f);
	bOk &= TestEqual(TEXT("Zero handle ignored"), GetPendingOverrideStopCount(Accessor.Node), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideReflectionCopySurvivalTest,
                                 "KawaiiPhysics.SettingsOverride.ReflectionCopySurvival",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideReflectionCopySurvivalTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 11);
		Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		Node.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 22);
		Node.RequestStopPhysicsSettingsOverride(11, 0.25f);

		bOk &= TestEqual(TEXT("Initial items"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("Initial pending"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Initial pending stops"), GetPendingOverrideStopCount(Node), 1);

		ApplyDefaultPresetStyleCopy(Node);

		bOk &= TestEqual(TEXT("Items survive preset-style copy"),
		                 Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("Pending survives preset-style copy"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Pending stops survive preset-style copy"), GetPendingOverrideStopCount(Node), 1);
	}

	{
		// ノードのコピーは空のストアから始まり、二重消費しない
		FAnimNode_KawaiiPhysics A;
		A.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 33);
		A.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		A.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 44);

		FAnimNode_KawaiiPhysics B = A;
		bOk &= TestTrue(TEXT("Copied queue is distinct"),
		                A.TransientForceStore.Queue.Get() != B.TransientForceStore.Queue.Get());
		bOk &= TestEqual(TEXT("B items empty"), B.TransientForceStore.SettingsOverrideItems.Num(), 0);
		bOk &= TestEqual(TEXT("B pending empty"), GetPendingOverrideCount(B), 0);
		bOk &= TestFalse(TEXT("B consume yields nothing"), B.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));

		bOk &= TestEqual(TEXT("A items preserved"), A.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("A pending preserved"), GetPendingOverrideCount(A), 1);
	}

	return bOk;
}

#endif
