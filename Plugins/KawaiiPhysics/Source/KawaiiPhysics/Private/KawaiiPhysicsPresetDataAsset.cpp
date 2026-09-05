// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "KawaiiPhysicsPresetDataAsset.h"

#include "KawaiiPhysicsCustomExternalForce.h"
#include "Serialization/CustomVersion.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsPresetDataAsset)

struct FKawaiiPhysicsPresetVersion
{
	enum Type
	{
		// プリセットDataAssetの初期バージョン
		Initial = 0,

		// ------------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	const static FGuid GUID;

private:
	FKawaiiPhysicsPresetVersion()
	{
	}
};

const FGuid FKawaiiPhysicsPresetVersion::GUID(0x7F6B4A92, 0xC0A94E13, 0xA73B51D8, 0x2E6F948C);
FCustomVersionRegistration GRegisterKawaiiPhysicsPresetVersion(FKawaiiPhysicsPresetVersion::GUID,
                                                               FKawaiiPhysicsPresetVersion::LatestVersion,
                                                               TEXT("KawaiiPhysicsPreset"));

namespace
{
	constexpr uint32 KawaiiPhysicsPresetIdenticalPortFlags = PPF_DeepComparison | PPF_DeepCompareInstances;

	const TSet<FName>& GetBoneAssignmentPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RootBone),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExcludeBones),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, AdditionalRootBones),
		};
		return Names;
	}

	const TSet<FName>& GetDeniedPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DeltaTime),
		};
		return Names;
	}

	const TSet<FName>& GetCopyTargetPropertyNames()
	{
		static const TSet<FName> Names = {
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DummyBoneLength),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneSubdivisionCount),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bBoneSubdivisionCollisionOnly),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bBoneSubdivisionDensifyByRadius),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintSubdivisionCount),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintSubdivisionFeedbackScale),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneForwardAxis),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsSettings),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimulationSpace),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimulationBaseBone),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TargetFramerate),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WarmUpFrames),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseWarmUpWhenResetDynamics),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bNeedWarmUp),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TeleportDistanceThreshold),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TeleportRotationThreshold),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarConstraint),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SkelCompMoveScale),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUpdatePhysicsSettingsInGame),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ResetBoneTransformWhenBoneNotFound),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, DampingCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, StiffnessCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WorldDampingLocationCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WorldDampingRotationCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, RadiusCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitAngleCurveData),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SphericalLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CapsuleLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, TaperedCapsuleLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoxLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PlanarLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, LimitsDataAsset),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, PhysicsAssetForLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, MirrorDataTableForLimits),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSkipMirroredBoneWithExistingCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSharedCollisionSource),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSharedCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SharedCollisionGroupTag),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintGlobalComplianceType),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintIterationCountBeforeCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintIterationCountAfterCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bAutoAddChildDummyBoneConstraint),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraints),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, BoneConstraintsDataAsset),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SyncBones),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, Gravity),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseLegacyGravity),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseDefaultGravityZProjectSetting),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseWorldSpaceGravity),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bEnableWind),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindScale),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, WindDirectionNoiseAngle),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleExternalForce),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseWorldSpaceSimpleExternalForce),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bAllowWorldCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bUseSimpleWorldCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherInterval),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionObjectTypes),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionConvexFallbackShape),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bOverrideSimpleWorldCollisionGatherRadius),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionGatherRadius),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bSimpleWorldCollisionGroundCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSkeletalMeshCollision),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSource),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, SimpleWorldCollisionSharedTag),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bOverrideCollisionParams),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CollisionChannelSettings),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, bIgnoreSelfComponent),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBones),
			GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, IgnoreBoneNamePrefix),
		};
		return Names;
	}

	void CopyNodeProperty(const FProperty& Property,
	                      const FAnimNode_KawaiiPhysics& SourceNode,
	                      FAnimNode_KawaiiPhysics& DestinationNode,
	                      UObject* CustomExternalForceOuter)
	{
		if (Property.GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces))
		{
			UKawaiiPhysicsPresetDataAsset::DuplicateCustomExternalForces(
				SourceNode.CustomExternalForces,
				DestinationNode.CustomExternalForces,
				CustomExternalForceOuter);
			return;
		}

		Property.CopyCompleteValue_InContainer(&DestinationNode, &SourceNode);
	}

}

EKawaiiPhysicsPresetPropertyClass UKawaiiPhysicsPresetDataAsset::ClassifyNodeProperty(const FProperty& Property)
{
	const FName PropertyName = Property.GetFName();
	if (Property.HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly) ||
		GetDeniedPropertyNames().Contains(PropertyName))
	{
		return EKawaiiPhysicsPresetPropertyClass::Deny;
	}

	if (GetBoneAssignmentPropertyNames().Contains(PropertyName))
	{
		return EKawaiiPhysicsPresetPropertyClass::BoneAssignment;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, KawaiiPhysicsTag))
	{
		return EKawaiiPhysicsPresetPropertyClass::Tag;
	}

	if (GetCopyTargetPropertyNames().Contains(PropertyName))
	{
		return EKawaiiPhysicsPresetPropertyClass::CopyTarget;
	}

	return EKawaiiPhysicsPresetPropertyClass::Unknown;
}

bool UKawaiiPhysicsPresetDataAsset::ShouldApplyNodeProperty(const FProperty& Property,
                                                            const FKawaiiPhysicsPresetApplyOptions& Options)
{
	switch (ClassifyNodeProperty(Property))
	{
	case EKawaiiPhysicsPresetPropertyClass::CopyTarget:
		return true;
	case EKawaiiPhysicsPresetPropertyClass::BoneAssignment:
		return Options.bApplyBoneAssignment;
	case EKawaiiPhysicsPresetPropertyClass::Tag:
		return Options.bApplyTag;
	case EKawaiiPhysicsPresetPropertyClass::Deny:
	case EKawaiiPhysicsPresetPropertyClass::Unknown:
	default:
		return false;
	}
}

void UKawaiiPhysicsPresetDataAsset::DuplicateCustomExternalForces(
	const TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>>& SourceForces,
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>>& DestinationForces,
	UObject* Outer)
{
	DestinationForces.Empty(SourceForces.Num());
	for (const TObjectPtr<UKawaiiPhysics_CustomExternalForce>& SourceForce : SourceForces)
	{
		DestinationForces.Add(SourceForce
			                      ? DuplicateObject<UKawaiiPhysics_CustomExternalForce>(SourceForce, Outer)
			                      : nullptr);
	}
}

void UKawaiiPhysicsPresetDataAsset::CopyFromNode(const FAnimNode_KawaiiPhysics& SourceNode)
{
	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		const EKawaiiPhysicsPresetPropertyClass PropertyClass = ClassifyNodeProperty(Property);
		if (PropertyClass == EKawaiiPhysicsPresetPropertyClass::CopyTarget ||
			PropertyClass == EKawaiiPhysicsPresetPropertyClass::BoneAssignment ||
			PropertyClass == EKawaiiPhysicsPresetPropertyClass::Tag)
		{
			CopyNodeProperty(Property, SourceNode, Node, this);
		}
	}

	if (SourceNode.KawaiiPhysicsTag.IsValid())
	{
		TargetTags.AddTag(SourceNode.KawaiiPhysicsTag);
	}
}

void UKawaiiPhysicsPresetDataAsset::ApplyToNode(FAnimNode_KawaiiPhysics& TargetNode,
                                                const FKawaiiPhysicsPresetApplyOptions& Options,
                                                UObject* TargetOuter) const
{
	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		if (!ShouldApplyNodeProperty(Property, Options))
		{
			continue;
		}

		const FName PropertyName = Property.GetFName();
		if (!TargetOuter &&
			(PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, ExternalForces) ||
				PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_KawaiiPhysics, CustomExternalForces)))
		{
			continue;
		}

		CopyNodeProperty(Property, Node, TargetNode, TargetOuter);
	}
}

bool UKawaiiPhysicsPresetDataAsset::MatchesNode(const FAnimNode_KawaiiPhysics& TargetNode,
                                                const FKawaiiPhysicsPresetApplyOptions& Options,
                                                TArray<FName>& OutDiffProperties,
                                                TArray<FName>* OutComparedProperties) const
{
	OutDiffProperties.Reset();
	if (OutComparedProperties)
	{
		OutComparedProperties->Reset();
	}

	for (TFieldIterator<FProperty> PropertyIt(FAnimNode_KawaiiPhysics::StaticStruct(), EFieldIteratorFlags::ExcludeSuper);
	     PropertyIt; ++PropertyIt)
	{
		const FProperty& Property = **PropertyIt;
		if (!ShouldApplyNodeProperty(Property, Options))
		{
			continue;
		}

		const FName PropertyName = Property.GetFName();
		if (OutComparedProperties)
		{
			OutComparedProperties->Add(PropertyName);
		}

		if (!Property.Identical_InContainer(&Node, &TargetNode, 0, KawaiiPhysicsPresetIdenticalPortFlags))
		{
			OutDiffProperties.Add(PropertyName);
		}
	}

	return OutDiffProperties.IsEmpty();
}

bool UKawaiiPhysicsPresetDataAsset::TargetsNodeTag(const FGameplayTag& NodeTag) const
{
	// Collect系APIはFilterTags空を全件扱いするが、TargetTags空は全ノード書き換え事故を避けるため対象なし扱いにする。
	return !TargetTags.IsEmpty() && (bTargetTagsExactMatch ? TargetTags.HasTagExact(NodeTag) : NodeTag.MatchesAny(TargetTags));
}

void UKawaiiPhysicsPresetDataAsset::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FKawaiiPhysicsPresetVersion::GUID);
}

USkeleton* UKawaiiPhysicsPresetDataAsset::GetSkeleton(bool& bInvalidSkeletonIsError,
                                                      const IPropertyHandle* PropertyHandle)
{
#if WITH_EDITORONLY_DATA
	return Skeleton;
#else
	return nullptr;
#endif
}
