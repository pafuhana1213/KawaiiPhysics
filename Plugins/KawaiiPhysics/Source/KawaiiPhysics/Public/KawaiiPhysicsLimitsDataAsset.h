// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsLimitsDataAsset.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLimitsChanged, struct FPropertyChangedEvent&);

USTRUCT(BlueprintType)
struct FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta=(DisplayPriority="1"))
	FBoneReference DrivingBoneReference;

	UPROPERTY(BlueprintReadWrite, meta=(DeprecatedProperty, DeprecationMessage="DrivingBoneName is deprecated"))
	FName DrivingBoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CollisionLimitBase)
	FVector OffsetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CollisionLimitBase,
		meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, BlueprintReadWrite)
	FQuat Rotation = FQuat::Identity;

	UPROPERTY(VisibleAnywhere, Category = Debug, meta = (IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

protected:
	void UpdateBase(const FCollisionLimitBase* Limit)
	{
		DrivingBoneReference = FBoneReference(Limit->DrivingBone.BoneName);
		OffsetLocation = Limit->OffsetLocation;
		OffsetRotation = Limit->OffsetRotation;
		Location = Limit->Location;
		Rotation = Limit->Rotation;
	}

	void ConvertBase(FCollisionLimitBase& Limit) const
	{
		Limit.DrivingBone.BoneName = DrivingBoneReference.BoneName;
		Limit.OffsetLocation = OffsetLocation;
		Limit.OffsetRotation = OffsetRotation;
		Limit.Location = Location;
		Limit.Rotation = Rotation;

#if  WITH_EDITORONLY_DATA
		Limit.bFromDataAsset = true;
		Limit.Guid = Guid;
#endif
	}
};

USTRUCT(BlueprintType)
struct FSphericalLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	/** Radius of the sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	void Update(const FSphericalLimit* Limit)
	{
		UpdateBase(Limit);
		Radius = Limit->Radius;
		LimitType = Limit->LimitType;
	}

	FSphericalLimit Convert() const
	{
		FSphericalLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.LimitType = LimitType;

		return Limit;
	}
};

USTRUCT(BlueprintType)
struct FCapsuleLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;

	void Update(const FCapsuleLimit* Limit)
	{
		UpdateBase(Limit);
		Radius = Limit->Radius;
		Length = Limit->Length;
	}

	FCapsuleLimit Convert() const
	{
		FCapsuleLimit Limit;
		ConvertBase(Limit);
		Limit.Radius = Radius;
		Limit.Length = Length;

		return Limit;
	}
};

USTRUCT(BlueprintType)
struct FPlanarLimitData : public FCollisionLimitDataBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = PlanarLimit, BlueprintReadWrite)
	FPlane Plane = FPlane(0, 0, 0, 0);

	void Update(const FPlanarLimit* Limit)
	{
		UpdateBase(Limit);
		Plane = Limit->Plane;
	}

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spherical Limits")
	TArray<FSphericalLimitData> SphericalLimitsData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule Limits")
	TArray<FCapsuleLimitData> CapsuleLimitsData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planar Limits")
	TArray<FPlanarLimitData> PlanarLimitsData;

#endif

	UPROPERTY()
	TArray<FSphericalLimit> SphericalLimits;
	UPROPERTY()
	TArray<FCapsuleLimit> CapsuleLimits;
	UPROPERTY()
	TArray<FPlanarLimit> PlanarLimits;

public:
	// Begin UObject Interface.
#if WITH_EDITORONLY_DATA
	virtual void Serialize(FStructuredArchiveRecord Record) override;
#endif
	virtual void PostLoad() override;
	// End UObject Interface.

#if WITH_EDITOR

	void UpdateLimit(FCollisionLimitBase* Limit);
	void Sync();

#endif

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;

#if WITH_EDITOR
	FOnLimitsChanged OnLimitsChanged;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
