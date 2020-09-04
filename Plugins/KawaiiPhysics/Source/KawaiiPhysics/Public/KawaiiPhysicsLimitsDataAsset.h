// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsLimitsDataAsset.generated.h"

// I chose this design because using FBoneReference with anything other than Persona gives me an error. 
// I want to make it simpler...
USTRUCT(BlueprintType)
struct FCollisionLimitDataBase
{
	GENERATED_BODY();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CollisionLimitBase, meta=(DisplayPriority="1"))
	FName DrivingBoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CollisionLimitBase)
	FVector OffsetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CollisionLimitBase, meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, BlueprintReadWrite)
	FQuat Rotation = FQuat::Identity;

	UPROPERTY(VisibleAnywhere, Category = Debug, meta = (IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

public:

protected:

	void UpdateBase(FCollisionLimitBase* Limit)
	{
		DrivingBoneName = Limit->DrivingBone.BoneName;
		OffsetLocation = Limit->OffsetLocation;
		OffsetRotation = Limit->OffsetRotation;
		Location = Limit->Location;
		Rotation = Limit->Rotation;
	}

	void ConvertBase(FCollisionLimitBase& Limit)
	{
		Limit.DrivingBone.BoneName = DrivingBoneName;
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
	GENERATED_BODY();

	/** Radius of the sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;

	void Update(FSphericalLimit* Limit) 
	{
		UpdateBase(Limit);
		Radius = Limit->Radius;
		LimitType = Limit->LimitType;
	}

	FSphericalLimit Convert()
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
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;

	void Update(FCapsuleLimit* Limit)
	{
		UpdateBase(Limit);
		Radius = Limit->Radius;
		Length = Limit->Length;
	}

	FCapsuleLimit Convert()
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
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = PlanarLimit, BlueprintReadWrite)
	FPlane Plane;

	void Update(FPlanarLimit* Limit)
	{
		UpdateBase(Limit);
		Plane = Limit->Plane;
	}

	FPlanarLimit Convert()
	{
		FPlanarLimit Limit;
		ConvertBase(Limit);
		Limit.Plane = Plane;

		return Limit;
	}
};
//#endif

/**
 * 
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsLimitsDataAsset : public UDataAsset
{
	GENERATED_BODY()
	
public:

#if WITH_EDITORONLY_DATA 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spherical Limits")
	TArray< FSphericalLimitData> SphericalLimitsData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capsule Limits")
	TArray< FCapsuleLimitData> CapsuleLimitsData;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planar Limits")
	TArray< FPlanarLimitData> PlanarLimitsData;

#endif

#if WITH_EDITOR

	//void UpdateLimit(FSphericalLimit* Limit);
	//void UpdateLimit(FCapsuleLimit* Limit);
	//void UpdateLimit(FPlanarLimit* Limit);
	void UpdateLimit(FCollisionLimitBase* Limit);

#endif

	UPROPERTY()
	TArray< FSphericalLimit> SphericalLimits;
	UPROPERTY()
	TArray< FCapsuleLimit> CapsuleLimits;
	UPROPERTY()
	TArray< FPlanarLimit> PlanarLimits;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

/*
#if WITH_EDITORONLY_DATA 
	UPROPERTY(EditAnywhere)
	USkeleton* MeshForPreviewBoneHierarchy;

	UPROPERTY()
	TArray<FName> BoneNameTree;
#endif
*/

};
