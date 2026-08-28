// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsMirrorUtils.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	constexpr float GMirrorVectorTol = 0.0001f;
	constexpr float GMirrorQuatTol = 0.0001f;

	bool IsSameRotation(const FQuat& A, const FQuat& B, float Tolerance = GMirrorQuatTol)
	{
		return A.GetNormalized().AngularDistance(B.GetNormalized()) <= Tolerance;
	}

	FQuat MakeQuat(float Pitch, float Yaw, float Roll)
	{
		return FRotator(Pitch, Yaw, Roll).Quaternion();
	}

	FVector ExpectedMirroredVector(const FVector& Vector, EAxis::Type MirrorAxis)
	{
		FVector Result = Vector;
		switch (MirrorAxis)
		{
		case EAxis::X:
			Result.X = -Result.X;
			break;
		case EAxis::Y:
			Result.Y = -Result.Y;
			break;
		case EAxis::Z:
			Result.Z = -Result.Z;
			break;
		default:
			break;
		}
		return Result;
	}

	FQuat ExpectedMirroredZRotation(float Degrees, EAxis::Type MirrorAxis)
	{
		const float ExpectedDegrees = MirrorAxis == EAxis::Z ? Degrees : -Degrees;
		return FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(ExpectedDegrees));
	}

	FSphericalLimit MakeSphere(FName BoneName, ECollisionSourceType SourceType = ECollisionSourceType::AnimNode)
	{
		FSphericalLimit Limit;
		Limit.DrivingBone = FBoneReference(BoneName);
		Limit.OffsetLocation = FVector(1.0f, 2.0f, 3.0f);
		Limit.OffsetRotation = FRotator(0.0f, 30.0f, 0.0f);
		Limit.SourceType = SourceType;
		Limit.Radius = 7.0f;
		Limit.LimitType = ESphericalLimitType::Inner;
		Limit.bEnable = true;
		return Limit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorIdentityOffsetTest,
                                 "KawaiiPhysics.Mirror.IdentityOffset",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorIdentityOffsetTest::RunTest(const FString& Parameters)
{
	const FQuat SourceRotations[] =
	{
		MakeQuat(10.0f, 20.0f, 30.0f),
		MakeQuat(-35.0f, 5.0f, 70.0f),
		MakeQuat(0.0f, 90.0f, -15.0f),
	};
	const FQuat TargetRotations[] =
	{
		MakeQuat(-12.0f, 40.0f, 9.0f),
		MakeQuat(50.0f, -20.0f, 33.0f),
		MakeQuat(5.0f, 10.0f, 80.0f),
	};
	const EAxis::Type Axes[] = { EAxis::X, EAxis::Y, EAxis::Z };

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Axes); ++Index)
	{
		const FVector MirroredLocation = KawaiiPhysicsMirrorUtils::MirrorOffsetLocation(
			FVector::ZeroVector, SourceRotations[Index], TargetRotations[Index], Axes[Index]);
		TestTrue(TEXT("Zero offset location remains zero"),
		         MirroredLocation.Equals(FVector::ZeroVector, KINDA_SMALL_NUMBER));

		const FQuat MirroredRotation = KawaiiPhysicsMirrorUtils::MirrorOffsetRotation(
			FQuat::Identity, SourceRotations[Index], TargetRotations[Index], Axes[Index]);
		TestTrue(TEXT("Identity offset rotation remains identity"),
		         IsSameRotation(MirroredRotation, FQuat::Identity, KINDA_SMALL_NUMBER));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorSymmetricSkeletonTest,
                                 "KawaiiPhysics.Mirror.SymmetricSkeleton",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorSymmetricSkeletonTest::RunTest(const FString& Parameters)
{
	const FVector SourceLocation(1.0f, 2.0f, 3.0f);
	const FQuat SourceRotation = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0f));
	const FQuat SourceBoneRefCS = MakeQuat(15.0f, -25.0f, 40.0f);
	const EAxis::Type Axes[] = { EAxis::X, EAxis::Y, EAxis::Z };

	for (EAxis::Type MirrorAxis : Axes)
	{
		const FQuat TargetBoneRefCS = FAnimationRuntime::MirrorQuat(SourceBoneRefCS, MirrorAxis);

		const FVector MirroredLocation = KawaiiPhysicsMirrorUtils::MirrorOffsetLocation(
			SourceLocation, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
		const FVector ExpectedLocation = ExpectedMirroredVector(SourceLocation, MirrorAxis);
		TestTrue(FString::Printf(TEXT("Symmetric location: got %s expected %s"),
		                         *MirroredLocation.ToString(), *ExpectedLocation.ToString()),
		         MirroredLocation.Equals(ExpectedLocation, GMirrorVectorTol));

		const FQuat MirroredRotation = KawaiiPhysicsMirrorUtils::MirrorOffsetRotation(
			SourceRotation, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
		const FQuat ExpectedRotation = ExpectedMirroredZRotation(30.0f, MirrorAxis);
		TestTrue(TEXT("Symmetric rotation matches the hand-derived mirrored Z rotation"),
		         IsSameRotation(MirroredRotation, ExpectedRotation));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorRoundTripTest,
                                 "KawaiiPhysics.Mirror.RoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorRoundTripTest::RunTest(const FString& Parameters)
{
	const FVector SourceLocation(4.0f, -5.0f, 6.0f);
	const FQuat SourceRotation = MakeQuat(12.0f, -23.0f, 34.0f);
	const FQuat SourceBoneRefCS = MakeQuat(20.0f, 35.0f, -10.0f);
	const FQuat TargetBoneRefCS = MakeQuat(-15.0f, 70.0f, 25.0f);
	const EAxis::Type Axes[] = { EAxis::X, EAxis::Y, EAxis::Z };

	for (EAxis::Type MirrorAxis : Axes)
	{
		const FVector MirroredLocation = KawaiiPhysicsMirrorUtils::MirrorOffsetLocation(
			SourceLocation, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
		const FVector RoundTripLocation = KawaiiPhysicsMirrorUtils::MirrorOffsetLocation(
			MirroredLocation, TargetBoneRefCS, SourceBoneRefCS, MirrorAxis);
		TestTrue(FString::Printf(TEXT("Round-trip location: got %s expected %s"),
		                         *RoundTripLocation.ToString(), *SourceLocation.ToString()),
		         RoundTripLocation.Equals(SourceLocation, 0.01f));

		const FQuat MirroredRotation = KawaiiPhysicsMirrorUtils::MirrorOffsetRotation(
			SourceRotation, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
		const FQuat RoundTripRotation = KawaiiPhysicsMirrorUtils::MirrorOffsetRotation(
			MirroredRotation, TargetBoneRefCS, SourceBoneRefCS, MirrorAxis);
		TestTrue(TEXT("Round-trip rotation returns to source"),
		         IsSameRotation(RoundTripRotation, SourceRotation, 0.01f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorBuildComponentRefRotationsTest,
                                 "KawaiiPhysics.Mirror.BuildComponentSpaceRefRotations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorBuildComponentRefRotationsTest::RunTest(const FString& Parameters)
{
	FReferenceSkeleton RefSkeleton;

	const FQuat RootRotation = MakeQuat(10.0f, 0.0f, 0.0f);
	const FQuat SpineRotation = MakeQuat(0.0f, 20.0f, 0.0f);
	const FQuat ArmRotation = MakeQuat(0.0f, 0.0f, 30.0f);
	const FQuat LegRotation = MakeQuat(-15.0f, 25.0f, 5.0f);

	{
		FReferenceSkeletonModifier Modifier(RefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(FName(TEXT("root")), TEXT("root"), INDEX_NONE), FTransform(RootRotation));
		Modifier.Add(FMeshBoneInfo(FName(TEXT("spine")), TEXT("spine"), 0), FTransform(SpineRotation));
		Modifier.Add(FMeshBoneInfo(FName(TEXT("arm_l")), TEXT("arm_l"), 1), FTransform(ArmRotation));
		Modifier.Add(FMeshBoneInfo(FName(TEXT("leg_l")), TEXT("leg_l"), 0), FTransform(LegRotation));
	}

	TArray<FQuat> CSRefRotations;
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefRotations(RefSkeleton, CSRefRotations);

	TestTrue(TEXT("Component-space ref rotations count matches bone count"), CSRefRotations.Num() == 4);
	TestTrue(TEXT("Root CS rotation matches local"), IsSameRotation(CSRefRotations[0], RootRotation));
	TestTrue(TEXT("Spine CS rotation is root * spine"), IsSameRotation(CSRefRotations[1], RootRotation * SpineRotation));
	TestTrue(TEXT("Arm CS rotation is root * spine * arm"),
	         IsSameRotation(CSRefRotations[2], RootRotation * SpineRotation * ArmRotation));
	TestTrue(TEXT("Leg CS rotation is root * leg"), IsSameRotation(CSRefRotations[3], RootRotation * LegRotation));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsAppendMirroredLimitsTest,
                                 "KawaiiPhysics.Mirror.AppendMirroredLimits",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsAppendMirroredLimitsTest::RunTest(const FString& Parameters)
{
	const FName SourceBoneName(TEXT("arm_l"));
	const FName TargetBoneName(TEXT("arm_r"));
	const FName CenterBoneName(TEXT("spine"));

	auto ResolveMirrorBoneName = [SourceBoneName, TargetBoneName](FName BoneName) -> FName
	{
		return BoneName == SourceBoneName ? TargetBoneName : NAME_None;
	};
	auto FindBoneIndex = [SourceBoneName, TargetBoneName, CenterBoneName](FName BoneName) -> int32
	{
		if (BoneName == SourceBoneName)
		{
			return 0;
		}
		if (BoneName == TargetBoneName)
		{
			return 1;
		}
		if (BoneName == CenterBoneName)
		{
			return 2;
		}
		return INDEX_NONE;
	};

	TArray<FQuat> CSRefRotations;
	CSRefRotations.Add(FQuat::Identity);
	CSRefRotations.Add(FQuat::Identity);
	CSRefRotations.Add(FQuat::Identity);

	TArray<FSphericalLimit> SourceLimits;
	SourceLimits.Add(MakeSphere(SourceBoneName, ECollisionSourceType::DataAsset));
#if WITH_EDITORONLY_DATA
	const FGuid SourceGuid = SourceLimits[0].Guid;
#endif

	TArray<FSphericalLimit> CenterSourceLimits;
	CenterSourceLimits.Add(MakeSphere(CenterBoneName));

	TArray<FSphericalLimit> OutNewLimits;
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		CenterSourceLimits, TArray<FSphericalLimit>(), TArray<FSphericalLimit>(), true,
		ResolveMirrorBoneName, FindBoneIndex, CSRefRotations, EAxis::X, OutNewLimits);
	TestTrue(TEXT("Center or unresolved mirror bones are skipped"), OutNewLimits.Num() == 0);

	TArray<FSphericalLimit> ExistingNonMirror;
	ExistingNonMirror.Add(MakeSphere(TargetBoneName, ECollisionSourceType::AnimNode));
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingNonMirror, TArray<FSphericalLimit>(), true,
		ResolveMirrorBoneName, FindBoneIndex, CSRefRotations, EAxis::X, OutNewLimits);
	TestTrue(TEXT("Existing non-Mirror target collision skips generation"), OutNewLimits.Num() == 0);

	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingNonMirror, TArray<FSphericalLimit>(), false,
		ResolveMirrorBoneName, FindBoneIndex, CSRefRotations, EAxis::X, OutNewLimits);
	TestTrue(TEXT("bSkipExisting=false allows generation"), OutNewLimits.Num() == 1);
	OutNewLimits.Reset();

	TArray<FSphericalLimit> ExistingMirror;
	ExistingMirror.Add(MakeSphere(TargetBoneName, ECollisionSourceType::Mirror));
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingMirror, TArray<FSphericalLimit>(), true,
		ResolveMirrorBoneName, FindBoneIndex, CSRefRotations, EAxis::X, OutNewLimits);

	TestTrue(TEXT("Existing Mirror-sourced collisions do not block generation"), OutNewLimits.Num() == 1);
	if (OutNewLimits.Num() > 0)
	{
		const FSphericalLimit& Generated = OutNewLimits[0];
		TestTrue(TEXT("Generated limit targets the mirrored bone"), Generated.DrivingBone.BoneName == TargetBoneName);
		TestTrue(TEXT("Generated limit source type is Mirror"),
		         Generated.SourceType == ECollisionSourceType::Mirror);
		TestTrue(TEXT("Generated limit preserves radius"),
		         FMath::IsNearlyEqual(Generated.Radius, SourceLimits[0].Radius, GMirrorVectorTol));
		TestTrue(TEXT("Generated limit preserves limit type"), Generated.LimitType == SourceLimits[0].LimitType);
		TestTrue(TEXT("Generated limit mirrors offset location"),
		         Generated.OffsetLocation.Equals(FVector(-1.0f, 2.0f, 3.0f), GMirrorVectorTol));
		TestTrue(TEXT("Generated limit mirrors offset rotation"),
		         IsSameRotation(Generated.OffsetRotation.Quaternion(),
		                        FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(-30.0f))));
#if WITH_EDITORONLY_DATA
		TestTrue(TEXT("Generated limit gets a new Guid"), Generated.Guid != SourceGuid);
		TestTrue(TEXT("Generated limit preserves editor limit type"),
		         Generated.Type == SourceLimits[0].Type);
#endif
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
