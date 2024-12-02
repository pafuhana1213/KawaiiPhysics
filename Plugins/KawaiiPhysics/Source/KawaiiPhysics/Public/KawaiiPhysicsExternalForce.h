// KawaiiPhysics : Copyright (c) 2019-2024 pafuhana1213, MIT License

#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "SceneManagement.h"
#include "Curves/CurveVector.h"
#include "KawaiiPhysicsExternalForce.generated.h"

/**
 * Enum representing the space in which external forces are simulated.
 */
UENUM(BlueprintType)
enum class EExternalForceSpace : uint8
{
	/** Simulate in component space. Moving the entire skeletal mesh will have no affect on velocities */
	ComponentSpace,
	/** Simulate in world space. Moving the skeletal mesh will generate velocity changes */
	WorldSpace,
	/** Simulate in another bone space. Moving the entire skeletal mesh and individually modifying the base bone will have no affect on velocities */
	BoneSpace,
};

/**
 * Enum representing the evaluation type for external force curves.
 */
UENUM(BlueprintType)
enum class EExternalForceCurveEvaluateType : uint8
{
	/** Evaluate the curve at a single point */
	Single,
	/** Evaluate the curve by averaging multiple points */
	Average,
	/** Evaluate the curve by taking the maximum value from multiple points */
	Max,
	/** Evaluate the curve by taking the minimum value from multiple points */
	Min
};


///
/// Base
///
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** Whether the external force is enabled */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	bool bIsEnabled = true;

	/** Whether to draw debug information for the external force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	bool bDrawDebug = false;

	/** 
	* 外力を適応するボーンを指定（＝指定しなかったボーンには適応しない）
	* Specify the bones to which the external force will be applied (= the force will not be applied to bones that are not specified)
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> ApplyBoneFilter;

	/** 
	* 外力を適応しないボーンを指定
	* Specify the bones to which the external force will be NOT applied
	*/
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	TArray<FBoneReference> IgnoreBoneFilter;

	/** The space in which the external force is simulated */
	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1, EditCondition=bCanSelectForceSpace, EditConditionHides),
		Category="KawaiiPhysics|ExternalForce")
	EExternalForceSpace ExternalForceSpace = EExternalForceSpace::WorldSpace;

	/** Range for randomizing the force scale */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	FFloatInterval RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);

	/** Owner of the external force */
	UPROPERTY()
	TObjectPtr<UObject> ExternalOwner;

	/** Whether the external force is applied only once */
	UPROPERTY()
	bool bIsOneShot = false;

#if ENABLE_ANIM_DEBUG
	/** Length of the debug arrow */
	float DebugArrowLength = 5.0f;

	/** Size of the debug arrow */
	float DebugArrowSize = 1.0f;

	/** Offset for the debug arrow */
	FVector DebugArrowOffset = FVector::Zero();

	/** Map of bone names to forces for debugging */
	TMap<FName, FVector> BoneForceMap;
#endif

protected:
	/** Randomized scale of the force */
	UPROPERTY()
	float RandomizedForceScale = 0.0f;

	/** The force vector */
	UPROPERTY()
	FVector Force = FVector::Zero();

	/** Transform of the component */
	UPROPERTY()
	FTransform ComponentTransform;

	/** Whether the force space can be selected */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bCanSelectForceSpace = true;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void Initialize(const FAnimationInitializeContext& Context)
	{
	}

	/** Prepares the external force before applying it */
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
	{
		ComponentTransform = SkelComp->GetComponentTransform();
		RandomizedForceScale = FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max);
	}

	/** Applies the external force to a bone */
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM = FTransform::Identity)
	{
	}

	/** Finalizes the external force after applying it */
	virtual void PostApply(FAnimNode_KawaiiPhysics& Node)
	{
		if (bIsOneShot)
		{
			Node.ExternalForces.RemoveAll([&](FInstancedStruct& InstancedStruct)
			{
				const auto* ExternalForcePtr = InstancedStruct.GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				return ExternalForcePtr == this;
			});
		}
	}

	/** Checks if debug information should be drawn */
	virtual bool IsDebugEnabled(bool bInPersona = false)
	{
		if (bInPersona)
		{
			return bDrawDebug && bIsEnabled;
		}

#if ENABLE_ANIM_DEBUG
		if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
		{
			return bDrawDebug && bIsEnabled;
		}
#endif

		return false;
	}

#if ENABLE_ANIM_DEBUG
	/** Draws debug information for the external force */
	virtual void AnimDrawDebug(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                           const FComponentSpacePoseContext& PoseContext)
	{
		if (IsDebugEnabled() && !Force.IsZero())
		{
			const auto AnimInstanceProxy = PoseContext.AnimInstanceProxy;
			const FVector ModifyRootBoneLocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
				Bone.Location);

			AnimInstanceProxy->AnimDrawDebugDirectionalArrow(
				ModifyRootBoneLocationWS + DebugArrowOffset,
				ModifyRootBoneLocationWS + DebugArrowOffset + BoneForceMap.Find(Bone.BoneRef.BoneName)->GetSafeNormal()
				*
				DebugArrowLength,
				DebugArrowSize, FColor::Red, false, 0.f, 2);
		}
	}
#endif

#if WITH_EDITOR
	/** Draws debug information for the external force in edit mode */
	virtual void AnimDrawDebugForEditMode(const FKawaiiPhysicsModifyBone& ModifyBone,
	                                      const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI)
	{
		if (IsDebugEnabled(true) && CanApply(ModifyBone) && !Force.IsNearlyZero() && BoneForceMap.Contains(
			ModifyBone.BoneRef.BoneName))
		{
			const FTransform ArrowTransform = FTransform(
				BoneForceMap.Find(ModifyBone.BoneRef.BoneName)->GetSafeNormal().ToOrientationRotator(),
				ModifyBone.Location + DebugArrowOffset);
			DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Red, DebugArrowLength, DebugArrowSize,
			                     SDPG_Foreground, 1.0f);
		}
	}
#endif

protected:
	/** Checks if the external force can be applied to a bone */
	bool CanApply(const FKawaiiPhysicsModifyBone& Bone) const
	{
		if (!ApplyBoneFilter.IsEmpty() && !ApplyBoneFilter.Contains(Bone.BoneRef))
		{
			return false;
		}

		if (!IgnoreBoneFilter.IsEmpty() && IgnoreBoneFilter.Contains(Bone.BoneRef))
		{
			return false;
		}

		return true;
	}
};

///
/// Basic
///
USTRUCT(BlueprintType, DisplayName = "Basic")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Basic : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** Direction of the force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FVector ForceDir = FVector::Zero();

	/** 
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/** Interval for applying the force */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	float Interval = 0.0f;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

private:
	/** Current time */
	UPROPERTY()
	float Time = 0.0f;

	/** Previous time */
	UPROPERTY()
	float PrevTime = 0.0f;
};

///
/// Gravity
///
USTRUCT(BlueprintType, DisplayName = "Gravity")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Gravity : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Gravity()
	{
		bCanSelectForceSpace = false;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/** 
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

	/** 
	* Character側で設定されたCustomGravityDirectionを使用するフラグ(UE5.4以降)
	* Flag to use CustomGravityDirection set on the Character side (UE5.4 and later)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityDirection = false;

	/** 
	* Character側で設定されたGravityScaleを使用するフラグ
	* Flag to use GravityScale set on the Character side
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	bool bUseCharacterGravityScale = false;

	/** 
	 * Direction to override the gravity.
	 * This direction is used when bUseOverrideGravityDirection is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (EditCondition = "bUseOverrideGravityDirection"), Category="KawaiiPhysics|ExternalForce")
	FVector OverrideGravityDirection = FVector::Zero();

	/** 
	 * Flag to determine whether to use the override gravity direction.
	 * If true, the gravity direction will be overridden by OverrideGravityDirection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle),
		Category="KawaiiPhysics|ExternalForce")
	bool bUseOverrideGravityDirection = false;

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};

///
/// Curve
///
USTRUCT(BlueprintType, DisplayName = "Curve")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Curve : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	/** 
	* 時間に応じて変化する外力をカーブで設定。X軸:Time Y軸:Force
	* Set the external force that changes over time using a curve. X-axis: Time Y-axis: Force
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (XAxisName="Time", YAxisName="Force"),
		Category="KawaiiPhysics|ExternalForce")
	FRuntimeVectorCurve ForceCurve;

	/** 
	* カーブの評価方式。
	* Single以外に設定した場合：前フレームからの経過時間をSubstepCountで分割し、
	* 分割後の各時間におけるカーブの値の平均・最大値・最小値を外力として使用
	* Curve evaluation method
	* If set to anything other than Single: The time elapsed from the previous frame is divided by SubstepCount,
	* and the Average, Maximum, or Minimum values of the curve at each time point after division are used as external forces.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	EExternalForceCurveEvaluateType CurveEvaluateType = EExternalForceCurveEvaluateType::Single;

	/** 
	* 経過時間の分割数
	* Number of divisions of elapsed time
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta=(EditCondition="CurveEvaluateType!=EExternalForceCurveEvaluateType::Single"),
		Category="KawaiiPhysics|ExternalForce")
	int SubstepCount = 10;

	/**
	 * Scale factor for the time.
	 * This value is used to scale the time for the external force.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category ="KawaiiPhysics|ExternalForce")
	float TimeScale = 1.0f;

	/** 
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

private:
	/**
	 * Current time.
	 * This value is used to track the current time for the external force.
	 */
	UPROPERTY()
	float Time = 0.0f;

	/**
	 * Previous time.
	 * This value is used to track the previous time for the external force.
	 */
	UPROPERTY()
	float PrevTime = 0.0f;

	/**
	 * Maximum curve time.
	 * This value is used to track the maximum time for the force curve.
	 */
	UPROPERTY()
	float MaxCurveTime = 0.0f;

public:
	/**
	 * Initializes the maximum curve time.
	 * This function calculates the maximum time value from the ForceCurve and sets it to MaxCurveTime.
	 */
	void InitMaxCurveTime();

	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};

///
/// Wind
///
USTRUCT(BlueprintType, DisplayName = "Wind")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Wind : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

	FKawaiiPhysics_ExternalForce_Wind()
	{
		bCanSelectForceSpace = false;
		ExternalForceSpace = EExternalForceSpace::WorldSpace;
	}

	/** 
	* 各ボーンに適用するForce Rateを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値をForceRateに乗算
	* Corrects the Force Rate applied to each bone.
	* Multiplies the ForceRate by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	FRuntimeFloatCurve ForceRateByBoneLengthRate;

private:
	/**
	 * Pointer to the world.
	 * This is used to access the world context for the external force.
	 */
	UPROPERTY()
	TObjectPtr<UWorld> World;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;
};
