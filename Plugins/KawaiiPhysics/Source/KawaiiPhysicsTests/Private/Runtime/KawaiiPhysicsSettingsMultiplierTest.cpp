// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsLibrary.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsTypes.h"
#include "KawaiiPhysicsTestHarness.h"

#include "UObject/UnrealType.h"

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

float GetPendingOverrideStopBlendOutTime(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0.0f;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultiplierStops.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsMultiplierStops[Index].BlendOutTime
		       : 0.0f;
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

FKawaiiPhysicsSettingsMultiplierPushRequest GetPendingOverrideSet(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	FKawaiiPhysicsSettingsMultiplierPushRequest Request;
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return Request;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsMultiplierPushes.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsMultiplierPushes[Index]
		       : Request;
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
		bOk &= TestEqual(TEXT("Pending before consume"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Items before consume"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);

		bOk &= TestTrue(TEXT("Consume reports active"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		bOk &= TestEqual(TEXT("Pending after consume"), GetPendingOverrideCount(Node), 0);
		bOk &= TestEqual(TEXT("Items after consume"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);

		if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
		{
			const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
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
	bool bOk = true;

	{
		// 0..1 クランプ対象は倍率で 1.0 を超えない
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.Stiffness = 0.9f;

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(5.0f, 5.0f, 5.0f, 5.0f, 1.0f, 1.0f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierCapEvictionTest,
                                 "KawaiiPhysics.SettingsMultiplier.CapEviction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierCapEvictionTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		// 取り込み済みが上限を超えたら最古から破棄する
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 5; ++Index)
		{
			Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, Index);
		}
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("First batch"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 5);

		for (int32 Index = 6; Index <= 9; ++Index)
		{
			Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, Index);
		}
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

		bOk &= TestEqual(TEXT("Capped item count"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		for (int32 Index = 0; Index < Node.TransientForceStore.SettingsMultiplierItems.Num(); ++Index)
		{
			bOk &= TestEqual(FString::Printf(TEXT("Remaining handle %d"), Index),
			                 Node.TransientForceStore.SettingsMultiplierItems[Index].HandleId,
			                 static_cast<int64>(Index + 2));
		}
	}

	{
		// 評価が走らない間の連打でも pending が上限を超えない
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 12; ++Index)
		{
			Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, Index);
		}

		bOk &= TestEqual(TEXT("Pending bounded"), GetPendingOverrideCount(Node),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
		bOk &= TestEqual(TEXT("Oldest pending dropped"), GetPendingOverrideHandle(Node, 0), static_cast<int64>(5));

		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		bOk &= TestEqual(TEXT("Consumed pending"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
		                 FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
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
		if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
		{
			const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
			bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("RiseTime cleared"), Item.RiseTime, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("HoldTime cleared"), Item.HoldTime, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("DecayTime replaced"), Item.DecayTime, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("ElapsedTime reset"), Item.ElapsedTime, 0.0f);
		}
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

	{
		// 同一ハンドルへの停止要求は pending 内で BlendOutTime を上書きする
		FAnimNode_KawaiiPhysics Node;
		Node.RequestStopPhysicsSettingsMultiplier(444, 0.1f);
		Node.RequestStopPhysicsSettingsMultiplier(444, 0.2f);
		Node.RequestStopPhysicsSettingsMultiplier(444, 0.3f);

		bOk &= TestEqual(TEXT("Coalesced stops"), GetPendingOverrideStopCount(Node), 1);
		bOk &= TestFloatNear(*this, TEXT("Coalesced BlendOutTime"), GetPendingOverrideStopBlendOutTime(Node, 0), 0.3f);

		for (int32 Index = 0; Index < 12; ++Index)
		{
			Node.RequestStopPhysicsSettingsMultiplier(1000 + Index, 0.1f);
		}
		bOk &= TestTrue(TEXT("Pending stops bounded"),
		                GetPendingOverrideStopCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenSetCoalesceTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenSetCoalesce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenSetCoalesceTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestPushPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.25f, 7);
		Node.RequestPushPhysicsSettingsMultiplier(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.5f, 7);
		Node.RequestPushPhysicsSettingsMultiplier(MakeScale(0.125f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 7);

		bOk &= TestEqual(TEXT("Coalesced pending set"), GetPendingOverrideSetCount(Node), 1);
		const FKawaiiPhysicsSettingsMultiplierPushRequest PendingSet = GetPendingOverrideSet(Node, 0);
		bOk &= TestEqual(TEXT("Coalesced handle"), PendingSet.HandleId, static_cast<int64>(7));
		bOk &= TestFloatNear(*this, TEXT("Coalesced alpha"), PendingSet.Alpha, 0.75f);
		bOk &= TestFloatNear(*this, TEXT("Coalesced scale"), PendingSet.Scale.Damping, 0.125f);
	}

	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 100 + Index);
		}
		bOk &= TestTrue(TEXT("Pending sets bounded"),
		                GetPendingOverrideSetCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenStopImmediateTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenStopImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenStopImmediateTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.RequestStopPhysicsSettingsMultiplier(7, 0.0f);

	bool bOk = TestFalse(TEXT("Immediate stop inactive"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Immediate stop removed"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenResetDuringFadeTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenResetDuringFade",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenResetDuringFadeTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.6f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.RequestStopPhysicsSettingsMultiplier(7, 1.0f);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f);

	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);
	bool bOk = TestTrue(TEXT("Redriven active"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Redriven count"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestTrue(TEXT("Redriven flag"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Redriven peak"), Item.PeakAlpha, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Redriven alpha"), Item.DrivenAlpha, 1.0f);
	}

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

	bool bOk = TestEqual(TEXT("Pending stop removed"), GetPendingOverrideStopCount(Node), 0);
	bOk &= TestTrue(TEXT("Consume driven"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestTrue(TEXT("Driven item"), Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenSetThenStopSameFrameTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenSetThenStopSameFrame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenSetThenStopSameFrameTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.8f, 7);
	Node.RequestStopPhysicsSettingsMultiplier(7, 1.0f);

	bool bOk = TestTrue(TEXT("Same frame active"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Same frame item count"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Same frame fading"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Same frame peak"), Item.PeakAlpha, 0.8f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenHandleZeroIgnoredTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenHandleZeroIgnored",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenHandleZeroIgnoredTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = TestFalse(TEXT("Zero handle rejected"),
	                     Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 0));
	bOk &= TestEqual(TEXT("No pending set"), GetPendingOverrideSetCount(Node), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenCapEvictionTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenCapEviction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenCapEvictionTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	for (int32 Index = 0; Index < FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers; ++Index)
	{
		Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 100 + Index);
	}
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.0f, 10.0f, 0.0f, 999);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	bool bOk = TestEqual(TEXT("Cap item count"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
	                     FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
	bOk &= TestFalse(TEXT("Oldest driven evicted"), ContainsSettingsMultiplierHandle(Node, 100));
	bOk &= TestTrue(TEXT("Timed item kept"), ContainsSettingsMultiplierHandle(Node, 999));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenConvertsTimedItemTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenConvertsTimedItem",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenConvertsTimedItemTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 10.0f, 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	bool bOk = TestEqual(TEXT("Converted count"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestTrue(TEXT("Converted driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Converted rise"), Item.RiseTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Converted hold"), Item.HoldTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Converted decay"), Item.DecayTime, 0.0f);
	}

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

	bool bOk = TestEqual(TEXT("Pending start removed"), GetPendingOverrideCount(Node), 0);
	bOk &= TestEqual(TEXT("Pending set kept"), GetPendingOverrideSetCount(Node), 1);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStartReplacesSameHandleTest,
                                 "KawaiiPhysics.SettingsMultiplier.StartReplacesSameHandle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStartReplacesSameHandleTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	Node.RequestStartPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.2f, 10.0f, 0.3f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	bool bOk = TestEqual(TEXT("Start replacement count"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Timed after start"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Timed rise"), Item.RiseTime, 0.2f);
		bOk &= TestFloatNear(*this, TEXT("Timed hold"), Item.HoldTime, 10.0f);
		bOk &= TestFloatNear(*this, TEXT("Timed decay"), Item.DecayTime, 0.3f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenLeaseExpiresTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenLeaseExpires",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenLeaseExpiresTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 0.6f, 7, 2, 1.0f);

	bool bOk = TestTrue(TEXT("Lease first consume"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Lease first remaining"), Node.TransientForceStore.SettingsMultiplierItems[0].LeaseRemaining, 2);
	bOk &= TestTrue(TEXT("Lease second consume"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Lease second remaining"), Node.TransientForceStore.SettingsMultiplierItems[0].LeaseRemaining, 1);
	bOk &= TestTrue(TEXT("Lease expiry fades"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	if (Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Lease no longer driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Lease peak"), Item.PeakAlpha, 0.6f);
		bOk &= TestFloatNear(*this, TEXT("Lease decay"), Item.DecayTime, 1.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenLeaseRefreshedTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenLeaseRefreshed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenLeaseRefreshedTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = true;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7, 2, 1.0f);
		bOk &= TestTrue(*FString::Printf(TEXT("Lease refresh consume %d"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		bOk &= TestTrue(*FString::Printf(TEXT("Lease refresh driven %d"), Index),
		                Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
		bOk &= TestEqual(*FString::Printf(TEXT("Lease refresh remaining %d"), Index),
		                 Node.TransientForceStore.SettingsMultiplierItems[0].LeaseRemaining, 2);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenLeaseInfiniteTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenLeaseInfinite",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenLeaseInfiniteTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7, 0, 1.0f);
	bool bOk = true;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		bOk &= TestTrue(*FString::Printf(TEXT("Infinite consume %d"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
		bOk &= TestTrue(*FString::Printf(TEXT("Infinite driven %d"), Index),
		                Node.TransientForceStore.SettingsMultiplierItems[0].bExternallyDriven);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenLeaseExpireImmediateTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenLeaseExpireImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenLeaseExpireImmediateTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPushPhysicsSettingsMultiplier(FKawaiiPhysicsSettingsMultiplier(), 1.0f, 7, 1, 0.0f);
	bool bOk = TestTrue(TEXT("Immediate lease first consume"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestFalse(TEXT("Immediate lease removed"), Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	bOk &= TestEqual(TEXT("Immediate lease count"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierDrivenGatingTest,
                                 "KawaiiPhysics.SettingsMultiplier.DrivenGating",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierDrivenGatingTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.bUpdatePhysicsSettingsInGame = false;
	Accessor.SetInitPhysicsSettings(false);

	bool bOk = TestTrue(TEXT("Driven first update"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Driven idle skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));

	Accessor.Node.RequestPushPhysicsSettingsMultiplier(
		MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 7);
	bOk &= TestTrue(TEXT("Driven alpha zero runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestTrue(TEXT("Driven applied flag set"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Driven alpha zero base"), Accessor.Bone(1), MakeBaseSettings());

	Accessor.Node.RequestStopPhysicsSettingsMultiplier(7, 0.0f);
	bOk &= TestTrue(TEXT("Driven restore frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestFalse(TEXT("Driven applied flag cleared"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Driven restored"), Accessor.Bone(1), MakeBaseSettings());

	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Driven post restore skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));
	bOk &= TestFloatNear(*this, TEXT("Driven post restore untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

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
	bOk &= TestEqual(TEXT("Reinit pending starts clear"), GetPendingOverrideCount(Accessor.Node), 0);
	bOk &= TestEqual(TEXT("Reinit pending sets clear"), GetPendingOverrideSetCount(Accessor.Node), 0);
	bOk &= TestEqual(TEXT("Reinit pending stops clear"), GetPendingOverrideStopCount(Accessor.Node), 0);
	bOk &= TestFalse(TEXT("Reinit applied flag clear"), Accessor.IsPhysicsSettingsMultiplierAppliedLastUpdate());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierLimitAngleZeroSemanticsTest,
                                 "KawaiiPhysics.SettingsMultiplier.LimitAngleZeroSemantics",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierLimitAngleZeroSemanticsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

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

	{
		// 通常倍率は素直に乗算される
		FKawaiiPhysicsTestAccessor Accessor;
		SetupChainWithBaseSettings(Accessor);
		Accessor.Node.PhysicsSettings.LimitAngle = 30.0f;

		Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f), 0.0f, 1.0f, 0.0f);
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Accessor.CallUpdatePhysicsSettings();

		bOk &= TestFloatNear(*this, TEXT("Scaled LimitAngle"), Accessor.Bone(1).PhysicsSettings.LimitAngle, 15.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierHandleMismatchNoopTest,
                                 "KawaiiPhysics.SettingsMultiplier.HandleMismatchNoop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierHandleMismatchNoopTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);

	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 100);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);

	// 失効ハンドルへの停止は何もしない
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(999, 0.5f);
	bool bOk = TestTrue(TEXT("Still active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.5f));
	bOk &= TestEqual(TEXT("Item kept"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFloatNear(*this, TEXT("HoldTime untouched"), Item.HoldTime, 10.0f);
		bOk &= TestFloatNear(*this, TEXT("DecayTime untouched"), Item.DecayTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("PeakAlpha untouched"), Item.PeakAlpha, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("ElapsedTime advanced"), Item.ElapsedTime, 0.5f);
	}

	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Still scaled"), Accessor.Bone(1).PhysicsSettings.Damping, 0.2f);

	// ハンドル 0 はキューにも積まれない
	Accessor.Node.RequestStopPhysicsSettingsMultiplier(0, 0.5f);
	bOk &= TestEqual(TEXT("Zero handle ignored"), GetPendingOverrideStopCount(Accessor.Node), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierReflectionCopySurvivalTest,
                                 "KawaiiPhysics.SettingsMultiplier.ReflectionCopySurvival",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierReflectionCopySurvivalTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 11);
		Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 22);
		Node.RequestPushPhysicsSettingsMultiplier(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 33);
		Node.RequestStopPhysicsSettingsMultiplier(11, 0.25f);

		bOk &= TestEqual(TEXT("Initial items"), Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		bOk &= TestEqual(TEXT("Initial pending"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Initial pending sets"), GetPendingOverrideSetCount(Node), 1);
		bOk &= TestEqual(TEXT("Initial pending stops"), GetPendingOverrideStopCount(Node), 1);

		ApplyDefaultPresetStyleCopy(Node);

		bOk &= TestEqual(TEXT("Items survive preset-style copy"),
		                 Node.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		bOk &= TestEqual(TEXT("Pending survives preset-style copy"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Pending sets survive preset-style copy"), GetPendingOverrideSetCount(Node), 1);
		bOk &= TestEqual(TEXT("Pending stops survive preset-style copy"), GetPendingOverrideStopCount(Node), 1);
	}

	{
		// ノードのコピーは空のストアから始まり、二重消費しない
		FAnimNode_KawaiiPhysics A;
		A.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 33);
		A.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
		A.RequestStartPhysicsSettingsMultiplier(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 44);
		A.RequestPushPhysicsSettingsMultiplier(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 55);

		FAnimNode_KawaiiPhysics B = A;
		bOk &= TestTrue(TEXT("Copied queue is distinct"),
		                A.TransientForceStore.Queue.Get() != B.TransientForceStore.Queue.Get());
		bOk &= TestEqual(TEXT("B items empty"), B.TransientForceStore.SettingsMultiplierItems.Num(), 0);
		bOk &= TestEqual(TEXT("B pending empty"), GetPendingOverrideCount(B), 0);
		bOk &= TestEqual(TEXT("B pending sets empty"), GetPendingOverrideSetCount(B), 0);
		bOk &= TestFalse(TEXT("B consume yields nothing"), B.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));

		bOk &= TestEqual(TEXT("A items preserved"), A.TransientForceStore.SettingsMultiplierItems.Num(), 1);
		bOk &= TestEqual(TEXT("A pending preserved"), GetPendingOverrideCount(A), 1);
		bOk &= TestEqual(TEXT("A pending sets preserved"), GetPendingOverrideSetCount(A), 1);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStartRequestBuilderNegativeDurationIsInfiniteTest,
                                 "KawaiiPhysics.SettingsMultiplier.StartRequestBuilderNegativeDurationIsInfinite",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStartRequestBuilderNegativeDurationIsInfiniteTest::RunTest(const FString& Parameters)
{
	const FKawaiiPhysicsSettingsMultiplier Scale = MakeScale(0.5f, 0.25f, 0.75f, 0.9f, 2.0f, 0.5f);
	FKawaiiPhysicsSettingsMultiplierRequest Request;

	bool bOk = TestTrue(TEXT("Built"),
	                    UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(Scale, -1.0f, 0.2f, 0.5f, Request));
	bOk &= TestTrue(TEXT("Infinite hold"), Request.bInfiniteHold);
	bOk &= TestFloatNear(*this, TEXT("Rise"), Request.RiseTime, 0.2f);
	bOk &= TestFloatNear(*this, TEXT("Hold"), Request.HoldTime, 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Decay"), Request.DecayTime, 0.0f);
	bOk &= TestFloatNear(*this, TEXT("Scale copied"), Request.Scale.Damping, 0.5f);
	bOk &= TestEqual(TEXT("Handle untouched by builder"), Request.HandleId, static_cast<int64>(0));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStartRequestBuilderZeroDurationRejectedTest,
                                 "KawaiiPhysics.SettingsMultiplier.StartRequestBuilderZeroDurationRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStartRequestBuilderZeroDurationRejectedTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSettingsMultiplierRequest Request;
	Request.RiseTime = 1.0f;
	Request.HandleId = 123;
	Request.bInfiniteHold = true;

	bool bOk = TestFalse(TEXT("Rejected"),
	                     UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(FKawaiiPhysicsSettingsMultiplier(),
	                                                                                0.0f, 0.2f, 0.5f, Request));
	bOk &= TestFloatNear(*this, TEXT("Rise untouched"), Request.RiseTime, 1.0f);
	bOk &= TestEqual(TEXT("Handle untouched"), Request.HandleId, static_cast<int64>(123));
	bOk &= TestTrue(TEXT("Infinite untouched"), Request.bInfiniteHold);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierStartRequestBuilderPositiveDurationTrapezoidTest,
                                 "KawaiiPhysics.SettingsMultiplier.StartRequestBuilderPositiveDurationTrapezoid",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierStartRequestBuilderPositiveDurationTrapezoidTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsSettingsMultiplierRequest Request;
	const KawaiiPhysics::FWindGustEnvelope Expected = KawaiiPhysics::ResolveWindGustEnvelope(2.0f, 0.2f, 0.5f);

	bool bOk = TestTrue(TEXT("Built"),
	                    UKawaiiPhysicsLibrary::BuildSettingsMultiplierStartRequest(FKawaiiPhysicsSettingsMultiplier(),
	                                                                               2.0f, 0.2f, 0.5f, Request));
	bOk &= TestFalse(TEXT("Finite"), Request.bInfiniteHold);
	bOk &= TestFloatNear(*this, TEXT("Rise"), Request.RiseTime, Expected.RiseTime);
	bOk &= TestFloatNear(*this, TEXT("Hold"), Request.HoldTime, Expected.HoldTime);
	bOk &= TestFloatNear(*this, TEXT("Decay"), Request.DecayTime, Expected.DecayTime);
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
	bool bStillActive = true;
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		bStillActive = Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f / 60.0f);
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopDecaysAndRemovesTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldStopDecaysAndRemoves",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldStopDecaysAndRemovesTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.RequestStartPhysicsSettingsMultiplier(MakeScale(0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f),
	                                                     0.2f, 0.0f, 0.5f, 7, true);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f);
	for (int32 Index = 0; Index < 1000; ++Index)
	{
		Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(1.0f / 60.0f);
	}

	Accessor.Node.RequestStopPhysicsSettingsMultiplier(7, 0.5f);
	bool bOk = TestTrue(TEXT("Stop converted"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.0f));
	if (Accessor.Node.TransientForceStore.SettingsMultiplierItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsMultiplier& Item = Accessor.Node.TransientForceStore.SettingsMultiplierItems[0];
		bOk &= TestFalse(TEXT("Infinite cleared"), Item.bInfiniteHold);
		bOk &= TestFloatNear(*this, TEXT("Peak captured"), Item.PeakAlpha, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Decay"), Item.DecayTime, 0.5f);
	}

	bOk &= TestTrue(TEXT("Decay mid active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.25f));
	const FKawaiiPhysicsSettingsMultiplier EffectiveMid = Accessor.CallComputeEffectiveSettingsMultiplierScale();
	bOk &= TestFloatNear(*this, TEXT("Mid alpha"), 1.0f - EffectiveMid.Damping, 0.5f);
	bOk &= TestFalse(TEXT("Decay finished"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsMultipliers(0.25f));
	bOk &= TestEqual(TEXT("Removed"), Accessor.Node.TransientForceStore.SettingsMultiplierItems.Num(), 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsMultiplierInfiniteHoldEvictionWarnsTest,
                                 "KawaiiPhysics.SettingsMultiplier.InfiniteHoldEvictionWarns",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsMultiplierInfiniteHoldEvictionWarnsTest::RunTest(const FString& Parameters)
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

	bool bOk = TestEqual(TEXT("Cap item count"), Node.TransientForceStore.SettingsMultiplierItems.Num(),
	                     FAnimNode_KawaiiPhysics::MaxPhysicsSettingsMultipliers);
	bOk &= TestFalse(TEXT("Oldest infinite evicted"), ContainsSettingsMultiplierHandle(Node, 100));
	bOk &= TestTrue(TEXT("Newest infinite kept"), ContainsSettingsMultiplierHandle(Node, 999));
	return bOk;
}

#endif
