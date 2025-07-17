// Copyright 2019-2025 pafuhana1213. All Rights Reserved.


#include "KawaiiPhysicsLimitsDataAsset.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsLimitsDataAsset)

DEFINE_LOG_CATEGORY(LogKawaiiPhysics);

struct FCollisionLimitDataCustomVersion
{
	enum Type
	{
		// FNameからFBoneReferenceに移行
		ChangeToBoneReference = 0,
		DeprecateLimitData,

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
template <typename CollisionLimitType>
void UpdateCollisionLimit(TArray<CollisionLimitType>& CollisionLimitsData, const CollisionLimitType& NewLimit)
{
	for (auto& LimitData : CollisionLimitsData)
	{
		if (LimitData.Guid == NewLimit.Guid)
		{
			LimitData = NewLimit;
			break;
		}
	}
}


void UKawaiiPhysicsLimitsDataAsset::UpdateLimit(FCollisionLimitBase* Limit)
{
	switch (Limit->Type)
	{
	case ECollisionLimitType::Spherical:
		UpdateCollisionLimit(SphericalLimits, *static_cast<FSphericalLimit*>(Limit));
		break;
	case ECollisionLimitType::Capsule:
		UpdateCollisionLimit(CapsuleLimits, *static_cast<FCapsuleLimit*>(Limit));
		break;
	case ECollisionLimitType::Box:
		UpdateCollisionLimit(BoxLimits, *static_cast<FBoxLimit*>(Limit));
		break;
	case ECollisionLimitType::Planar:
		UpdateCollisionLimit(PlanarLimits, *static_cast<FPlanarLimit*>(Limit));
		break;
	case ECollisionLimitType::None:
		break;
	default:
		break;
	}

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

void UKawaiiPhysicsLimitsDataAsset::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);


	FName ArrayPropertyName = PropertyChangedEvent.MemberProperty
		                          ? PropertyChangedEvent.MemberProperty->GetFName()
		                          : NAME_None;
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet &&
		PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		ArrayPropertyName = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue()->GetFName();
	}

	auto UpdateLimits = [&](auto& Limits)
	{
		int32 ArrayIndex = PropertyChangedEvent.GetArrayIndex(ArrayPropertyName.ToString());

		if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd ||
			PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet)
		{
			Limits[ArrayIndex].SourceType = ECollisionSourceType::DataAsset;
		}
		else if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
		{
			Limits[ArrayIndex].Guid = FGuid::NewGuid();
		}
	};

	if (ArrayPropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, SphericalLimits))
	{
		UpdateLimits(SphericalLimits);
	}
	else if (ArrayPropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, CapsuleLimits))
	{
		UpdateLimits(CapsuleLimits);
	}
	else if (ArrayPropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, BoxLimits))
	{
		UpdateLimits(BoxLimits);
	}
	else if (ArrayPropertyName == GET_MEMBER_NAME_CHECKED(UKawaiiPhysicsLimitsDataAsset, PlanarLimits))
	{
		UpdateLimits(PlanarLimits);
	}

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

	if (GetLinkerCustomVersion(FCollisionLimitDataCustomVersion::GUID) <
		FCollisionLimitDataCustomVersion::DeprecateLimitData)
	{
#if WITH_EDITORONLY_DATA
		Sync();
		UE_LOG(LogKawaiiPhysics, Log, TEXT("Update : Deprecate LimitData (%s)"), *this->GetName());
#endif
	}
}
