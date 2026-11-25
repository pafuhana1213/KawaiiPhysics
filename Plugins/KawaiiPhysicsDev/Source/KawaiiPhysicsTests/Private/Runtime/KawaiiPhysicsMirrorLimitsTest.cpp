// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsMirrorUtils.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#if WITH_EDITOR
#include "KawaiiPhysicsTestHarness.h"
#include "Animation/MirrorDataTable.h"
#include "Animation/Skeleton.h"
#include "UObject/StrongObjectPtr.h"
#endif

namespace KawaiiPhysicsMirrorLimitsTestPrivate
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

	template <typename TLimit>
	TArray<TLimit> MakeMirroredLimits(const TLimit& Source, const FTransform& SourceBoneRefCS,
	                                const FTransform& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		const FName SourceBoneName(TEXT("source_l"));
		const FName TargetBoneName(TEXT("target_r"));
		TArray<TLimit> SourceLimits;
		SourceLimits.Add(Source);
		TArray<FTransform> CSRefTransforms;
		CSRefTransforms.Add(SourceBoneRefCS);
		CSRefTransforms.Add(TargetBoneRefCS);
		TArray<TLimit> MirroredLimits;
		KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
			SourceLimits, TArray<TLimit>(), TArray<TLimit>(), false,
			[SourceBoneName, TargetBoneName](FName BoneName) -> FName
			{
				return BoneName == SourceBoneName ? TargetBoneName : NAME_None;
			},
			[SourceBoneName, TargetBoneName](FName BoneName) -> int32
			{
				return BoneName == SourceBoneName ? 0 : BoneName == TargetBoneName ? 1 : INDEX_NONE;
			},
			CSRefTransforms, MirrorAxis, MirroredLimits);
		return MirroredLimits;
	}

	template <typename TLimit>
	TArray<TLimit> MakeMirroredLimits(const TLimit& Source, const FQuat& SourceBoneRefCS,
	                                const FQuat& TargetBoneRefCS, EAxis::Type MirrorAxis)
	{
		return MakeMirroredLimits(Source, FTransform(SourceBoneRefCS), FTransform(TargetBoneRefCS), MirrorAxis);
	}

	template <typename TLimit>
	FVector GetCenterCS(const TLimit& Limit, const FQuat& BoneRefCS)
	{
		return BoneRefCS.RotateVector(Limit.OffsetLocation);
	}

	template <typename TLimit>
	FVector GetEndpointCS(const TLimit& Limit, const FQuat& BoneRefCS, float EndSign)
	{
		const FQuat RotationCS = BoneRefCS * Limit.OffsetRotation.Quaternion();
		return GetCenterCS(Limit, BoneRefCS) + RotationCS.GetAxisZ() * Limit.Length * 0.5f * EndSign;
	}

	bool HasSamePointSet(const TArray<FVector>& Actual, const TArray<FVector>& Expected, float Tolerance)
	{
		if (Actual.Num() != Expected.Num())
		{
			return false;
		}
		for (const FVector& Point : Expected)
		{
			if (!Actual.ContainsByPredicate([&Point, Tolerance](const FVector& Candidate)
				{
				return Candidate.Equals(Point, Tolerance);
				}))
			{
				return false;
			}
		}
		return true;
	}

	TArray<FVector> GetBoxCornersCS(const FBoxLimit& Limit, const FQuat& BoneRefCS)
	{
		const FVector CenterCS = GetCenterCS(Limit, BoneRefCS);
		const FQuat RotationCS = BoneRefCS * Limit.OffsetRotation.Quaternion();
		TArray<FVector> Corners;
		for (int32 XSign : {-1, 1})
		{
			for (int32 YSign : {-1, 1})
			{
				for (int32 ZSign : {-1, 1})
				{
					Corners.Add(CenterCS + RotationCS.RotateVector(FVector(
						Limit.Extent.X * XSign, Limit.Extent.Y * YSign, Limit.Extent.Z * ZSign)));
				}
			}
		}
		return Corners;
	}

	TArray<FVector> GetBoxCornersCS(const FBoxLimit& Limit, const FTransform& BoneRefCS)
	{
		const FVector CenterCS = BoneRefCS.TransformVector(Limit.OffsetLocation);
		const FQuat RotationCS = BoneRefCS.GetRotation() * Limit.OffsetRotation.Quaternion();
		TArray<FVector> Corners;
		for (int32 XSign : {-1, 1})
		{
			for (int32 YSign : {-1, 1})
			{
			for (int32 ZSign : {-1, 1})
				{
				Corners.Add(CenterCS + RotationCS.RotateVector(FVector(
					Limit.Extent.X * XSign, Limit.Extent.Y * YSign, Limit.Extent.Z * ZSign)));
			}
			}
		}
		return Corners;
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
			SourceLocation, FTransform(SourceBoneRefCS), FTransform(TargetBoneRefCS), MirrorAxis);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorAsymmetricBoneOriginsStayBoneRelativeTest,
	"KawaiiPhysics.Mirror.AsymmetricBoneOriginsStayBoneRelative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorAsymmetricBoneOriginsStayBoneRelativeTest::RunTest(const FString& Parameters)
{
	const FTransform SourceBoneRefCS(FQuat::Identity, FVector(10.0f, 0.0f, 0.0f));
	const FTransform TargetBoneRefCS(FQuat::Identity, FVector(-9.0f, 0.0f, 0.0f));
	const FVector ExpectedOffset(-1.0f, 0.0f, 0.0f);
	const FVector ExpectedCenterCS(-10.0f, 0.0f, 0.0f);

	// 元ボーンの CS 原点 (10,0,0) と先ボーンの CS 原点 (-9,0,0) は非対称。
	// 元の OffsetLocation (1,0,0) を X ミラーすると先ボーン基準で (-1,0,0) となり、
	// 生成形状の CS 中心は -9 + (-1) = -10。元の CS 中心 11 の厳密な鏡像 -11 にはしない。
	FSphericalLimit Sphere = MakeSphere(TEXT("source_l"));
	Sphere.OffsetLocation = FVector(1.0f, 0.0f, 0.0f);
	const TArray<FSphericalLimit> MirroredSpheres = MakeMirroredLimits(
		Sphere, SourceBoneRefCS, TargetBoneRefCS, EAxis::X);
	if (TestEqual(TEXT("Asymmetric-origin Sphere count"), MirroredSpheres.Num(), 1))
	{
		TestTrue(TEXT("Sphere offset stays bone-relative"),
			MirroredSpheres[0].OffsetLocation.Equals(ExpectedOffset, GMirrorVectorTol));
		TestTrue(TEXT("Sphere CS center uses the target bone origin"),
			TargetBoneRefCS.TransformPosition(MirroredSpheres[0].OffsetLocation).Equals(
				ExpectedCenterCS, GMirrorVectorTol));
	}

	FBoxLimit Box;
	Box.DrivingBone = Sphere.DrivingBone;
	Box.OffsetLocation = Sphere.OffsetLocation;
	Box.Extent = FVector(2.0f, 3.0f, 4.0f);
	const TArray<FBoxLimit> MirroredBoxes = MakeMirroredLimits(
		Box, SourceBoneRefCS, TargetBoneRefCS, EAxis::X);
	if (TestEqual(TEXT("Asymmetric-origin Box count"), MirroredBoxes.Num(), 1))
	{
		TestTrue(TEXT("Box offset stays bone-relative"),
			MirroredBoxes[0].OffsetLocation.Equals(ExpectedOffset, GMirrorVectorTol));
		TestTrue(TEXT("Box CS center uses the target bone origin"),
			TargetBoneRefCS.TransformPosition(MirroredBoxes[0].OffsetLocation).Equals(
				ExpectedCenterCS, GMirrorVectorTol));
	}

	// 先ボーンを Z 軸まわりに 90 度回しても、生成オフセットの CS 相対位置は (-1,0,0)。
	const FQuat TargetQuarterTurn(FVector::ZAxisVector, PI * 0.5f);
	const FTransform RotatedTargetBoneRefCS(TargetQuarterTurn, FVector(-9.0f, 0.0f, 0.0f));
	const TArray<FSphericalLimit> RotatedMirroredSpheres = MakeMirroredLimits(
		Sphere, SourceBoneRefCS, RotatedTargetBoneRefCS, EAxis::X);
	if (TestEqual(TEXT("Rotated asymmetric-origin Sphere count"), RotatedMirroredSpheres.Num(), 1))
	{
		TestTrue(TEXT("Rotated target offset has the mirrored CS relative position"),
			TargetQuarterTurn.RotateVector(RotatedMirroredSpheres[0].OffsetLocation).Equals(
				FVector(-1.0f, 0.0f, 0.0f), GMirrorVectorTol));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorAsymmetricSkeletonShapesTest,
                                 "KawaiiPhysics.Mirror.AsymmetricSkeletonShapes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorAsymmetricSkeletonShapesTest::RunTest(const FString& Parameters)
{
	const FQuat SourceBoneRefCS = MakeQuat(18.0f, -27.0f, 33.0f);
	const FQuat OffsetRotation = MakeQuat(12.0f, 37.0f, -21.0f);
	const EAxis::Type Axes[] = {EAxis::X, EAxis::Y, EAxis::Z};
	const FQuat TargetExtraRotations[] = {
		FQuat(FVector::ZAxisVector, PI * 0.5f),
		FQuat(FVector(1.0f, 2.0f, 3.0f).GetSafeNormal(), FMath::DegreesToRadians(41.0f))
	};

	for (EAxis::Type MirrorAxis : Axes)
	{
		for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(TargetExtraRotations); ++CaseIndex)
		{
			const FQuat TargetBoneRefCS =
				FAnimationRuntime::MirrorQuat(SourceBoneRefCS, MirrorAxis) * TargetExtraRotations[CaseIndex];
			const FString Case = FString::Printf(TEXT("axis=%d case=%d"), static_cast<int32>(MirrorAxis), CaseIndex);

			FBoxLimit Box;
			Box.DrivingBone = FBoneReference(FName(TEXT("source_l")));
			Box.OffsetLocation = FVector(3.0f, -4.0f, 7.0f);
			Box.OffsetRotation = OffsetRotation.Rotator();
			Box.Extent = FVector(2.0f, 5.0f, 9.0f);
			const TArray<FBoxLimit> MirroredBoxes = MakeMirroredLimits(Box, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
			if (!TestEqual(FString::Printf(TEXT("Box count %s"), *Case), MirroredBoxes.Num(), 1))
			{
				continue;
			}
			TArray<FVector> ExpectedCorners;
			for (const FVector& Point : GetBoxCornersCS(Box, SourceBoneRefCS))
			{
				ExpectedCorners.Add(ExpectedMirroredVector(Point, MirrorAxis));
			}
			TestTrue(FString::Printf(TEXT("Box corner set %s"), *Case),
				HasSamePointSet(GetBoxCornersCS(MirroredBoxes[0], TargetBoneRefCS), ExpectedCorners, 1e-3f));

			FCapsuleLimit Capsule;
			Capsule.DrivingBone = Box.DrivingBone;
			Capsule.OffsetLocation = Box.OffsetLocation;
			Capsule.OffsetRotation = Box.OffsetRotation;
			Capsule.Length = 17.0f;
			Capsule.Radius = 3.0f;
			const TArray<FCapsuleLimit> MirroredCapsules =
				MakeMirroredLimits(Capsule, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
			if (!TestEqual(FString::Printf(TEXT("Capsule count %s"), *Case), MirroredCapsules.Num(), 1))
			{
				continue;
			}
			const TArray<FVector> ExpectedCapsuleEnds = {
				ExpectedMirroredVector(GetEndpointCS(Capsule, SourceBoneRefCS, 1.0f), MirrorAxis),
				ExpectedMirroredVector(GetEndpointCS(Capsule, SourceBoneRefCS, -1.0f), MirrorAxis)
			};
			const TArray<FVector> GeneratedCapsuleEnds = {
				GetEndpointCS(MirroredCapsules[0], TargetBoneRefCS, 1.0f),
				GetEndpointCS(MirroredCapsules[0], TargetBoneRefCS, -1.0f)
			};
			TestTrue(FString::Printf(TEXT("Capsule endpoint set %s"), *Case),
				HasSamePointSet(GeneratedCapsuleEnds, ExpectedCapsuleEnds, 1e-3f));
			TestTrue(FString::Printf(TEXT("Capsule radius %s"), *Case),
				FMath::IsNearlyEqual(MirroredCapsules[0].Radius, Capsule.Radius));

			FTaperedCapsuleLimit Tapered;
			Tapered.DrivingBone = Box.DrivingBone;
			Tapered.OffsetLocation = Box.OffsetLocation;
			Tapered.OffsetRotation = Box.OffsetRotation;
			Tapered.Length = 17.0f;
			Tapered.Radius0 = 3.0f;
			Tapered.Radius1 = 6.0f;
			const TArray<FTaperedCapsuleLimit> MirroredTapered =
				MakeMirroredLimits(Tapered, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
			if (!TestEqual(FString::Printf(TEXT("Tapered count %s"), *Case), MirroredTapered.Num(), 1))
			{
				continue;
			}
			const FTaperedCapsuleLimit& Generated = MirroredTapered[0];
			TestTrue(FString::Printf(TEXT("Tapered radius pair %s"), *Case),
				(FMath::IsNearlyEqual(Generated.Radius0, Tapered.Radius0)
					&& FMath::IsNearlyEqual(Generated.Radius1, Tapered.Radius1))
				|| (FMath::IsNearlyEqual(Generated.Radius0, Tapered.Radius1)
					&& FMath::IsNearlyEqual(Generated.Radius1, Tapered.Radius0)));
			const FVector GeneratedRadius0End = GetEndpointCS(Generated, TargetBoneRefCS,
				FMath::IsNearlyEqual(Generated.Radius0, Tapered.Radius0) ? 1.0f : -1.0f);
			const FVector GeneratedRadius1End = GetEndpointCS(Generated, TargetBoneRefCS,
				FMath::IsNearlyEqual(Generated.Radius1, Tapered.Radius1) ? -1.0f : 1.0f);
			TestTrue(FString::Printf(TEXT("Tapered Radius0 endpoint %s"), *Case),
				GeneratedRadius0End.Equals(
					ExpectedMirroredVector(GetEndpointCS(Tapered, SourceBoneRefCS, 1.0f), MirrorAxis), 1e-3f));
			TestTrue(FString::Printf(TEXT("Tapered Radius1 endpoint %s"), *Case),
				GeneratedRadius1End.Equals(
					ExpectedMirroredVector(GetEndpointCS(Tapered, SourceBoneRefCS, -1.0f), MirrorAxis), 1e-3f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorPlanarNormalTest,
                                 "KawaiiPhysics.Mirror.PlanarNormal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorPlanarNormalTest::RunTest(const FString& Parameters)
{
	const FQuat SourceBoneRefCS = MakeQuat(17.0f, -31.0f, 24.0f);
	const EAxis::Type Axes[] = {EAxis::X, EAxis::Y, EAxis::Z};
	FPlanarLimit Plane;
	Plane.DrivingBone = FBoneReference(FName(TEXT("source_l")));
	Plane.OffsetLocation = FVector(4.0f, -6.0f, 2.0f);
	Plane.OffsetRotation = FRotator(23.0f, 11.0f, -38.0f);
	for (EAxis::Type MirrorAxis : Axes)
	{
		const FQuat SymmetricTarget = FAnimationRuntime::MirrorQuat(SourceBoneRefCS, MirrorAxis);
		const FQuat TargetRotations[] = {
			SymmetricTarget,
			SymmetricTarget * FQuat(FVector(2.0f, 1.0f, -3.0f).GetSafeNormal(), FMath::DegreesToRadians(53.0f))
		};
		for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(TargetRotations); ++CaseIndex)
		{
			const FQuat TargetBoneRefCS = TargetRotations[CaseIndex];
			const TArray<FPlanarLimit> MirroredPlanes =
				MakeMirroredLimits(Plane, SourceBoneRefCS, TargetBoneRefCS, MirrorAxis);
			if (!TestEqual(TEXT("Planar count"), MirroredPlanes.Num(), 1))
			{
				continue;
			}
			const FPlanarLimit& Generated = MirroredPlanes[0];
			const FString Case = FString::Printf(TEXT("axis=%d case=%d"), static_cast<int32>(MirrorAxis), CaseIndex);
			TestTrue(FString::Printf(TEXT("Planar point %s"), *Case),
				GetCenterCS(Generated, TargetBoneRefCS).Equals(
					ExpectedMirroredVector(GetCenterCS(Plane, SourceBoneRefCS), MirrorAxis), 1e-3f));
			const FVector ExpectedNormal = ExpectedMirroredVector(
				(SourceBoneRefCS * Plane.OffsetRotation.Quaternion()).GetAxisZ(), MirrorAxis).GetSafeNormal();
			const FVector GeneratedNormal =
				(TargetBoneRefCS * Generated.OffsetRotation.Quaternion()).GetAxisZ().GetSafeNormal();
			TestTrue(FString::Printf(TEXT("Planar oriented normal %s"), *Case),
				FVector::DotProduct(GeneratedNormal, ExpectedNormal) >= 1.0f - 1e-4f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorScaledReferencePoseTest,
	"KawaiiPhysics.Mirror.ScaledReferencePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorScaledReferencePoseTest::RunTest(const FString& Parameters)
{
	const FQuat QuarterTurn(FVector::ZAxisVector, PI * 0.5f);
	const FTransform SourceRefs[] = {
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f)),
		FTransform(QuarterTurn, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f))
	};
	const FTransform TargetRefs[] = {
		FTransform::Identity,
		FTransform(FQuat(FVector::ZAxisVector, PI * 0.25f))
	};
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(SourceRefs); ++CaseIndex)
	{
		FSphericalLimit Sphere = MakeSphere(TEXT("source_l"));
		Sphere.OffsetLocation = FVector(1.0f, 0.0f, 0.0f);
		const TArray<FSphericalLimit> MirroredSpheres = MakeMirroredLimits(
			Sphere, SourceRefs[CaseIndex], TargetRefs[CaseIndex], EAxis::X);
		if (!TestEqual(TEXT("Scaled Sphere count"), MirroredSpheres.Num(), 1))
		{
			continue;
		}
		const FVector ExpectedCenter = ExpectedMirroredVector(
			SourceRefs[CaseIndex].TransformVector(Sphere.OffsetLocation), EAxis::X);
		const FVector GeneratedCenter = TargetRefs[CaseIndex].TransformVector(MirroredSpheres[0].OffsetLocation);
		TestTrue(TEXT("Scaled Sphere CS center is the mirrored source center"),
			GeneratedCenter.Equals(ExpectedCenter, GMirrorVectorTol));
		if (CaseIndex == 0)
		{
			// 元の CS 中心は (2,0,0)。X ミラー先のスケールは 1 なので生成 CS 中心は (-2,0,0)。
			TestTrue(TEXT("Identity rotations give the hand-derived center"),
				GeneratedCenter.Equals(FVector(-2.0f, 0.0f, 0.0f), GMirrorVectorTol));
		}

		FBoxLimit Box;
		Box.DrivingBone = Sphere.DrivingBone;
		Box.OffsetLocation = Sphere.OffsetLocation;
		Box.OffsetRotation = FRotator(15.0f, 25.0f, -10.0f);
		Box.Extent = FVector(2.0f, 3.0f, 4.0f);
		const TArray<FBoxLimit> MirroredBoxes = MakeMirroredLimits(
			Box, SourceRefs[CaseIndex], TargetRefs[CaseIndex], EAxis::X);
		if (!TestEqual(TEXT("Scaled Box count"), MirroredBoxes.Num(), 1))
		{
			continue;
		}
		TestTrue(TEXT("Scaled Box CS center is the mirrored source center"),
			TargetRefs[CaseIndex].TransformVector(MirroredBoxes[0].OffsetLocation).Equals(
				ExpectedCenter, GMirrorVectorTol));
		TArray<FVector> ExpectedCorners;
		for (const FVector& Corner : GetBoxCornersCS(Box, SourceRefs[CaseIndex]))
		{
			ExpectedCorners.Add(ExpectedMirroredVector(Corner, EAxis::X));
		}
		TestTrue(TEXT("Scaled Box CS corner set is mirrored"), HasSamePointSet(
			GetBoxCornersCS(MirroredBoxes[0], TargetRefs[CaseIndex]), ExpectedCorners, 1e-3f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorScaledTargetReferencePoseTest,
	"KawaiiPhysics.Mirror.ScaledTargetReferencePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorScaledTargetReferencePoseTest::RunTest(const FString& Parameters)
{
	const FQuat QuarterTurn(FVector::ZAxisVector, PI * 0.5f);
	const FQuat EighthTurn(FVector::ZAxisVector, PI * 0.25f);
	const FTransform SourceRefs[] = {
		FTransform::Identity,
		FTransform(QuarterTurn)
	};
	const FTransform TargetRefs[] = {
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f)),
		FTransform(EighthTurn, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f))
	};
	FSphericalLimit Sphere = MakeSphere(TEXT("source_l"));
	Sphere.OffsetLocation = FVector(1.0f, 0.0f, 0.0f);
	for (int32 CaseIndex = 0; CaseIndex < UE_ARRAY_COUNT(SourceRefs); ++CaseIndex)
	{
		const TArray<FSphericalLimit> MirroredSpheres = MakeMirroredLimits(
			Sphere, SourceRefs[CaseIndex], TargetRefs[CaseIndex], EAxis::X);
		if (!TestEqual(FString::Printf(TEXT("Scaled target Sphere count case=%d"), CaseIndex),
			MirroredSpheres.Num(), 1))
		{
			continue;
		}
		const FVector ExpectedCenter = ExpectedMirroredVector(
			SourceRefs[CaseIndex].TransformVector(Sphere.OffsetLocation), EAxis::X);
		const FVector GeneratedCenter = TargetRefs[CaseIndex].TransformVector(MirroredSpheres[0].OffsetLocation);
		TestTrue(FString::Printf(TEXT("Scaled target Sphere CS center case=%d"), CaseIndex),
			GeneratedCenter.Equals(ExpectedCenter, GMirrorVectorTol));
		if (CaseIndex == 0)
		{
			// 元の CS 中心は (1,0,0)、鏡像は (-1,0,0)。正しい生成オフセットは (-0.5,0,0) で CS 中心は (-1,0,0)。
			// 先ボーンのスケールを無視すると生成オフセットは (-1,0,0)、CS 中心は (-2,0,0) になる。
			TestTrue(TEXT("Scaled target identity rotations give the hand-derived center"),
				GeneratedCenter.Equals(FVector(-1.0f, 0.0f, 0.0f), GMirrorVectorTol));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorPlanarHistoryKeptAcrossRebuildTest,
	"KawaiiPhysics.Mirror.PlanarHistoryKeptAcrossRebuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorPlanarHistoryKeptAcrossRebuildTest::RunTest(const FString& Parameters)
{
	FPlanarLimit Source;
	Source.DrivingBone = FBoneReference(TEXT("source_l"));
	Source.PreviousStepPlane = FPlane(FVector(3.0f, 2.0f, 1.0f), FVector::ZAxisVector);
	Source.bHasPreviousStepPlane = true;
	TArray<FPlanarLimit> SourceLimits{Source, Source};
	SourceLimits[1].OffsetLocation = FVector(1.0f, 2.0f, 3.0f);
	const FName SourceBoneName(TEXT("source_l"));
	const FName TargetBoneName(TEXT("target_r"));
	const auto Resolve = [SourceBoneName, TargetBoneName](FName BoneName) -> FName
	{
		return BoneName == SourceBoneName ? TargetBoneName : NAME_None;
	};
	const auto Find = [SourceBoneName, TargetBoneName](FName BoneName) -> int32
	{
		return BoneName == SourceBoneName ? 0 : BoneName == TargetBoneName ? 1 : INDEX_NONE;
	};
	const TArray<FTransform> RefTransforms{FTransform::Identity, FTransform::Identity};
	const auto Build = [&SourceLimits, &Resolve, &Find, &RefTransforms]()
	{
		TArray<FPlanarLimit> Result;
		KawaiiPhysicsMirrorUtils::AppendMirroredLimits(SourceLimits, TArray<FPlanarLimit>(),
			TArray<FPlanarLimit>(), false, Resolve, Find, RefTransforms, EAxis::X, Result);
		return Result;
	};
	TArray<FPlanarLimit> Previous = Build();
	if (!TestEqual(TEXT("Initial mirrored Planar count"), Previous.Num(), 2))
	{
		return false;
	}
	TestFalse(TEXT("Initial mirrored Planar has no source history"), Previous[0].bHasPreviousStepPlane);
	TestFalse(TEXT("Second initial mirrored Planar has no source history"), Previous[1].bHasPreviousStepPlane);
	const FPlane FirstPlane(FVector(4.0f, 0.0f, 1.0f), FVector::ZAxisVector);
	const FPlane SecondPlane(FVector(9.0f, 0.0f, 2.0f), FVector::ZAxisVector);
	Previous[0].PreviousStepPlane = FirstPlane;
	Previous[0].bHasPreviousStepPlane = true;
	Previous[1].PreviousStepPlane = SecondPlane;
	Previous[1].bHasPreviousStepPlane = true;
	SourceLimits.Add(Source);
	TArray<FPlanarLimit> Rebuilt = Build();
	KawaiiPhysicsMirrorUtils::RestoreMirroredPlanarHistory(Previous, Rebuilt);
	if (!TestEqual(TEXT("Rebuilt mirrored Planar count"), Rebuilt.Num(), 3))
	{
		return false;
	}
	TestTrue(TEXT("First Planar keeps its previous-step flag"), Rebuilt[0].bHasPreviousStepPlane);
	TestTrue(TEXT("Second Planar keeps its previous-step flag"), Rebuilt[1].bHasPreviousStepPlane);
	TestTrue(TEXT("First Planar keeps its own plane"), Rebuilt[0].PreviousStepPlane == FirstPlane);
	TestTrue(TEXT("Second Planar keeps its own plane"), Rebuilt[1].PreviousStepPlane == SecondPlane);
	TestFalse(TEXT("New ordinal has no history"), Rebuilt[2].bHasPreviousStepPlane);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorPlanarHistoryKeptThroughApplyMirrorLimitsTest,
	"KawaiiPhysics.Mirror.PlanarHistoryKeptThroughApplyMirrorLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorPlanarHistoryKeptThroughApplyMirrorLimitsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
	{
		FReferenceSkeletonModifier Modifier(Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("source_l"), TEXT("source_l"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("target_r"), TEXT("target_r"), 0), FTransform::Identity);
	}
	FBoneContainer Container;
	Container.InitializeTo(TArray<FBoneIndexType>{0, 1, 2}, UE::Anim::FCurveFilterSettings(), *Skeleton);
	TStrongObjectPtr<UMirrorDataTable> Table(NewObject<UMirrorDataTable>());
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->MirrorAxis = EAxis::X;
	FMirrorTableRow Row;
	Row.Name = TEXT("source_l");
	Row.MirroredName = TEXT("target_r");
	Table->AddRow(Row.Name, Row);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.MirrorDataTableForLimits = Table.Get();
	FPlanarLimit Source;
	Source.DrivingBone = FBoneReference(TEXT("source_l"));
	Source.PreviousStepPlane = FPlane(FVector(3.0f, 2.0f, 1.0f), FVector::ZAxisVector);
	Source.bHasPreviousStepPlane = true;
	Accessor.Node.PlanarLimits.Add(Source);
	Accessor.ApplyMirrorLimits(Container);
	if (!TestEqual(TEXT("Initial ApplyMirrorLimits creates one mirrored Planar"),
		Accessor.Node.PlanarLimitsData.Num(), 1))
	{
		return false;
	}
	FPlanarLimit& Mirrored = Accessor.Node.PlanarLimitsData[0];
	TestTrue(TEXT("Initial Planar is mirrored to target_r"),
		Mirrored.SourceType == ECollisionSourceType::Mirror && Mirrored.DrivingBone.BoneName == TEXT("target_r"));
	TestFalse(TEXT("Initial mirrored Planar has no source history"), Mirrored.bHasPreviousStepPlane);
	const FPlane PreviousPlane(FVector(4.0f, 0.0f, 1.0f), FVector::ZAxisVector);
	Mirrored.PreviousStepPlane = PreviousPlane;
	Mirrored.bHasPreviousStepPlane = true;
	Accessor.ApplyMirrorLimits(Container);
	if (!TestEqual(TEXT("Second ApplyMirrorLimits rebuilds one mirrored Planar"),
		Accessor.Node.PlanarLimitsData.Num(), 1))
	{
		return false;
	}
	const FPlanarLimit& Rebuilt = Accessor.Node.PlanarLimitsData[0];
	TestTrue(TEXT("Rebuilt Planar is the same mirrored target"),
		Rebuilt.SourceType == ECollisionSourceType::Mirror && Rebuilt.DrivingBone.BoneName == TEXT("target_r"));
	TestTrue(TEXT("Rebuilt Planar keeps previous-step flag"), Rebuilt.bHasPreviousStepPlane);
	TestTrue(TEXT("Rebuilt Planar keeps previous-step plane"), Rebuilt.PreviousStepPlane == PreviousPlane);
	return true;
}
#endif

// 削除・挿入・並べ替え後も、ミラー平面の履歴を内容で対応づける。
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorPlanarHistoryMatchesByContentTest,
	"KawaiiPhysics.Mirror.PlanarHistoryMatchesByContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorPlanarHistoryMatchesByContentTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>());
	{
		FReferenceSkeletonModifier Modifier(Skeleton.Get());
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("source_l"), TEXT("source_l"), 0), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("target_r"), TEXT("target_r"), 0), FTransform::Identity);
	}
	FBoneContainer Container;
	Container.InitializeTo(TArray<FBoneIndexType>{0, 1, 2}, UE::Anim::FCurveFilterSettings(), *Skeleton);
	TStrongObjectPtr<UMirrorDataTable> Table(NewObject<UMirrorDataTable>());
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->MirrorAxis = EAxis::X;
	FMirrorTableRow Row;
	Row.Name = TEXT("source_l");
	Row.MirroredName = TEXT("target_r");
	Table->AddRow(Row.Name, Row);

	FKawaiiPhysicsTestAccessor Accessor;
	Accessor.Node.MirrorDataTableForLimits = Table.Get();
	FPlanarLimit A;
	A.DrivingBone = FBoneReference(TEXT("source_l"));
	A.OffsetLocation = FVector(0.0f, 0.0f, 0.0f);
	FPlanarLimit B = A;
	B.OffsetLocation = FVector(0.0f, 0.0f, 100.0f);
	FPlanarLimit C = A;
	C.OffsetLocation = FVector(0.0f, 0.0f, 200.0f);
	Accessor.Node.PlanarLimits = {A, B};
	Accessor.ApplyMirrorLimits(Container);
	if (!TestEqual(TEXT("Initial mirrored Planar count"), Accessor.Node.PlanarLimitsData.Num(), 2))
	{
		return false;
	}
	const FPlane AHistory(FVector(0.0f, 0.0f, 0.0f), FVector::ZAxisVector);
	const FPlane BHistory(FVector(0.0f, 0.0f, 100.0f), FVector::ZAxisVector);
	Accessor.Node.PlanarLimitsData[0].PreviousStepPlane = AHistory;
	Accessor.Node.PlanarLimitsData[0].bHasPreviousStepPlane = true;
	Accessor.Node.PlanarLimitsData[1].PreviousStepPlane = BHistory;
	Accessor.Node.PlanarLimitsData[1].bHasPreviousStepPlane = true;
	const TArray<FPlanarLimit> Previous = Accessor.Node.PlanarLimitsData;

	Accessor.Node.PlanarLimits = {B};
	Accessor.ApplyMirrorLimits(Container);
	if (TestEqual(TEXT("Deleting A leaves one mirrored Planar"), Accessor.Node.PlanarLimitsData.Num(), 1))
	{
		TestTrue(TEXT("B retains its history after deleting A"),
			Accessor.Node.PlanarLimitsData[0].bHasPreviousStepPlane
			&& Accessor.Node.PlanarLimitsData[0].PreviousStepPlane == BHistory);
		TestFalse(TEXT("B does not receive A's history"),
			Accessor.Node.PlanarLimitsData[0].PreviousStepPlane == AHistory);
	}

	TArray<FPlanarLimit> PreviousWithoutBHistory = Previous;
	PreviousWithoutBHistory[1].bHasPreviousStepPlane = false;
	Accessor.Node.PlanarLimitsData = PreviousWithoutBHistory;
	Accessor.Node.PlanarLimits = {B};
	Accessor.ApplyMirrorLimits(Container);
	if (TestEqual(TEXT("Deleting A still leaves B when B has no history"), Accessor.Node.PlanarLimitsData.Num(), 1))
	{
		TestFalse(TEXT("B does not receive A's history when B has none"),
			Accessor.Node.PlanarLimitsData[0].bHasPreviousStepPlane);
	}

	Accessor.Node.PlanarLimitsData = Previous;
	Accessor.Node.PlanarLimits = {C, A, B};
	Accessor.ApplyMirrorLimits(Container);
	if (TestEqual(TEXT("Inserting C creates three mirrored Planars"), Accessor.Node.PlanarLimitsData.Num(), 3))
	{
		TestFalse(TEXT("C has no history"), Accessor.Node.PlanarLimitsData[0].bHasPreviousStepPlane);
		TestTrue(TEXT("A retains its history after inserting C"),
			Accessor.Node.PlanarLimitsData[1].bHasPreviousStepPlane
			&& Accessor.Node.PlanarLimitsData[1].PreviousStepPlane == AHistory);
		TestTrue(TEXT("B retains its history after inserting C"),
			Accessor.Node.PlanarLimitsData[2].bHasPreviousStepPlane
			&& Accessor.Node.PlanarLimitsData[2].PreviousStepPlane == BHistory);
	}

	Accessor.Node.PlanarLimitsData = Previous;
	Accessor.Node.PlanarLimits = {B, A};
	Accessor.ApplyMirrorLimits(Container);
	if (TestEqual(TEXT("Reordering keeps two mirrored Planars"), Accessor.Node.PlanarLimitsData.Num(), 2))
	{
		TestTrue(TEXT("B retains its history after reordering"),
			Accessor.Node.PlanarLimitsData[0].bHasPreviousStepPlane
			&& Accessor.Node.PlanarLimitsData[0].PreviousStepPlane == BHistory);
		TestTrue(TEXT("A retains its history after reordering"),
			Accessor.Node.PlanarLimitsData[1].bHasPreviousStepPlane
			&& Accessor.Node.PlanarLimitsData[1].PreviousStepPlane == AHistory);
	}

	TArray<FPlanarLimit> DuplicatePrevious{Previous[0], Previous[0]};
	DuplicatePrevious[1].PreviousStepPlane = BHistory;
	TArray<FPlanarLimit> DuplicateRebuilt{Previous[0], Previous[0], Previous[0]};
	KawaiiPhysicsMirrorUtils::RestoreMirroredPlanarHistory(DuplicatePrevious, DuplicateRebuilt);
	TestTrue(TEXT("First duplicate receives the first history"),
		DuplicateRebuilt[0].bHasPreviousStepPlane && DuplicateRebuilt[0].PreviousStepPlane == AHistory);
	TestTrue(TEXT("Second duplicate receives the second history"),
		DuplicateRebuilt[1].bHasPreviousStepPlane && DuplicateRebuilt[1].PreviousStepPlane == BHistory);
	TestFalse(TEXT("Third duplicate does not reuse previous history"), DuplicateRebuilt[2].bHasPreviousStepPlane);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsMirrorPlanarDoesNotInheritPreviousPlaneTest,
                                 "KawaiiPhysics.Mirror.PlanarDoesNotInheritPreviousPlane",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsMirrorPlanarDoesNotInheritPreviousPlaneTest::RunTest(const FString& Parameters)
{
	FPlanarLimit Plane;
	Plane.DrivingBone = FBoneReference(FName(TEXT("source_l")));
	Plane.PreviousStepPlane = FPlane(FVector(3.0f, 2.0f, 1.0f), FVector::ZAxisVector);
	Plane.bHasPreviousStepPlane = true;
	const TArray<FPlanarLimit> MirroredPlanes = MakeMirroredLimits(
		Plane, FQuat::Identity, FQuat(FVector::ZAxisVector, PI * 0.5f), EAxis::X);
	if (TestEqual(TEXT("Previous-plane Planar count"), MirroredPlanes.Num(), 1))
	{
		TestFalse(TEXT("Mirrored Planar has no previous plane"), MirroredPlanes[0].bHasPreviousStepPlane);
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
	TArray<FTransform> CSRefTransforms;
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefTransforms(RefSkeleton, CSRefTransforms);

	TestTrue(TEXT("Component-space ref rotations count matches bone count"), CSRefRotations.Num() == 4);
	TestTrue(TEXT("Root CS rotation matches local"), IsSameRotation(CSRefRotations[0], RootRotation));
	TestTrue(TEXT("Spine CS rotation is root * spine"), IsSameRotation(CSRefRotations[1], RootRotation * SpineRotation));
	TestTrue(TEXT("Arm CS rotation is root * spine * arm"),
	         IsSameRotation(CSRefRotations[2], RootRotation * SpineRotation * ArmRotation));
	TestTrue(TEXT("Leg CS rotation is root * leg"), IsSameRotation(CSRefRotations[3], RootRotation * LegRotation));
	TestEqual(TEXT("Component-space ref transform count"), CSRefTransforms.Num(), 4);
	TestTrue(TEXT("Component-space ref rotation agrees with transform"),
		IsSameRotation(CSRefTransforms[2].GetRotation(), RootRotation * SpineRotation * ArmRotation));
	FReferenceSkeleton ScaledRefSkeleton;
	const FQuat ScaledRootRotation = MakeQuat(0.0f, 30.0f, 0.0f);
	const FQuat ScaledChildRotation = MakeQuat(0.0f, 90.0f, 0.0f);
	{
		FReferenceSkeletonModifier Modifier(ScaledRefSkeleton, nullptr);
		Modifier.Add(FMeshBoneInfo(TEXT("scaled_root"), TEXT("scaled_root"), INDEX_NONE),
			FTransform(ScaledRootRotation, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f)));
		Modifier.Add(FMeshBoneInfo(TEXT("scaled_child"), TEXT("scaled_child"), 0),
			FTransform(ScaledChildRotation, FVector::ZeroVector, FVector(1.0f, 3.0f, 1.0f)));
	}
	TArray<FTransform> ScaledCSTransforms;
	KawaiiPhysicsMirrorUtils::BuildComponentSpaceRefTransforms(ScaledRefSkeleton, ScaledCSTransforms);
	if (TestEqual(TEXT("Scaled component-space transform count"), ScaledCSTransforms.Num(), 2))
	{
		TestTrue(TEXT("Parent and child scales compose by FTransform multiplication"),
			ScaledCSTransforms[1].GetScale3D().Equals(FVector(2.0f, 3.0f, 1.0f), GMirrorVectorTol));
		TestTrue(TEXT("Scaled CS rotation composes in parent-child order"),
			IsSameRotation(ScaledCSTransforms[1].GetRotation(), ScaledRootRotation * ScaledChildRotation));
	}

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

	TArray<FTransform> CSRefTransforms;
	CSRefTransforms.Add(FTransform::Identity);
	CSRefTransforms.Add(FTransform::Identity);
	CSRefTransforms.Add(FTransform::Identity);

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
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::X, OutNewLimits);
	TestTrue(TEXT("Center or unresolved mirror bones are skipped"), OutNewLimits.Num() == 0);

	TArray<FSphericalLimit> ExistingNonMirror;
	ExistingNonMirror.Add(MakeSphere(TargetBoneName, ECollisionSourceType::AnimNode));
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingNonMirror, TArray<FSphericalLimit>(), true,
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::X, OutNewLimits);
	TestTrue(TEXT("Existing non-Mirror target collision skips generation"), OutNewLimits.Num() == 0);

	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingNonMirror, TArray<FSphericalLimit>(), false,
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::X, OutNewLimits);
	TestTrue(TEXT("bSkipExisting=false allows generation"), OutNewLimits.Num() == 1);
	OutNewLimits.Reset();

	TArray<FSphericalLimit> ExistingMirror;
	ExistingMirror.Add(MakeSphere(TargetBoneName, ECollisionSourceType::Mirror));
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		SourceLimits, ExistingMirror, TArray<FSphericalLimit>(), true,
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::X, OutNewLimits);

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

	// TaperedCapsule は鏡像後も Radius0 = +Z 端 / Radius1 = -Z 端の対応を元の物理端点に保つ。
	// 参照回転が Identity のため、X ミラーは +Z が変わらず半径そのまま、Z ミラーは +Z 端が反転して半径が入れ替わる
	FTaperedCapsuleLimit TaperedSource;
	TaperedSource.DrivingBone = FBoneReference(SourceBoneName);
	TaperedSource.OffsetRotation = FRotator::ZeroRotator;
	TaperedSource.Radius0 = 9.0f;
	TaperedSource.Radius1 = 2.0f;
	TaperedSource.Length = 12.0f;
	TArray<FTaperedCapsuleLimit> TaperedSources{TaperedSource};
	TArray<FTaperedCapsuleLimit> MirroredTapered;

	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		TaperedSources, TArray<FTaperedCapsuleLimit>(), TArray<FTaperedCapsuleLimit>(), false,
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::X, MirroredTapered);
	TestEqual(TEXT("X mirror preserves tapered +Z endpoint radius"), MirroredTapered.Num(), 1);
	if (MirroredTapered.Num() == 1)
	{
		TestTrue(TEXT("X mirror keeps Radius0"),
		         FMath::IsNearlyEqual(MirroredTapered[0].Radius0, TaperedSource.Radius0, GMirrorVectorTol));
		TestTrue(TEXT("X mirror keeps Radius1"),
		         FMath::IsNearlyEqual(MirroredTapered[0].Radius1, TaperedSource.Radius1, GMirrorVectorTol));
	}

	MirroredTapered.Reset();
	KawaiiPhysicsMirrorUtils::AppendMirroredLimits(
		TaperedSources, TArray<FTaperedCapsuleLimit>(), TArray<FTaperedCapsuleLimit>(), false,
		ResolveMirrorBoneName, FindBoneIndex, CSRefTransforms, EAxis::Z, MirroredTapered);
	TestEqual(TEXT("Z mirror produces one tapered capsule"), MirroredTapered.Num(), 1);
	if (MirroredTapered.Num() == 1)
	{
		TestTrue(TEXT("Z mirror moves source +Z radius to generated -Z endpoint"),
		         FMath::IsNearlyEqual(MirroredTapered[0].Radius1, TaperedSource.Radius0, GMirrorVectorTol));
		TestTrue(TEXT("Z mirror moves source -Z radius to generated +Z endpoint"),
		         FMath::IsNearlyEqual(MirroredTapered[0].Radius0, TaperedSource.Radius1, GMirrorVectorTol));
	}

	return true;
}

} // namespace KawaiiPhysicsMirrorLimitsTestPrivate

#endif // WITH_DEV_AUTOMATION_TESTS
