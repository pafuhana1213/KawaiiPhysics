// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsLimitsDataAsset.h"
#include "AnimNode_KawaiiPhysics.h"

#if WITH_EDITOR

void UKawaiiPhysicsLimitsDataAsset::UpdateLimit(FCollisionLimitBase* Limit)
{
	switch (Limit->Type)
	{
		case ECollisionLimitType::Spherical:

			for (FSphericalLimitData& LimitData : SphericalLimitsData)
			{
				if (LimitData.Guid == Limit->Guid)
				{
					LimitData.Update(static_cast<FSphericalLimit*>(Limit));
					break;
				}
			}

		break;
		case ECollisionLimitType::Capsule:

			for (FCapsuleLimitData& LimitData : CapsuleLimitsData)
			{
				if (LimitData.Guid == Limit->Guid)
				{
					LimitData.Update(static_cast<FCapsuleLimit*>(Limit));
					break;
				}
			}

		break;
		case ECollisionLimitType::Planar:

			for (FPlanarLimitData& LimitData : PlanarLimitsData)
			{
				if (LimitData.Guid == Limit->Guid)
				{
					LimitData.Update(static_cast<FPlanarLimit*>(Limit));
					break;
				}
			}

		break;
		case ECollisionLimitType::None:
			break;
		default:
			break;
	}

	Sync();
	
	MarkPackageDirty();
}

void UKawaiiPhysicsLimitsDataAsset::Sync()
{
	SphericalLimits.Empty();
	for (FSphericalLimitData& Data : SphericalLimitsData)
	{
		SphericalLimits.Add(Data.Convert());
	}

	CapsuleLimits.Empty();
	for (FCapsuleLimitData& Data : CapsuleLimitsData)
	{
		CapsuleLimits.Add(Data.Convert());
	}

	PlanarLimits.Empty();
	for (FPlanarLimitData& Data : PlanarLimitsData)
	{
		PlanarLimits.Add(Data.Convert());
	}
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
