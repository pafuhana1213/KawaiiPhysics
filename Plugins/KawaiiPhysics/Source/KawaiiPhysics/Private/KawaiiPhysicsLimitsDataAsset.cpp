// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsLimitsDataAsset.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysics.h"

DEFINE_LOG_CATEGORY(LogKawaiiPhysics);

struct FCollisionLimitDataCustomVersion
{
	enum Type
	{
		// FNameからFBoneReferenceに移行
		ChangeToBoneReference = 0,

		// ------------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FCollisionLimitDataCustomVersion()
	{
	}
};

const FGuid FCollisionLimitDataCustomVersion::GUID(0x3A1F7B2E, 0x7B9D6E8C, 0x4C2A9F1D, 0x85B3E4F1);
FCustomVersionRegistration GRegisterCollisionLimitDataCustomVersion(FCollisionLimitDataCustomVersion::GUID,
                                                                    FCollisionLimitDataCustomVersion::LatestVersion,
                                                                    TEXT("CollisionLimitData"));

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
	case ECollisionLimitType::Box:
		UpdateCollisionLimit(BoxLimitsData, static_cast<FBoxLimit*>(Limit));
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
void SyncCollisionLimits(const TArray<CollisionLimitDataType>& CollisionLimitData,
                         TArray<CollisionLimitType>& CollisionLimits)
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
	SyncCollisionLimits(BoxLimitsData, BoxLimits);
	SyncCollisionLimits(PlanarLimitsData, PlanarLimits);
}


void UKawaiiPhysicsLimitsDataAsset::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.MemberProperty
		                           ? PropertyChangedEvent.MemberProperty->GetFName()
		                           : NAME_None;

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
	else if (PropertyName == FName(TEXT("BoxLimitsData")))
	{
		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			BoxLimitsData[PropertyChangedEvent.GetArrayIndex(PropertyName.ToString())].Guid = FGuid::NewGuid();
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
	OnLimitsChanged.Broadcast(PropertyChangedEvent);
}
#endif

#if WITH_EDITORONLY_DATA
void UKawaiiPhysicsLimitsDataAsset::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FCollisionLimitDataCustomVersion::GUID);
}
#endif

USkeleton* UKawaiiPhysicsLimitsDataAsset::GetSkeleton(bool& bInvalidSkeletonIsError,
                                                      const IPropertyHandle* PropertyHandle)
{
#if WITH_EDITORONLY_DATA
	return Skeleton;
#else
	return nullptr;
#endif
}

void UKawaiiPhysicsLimitsDataAsset::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FCollisionLimitDataCustomVersion::GUID) <
		FCollisionLimitDataCustomVersion::ChangeToBoneReference)
	{
#if WITH_EDITORONLY_DATA
		for (auto& Data : SphericalLimitsData)
		{
			Data.DrivingBoneReference = FBoneReference(Data.DrivingBoneName);
		}
		for (auto& Data : CapsuleLimitsData)
		{
			Data.DrivingBoneReference = FBoneReference(Data.DrivingBoneName);
		}
		for (auto& Data : BoxLimitsData)
		{
			Data.DrivingBoneReference = FBoneReference(Data.DrivingBoneName);
		}
		for (auto& Data : PlanarLimitsData)
		{
			Data.DrivingBoneReference = FBoneReference(Data.DrivingBoneName);
		}
		UE_LOG(LogKawaiiPhysics, Log, TEXT("Update : BoneName -> BoneReference (%s)"), *this->GetName());
#endif
	}
}
