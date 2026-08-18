// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AnimNode_KawaiiPhysics.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"
#include "KawaiiPhysicsPresetDataAsset.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Curves/CurveFloat.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr float GTransientForceTol = 0.000001f;

FKawaiiPhysics_ExternalForce_ProceduralWind* GetTransientWind(FAnimNode_KawaiiPhysics& Node, const int32 Index = 0)
{
	if (!Node.TransientForceStore.Items.IsValidIndex(Index))
	{
		return nullptr;
	}

	return Node.TransientForceStore.Items[Index].Force.GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
}

bool TestFloatNear(FAutomationTestBase& Test, const TCHAR* Name, const float Actual, const float Expected)
{
	return Test.TestTrue(FString::Printf(TEXT("%s: got %.9f expected %.9f"), Name, Actual, Expected),
	                     FMath::IsNearlyEqual(Actual, Expected, GTransientForceTol));
}

void RunPreApply(FAnimNode_KawaiiPhysics& Node, FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
{
	FAnimInstanceProxy AnimInstanceProxy;
	FComponentSpacePoseContext PoseContext(&AnimInstanceProxy);
	Wind.PreApply(Node, PoseContext);
}

int32 GetPendingGustCount(FAnimNode_KawaiiPhysics& Node)
{
	if (!Node.TransientForceStore.Queue.IsValid())
	{
		return 0;
	}

	FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
	return Node.TransientForceStore.Queue->PendingGusts.Num();
}

void AddAuthoredProceduralWind(FAnimNode_KawaiiPhysics& Node, const bool bIsEnabled, const FVector& Direction,
                               const EExternalForceSpace ForceSpace, const float TimeScale,
                               const bool bWithFiltersAndCurve, const float WindDirectionNoiseAngle = 0.0f,
                               const float WindDirectionNoisePeriod = 1.0f, const int32 Seed = 0,
                               const FFloatInterval RandomForceScaleRange = FFloatInterval(1.0f, 1.0f))
{
	FInstancedStruct InstancedWind = FInstancedStruct::Make<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind =
		InstancedWind.GetMutablePtr<FKawaiiPhysics_ExternalForce_ProceduralWind>();
	check(Wind);

	Wind->bIsEnabled = bIsEnabled;
	Wind->WindDirection = Direction;
	Wind->ExternalForceSpace = ForceSpace;
	Wind->TimeScale = TimeScale;
	Wind->WindDirectionNoiseAngle = WindDirectionNoiseAngle;
	Wind->WindDirectionNoisePeriod = WindDirectionNoisePeriod;
	Wind->Seed = Seed;
	Wind->RandomForceScaleRange = RandomForceScaleRange;

	if (bWithFiltersAndCurve)
	{
		FBoneReference BoneReference;
		BoneReference.BoneName = TEXT("transient_force_test_bone");
		Wind->ApplyBoneFilter.Add(BoneReference);
		Wind->ForceRateByBoneLengthRate.GetRichCurve()->AddKey(0.25f, 0.75f);
	}

	Node.ExternalForces.Emplace(MoveTemp(InstancedWind));
}

void AddStandardAuthoredWinds(FAnimNode_KawaiiPhysics& Node)
{
	AddAuthoredProceduralWind(Node, false, FVector(1.0f, 0.0f, 0.0f),
	                          EExternalForceSpace::WorldSpace, 1.0f, false);
	AddAuthoredProceduralWind(Node, true, FVector(0.0f, 1.0f, 0.0f),
	                          EExternalForceSpace::ComponentSpace, 2.0f, true, 15.0f, 0.5f, 42,
	                          FFloatInterval(0.5f, 2.0f));
}

bool TestInheritedRuntimeFields(FAutomationTestBase& Test, FKawaiiPhysics_ExternalForce_ProceduralWind& Wind)
{
	bool bOk = true;
	bOk &= Test.TestEqual(TEXT("ApplyBoneFilter.Num"), Wind.ApplyBoneFilter.Num(), 1);
	bOk &= Test.TestEqual(TEXT("Curve key count"), Wind.ForceRateByBoneLengthRate.GetRichCurveConst()->GetNumKeys(), 1);
	bOk &= TestFloatNear(Test, TEXT("RandomForceScaleRange.Min"), Wind.RandomForceScaleRange.Min, 0.5f);
	bOk &= TestFloatNear(Test, TEXT("RandomForceScaleRange.Max"), Wind.RandomForceScaleRange.Max, 2.0f);
	bOk &= TestFloatNear(Test, TEXT("TimeScale"), Wind.TimeScale, 2.0f);
	return bOk;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceGustConsumeCreatesActiveGustTest,
                                 "KawaiiPhysics.TransientForce.GustConsumeCreatesActiveGust",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceGustConsumeCreatesActiveGustTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestTransientGust(4.0f, 0.2f, 0.6f, FVector(0.0f, 0.0f, 2.0f), INDEX_NONE);

	Node.ConsumeAndSweepTransientExternalForces(0.0f);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Items.Num"), Node.TransientForceStore.Items.Num(), 1);

	FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
	bOk &= TestTrue(TEXT("Transient force is ProceduralWind"), Wind != nullptr);
	if (!Wind)
	{
		return false;
	}

	bOk &= TestTrue(TEXT("ExternalForceSpace"), Wind->ExternalForceSpace == EExternalForceSpace::WorldSpace);
	bOk &= TestTrue(TEXT("WindDirection"), Wind->WindDirection.Equals(FVector(0.0f, 0.0f, 2.0f)));

	RunPreApply(Node, *Wind);

	bOk &= TestTrue(TEXT("ActiveGust active"), Wind->RuntimeState->ActiveGust.bIsActive);
	TestFloatNear(*this, TEXT("ActiveGust Strength"), Wind->RuntimeState->ActiveGust.Strength, 4.0f);
	TestFloatNear(*this, TEXT("ActiveGust RiseTime"), Wind->RuntimeState->ActiveGust.RiseTime, 0.2f);
	TestFloatNear(*this, TEXT("ActiveGust DecayTime"), Wind->RuntimeState->ActiveGust.DecayTime, 0.6f);
	TestFloatNear(*this, TEXT("RemainingLifetime"),
	              Node.TransientForceStore.Items[0].RemainingLifetime, 1.0f);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceLifetimeSweepTest,
                                 "KawaiiPhysics.TransientForce.LifetimeSweep",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceLifetimeSweepTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestTransientGust(1.0f, 0.05f, 0.0f, FVector::ForwardVector, INDEX_NONE);

	Node.ConsumeAndSweepTransientExternalForces(0.0f);
	bool bOk = TestEqual(TEXT("Initial consume"), Node.TransientForceStore.Items.Num(), 1);

	Node.ConsumeAndSweepTransientExternalForces(0.13f);
	bOk &= TestEqual(TEXT("Still alive"), Node.TransientForceStore.Items.Num(), 1);

	Node.ConsumeAndSweepTransientExternalForces(0.13f);
	bOk &= TestEqual(TEXT("Expired"), Node.TransientForceStore.Items.Num(), 0);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceMultiGustTest,
                                 "KawaiiPhysics.TransientForce.MultiGust",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceMultiGustTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector(1.0f, 0.0f, 0.0f), INDEX_NONE);
	Node.RequestTransientGust(2.0f, 0.1f, 0.1f, FVector(0.0f, 1.0f, 0.0f), INDEX_NONE);
	Node.RequestTransientGust(3.0f, 0.1f, 0.1f, FVector(0.0f, 0.0f, 1.0f), INDEX_NONE);

	Node.ConsumeAndSweepTransientExternalForces(0.0f);

	return TestEqual(TEXT("Items.Num"), Node.TransientForceStore.Items.Num(), 3);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceCapDropsOldestTest,
                                 "KawaiiPhysics.TransientForce.CapDropsOldest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceCapDropsOldestTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector(static_cast<float>(Index), 1.0f, 0.0f), INDEX_NONE);
	}

	Node.ConsumeAndSweepTransientExternalForces(0.0f);

	bool bOk = TestEqual(TEXT("Items.Num"), Node.TransientForceStore.Items.Num(), FAnimNode_KawaiiPhysics::MaxTransientExternalForces);
	for (int32 Index = 0; Index < Node.TransientForceStore.Items.Num(); ++Index)
	{
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node, Index);
		bOk &= TestTrue(FString::Printf(TEXT("Wind %d valid"), Index), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestTrue(FString::Printf(TEXT("Direction %d"), Index),
			                Wind->WindDirection.Equals(FVector(static_cast<float>(Index + 2), 1.0f, 0.0f)));
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForcePendingCapBoundsQueueTest,
                                 "KawaiiPhysics.TransientForce.PendingCapBoundsQueue",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForcePendingCapBoundsQueueTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	for (int32 Index = 1; Index <= 12; ++Index)
	{
		Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector(static_cast<float>(Index), 0.0f, 0.0f), INDEX_NONE);
	}

	bool bOk = TestTrue(TEXT("Queue valid"), Node.TransientForceStore.Queue.IsValid());
	if (Node.TransientForceStore.Queue.IsValid())
	{
		FScopeLock Lock(&Node.TransientForceStore.Queue->Mutex);
		bOk &= TestEqual(TEXT("PendingGusts.Num"), Node.TransientForceStore.Queue->PendingGusts.Num(), 8);
		if (Node.TransientForceStore.Queue->PendingGusts.IsValidIndex(0))
		{
			bOk &= TestTrue(TEXT("PendingGusts[0].Direction.X"),
			                FMath::IsNearlyEqual(Node.TransientForceStore.Queue->PendingGusts[0].Direction.X,
			                                     5.0f, GTransientForceTol));
		}
		else
		{
			bOk = false;
		}
	}

	Node.ConsumeAndSweepTransientExternalForces(0.0f);
	bOk &= TestEqual(TEXT("Items.Num"), Node.TransientForceStore.Items.Num(), 8);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceDirectionSentinelBoundaryTest,
                                 "KawaiiPhysics.TransientForce.DirectionSentinelBoundary",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceDirectionSentinelBoundaryTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics Node;
	Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector::ZeroVector, INDEX_NONE);
	Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector(KINDA_SMALL_NUMBER * 0.5f, 0.0f, 0.0f), INDEX_NONE);
	Node.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector(KINDA_SMALL_NUMBER * 10.0f, 0.0f, 0.0f), INDEX_NONE);

	Node.ConsumeAndSweepTransientExternalForces(0.0f);

	bool bOk = TestEqual(TEXT("Items.Num"), Node.TransientForceStore.Items.Num(), 3);
	FKawaiiPhysics_ExternalForce_ProceduralWind* ZeroWind = GetTransientWind(Node, 0);
	FKawaiiPhysics_ExternalForce_ProceduralWind* NearZeroWind = GetTransientWind(Node, 1);
	FKawaiiPhysics_ExternalForce_ProceduralWind* ExplicitWind = GetTransientWind(Node, 2);
	bOk &= TestTrue(TEXT("ZeroWind valid"), ZeroWind != nullptr);
	bOk &= TestTrue(TEXT("NearZeroWind valid"), NearZeroWind != nullptr);
	bOk &= TestTrue(TEXT("ExplicitWind valid"), ExplicitWind != nullptr);
	if (!ZeroWind || !NearZeroWind || !ExplicitWind)
	{
		return false;
	}

	bOk &= TestTrue(TEXT("Zero direction default"), ZeroWind->WindDirection.Equals(FVector::ForwardVector));
	bOk &= TestTrue(TEXT("Zero space default"), ZeroWind->ExternalForceSpace == EExternalForceSpace::WorldSpace);
	bOk &= TestTrue(TEXT("Near-zero direction default"), NearZeroWind->WindDirection.Equals(FVector::ForwardVector));
	bOk &= TestTrue(TEXT("Near-zero space default"), NearZeroWind->ExternalForceSpace == EExternalForceSpace::WorldSpace);
	bOk &= TestTrue(TEXT("Explicit direction"), ExplicitWind->WindDirection.Equals(FVector(KINDA_SMALL_NUMBER * 10.0f, 0.0f, 0.0f)));
	bOk &= TestTrue(TEXT("Explicit space"), ExplicitWind->ExternalForceSpace == EExternalForceSpace::WorldSpace);

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceInheritFromAuthoredTest,
                                 "KawaiiPhysics.TransientForce.InheritFromAuthored",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceInheritFromAuthoredTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		AddStandardAuthoredWinds(Node);
		Node.RequestTransientGust(3.0f, 0.2f, 0.6f, FVector::ZeroVector, 1);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
		bOk &= TestTrue(TEXT("Indexed inherited wind valid"), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestTrue(TEXT("Indexed WindDirection"), Wind->WindDirection.Equals(FVector(0.0f, 1.0f, 0.0f)));
			bOk &= TestTrue(TEXT("Indexed ExternalForceSpace"), Wind->ExternalForceSpace == EExternalForceSpace::ComponentSpace);
			bOk &= TestInheritedRuntimeFields(*this, *Wind);
			bOk &= TestFloatNear(*this, TEXT("Indexed WindDirectionNoiseAngle"), Wind->WindDirectionNoiseAngle, 15.0f);
			bOk &= TestFloatNear(*this, TEXT("Indexed WindDirectionNoisePeriod"), Wind->WindDirectionNoisePeriod, 0.5f);
			bOk &= TestEqual(TEXT("Indexed Seed"), Wind->Seed, 42);
			bOk &= TestFloatNear(*this, TEXT("Indexed lifetime"), Node.TransientForceStore.Items[0].RemainingLifetime, 0.6f);
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		AddStandardAuthoredWinds(Node);
		Node.RequestTransientGust(3.0f, 0.2f, 0.6f, FVector::ZeroVector, INDEX_NONE);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
		bOk &= TestTrue(TEXT("Fallback inherited wind valid"), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestTrue(TEXT("Fallback skips disabled"), Wind->WindDirection.Equals(FVector(0.0f, 1.0f, 0.0f)));
			bOk &= TestTrue(TEXT("Fallback ExternalForceSpace"), Wind->ExternalForceSpace == EExternalForceSpace::ComponentSpace);
			bOk &= TestInheritedRuntimeFields(*this, *Wind);
			bOk &= TestFloatNear(*this, TEXT("Fallback WindDirectionNoiseAngle"), Wind->WindDirectionNoiseAngle, 15.0f);
			bOk &= TestFloatNear(*this, TEXT("Fallback WindDirectionNoisePeriod"), Wind->WindDirectionNoisePeriod, 0.5f);
			bOk &= TestEqual(TEXT("Fallback Seed"), Wind->Seed, 42);
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		AddStandardAuthoredWinds(Node);
		Node.RequestTransientGust(3.0f, 0.2f, 0.6f, FVector(0.0f, 0.0f, 3.0f), 1);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
		bOk &= TestTrue(TEXT("Explicit inherited wind valid"), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestTrue(TEXT("Explicit WindDirection"), Wind->WindDirection.Equals(FVector(0.0f, 0.0f, 3.0f)));
			bOk &= TestTrue(TEXT("Explicit ExternalForceSpace"), Wind->ExternalForceSpace == EExternalForceSpace::WorldSpace);
			bOk &= TestInheritedRuntimeFields(*this, *Wind);
			bOk &= TestFloatNear(*this, TEXT("Explicit WindDirectionNoiseAngle"), Wind->WindDirectionNoiseAngle, 0.0f);
			bOk &= TestFloatNear(*this, TEXT("Explicit WindDirectionNoisePeriod"), Wind->WindDirectionNoisePeriod, 1.0f);
			bOk &= TestEqual(TEXT("Explicit Seed"), Wind->Seed, 0);
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestTransientGust(3.0f, 0.2f, 0.6f, FVector::ZeroVector, INDEX_NONE);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
		bOk &= TestTrue(TEXT("Default inherited wind valid"), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestFloatNear(*this, TEXT("Default RandomForceScaleRange.Min"),
			                      Wind->RandomForceScaleRange.Min, 1.0f);
			bOk &= TestFloatNear(*this, TEXT("Default RandomForceScaleRange.Max"),
			                      Wind->RandomForceScaleRange.Max, 1.0f);
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceSpreadAcrossAuthoredWindsTest,
                                 "KawaiiPhysics.TransientForce.SpreadAcrossAuthoredWinds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceSpreadAcrossAuthoredWindsTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics Node;
		AddAuthoredProceduralWind(Node, true, FVector(0.0f, 1.0f, 0.0f),
		                          EExternalForceSpace::ComponentSpace, 1.0f, true, 10.0f, 0.5f, 11);
		AddAuthoredProceduralWind(Node, true, FVector(0.0f, 0.0f, 1.0f),
		                          EExternalForceSpace::WorldSpace, 2.0f, false, 20.0f, 0.75f, 22);
		AddAuthoredProceduralWind(Node, false, FVector(1.0f, 0.0f, 0.0f),
		                          EExternalForceSpace::WorldSpace, 3.0f, false, 30.0f, 1.0f, 33);

		Node.RequestTransientGust(3.0f, 0.1f, 0.3f, FVector::ZeroVector,
		                          FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		bOk &= TestEqual(TEXT("Spread Items.Num"), Node.TransientForceStore.Items.Num(), 2);
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind0 = GetTransientWind(Node, 0);
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind1 = GetTransientWind(Node, 1);
		bOk &= TestTrue(TEXT("Spread Wind0 valid"), Wind0 != nullptr);
		bOk &= TestTrue(TEXT("Spread Wind1 valid"), Wind1 != nullptr);
		if (Wind0)
		{
			bOk &= TestTrue(TEXT("Spread Wind0 direction"), Wind0->WindDirection.Equals(FVector(0.0f, 1.0f, 0.0f)));
			bOk &= TestEqual(TEXT("Spread Wind0 ApplyBoneFilter.Num"), Wind0->ApplyBoneFilter.Num(), 1);
		}
		if (Wind1)
		{
			bOk &= TestTrue(TEXT("Spread Wind1 direction"), Wind1->WindDirection.Equals(FVector(0.0f, 0.0f, 1.0f)));
			bOk &= TestFloatNear(*this, TEXT("Spread Wind1 TimeScale"), Wind1->TimeScale, 2.0f);
			bOk &= TestFloatNear(*this, TEXT("Spread Wind1 lifetime"),
			                      Node.TransientForceStore.Items[1].RemainingLifetime, 0.4f);
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		for (int32 Index = 1; Index <= 10; ++Index)
		{
			AddAuthoredProceduralWind(Node, true, FVector(static_cast<float>(Index), 0.0f, 0.0f),
			                          EExternalForceSpace::WorldSpace, 1.0f, false);
		}

		Node.RequestTransientGust(3.0f, 0.1f, 0.3f, FVector::ZeroVector,
		                          FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		// cap を超える authored wind では最古（先頭側）から切り捨てられる仕様を固定する。
		bOk &= TestEqual(TEXT("Spread cap Items.Num"), Node.TransientForceStore.Items.Num(),
		                 FAnimNode_KawaiiPhysics::MaxTransientExternalForces);
		for (int32 ItemIndex = 0; ItemIndex < Node.TransientForceStore.Items.Num(); ++ItemIndex)
		{
			FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node, ItemIndex);
			bOk &= TestTrue(FString::Printf(TEXT("Spread cap Wind %d valid"), ItemIndex), Wind != nullptr);
			if (Wind)
			{
				const float ExpectedDirectionX = static_cast<float>(ItemIndex + 3);
				bOk &= TestFloatNear(*this,
				                      *FString::Printf(TEXT("Spread cap Wind %d direction X"), ItemIndex),
				                      Wind->WindDirection.X,
				                      ExpectedDirectionX);
			}
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		Node.RequestTransientGust(3.0f, 0.1f, 0.3f, FVector::ZeroVector,
		                          FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		bOk &= TestEqual(TEXT("Default Items.Num"), Node.TransientForceStore.Items.Num(), 1);
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind = GetTransientWind(Node);
		bOk &= TestTrue(TEXT("Default Wind valid"), Wind != nullptr);
		if (Wind)
		{
			bOk &= TestTrue(TEXT("Default direction"), Wind->WindDirection.Equals(FVector::ForwardVector));
		}
	}

	{
		FAnimNode_KawaiiPhysics Node;
		AddAuthoredProceduralWind(Node, true, FVector(0.0f, 1.0f, 0.0f),
		                          EExternalForceSpace::ComponentSpace, 1.0f, true, 10.0f, 0.5f, 11);
		AddAuthoredProceduralWind(Node, true, FVector(0.0f, 0.0f, 1.0f),
		                          EExternalForceSpace::WorldSpace, 2.0f, false, 20.0f, 0.75f, 22);
		AddAuthoredProceduralWind(Node, false, FVector(1.0f, 0.0f, 0.0f),
		                          EExternalForceSpace::WorldSpace, 3.0f, false, 30.0f, 1.0f, 33);

		Node.RequestTransientGust(3.0f, 0.1f, 0.3f, FVector(5.0f, 0.0f, 0.0f),
		                          FAnimNode_KawaiiPhysics::TransientGustInheritAllWinds);
		Node.ConsumeAndSweepTransientExternalForces(0.0f);

		bOk &= TestEqual(TEXT("Explicit Items.Num"), Node.TransientForceStore.Items.Num(), 2);
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind0 = GetTransientWind(Node, 0);
		FKawaiiPhysics_ExternalForce_ProceduralWind* Wind1 = GetTransientWind(Node, 1);
		bOk &= TestTrue(TEXT("Explicit Wind0 valid"), Wind0 != nullptr);
		bOk &= TestTrue(TEXT("Explicit Wind1 valid"), Wind1 != nullptr);
		if (Wind0)
		{
			bOk &= TestTrue(TEXT("Explicit Wind0 direction"), Wind0->WindDirection.Equals(FVector(5.0f, 0.0f, 0.0f)));
			bOk &= TestTrue(TEXT("Explicit Wind0 space"), Wind0->ExternalForceSpace == EExternalForceSpace::WorldSpace);
			bOk &= TestEqual(TEXT("Explicit Wind0 ApplyBoneFilter.Num"), Wind0->ApplyBoneFilter.Num(), 1);
			bOk &= TestFloatNear(*this, TEXT("Explicit Wind0 WindDirectionNoiseAngle"), Wind0->WindDirectionNoiseAngle, 0.0f);
		}
		if (Wind1)
		{
			bOk &= TestTrue(TEXT("Explicit Wind1 direction"), Wind1->WindDirection.Equals(FVector(5.0f, 0.0f, 0.0f)));
			bOk &= TestTrue(TEXT("Explicit Wind1 space"), Wind1->ExternalForceSpace == EExternalForceSpace::WorldSpace);
			bOk &= TestFloatNear(*this, TEXT("Explicit Wind1 TimeScale"), Wind1->TimeScale, 2.0f);
			bOk &= TestFloatNear(*this, TEXT("Explicit Wind1 WindDirectionNoiseAngle"), Wind1->WindDirectionNoiseAngle, 0.0f);
		}
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceStoreCopyIsIndependentTest,
                                 "KawaiiPhysics.TransientForce.StoreCopyIsIndependent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceStoreCopyIsIndependentTest::RunTest(const FString& Parameters)
{
	bool bOk = true;

	{
		FAnimNode_KawaiiPhysics A;
		A.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector::ForwardVector, INDEX_NONE);
		FAnimNode_KawaiiPhysics B = A;

		bOk &= TestTrue(TEXT("Copied queue is distinct"),
		                A.TransientForceStore.Queue.Get() != B.TransientForceStore.Queue.Get());
		bOk &= TestEqual(TEXT("B pending is empty after copy"), GetPendingGustCount(B), 0);
		B.ConsumeAndSweepTransientExternalForces(0.0f);
		bOk &= TestEqual(TEXT("B consume yields no items"), B.TransientForceStore.Items.Num(), 0);

		A.ConsumeAndSweepTransientExternalForces(0.0f);
		bOk &= TestEqual(TEXT("A consume still yields one item"), A.TransientForceStore.Items.Num(), 1);

		B.RequestTransientGust(2.0f, 0.1f, 0.1f, FVector::RightVector, INDEX_NONE);
		B.ConsumeAndSweepTransientExternalForces(0.0f);
		bOk &= TestEqual(TEXT("B new consume yields one item"), B.TransientForceStore.Items.Num(), 1);
		bOk &= TestEqual(TEXT("A items unaffected by B"), A.TransientForceStore.Items.Num(), 1);
	}

	{
		FAnimNode_KawaiiPhysics A;
		A.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector::ForwardVector, INDEX_NONE);
		A.ConsumeAndSweepTransientExternalForces(0.0f);
		FAnimNode_KawaiiPhysics B = A;

		bOk &= TestEqual(TEXT("B copied Items empty"), B.TransientForceStore.Items.Num(), 0);
		bOk &= TestEqual(TEXT("A copied-from Items preserved"), A.TransientForceStore.Items.Num(), 1);
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsTransientForceSurviveReflectionCopyTest,
                                 "KawaiiPhysics.TransientForce.SurviveReflectionCopy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsTransientForceSurviveReflectionCopyTest::RunTest(const FString& Parameters)
{
	FAnimNode_KawaiiPhysics A;
	A.RequestTransientGust(1.0f, 0.1f, 0.1f, FVector::ForwardVector, INDEX_NONE);
	A.ConsumeAndSweepTransientExternalForces(0.0f);
	A.RequestTransientGust(2.0f, 0.1f, 0.1f, FVector::RightVector, INDEX_NONE);

	bool bOk = true;
	bOk &= TestEqual(TEXT("Initial Items"), A.TransientForceStore.Items.Num(), 1);
	bOk &= TestEqual(TEXT("Initial pending"), GetPendingGustCount(A), 1);
	bOk &= TestTrue(TEXT("TransientForceStore is not reflected"),
	                FindFProperty<FProperty>(FAnimNode_KawaiiPhysics::StaticStruct(), TEXT("TransientForceStore")) == nullptr);

	ApplyDefaultPresetStyleCopy(A);

	bOk &= TestEqual(TEXT("Items survive preset-style copy"), A.TransientForceStore.Items.Num(), 1);
	bOk &= TestEqual(TEXT("Pending survives preset-style copy"), GetPendingGustCount(A), 1);

	return bOk;
}

#endif
