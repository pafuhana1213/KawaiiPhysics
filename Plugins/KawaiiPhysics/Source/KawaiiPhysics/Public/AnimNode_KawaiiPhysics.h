#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_KawaiiPhysics.generated.h"

class UKawaiiPhysicsLimitsDataAsset;
class UKawaiiPhysicsBoneConstraintsDataAsset;

UENUM()
enum class EPlanarConstraint : uint8
{
	None,
	X,
	Y,
	Z,
};

UENUM()
enum class EBoneForwardAxis : uint8
{
	X_Positive,
	X_Negative,
	Y_Positive,
	Y_Negative,
	Z_Positive,
	Z_Negative,
};

UENUM()
enum class ECollisionLimitType : uint8
{
	None,
	Spherical,
	Capsule,
	Planar,
};

USTRUCT()
struct FCollisionLimitBase
{
	GENERATED_BODY()

	/** Bone to attach the sphere to */
	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	FBoneReference DrivingBone;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase)
	FVector OffsetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = CollisionLimitBase, meta = (ClampMin = "-360", ClampMax = "360"))
	FRotator OffsetRotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	UPROPERTY()
	bool bEnable = true;

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	bool bFromDataAsset = false;

	UPROPERTY(VisibleAnywhere, Category = Debug, meta = (IgnoreForMemberInitializationTest))
	FGuid Guid = FGuid::NewGuid();

	UPROPERTY()
	ECollisionLimitType Type = ECollisionLimitType::None;

#endif
};

USTRUCT(BlueprintType)
struct FSphericalLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FSphericalLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Spherical;
#endif
	}

	/** Radius of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;
};

USTRUCT(BlueprintType)
struct FCapsuleLimit : public FCollisionLimitBase
{
	GENERATED_BODY()


	FCapsuleLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Capsule;
#endif
	}

	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;
};

USTRUCT(BlueprintType)
struct FPlanarLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	FPlanarLimit()
	{
#if WITH_EDITORONLY_DATA
		Type = ECollisionLimitType::Planar;
#endif
	}

	UPROPERTY()
	FPlane Plane = FPlane(0, 0, 0, 0);
};

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Damping = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingLocation = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingRotation = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Stiffness = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Radius = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float LimitAngle = 0.0f;
};

USTRUCT()
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	FBoneReference BoneRef;
	UPROPERTY()
	int32 ParentIndex = -1;
	UPROPERTY()
	TArray<int32> ChildIndexs;

	UPROPERTY()
	FKawaiiPhysicsSettings PhysicsSettings;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;
	UPROPERTY()
	FVector PrevLocation = FVector::ZeroVector;
	UPROPERTY()
	FQuat PrevRotation = FQuat::Identity;
	UPROPERTY()
	FVector PoseLocation = FVector::ZeroVector;
	UPROPERTY()
	FQuat PoseRotation = FQuat::Identity;
	UPROPERTY()
	FVector PoseScale = FVector::OneVector;
	UPROPERTY()
	float LengthFromRoot = 0.0f;
	UPROPERTY()
	bool bDummy = false;
	UPROPERTY()
	bool bSkipSimulate = false;

public:
	void UpdatePoseTransform(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& Pose,
	                         bool ResetBoneTransformWhenBoneNotFound)
	{
		const auto CompactPoseIndex = BoneRef.GetCompactPoseIndex(BoneContainer);
		if (CompactPoseIndex < 0)
		{
			// Reset bone location and rotation may cause trouble when switching between skeleton LODs #44
			if (ResetBoneTransformWhenBoneNotFound)
			{
				PoseLocation = FVector::ZeroVector;
				PoseRotation = FQuat::Identity;
				PoseScale = FVector::OneVector;
			}
			return;
		}

		const auto ComponentSpaceTransform = Pose.GetComponentSpaceTransform(CompactPoseIndex);
		PoseLocation = ComponentSpaceTransform.GetLocation();
		PoseRotation = ComponentSpaceTransform.GetRotation();
		PoseScale = ComponentSpaceTransform.GetScale3D();
	}

	FKawaiiPhysicsModifyBone()
	{
	}
};

UENUM()
enum class EXPBDComplianceType : uint8
{
	Concrete UMETA(DisplayName = "Concrete"),
	Wood UMETA(DisplayName = "Wood"),
	Leather UMETA(DisplayName = "Leather"),
	Tendon UMETA(DisplayName = "Tendon"),
	Rubber UMETA(DisplayName = "Rubber"),
	Muscle UMETA(DisplayName = "Muscle"),
	Fat UMETA(DisplayName = "Fat"),
};

USTRUCT()
struct FModifyBoneConstraint
{
	GENERATED_BODY()

	FModifyBoneConstraint()
	{
	}

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference Bone1;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics")
	FBoneReference Bone2;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(InlineEditConditionToggle))
	bool bOverrideCompliance = false;

	UPROPERTY(EditAnywhere, category = "KawaiiPhysics", meta=(EditCondition="bOverrideCompliance"))
	EXPBDComplianceType ComplianceType = EXPBDComplianceType::Leather;

	UPROPERTY()
	int32 ModifyBoneIndex1 = -1;

	UPROPERTY()
	int32 ModifyBoneIndex2 = -1;

	UPROPERTY()
	float Length = -1.0f;

	UPROPERTY()
	bool bIsDummy = false;

	UPROPERTY()
	float Lambda = 0.0f;

	FORCEINLINE bool operator ==(const FModifyBoneConstraint& Other) const
	{
		return ((Bone1 == Other.Bone1 && Bone2 == Other.Bone2) || (Bone1 == Other.Bone2 && Bone2 == Other.Bone1)) &&
			ComplianceType == Other.ComplianceType;
	}

	void InitializeBone(const FBoneContainer& RequiredBones)
	{
		Bone1.Initialize(RequiredBones);
		Bone2.Initialize(RequiredBones);
	}

	bool IsBoneReferenceValid() const
	{
		//return Bone1.IsValidToEvaluate() && Bone2.IsValidToEvaluate();
		return ModifyBoneIndex1 >= 0 && ModifyBoneIndex2 >= 0;
	}

	bool IsValid() const
	{
		return Length > 0.0f;
	}
};


USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FAnimNode_KawaiiPhysics : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, Category = ModifyTarget)
	FBoneReference RootBone;
	UPROPERTY(EditAnywhere, Category = ModifyTarget)
	TArray<FBoneReference> ExcludeBones;

	UPROPERTY(EditAnywhere, Category = "TargetFramerate", meta = (EditCondition = "OverrideTargetFramerate"))
	int32 TargetFramerate = 60;
	UPROPERTY(EditAnywhere, Category = "TargetFramerate", meta = (InlineEditConditionToggle))
	bool OverrideTargetFramerate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmUp",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bNeedWarmUp = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WarmUp",
		meta = (PinHiddenByDefault, EditCondition="bNeedWarmUp", ClampMin = "0"))
	int32 WarmUpFrames = 0;

	/** Settings for control of physical behavior */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FKawaiiPhysicsSettings PhysicsSettings;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* DampingCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* WorldDampingLocationCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* WorldDampingRotationCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* StiffnessCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* RadiusCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY()
	UCurveFloat* LimitAngleCurve_DEPRECATED = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve DampingCurveData;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve WorldDampingLocationCurveData;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve WorldDampingRotationCurveData;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve StiffnessCurveData;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve RadiusCurveData;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FRuntimeFloatCurve LimitAngleCurveData;

	/** Flag to update each frame physical parameter. Disable to improve performance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;

	/** Add a dummy bone to the end bone if it's above 0. It affects end bone rotation. For example, it rotates well if it is short */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DummyBone", meta = (PinHiddenByDefault, ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/** Bone forward direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DummyBone", meta = (PinHiddenByDefault))
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	/** Fix the bone on the specified plane  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	bool ResetBoneTransformWhenBoneNotFound = false;


	UPROPERTY(EditAnywhere, Category = "Spherical Limits")
	TArray<FSphericalLimit> SphericalLimits;
	UPROPERTY(EditAnywhere, Category = "Capsule Limits")
	TArray<FCapsuleLimit> CapsuleLimits;
	UPROPERTY(EditAnywhere, Category = "Planar Limits")
	TArray<FPlanarLimit> PlanarLimits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	EXPBDComplianceType BoneConstraintGlobalComplianceType = EXPBDComplianceType::Leather;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountBeforeCollision = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountAfterCollision = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	bool bAutoAddChildDummyBoneConstraint = true;
	UPROPERTY(EditAnywhere, Category = "Bone Constraint (Experimental)", meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraints;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset;
	UPROPERTY(VisibleAnywhere, Category = "Bone Constraint (Experimental)", AdvancedDisplay,
		meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraintsData;
	UPROPERTY()
	TArray<FModifyBoneConstraint> MergedBoneConstraints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits Data", meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitsDataAsset = nullptr;
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits Data")
	TArray<FSphericalLimit> SphericalLimitsData;
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits Data")
	TArray<FCapsuleLimit> CapsuleLimitsData;
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits Data")
	TArray<FPlanarLimit> PlanarLimitsData;


	/** If the movement amount of one frame exceeds the threshold, ignore the movement  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport", meta = (PinHiddenByDefault))
	float TeleportDistanceThreshold = 300.0f;

	/** If the rotation amount of one frame exceeds the threshold, ignore the rotation  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport", meta = (PinHiddenByDefault))
	float TeleportRotationThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	FVector Gravity = FVector::ZeroVector;

	/** Whether or not wind is enabled for the bodies in this simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (PinHiddenByDefault))
	bool bEnableWind = false;

	/** Scale to apply to calculated wind velocities in the solver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (DisplayAfter = "bEnableWind"),
		meta = (PinHiddenByDefault))
	float WindScale = 1.0f;

	/**
	 *	EXPERIMENTAL. Perform sweeps for each simulating bodies to avoid collisions with the world.
	 *	This greatly increases the cost of the physics simulation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (PinHiddenByDefault))
	bool bAllowWorldCollision = false;
	//use component collision channel settings by default
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bOverrideCollisionParams = false;
	/** Types of objects that this physics objects will collide with. */
	UPROPERTY(EditAnywhere, Category = "Collision",
		meta = (FullyExpand = "true", EditCondition = "bAllowWorldCollision&&bOverrideCollisionParams"))
	FBodyInstance CollisionChannelSettings;


	/** Self collision is best done by setting the "Limits" in this node, but if you really need using PhysicsAsset collision, uncheck this!*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bIgnoreSelfComponent = true;
	/** Self bone is always ignored*/
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FBoneReference> IgnoreBones;
	/** If the bone starts with this name, will be ignored (Self bone is always ignored)*/
	UPROPERTY(EditAnywhere, Category = "Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FName> IgnoreBoneNamePrefix;

	UPROPERTY()
	TArray<FKawaiiPhysicsModifyBone> ModifyBones;

protected:
	UPROPERTY()
	float TotalBoneLength = 0;
	UPROPERTY()
	FTransform PreSkelCompTransform;
	UPROPERTY()
	bool bInitPhysicsSettings = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bEditing = false;
#endif

	FVector SkelCompMoveVector;
	FQuat SkelCompMoveRotation;
	float DeltaTime;
	float DeltaTimeOld;
	bool bResetDynamics;

public:
	FAnimNode_KawaiiPhysics();

	// FAnimNode_Base interface
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	                                               TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

	// End of FAnimNode_SkeletalControlBase interface

	// For AnimGraphNode
	float GetTotalBoneLength() const
	{
		return TotalBoneLength;
	}

protected:
	FVector GetBoneForwardVector(const FQuat& Rotation) const
	{
		switch (BoneForwardAxis)
		{
		default:
		case EBoneForwardAxis::X_Positive:
			return Rotation.GetAxisX();
		case EBoneForwardAxis::X_Negative:
			return -Rotation.GetAxisX();
		case EBoneForwardAxis::Y_Positive:
			return Rotation.GetAxisY();
		case EBoneForwardAxis::Y_Negative:
			return -Rotation.GetAxisY();
		case EBoneForwardAxis::Z_Positive:
			return Rotation.GetAxisZ();
		case EBoneForwardAxis::Z_Negative:
			return -Rotation.GetAxisZ();
		}
	}

	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	// Initialize
	void InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);
	void InitBoneConstraints();
	void ApplyLimitsDataAsset(const FBoneContainer& RequiredBones);
	void ApplyBoneConstraintDataAsset(const FBoneContainer& RequiredBones);
	int32 AddModifyBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                    const FReferenceSkeleton& RefSkeleton, int32 BoneIndex);

	// clone from FReferenceSkeleton::GetDirectChildBones
	int32 CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex,
	                        TArray<int32>& Children) const;
	void CalcBoneLength(FKawaiiPhysicsModifyBone& Bone, const TArray<FTransform>& RefBonePose);

	// Updates for simulate
	void UpdatePhysicsSettingsOfModifyBones();
	void UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
	                           const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
	                         const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
	                        const FBoneContainer& BoneContainer, const FTransform& ComponentTransform);
	void UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);
	void UpdateSkelCompMove(const FTransform& ComponentTransform);

	// Simulate
	void SimulateModifyBones(const FComponentSpacePoseContext& Output,
	                         const FTransform& ComponentTransform);
	void Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene, const FTransform& ComponentTransform,
	              const FVector& GravityCS, const float& Exponent);
	void AdjustByWorldCollision(FKawaiiPhysicsModifyBone& Bone, const USkeletalMeshComponent* OwningComp);
	void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits);
	void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits);
	void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits);
	void AdjustByAngleLimit(
		FKawaiiPhysicsModifyBone& Bone,
		const FKawaiiPhysicsModifyBone& ParentBone);
	void AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone, const FKawaiiPhysicsModifyBone& ParentBone);
	void AdjustByBoneConstraints();

	void ApplySimulateResult(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	                         TArray<FBoneTransform>& OutBoneTransforms);
	void WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
	            FTransform& ComponentTransform);

	FVector GetWindVelocity(const FSceneInterface* Scene, const FTransform& ComponentTransform,
	                        const FKawaiiPhysicsModifyBone& Bone) const;

#if ENABLE_ANIM_DEBUG
	void AnimDrawDebug(const FComponentSpacePoseContext& Output);
#endif
};
