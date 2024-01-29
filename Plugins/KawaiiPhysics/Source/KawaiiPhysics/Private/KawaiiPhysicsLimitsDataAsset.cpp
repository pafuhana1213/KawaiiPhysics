// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsLimitsDataAsset.h"
#include "AnimNode_KawaiiPhysics.h"

#if WITH_EDITOR

template <typename CollisionLimitDataType, typename CollisionLimitType>
void UpdateCollisionLimit(TArray<CollisionLimitDataType>& CollisionLimitsData, const CollisionLimitType* OutLimit)
{
	for (auto& LimitData : CollisionLimitsData)
	{
		if (LimitData.Guid == OutLimit->Guid)
		{
			LimitData.Update(OutLimit);
			break;
		}
	}
}

void UKawaiiPhysicsLimitsDataAsset::UpdateLimit(FCollisionLimitBase* Limit)
{
	switch (Limit->Type)
	{
		case ECollisionLimitType::Spherical:
			UpdateCollisionLimit(SphericalLimitsData, static_cast<FSphericalLimit*>(Limit));
			break;
		case ECollisionLimitType::Capsule:
			UpdateCollisionLimit(CapsuleLimitsData, static_cast<FCapsuleLimit*>(Limit));
			break;
		case ECollisionLimitType::Planar:
			UpdateCollisionLimit(PlanarLimitsData, static_cast<FPlanarLimit*>(Limit));
			break;
		case ECollisionLimitType::None:
			break;
		default:
			break;
	}

	Sync();
	
	MarkPackageDirty();
}

template <typename CollisionLimitDataType, typename CollisionLimitType>
void SyncCollisionLimits(TArray<CollisionLimitDataType>& CollisionLimitData, TArray<CollisionLimitType>& CollisionLimits)
{
	CollisionLimits.Empty();
	for (const auto& Data : CollisionLimitData)
	{
		CollisionLimits.Add(Data.Convert());
	}
}

void UKawaiiPhysicsLimitsDataAsset::Sync()
{
	SyncCollisionLimits(SphericalLimitsData, SphericalLimits);
	SyncCollisionLimits(CapsuleLimitsData, CapsuleLimits);
	SyncCollisionLimits(PlanarLimitsData, PlanarLimits);
}

void UKawaiiPhysicsLimitsDataAsset::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

	if (PropertyName == FName(TEXT("SphericalLimitsData")))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			SphericalLimitsData[PropertyChangedEvent.GetArrayIndex(PropertyName.ToString())].Guid = FGuid::NewGuid();
		}
	}
	else if (PropertyName == FName(TEXT("CapsuleLimitsData")))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			CapsuleLimitsData[PropertyChangedEvent.GetArrayIndex(PropertyName.ToString())].Guid = FGuid::NewGuid();
		}
	}
	else if (PropertyName == FName(TEXT("PlanarLimitsData")))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			PlanarLimitsData[PropertyChangedEvent.GetArrayIndex(PropertyName.ToString())].Guid = FGuid::NewGuid();
		}
	}

	Sync();
}


#endif
