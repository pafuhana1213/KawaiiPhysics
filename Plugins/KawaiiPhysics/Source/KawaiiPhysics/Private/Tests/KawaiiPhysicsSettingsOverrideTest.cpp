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

int32 GetPendingOverrideSetCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrideSets.Num();
}

FKawaiiPhysicsSettingsOverrideSetRequest GetPendingOverrideSet(FAnimNode_KawaiiPhysics& Node, const int32 Index)
{
	FKawaiiPhysicsSettingsOverrideSetRequest Request;
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return Request;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingSettingsOverrideSets.IsValidIndex(Index)
		       ? Node.TransientForceStore.Queue->PendingSettingsOverrideSets[Index]
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

bool ContainsSettingsOverrideHandle(const FAnimNode_KawaiiPhysics& Node, const int64 HandleId)
{
	return Node.TransientForceStore.SettingsOverrideItems.ContainsByPredicate(
		[HandleId](const FKawaiiPhysicsActiveSettingsOverride& Item)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenSetCreateAndUpdateTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenSetCreateAndUpdate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenSetCreateAndUpdateTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	const FKawaiiPhysicsSettingsScale Scale1 = MakeScale(0.5f, 0.25f, 0.5f, 0.5f, 2.0f, 0.5f);
	const FKawaiiPhysicsSettingsScale Scale2 = MakeScale(0.25f, 0.5f, 0.75f, 0.8f, 1.5f, 0.75f);

	bool bOk = TestTrue(TEXT("Request set"), Accessor.Node.RequestSetPhysicsSettingsOverride(Scale1, 0.5f, 7));
	bOk &= TestTrue(TEXT("Consume set"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Item count"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Accessor.Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestTrue(TEXT("Driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("DrivenAlpha"), Item.DrivenAlpha, 0.5f);
		bOk &= TestFloatNear(*this, TEXT("PeakAlpha"), Item.PeakAlpha, 1.0f);
	}
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Damping half alpha"), Accessor.Bone(1).PhysicsSettings.Damping,
	                     Base.Damping * FMath::Lerp(1.0f, Scale1.Damping, 0.5f));

	bOk &= TestTrue(TEXT("Request update"), Accessor.Node.RequestSetPhysicsSettingsOverride(Scale2, 1.0f, 7));
	bOk &= TestTrue(TEXT("Consume update"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Updated item count"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Accessor.Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestFloatNear(*this, TEXT("Updated Damping scale"), Item.Scale.Damping, Scale2.Damping);
		bOk &= TestFloatNear(*this, TEXT("Updated alpha"), Item.DrivenAlpha, 1.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenSetCoalesceTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenSetCoalesce",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenSetCoalesceTest::RunTest(const FString& Parameters)
{
	bool bOk = true;
	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestSetPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.25f, 7);
		Node.RequestSetPhysicsSettingsOverride(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.5f, 7);
		Node.RequestSetPhysicsSettingsOverride(MakeScale(0.125f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 7);

		bOk &= TestEqual(TEXT("Coalesced pending set"), GetPendingOverrideSetCount(Node), 1);
		const FKawaiiPhysicsSettingsOverrideSetRequest PendingSet = GetPendingOverrideSet(Node, 0);
		bOk &= TestEqual(TEXT("Coalesced handle"), PendingSet.HandleId, static_cast<int64>(7));
		bOk &= TestFloatNear(*this, TEXT("Coalesced alpha"), PendingSet.Alpha, 0.75f);
		bOk &= TestFloatNear(*this, TEXT("Coalesced scale"), PendingSet.Scale.Damping, 0.125f);
	}

	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 100 + Index);
		}
		bOk &= TestTrue(TEXT("Pending sets bounded"),
		                GetPendingOverrideSetCount(Node) <= FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenAlphaClampTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenAlphaClamp",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenAlphaClampTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettingsScale Scale = MakeScale(0.5f, 0.5f, 0.5f, 0.5f, 2.0f, 0.5f);

	bool bOk = TestTrue(TEXT("Request alpha high"), Accessor.Node.RequestSetPhysicsSettingsOverride(Scale, 2.0f, 7));
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	bOk &= TestFloatNear(*this, TEXT("Alpha clamped high"),
	                     Accessor.Node.TransientForceStore.SettingsOverrideItems[0].DrivenAlpha, 1.0f);

	bOk &= TestTrue(TEXT("Request alpha low"), Accessor.Node.RequestSetPhysicsSettingsOverride(Scale, -1.0f, 7));
	bOk &= TestTrue(TEXT("Alpha zero remains active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestFloatNear(*this, TEXT("Alpha clamped low"),
	                     Accessor.Node.TransientForceStore.SettingsOverrideItems[0].DrivenAlpha, 0.0f);
	const FKawaiiPhysicsSettingsScale Effective = Accessor.CallComputeEffectiveSettingsOverrideScale();
	bOk &= TestFloatNear(*this, TEXT("Effective Damping identity"), Effective.Damping, 1.0f);
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestBoneSettings(*this, TEXT("Alpha zero base"), Accessor.Bone(1), MakeBaseSettings());

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenNoExpiryTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenNoExpiry",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenNoExpiryTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);

	bool bOk = true;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		bOk &= TestTrue(*FString::Printf(TEXT("Consume %d active"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsOverrides(100.0f));
		bOk &= TestEqual(*FString::Printf(TEXT("Consume %d item count"), Index),
		                 Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenStopBlendOutTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenStopBlendOut",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenStopBlendOutTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	const FKawaiiPhysicsSettings Base = MakeBaseSettings();
	const FKawaiiPhysicsSettingsScale Scale = MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

	Accessor.Node.RequestSetPhysicsSettingsOverride(Scale, 0.6f, 7);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Accessor.Node.RequestStopPhysicsSettingsOverride(7, 1.0f);

	bool bOk = TestTrue(TEXT("Stop consume active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	if (Accessor.Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Accessor.Node.TransientForceStore.SettingsOverrideItems[0];
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

	bOk &= TestTrue(TEXT("Half fade active"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f));
	Accessor.CallUpdatePhysicsSettings();
	bOk &= TestFloatNear(*this, TEXT("Damping half fade"), Accessor.Bone(1).PhysicsSettings.Damping,
	                     Base.Damping * FMath::Lerp(1.0f, Scale.Damping, 0.3f));

	bOk &= TestFalse(TEXT("Fade completed"), Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f));
	bOk &= TestEqual(TEXT("Item removed"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenStopImmediateTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenStopImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenStopImmediateTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.RequestStopPhysicsSettingsOverride(7, 0.0f);

	bool bOk = TestFalse(TEXT("Immediate stop inactive"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Immediate stop removed"), Node.TransientForceStore.SettingsOverrideItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenResetDuringFadeTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenResetDuringFade",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenResetDuringFadeTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.6f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.RequestStopPhysicsSettingsOverride(7, 1.0f);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.5f);

	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);
	bool bOk = TestTrue(TEXT("Redriven active"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Redriven count"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestTrue(TEXT("Redriven flag"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Redriven peak"), Item.PeakAlpha, 1.0f);
		bOk &= TestFloatNear(*this, TEXT("Redriven alpha"), Item.DrivenAlpha, 1.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenSetSupersedesPendingStopTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenSetSupersedesPendingStop",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenSetSupersedesPendingStopTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestStopPhysicsSettingsOverride(7, 1.0f);
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);

	bool bOk = TestEqual(TEXT("Pending stop removed"), GetPendingOverrideStopCount(Node), 0);
	bOk &= TestTrue(TEXT("Consume driven"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestTrue(TEXT("Driven item"), Node.TransientForceStore.SettingsOverrideItems[0].bExternallyDriven);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenSetThenStopSameFrameTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenSetThenStopSameFrame",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenSetThenStopSameFrameTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.8f, 7);
	Node.RequestStopPhysicsSettingsOverride(7, 1.0f);

	bool bOk = TestTrue(TEXT("Same frame active"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Same frame item count"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestFalse(TEXT("Same frame fading"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Same frame peak"), Item.PeakAlpha, 0.8f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenHandleZeroIgnoredTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenHandleZeroIgnored",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenHandleZeroIgnoredTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = TestFalse(TEXT("Zero handle rejected"),
	                     Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 0));
	bOk &= TestEqual(TEXT("No pending set"), GetPendingOverrideSetCount(Node), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenCapEvictionTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenCapEviction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenCapEvictionTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	for (int32 Index = 0; Index < FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides; ++Index)
	{
		Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 100 + Index);
	}
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, 999);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

	bool bOk = TestEqual(TEXT("Cap item count"), Node.TransientForceStore.SettingsOverrideItems.Num(),
	                     FAnimNode_KawaiiPhysics::MaxPhysicsSettingsOverrides);
	bOk &= TestFalse(TEXT("Oldest driven evicted"), ContainsSettingsOverrideHandle(Node, 100));
	bOk &= TestTrue(TEXT("Timed item kept"), ContainsSettingsOverrideHandle(Node, 999));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenConvertsTimedItemTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenConvertsTimedItem",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenConvertsTimedItemTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 10.0f, 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

	bool bOk = TestEqual(TEXT("Converted count"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestTrue(TEXT("Converted driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Converted rise"), Item.RiseTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Converted hold"), Item.HoldTime, 0.0f);
		bOk &= TestFloatNear(*this, TEXT("Converted decay"), Item.DecayTime, 0.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideSetRemovesPendingStartTest,
                                 "KawaiiPhysics.SettingsOverride.SetRemovesPendingStart",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideSetRemovesPendingStartTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, 7);
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);

	bool bOk = TestEqual(TEXT("Pending start removed"), GetPendingOverrideCount(Node), 0);
	bOk &= TestEqual(TEXT("Pending set kept"), GetPendingOverrideSetCount(Node), 1);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideStartReplacesSameHandleTest,
                                 "KawaiiPhysics.SettingsOverride.StartReplacesSameHandle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideStartReplacesSameHandleTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.2f, 10.0f, 0.3f, 7);
	Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);

	bool bOk = TestEqual(TEXT("Start replacement count"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
	if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestFalse(TEXT("Timed after start"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Timed rise"), Item.RiseTime, 0.2f);
		bOk &= TestFloatNear(*this, TEXT("Timed hold"), Item.HoldTime, 10.0f);
		bOk &= TestFloatNear(*this, TEXT("Timed decay"), Item.DecayTime, 0.3f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenLeaseExpiresTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenLeaseExpires",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenLeaseExpiresTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.6f, 7, 2, 1.0f);

	bool bOk = TestTrue(TEXT("Lease first consume"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Lease first remaining"), Node.TransientForceStore.SettingsOverrideItems[0].LeaseRemaining, 2);
	bOk &= TestTrue(TEXT("Lease second consume"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Lease second remaining"), Node.TransientForceStore.SettingsOverrideItems[0].LeaseRemaining, 1);
	bOk &= TestTrue(TEXT("Lease expiry fades"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	if (Node.TransientForceStore.SettingsOverrideItems.IsValidIndex(0))
	{
		const FKawaiiPhysicsActiveSettingsOverride& Item = Node.TransientForceStore.SettingsOverrideItems[0];
		bOk &= TestFalse(TEXT("Lease no longer driven"), Item.bExternallyDriven);
		bOk &= TestFloatNear(*this, TEXT("Lease peak"), Item.PeakAlpha, 0.6f);
		bOk &= TestFloatNear(*this, TEXT("Lease decay"), Item.DecayTime, 1.0f);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenLeaseRefreshedTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenLeaseRefreshed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenLeaseRefreshedTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	bool bOk = true;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7, 2, 1.0f);
		bOk &= TestTrue(*FString::Printf(TEXT("Lease refresh consume %d"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
		bOk &= TestTrue(*FString::Printf(TEXT("Lease refresh driven %d"), Index),
		                Node.TransientForceStore.SettingsOverrideItems[0].bExternallyDriven);
		bOk &= TestEqual(*FString::Printf(TEXT("Lease refresh remaining %d"), Index),
		                 Node.TransientForceStore.SettingsOverrideItems[0].LeaseRemaining, 2);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenLeaseInfiniteTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenLeaseInfinite",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenLeaseInfiniteTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7, 0, 1.0f);
	bool bOk = true;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		bOk &= TestTrue(*FString::Printf(TEXT("Infinite consume %d"), Index),
		                Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
		bOk &= TestTrue(*FString::Printf(TEXT("Infinite driven %d"), Index),
		                Node.TransientForceStore.SettingsOverrideItems[0].bExternallyDriven);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenLeaseExpireImmediateTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenLeaseExpireImmediate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenLeaseExpireImmediateTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7, 1, 0.0f);
	bool bOk = TestTrue(TEXT("Immediate lease first consume"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestFalse(TEXT("Immediate lease removed"), Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));
	bOk &= TestEqual(TEXT("Immediate lease count"), Node.TransientForceStore.SettingsOverrideItems.Num(), 0);
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenGatingTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenGating",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenGatingTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.bUpdatePhysicsSettingsInGame = false;
	Accessor.SetInitPhysicsSettings(false);

	bool bOk = TestTrue(TEXT("Driven first update"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Driven idle skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));

	Accessor.Node.RequestSetPhysicsSettingsOverride(
		MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 7);
	bOk &= TestTrue(TEXT("Driven alpha zero runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestTrue(TEXT("Driven applied flag set"), Accessor.IsPhysicsSettingsOverrideAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Driven alpha zero base"), Accessor.Bone(1), MakeBaseSettings());

	Accessor.Node.RequestStopPhysicsSettingsOverride(7, 0.0f);
	bOk &= TestTrue(TEXT("Driven restore frame runs"), Accessor.RunPhysicsSettingsUpdateGate(0.0f));
	bOk &= TestFalse(TEXT("Driven applied flag cleared"), Accessor.IsPhysicsSettingsOverrideAppliedLastUpdate());
	bOk &= TestBoneSettings(*this, TEXT("Driven restored"), Accessor.Bone(1), MakeBaseSettings());

	Accessor.Bone(1).PhysicsSettings.Damping = 123.0f;
	bOk &= TestFalse(TEXT("Driven post restore skipped"), Accessor.RunPhysicsSettingsUpdateGate(0.016f));
	bOk &= TestFloatNear(*this, TEXT("Driven post restore untouched"), Accessor.Bone(1).PhysicsSettings.Damping, 123.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsSettingsOverrideDrivenReinitClearsTest,
                                 "KawaiiPhysics.SettingsOverride.DrivenReinitClears",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsSettingsOverrideDrivenReinitClearsTest::RunTest(const FString& Parameters)
{
	FKawaiiPhysicsTestAccessor Accessor;
	SetupChainWithBaseSettings(Accessor);
	Accessor.Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 7);
	Accessor.Node.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
	Accessor.Node.RequestSetPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 1.0f, 8);
	Accessor.Node.RequestPhysicsSettingsOverride(FKawaiiPhysicsSettingsScale(), 0.0f, 10.0f, 0.0f, 9);
	Accessor.Node.RequestStopPhysicsSettingsOverride(10, 0.5f);
	Accessor.SetPhysicsSettingsOverrideAppliedLastUpdate(true);

	Accessor.CallResetTransientRuntimeState();

	bool bOk = TestEqual(TEXT("Reinit items clear"), Accessor.Node.TransientForceStore.Items.Num(), 0);
	bOk &= TestEqual(TEXT("Reinit settings items clear"), Accessor.Node.TransientForceStore.SettingsOverrideItems.Num(), 0);
	bOk &= TestEqual(TEXT("Reinit pending starts clear"), GetPendingOverrideCount(Accessor.Node), 0);
	bOk &= TestEqual(TEXT("Reinit pending sets clear"), GetPendingOverrideSetCount(Accessor.Node), 0);
	bOk &= TestEqual(TEXT("Reinit pending stops clear"), GetPendingOverrideStopCount(Accessor.Node), 0);
	bOk &= TestFalse(TEXT("Reinit applied flag clear"), Accessor.IsPhysicsSettingsOverrideAppliedLastUpdate());

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
		Node.RequestSetPhysicsSettingsOverride(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 33);
		Node.RequestStopPhysicsSettingsOverride(11, 0.25f);

		bOk &= TestEqual(TEXT("Initial items"), Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("Initial pending"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Initial pending sets"), GetPendingOverrideSetCount(Node), 1);
		bOk &= TestEqual(TEXT("Initial pending stops"), GetPendingOverrideStopCount(Node), 1);

		ApplyDefaultPresetStyleCopy(Node);

		bOk &= TestEqual(TEXT("Items survive preset-style copy"),
		                 Node.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("Pending survives preset-style copy"), GetPendingOverrideCount(Node), 1);
		bOk &= TestEqual(TEXT("Pending sets survive preset-style copy"), GetPendingOverrideSetCount(Node), 1);
		bOk &= TestEqual(TEXT("Pending stops survive preset-style copy"), GetPendingOverrideStopCount(Node), 1);
	}

	{
		// ノードのコピーは空のストアから始まり、二重消費しない
		FAnimNode_KawaiiPhysics A;
		A.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 33);
		A.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f);
		A.RequestPhysicsSettingsOverride(MakeScale(0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 10.0f, 0.0f, 44);
		A.RequestSetPhysicsSettingsOverride(MakeScale(0.25f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f), 0.75f, 55);

		FAnimNode_KawaiiPhysics B = A;
		bOk &= TestTrue(TEXT("Copied queue is distinct"),
		                A.TransientForceStore.Queue.Get() != B.TransientForceStore.Queue.Get());
		bOk &= TestEqual(TEXT("B items empty"), B.TransientForceStore.SettingsOverrideItems.Num(), 0);
		bOk &= TestEqual(TEXT("B pending empty"), GetPendingOverrideCount(B), 0);
		bOk &= TestEqual(TEXT("B pending sets empty"), GetPendingOverrideSetCount(B), 0);
		bOk &= TestFalse(TEXT("B consume yields nothing"), B.ConsumeAndAdvancePhysicsSettingsOverrides(0.0f));

		bOk &= TestEqual(TEXT("A items preserved"), A.TransientForceStore.SettingsOverrideItems.Num(), 1);
		bOk &= TestEqual(TEXT("A pending preserved"), GetPendingOverrideCount(A), 1);
		bOk &= TestEqual(TEXT("A pending sets preserved"), GetPendingOverrideSetCount(A), 1);
	}

	return bOk;
}

#endif
