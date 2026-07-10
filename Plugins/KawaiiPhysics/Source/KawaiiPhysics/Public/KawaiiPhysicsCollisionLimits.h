// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"

#include "KawaiiPhysicsCollisionLimits.generated.h"

/**
 * Enum representing the type of collision limit in KawaiiPhysics.
 */
UENUM()
enum class ECollisionLimitType : uint8
{
	None,
	Spherical,
	Capsule,
	Box,
	Planar,
};

/**
 * Enum representing the source type of the collision limit in KawaiiPhysics.
 */
UENUM()
enum class ECollisionSourceType : uint8
{
	/** Use the value set in the AnimNode */
	AnimNode,
	/** Use the value set in the DataAsset */
	DataAsset,
	/** Use the value set in the PhysicsAsset */
	PhysicsAsset,
};

/**
 * Base structure for defining collision limits in KawaiiPhysics.
 */
USTRUCT()
struct FCollisionLimitBase
{
	GENERATED_BODY()

	/** Bone to attach the sphere to */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	FBoneReference DrivingBone;

	/** Offset location from the driving bone */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	FVector OffsetLocation = FVector::ZeroVector;

	/** Offset rotation from the driving bone */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	/** Location of the collision limit */
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	/** Rotation of the collision limit */
	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	/** Whether the collision limit is enabled */
	UPROPERTY()
	bool bEnable = true;

	/** Source type of the collision limit */
	UPROPERTY(VisibleAnywhere, Category = CollisionLimitBase)
	ECollisionSourceType SourceType = ECollisionSourceType::AnimNode;

#if WITH_EDITORONLY_DATA

	/** Unique identifier for the collision limit (editor only) */
	UPROPERTY(VisibleAnywhere, Category = Debug, meta = (IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

	/** Type of the collision limit (editor only) */
	UPROPERTY()
	ECollisionLimitType Type = ECollisionLimitType::None;

#endif

	/** Assignment operator */
	FCollisionLimitBase& operator=(const FCollisionLimitBase& Other)
	{
		DrivingBone = Other.DrivingBone;
		OffsetLocation = Other.OffsetLocation;
		OffsetRotation = Other.OffsetRotation;
		Location = Other.Location;
		Rotation = Other.Rotation;
		bEnable = Other.bEnable;
		SourceType = Other.SourceType;
#if WITH_EDITORONLY_DATA
		Guid = Other.Guid;
		Type = Other.Type;
#endif
		return *this;
	}
};

/**
 * Structure representing a spherical limit for collision in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct FSphericalLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	/** Default constructor */
	FSphericalLimit()
	{
#if WITH_EDITORONLY_DATA
		// Set the collision limit type to spherical
		Type = ECollisionLimitType::Spherical;
#endif
	}

	/** Radius of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	/** Assignment operator */
	FSphericalLimit& operator=(const FSphericalLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Radius = Other.Radius;
		LimitType = Other.LimitType;
		return *this;
	}
};

/**
 * Structure representing a capsule limit for collision in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct FCapsuleLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	/** Default constructor */
	FCapsuleLimit()
	{
#if WITH_EDITORONLY_DATA
		// Set the collision limit type to capsule
		Type = ECollisionLimitType::Capsule;
#endif
	}

	/** Radius of the capsule */
	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Length of the capsule */
	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;

	/**
	 * ランタイム専用キャッシュ。非UPROPERTYのためシリアライズされない。
	 * 既存の自前operator=には意図的に追加しない。キャッシュを代入で運ぶと新フィールド追記漏れが
	 * 静かな陳腐化になるため、毎ステップUpdateRuntimeCache()で再計算する。
	 * Runtime-only cache. Not serialized because it is intentionally not a UPROPERTY.
	 * It is intentionally excluded from the custom operator=. Carrying caches through assignment could silently
	 * leave stale values when new fields are added, so caches are recomputed every step with UpdateRuntimeCache().
	 */
	FVector CachedStartPoint = FVector::ZeroVector;
	FVector CachedEndPoint = FVector::ZeroVector;
	FVector CachedFallbackPushDir = FVector::ZeroVector;

	void UpdateRuntimeCache()
	{
		CachedStartPoint = Location + Rotation.GetAxisZ() * Length * 0.5f;
		CachedEndPoint = Location + Rotation.GetAxisZ() * Length * -0.5f;
		CachedFallbackPushDir = Rotation.GetAxisX();
	}

	/** Assignment operator */
	FCapsuleLimit& operator=(const FCapsuleLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Radius = Other.Radius;
		Length = Other.Length;
		return *this;
	}
};

/**
 * Structure representing a box limit for collision in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct FBoxLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	/** Default constructor */
	FBoxLimit()
	{
#if WITH_EDITORONLY_DATA
		// Set the collision limit type to box
		Type = ECollisionLimitType::Box;
#endif
	}

	/** The extent of the box defining the box limit */
	UPROPERTY(EditAnywhere, Category = BoxLimit)
	FVector Extent = FVector(5.0f, 5.0f, 5.0f);

	/**
	 * ランタイム専用キャッシュ。非UPROPERTYのためシリアライズされない。
	 * 既存の自前operator=には意図的に追加しない。キャッシュを代入で運ぶと新フィールド追記漏れが
	 * 静かな陳腐化になるため、毎ステップUpdateRuntimeCache()で再計算する。
	 * Runtime-only cache. Not serialized because it is intentionally not a UPROPERTY.
	 * It is intentionally excluded from the custom operator=. Carrying caches through assignment could silently
	 * leave stale values when new fields are added, so caches are recomputed every step with UpdateRuntimeCache().
	 */
	FTransform CachedBoxTransform = FTransform::Identity;
	// FBox のデフォルト構築は Min/Max を初期化しないため ForceInit で明示的にゼロ化する。
	// FBox's default constructor leaves Min/Max uninitialized, so zero them explicitly.
	FBox CachedLocalBox = FBox(ForceInit);

	void UpdateRuntimeCache()
	{
		CachedBoxTransform = FTransform(Rotation, Location);
		CachedLocalBox = FBox(-Extent, Extent);
	}

	/** Assignment operator */
	FBoxLimit& operator=(const FBoxLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Extent = Other.Extent;
		return *this;
	}
};

/**
 * Structure representing a planar limit for collision in KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct FPlanarLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	/** Default constructor */
	FPlanarLimit()
	{
#if WITH_EDITORONLY_DATA
		// Set the collision limit type to planar
		Type = ECollisionLimitType::Planar;
#endif
	}

	/** The plane defining the planar limit */
	UPROPERTY()
	FPlane Plane = FPlane(0, 0, 0, 0);

	/**
	 * ランタイム専用キャッシュ。非UPROPERTYのためシリアライズされない。
	 * 既存の自前operator=には意図的に追加しない。キャッシュを代入で運ぶと新フィールド追記漏れが
	 * 静かな陳腐化になるため、毎ステップUpdateRuntimeCache()で再計算する。
	 * Runtime-only cache. Not serialized because it is intentionally not a UPROPERTY.
	 * It is intentionally excluded from the custom operator=. Carrying caches through assignment could silently
	 * leave stale values when new fields are added, so caches are recomputed every step with UpdateRuntimeCache().
	 */
	FVector CachedNormal = FVector::ZeroVector;

	void UpdateRuntimeCache()
	{
		CachedNormal = Rotation.GetUpVector();
	}

	/** Assignment operator */
	FPlanarLimit& operator=(const FPlanarLimit& Other)
	{
		FCollisionLimitBase::operator=(Other);
		Plane = Other.Plane;
		return *this;
	}
};
