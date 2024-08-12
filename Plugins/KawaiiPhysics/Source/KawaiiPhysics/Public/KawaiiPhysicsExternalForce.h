#pragma once
#include "AnimNode_KawaiiPhysics.h"
#include "Curves/CurveVector.h"

#include "KawaiiPhysicsExternalForce.generated.h"

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

UENUM(BlueprintType)
enum class EExternalForceCurveEvaluateType : uint8
{
	Single,
	Average,
	Max,
	Min
};


///
/// Base
///
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	bool bIsEnabled = true;

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


	UPROPERTY(EditAnywhere, meta=(DisplayPriority=1, EditCondition=bCanSelectForceSpace, EditConditionHides),
		Category="KawaiiPhysics|ExternalForce")
	EExternalForceSpace ExternalForceSpace = EExternalForceSpace::WorldSpace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(DisplayPriority=1), Category="KawaiiPhysics|ExternalForce")
	FFloatInterval RandomForceScaleRange = FFloatInterval(1.0f, 1.0f);

	UPROPERTY()
	TObjectPtr<UObject> ExternalOwner;

	UPROPERTY()
	bool bIsOneShot;

#if ENABLE_ANIM_DEBUG
	float DebugArrowLength = 5.0f;
	float DebugArrowSize = 1.0f;
	FVector DebugArrowOffset = FVector::Zero();
	TMap<FName, FVector> BoneForceMap;
#endif

protected:
	UPROPERTY()
	float RandomizedForceScale = 0.0f;

	UPROPERTY()
	FVector Force = FVector::Zero();

	UPROPERTY()
	FTransform ComponentTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	bool bCanSelectForceSpace = true;

public:
	virtual ~FKawaiiPhysics_ExternalForce() = default;

	virtual void Initialize(const FAnimationInitializeContext& Context)
	{
	}

	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp)
	{
		ComponentTransform = SkelComp->GetComponentTransform();
		RandomizedForceScale = FMath::RandRange(RandomForceScaleRange.Min, RandomForceScaleRange.Max);
	}

	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext, const FTransform& BoneTM = FTransform::Identity)
	{
	}

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

public:
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KawaiiPhysics|ExternalForce")
	float Interval = 0.0f;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

private:
	UPROPERTY()
	float Time = 0.0f;
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

public:
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (editcondition = "bUseOverrideGravityDirection"), Category="KawaiiPhysics|ExternalForce")
	FVector OverrideGravityDirection = FVector::Zero();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (InlineEditConditionToggle),
		Category="KawaiiPhysics|ExternalForce")
	bool bUseOverrideGravityDirection = false;

private:

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

protected:
};

///
/// Curve
///
USTRUCT(BlueprintType, DisplayName = "Curve")
struct KAWAIIPHYSICS_API FKawaiiPhysics_ExternalForce_Curve : public FKawaiiPhysics_ExternalForce
{
	GENERATED_BODY()

public:
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
	UPROPERTY()
	float Time = 0.0f;
	UPROPERTY()
	float PrevTime = 0.0f;
	UPROPERTY()
	float MaxCurveTime = 0.0f;

public:
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

public:
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
	UPROPERTY()
	UWorld* World;

public:
	virtual void PreApply(FAnimNode_KawaiiPhysics& Node, const USkeletalMeshComponent* SkelComp) override;
	virtual void Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
	                   const FComponentSpacePoseContext& PoseContext,
	                   const FTransform& BoneTM = FTransform::Identity) override;

protected:
};
