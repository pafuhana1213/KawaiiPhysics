// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsLimitsDataAsset.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLimitsChanged, struct FPropertyChangedEvent&);

// Deprecated
USTRUCT()
struct FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(meta=(DeprecatedProperty))
	FBoneReference DrivingBoneReference;

	UPROPERTY(meta=(DeprecatedProperty))
	FName DrivingBoneName;

	UPROPERTY(meta=(DeprecatedProperty))
	FVector OffsetLocation = FVector::ZeroVector;

	UPROPERTY(meta=(DeprecatedProperty))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	UPROPERTY(meta=(DeprecatedProperty))
	FVector Location = FVector::ZeroVector;

	UPROPERTY(meta=(DeprecatedProperty))
	FQuat Rotation = FQuat::Identity;

	UPROPERTY(meta=(DeprecatedProperty))
	FGuid Guid = FGuid::NewGuid();

protected:
	void ConvertBase(FCollisionLimitBase& Limit) const
	{
		Limit.DrivingBone.BoneName = DrivingBoneReference.BoneName;
		Limit.OffsetLocation = OffsetLocation;
		Limit.OffsetRotation = OffsetRotation;
		Limit.Location = Location;
		Limit.Rotation = Rotation;

#if  WITH_EDITORONLY_DATA
		Limit.SourceType = ECollisionSourceType::DataAsset;
		Limit.Guid = Guid;
#endif
	}
};

// Deprecated
USTRUCT()
struct FSphericalLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** Radius of the sphere */
	UPROPERTY(meta=(DeprecatedProperty))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(meta=(DeprecatedProperty))
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	FSphericalLimit Convert() const
	{
		FSphericalLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.LimitType = LimitType;

		return Limit;
	}
};

// Deprecated
USTRUCT()
struct FCapsuleLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(meta=(DeprecatedProperty))
	float Radius = 5.0f;

	UPROPERTY(meta=(DeprecatedProperty))
	float Length = 10.0f;

	FCapsuleLimit Convert() const
	{
		FCapsuleLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.Length = Length;

		return Limit;
	}
};

// Deprecated
USTRUCT()
struct FBoxLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(meta=(DeprecatedProperty))
	FVector Extent = FVector(5.0f, 5.0f, 5.0f);

	FBoxLimit Convert() const
	{
		FBoxLimit Limit;
		ConvertBase(Limit);
		Limit.Extent = Extent;

		return Limit;
	}
};

// Deprecated
USTRUCT()
struct FPlanarLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(meta=(DeprecatedProperty))
	FPlane Plane = FPlane(0, 0, 0, 0);

	FPlanarLimit Convert() const
	{
		FPlanarLimit Limit;
		ConvertBase(Limit);
		Limit.Plane = Plane;

		return Limit;
	}
};

/**
 * 
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsLimitsDataAsset : public UDataAsset, public IBoneReferenceSkeletonProvider

{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton")
	TObjectPtr<USkeleton> Skeleton;

	// Deprecated
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FSphericalLimitData> SphericalLimitsData;
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FCapsuleLimitData> CapsuleLimitsData;
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FBoxLimitData> BoxLimitsData;
	UPROPERTY(meta=(DeprecatedProperty))
	TArray<FPlanarLimitData> PlanarLimitsData;

#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spherical Limits")
	TArray<FSphericalLimit> SphericalLimits;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule Limits")
	TArray<FCapsuleLimit> CapsuleLimits;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Box Limits")
	TArray<FBoxLimit> BoxLimits;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planar Limits")
	TArray<FPlanarLimit> PlanarLimits;

	// Begin UObject Interface.
#if WITH_EDITORONLY_DATA
	virtual void Serialize(FStructuredArchiveRecord Record) override;
#endif
	virtual void PostLoad() override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

#if WITH_EDITOR
	void UpdateLimit(FCollisionLimitBase* Limit);

	FOnLimitsChanged OnLimitsChanged;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_EDITOR
	void Sync();
#endif
};
