#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimNodeBase.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"

#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#include "AnimNode_KawaiiPhysics.generated.h"

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

};

USTRUCT()
struct FSphericalLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	/** Radius of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	/** Whether to lock bodies inside or outside of the sphere */
	UPROPERTY(EditAnywhere, Category = SphericalLimit)
	ESphericalLimitType LimitType = ESphericalLimitType::Outer;
};

USTRUCT()
struct FCapsuleLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Radius = 5.0f;

	UPROPERTY(EditAnywhere, Category = CapsuleLimit, meta = (ClampMin = "0"))
	float Length = 10.0f;
};

USTRUCT()
struct FPlanarLimit : public FCollisionLimitBase
{
	GENERATED_BODY()

	UPROPERTY()
	FPlane Plane;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (PinHiddenByDefault, UIMin = "0", UIMax = "180", ClampMin = "0", ClampMax = "180"), category = "KawaiiPhysics")
	float LimitAngle = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (PinHiddenByDefault), category = "KawaiiPhysics")
	bool bUseSplitAxisLimit = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (PinHiddenByDefault, EditCondition = "bUseSplitAxisLimit", UIMin = "0", UIMax = "180", ClampMin = "0", ClampMax = "180"), category = "KawaiiPhysics")
	FVector AngularLimitPositiveMax;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (PinHiddenByDefault, EditCondition = "bUseSplitAxisLimit", UIMin = "0", UIMax = "180", ClampMin = "0", ClampMax = "180"), category = "KawaiiPhysics")
	FVector AngularLimitNegativeMax;
};

USTRUCT()
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()
	
public:
	UPROPERTY()
	FBoneReference BoneRef;
	UPROPERTY()
	int ParentIndex = -1;
	UPROPERTY()
	TArray<int> ChildIndexs;

	UPROPERTY()
	FKawaiiPhysicsSettings PhysicsSettings;

	UPROPERTY()
	FVector Location;
	UPROPERTY()
	FVector PrevLocation;
	UPROPERTY()
	FQuat PrevRotation;
	UPROPERTY()
	FVector PoseLocation;
	UPROPERTY()
	FQuat PoseRotation;
	UPROPERTY()
	FVector PoseScale;
	UPROPERTY()
	float LengthFromRoot;
	UPROPERTY()
	bool bDummy = false;


public:

	void UpdatePoseTranform(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& Pose)
	{
		auto CompactPoseIndex = BoneRef.GetCompactPoseIndex(BoneContainer);
		if (CompactPoseIndex < 0)
		{
			PoseLocation = FVector::ZeroVector;
			PoseRotation = FQuat::Identity;
			PoseScale = FVector::OneVector;
			return;
		}

		auto ComponentSpaceTransform = Pose.GetComponentSpaceTransform(CompactPoseIndex);
		PoseLocation = ComponentSpaceTransform.GetLocation();
		PoseRotation = ComponentSpaceTransform.GetRotation();
		PoseScale = ComponentSpaceTransform.GetScale3D();
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

	UPROPERTY(EditAnywhere, Category = TargetFramerate, meta = (EditCondition = "OverrideTargetFramerate"))
	int TargetFramerate = 60;
	UPROPERTY(EditAnywhere, Category = TargetFramerate, meta = (InlineEditConditionToggle))
	bool OverrideTargetFramerate = false;

	/** Settings for control of physical behavior */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	FKawaiiPhysicsSettings PhysicsSettings;
	
	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* DampingCurve = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* WorldDampingLocationCurve = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* WorldDampingRotationCurve = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* StiffnessCurve = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* RadiusCurve = nullptr;

	/** Curve for adjusting the set value of physical behavior. Use rate of bone length from Root */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	UCurveFloat* LimitAngleCurve = nullptr;

	/** Flag to update each frame physical parameter. Disable to improve performance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;
	
	/** Add a dummy bone to the end bone if it's above 0. It affects end bone rotation. For example, it rotates well if it is short */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault, ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/** Bone forward direction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	/** Fix the bone on the specified plane  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced Physics Settings", meta = (PinHiddenByDefault))
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;


	UPROPERTY(EditAnywhere, Category = "Spherical Limits")
	TArray< FSphericalLimit> SphericalLimits;
	UPROPERTY(EditAnywhere, Category = "Capsule Limits")
	TArray< FCapsuleLimit> CapsuleLimits;
	UPROPERTY(EditAnywhere, Category = "Planar Limits")
	TArray< FPlanarLimit> PlanarLimits;

	/** If the movement amount of one frame exceeds the threshold, ignore the movement  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport", meta = (PinHiddenByDefault))
	float TeleportDistanceThreshold = 300.0f;

	/** If the rotation amount of one frame exceeds the threshold, ignore the rotation  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport", meta = (PinHiddenByDefault))
	float TeleportRotationThreshold = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (PinHiddenByDefault))
	FVector Gravity = FVector::ZeroVector;
	
	/** Whether or not wind is enabled for the bodies in this simulation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Wind, meta = (PinHiddenByDefault))
	bool bEnableWind = false;

	/** Scale to apply to calculated wind velocities in the solver */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Wind, meta = (DisplayAfter = "bEnableWind"), meta = (PinHiddenByDefault))
	float WindScale = 1.0f;


	UPROPERTY()
	TArray< FKawaiiPhysicsModifyBone > ModifyBones;


private:
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

public:
	FAnimNode_KawaiiPhysics();

	// FAnimNode_Base interface
	//virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	//virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	// End of FAnimNode_Base interface

	// 
	void InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer);
	float GetTotalBoneLength() 
	{
		return TotalBoneLength;
	}

	FVector GetBoneForwardVector(const FQuat& Rotation)
	{
		switch(BoneForwardAxis) {
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

protected:
	// FAnimNode_SkeletalControlBase interface
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

private:

	int AddModifyBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, const FReferenceSkeleton& RefSkeleton, int BoneIndex);
	
	// clone from FReferenceSkeleton::GetDirectChildBones
	int32 CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex, TArray<int32> & Children) const;
	void CalcBoneLength(FKawaiiPhysicsModifyBone& Bone, const TArray<FTransform>& RefBonePose);

	void UpdatePhysicsSettingsOfModifyBones();
	void UpdateSphericalLimits(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, FTransform& ComponentTransform);
	void UpdateCapsuleLimits(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, FTransform& ComponentTransform);
	void UpdatePlanerLimits(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, FTransform& ComponentTransform);

	void SimulateModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, FTransform& ComponentTransform);
	void AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone);
	void AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone);
	void AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone);
	void AdjustByAngleLimit(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, FTransform& ComponentTransform, FKawaiiPhysicsModifyBone& Bone, FKawaiiPhysicsModifyBone& ParentBone);
	void AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone, FKawaiiPhysicsModifyBone& ParentBone);

	void ApplySimuateResult(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer, TArray<FBoneTransform>& OutBoneTransforms);
	
};