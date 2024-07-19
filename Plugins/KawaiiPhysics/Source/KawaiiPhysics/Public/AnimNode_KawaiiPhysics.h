#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "InstancedStruct.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_KawaiiPhysics.generated.h"

class UKawaiiPhysics_CustomExternalForce;
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

	/** 
	* 減衰度：揺れの強さを制御。値が小さいほど、加速度を物理挙動に反映
	* Damping physical behavior. As the value is smaller, the acceleration is more reflected to the physical behavior
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Damping = 0.1f;

	/** 
	* 剛性度：値が大きいほど、元の形状を維持
	* Stiffness of physical behavior.As the value is larger, pre-physics shape is more respected
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float Stiffness = 0.05f;

	/** 
	* ワールド座標系におけるSkeletal Mesh Componentの移動量の反映度
	* Influence from movement in world coordinate system of Skeletal Mesh Component
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingLocation = 0.8f;

	/** 
	* ワールド座標系におけるSkeletal Mesh Componentの回転量の反映度
	* Influence from rotation in world coordinate system of Skeletal Mesh Component
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float WorldDampingRotation = 0.8f;

	/** 
	* 各ボーンのコリジョン半径
	* Radius of bone's collision
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", DisplayName="Collision Radius"),
		category = "KawaiiPhysics")
	float Radius = 3.0f;

	/** 
	* 物理挙動による回転制限。適切に設定することで荒ぶりを抑制
	* Rotational limitations in physical behavior. Setting the value properly can prevent rampage
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), category = "KawaiiPhysics")
	float LimitAngle = 0.0f;
};

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY()
	FBoneReference BoneRef;
	UPROPERTY(BlueprintReadOnly)
	int32 Index = -1;
	UPROPERTY(BlueprintReadOnly)
	int32 ParentIndex = -1;
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> ChildIndexs;

	UPROPERTY(BlueprintReadOnly)
	FKawaiiPhysicsSettings PhysicsSettings;

	UPROPERTY(BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	FVector PrevLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	FQuat PrevRotation = FQuat::Identity;
	UPROPERTY(BlueprintReadOnly)
	FVector PoseLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	FQuat PoseRotation = FQuat::Identity;
	UPROPERTY(BlueprintReadOnly)
	FVector PoseScale = FVector::OneVector;
	UPROPERTY(BlueprintReadOnly)
	float LengthFromRoot = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	bool bDummy = false;
	UPROPERTY(BlueprintReadOnly)
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
	/** 
	* 指定ボーンとそれ以下のボーンを制御対象に
	* Control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones")
	FBoneReference RootBone;
	/** 
	* 指定したボーンとそれ以下のボーンを制御対象から除去
	* Do NOT control the specified bone and the bones below it
	*/
	UPROPERTY(EditAnywhere, Category = "Bones")
	TArray<FBoneReference> ExcludeBones;

	/** 
	* 0より大きい場合は、制御ボーンの末端にダミーボーンを追加。ダミーボーンを追加することで、末端のボーンの物理制御を改善
	* Add a dummy bone to the end bone if it's above 0. It affects end bone rotation. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (PinHiddenByDefault, ClampMin = "0"))
	float DummyBoneLength = 0.0f;

	/** 
	* ボーンの前方。物理制御やダミーボーンの配置位置に影響
	* Bone forward direction. Affects the placement of physical controls and dummy bones
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bones", meta = (PinHiddenByDefault))
	EBoneForwardAxis BoneForwardAxis = EBoneForwardAxis::X_Positive;

	/** 
	* 物理制御の基本設定
	* Basic physics settings
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, DisplayPriority=0))
	FKawaiiPhysicsSettings PhysicsSettings;

	/** 
	* ターゲットとなるフレームレート
	* Target Frame Rate
	*/
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (EditCondition = "OverrideTargetFramerate"))
	int32 TargetFramerate = 60;
	UPROPERTY(EditAnywhere, Category = "Physics Settings", meta = (InlineEditConditionToggle))
	bool OverrideTargetFramerate = false;

	/** 
	* 物理の空回し回数。物理処理が落ち着いてから開始・表示したい際に使用
	* Number of times physics has been idle. Used when you want to start/display after physics processing has settled down
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, EditCondition="bNeedWarmUp", ClampMin = "0"))
	int32 WarmUpFrames = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bNeedWarmUp = false;

	/** 
	* 1フレームにおけるSkeletalMeshComponentの移動量が設定値を超えた場合、その移動量を物理制御に反映しない
	* If the amount of movement of a SkeletalMeshComponent in one frame exceeds the set value, that amount of movement will not be reflected in the physics control.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	float TeleportDistanceThreshold = 300.0f;

	/** 
	* 1フレームにおけるSkeletalMeshComponentの回転量が設定値を超えた場合、その回転量を物理制御に反映しない
	* If the rotation amount of SkeletalMeshComponent in one frame exceeds the set value, the rotation amount will not be reflected in the physics control.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	float TeleportRotationThreshold = 10.0f;

	/** 
	* 指定した軸に応じた平面上に各ボーンを固定
	* Fix the bone on the specified plane 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", meta = (PinHiddenByDefault))
	EPlanarConstraint PlanarConstraint = EPlanarConstraint::None;

	/** 
 	* 各ボーンの物理パラメータを毎フレーム更新するフラグ。
 	* 無効にするとパフォーマンスが僅かに改善するが、実行中に物理パラメータを変更することが不可能に
	* Flag to update the physics parameters of each bone every frame.
	* Disabling this will slightly improve performance, but it will make it impossible to change physics parameters during execution.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool bUpdatePhysicsSettingsInGame = true;

	/** 
	* 制御対象のボーンが見つからない場合にTransformをリセットするフラグ。基本的には無効を推奨
	* Flag to reset Transform when the controlled bone is not found. It is generally recommended to disable this.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault))
	bool ResetBoneTransformWhenBoneNotFound = false;

	UPROPERTY()
	UCurveFloat* DampingCurve_DEPRECATED = nullptr;
	UPROPERTY()
	UCurveFloat* WorldDampingLocationCurve_DEPRECATED = nullptr;
	UPROPERTY()
	UCurveFloat* WorldDampingRotationCurve_DEPRECATED = nullptr;
	UPROPERTY()
	UCurveFloat* StiffnessCurve_DEPRECATED = nullptr;
	UPROPERTY()
	UCurveFloat* RadiusCurve_DEPRECATED = nullptr;
	UPROPERTY()
	UCurveFloat* LimitAngleCurve_DEPRECATED = nullptr;

	/** 
	* 各ボーンに適用するPhysics Settings/ Damping パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/Damping parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Damping Rate by Bone Length Rate"))
	FRuntimeFloatCurve DampingCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ Stiffness パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/Stiffness parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Stiffness Rate by Bone Length Rate"))
	FRuntimeFloatCurve StiffnessCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ WorldDampingLocation パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/WorldDampingLocation parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "World Damping Location Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingLocationCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ WorldDampingRotation パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/WorldDampingRotation parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "World Damping Rotation Rate by Bone Length Rate"))
	FRuntimeFloatCurve WorldDampingRotationCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ CollisionRadius パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/CollisionRadius parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "Radius Rate by Bone Length Rate"))
	FRuntimeFloatCurve RadiusCurveData;

	/** 
	* 各ボーンに適用するPhysics Settings/ LimitAngle パラメータを補正。
	* 「RootBoneから特定のボーンまでの長さ / RootBoneから末端のボーンまでの長さ」(0.0~1.0)の値におけるカーブの値を各パラメータに乗算
	* Corrects the Physics Settings/LimitAngle parameters applied to each bone.
	* Multiplies each parameter by the curve value for "Length from RootBone to specific bone / Length from RootBone to end bone" (0.0~1.0).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings", AdvancedDisplay,
		meta = (PinHiddenByDefault, DisplayName = "LimitAngle Rate by Bone Length Rate"))
	FRuntimeFloatCurve LimitAngleCurveData;

	/** 
	* コリジョン（球）
	* Spherical Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimits;
	/** 
	* コリジョン（カプセル）
	* Capsule Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimits;
	/** 
	* コリジョン（平面）
	* Planar Collision
	*/
	UPROPERTY(EditAnywhere, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimits;

	/** 
	* コリジョン設定（DataAsset版）。別AnimNode・ABPで設定を流用したい場合はこちらを推奨
	* Collision settings (DataAsset version). This is recommended if you want to reuse the settings for another AnimNode or ABP.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limits", meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsLimitsDataAsset> LimitsDataAsset = nullptr;
	/** 
	* コリジョン設定（DataAsset版）における球コリジョンのプレビュー
	* Preview of sphere collision in collision settings (DataAsset version)
	*/
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FSphericalLimit> SphericalLimitsData;
	/** 
	* コリジョン設定（DataAsset版）におけるカプセルコリジョンのプレビュー
	* Preview of capsule collision in collision settings (DataAsset version)
	*/
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FCapsuleLimit> CapsuleLimitsData;
	/** 
	* コリジョン設定（DataAsset版）における平面コリジョンのプレビュー
	* Preview of planar collision in collision settings (DataAsset version)
	*/
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Limits")
	TArray<FPlanarLimit> PlanarLimitsData;

	/** 
	* Bone Constraintで用いる剛性タイプ
	* Stiffness type to use in Bone Constraint
	* http://blog.mmacklin.com/2016/10/12/xpbd-slides-and-stiffness/
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	EXPBDComplianceType BoneConstraintGlobalComplianceType = EXPBDComplianceType::Leather;
	/** 
	* Bone Constraintの処理回数（コリジョン処理前）
	* Number of Bone Constraints processed before collision processing
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountBeforeCollision = 1;
	/** 
	* Bone Constraintの処理回数（コリジョン処理後）
	* Number of Bone Constraints processed after collision processing
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	int32 BoneConstraintIterationCountAfterCollision = 1;
	/** 
	* 末端ボーンをBoneConstraint処理の対象にした場合、自動的にダミーボーンも処理対象にするフラグ
	* Flag to automatically processes dummy bones when the end bones are subject to BoneConstraint processing.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	bool bAutoAddChildDummyBoneConstraint = true;

	/** 
	* BoneConstraint処理の対象となるボーンのペアを設定。スカートのように、ボーン間の距離を維持したい場合に使用
	* Sets the bone pair to be processed by BoneConstraint. Used when you want to maintain the distance between bones, such as a skirt.
	*/
	UPROPERTY(EditAnywhere, Category = "Bone Constraint (Experimental)", meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraints;

	/** 
	* BoneConstraint処理の対象となるボーンのペアを設定 (DataAsset版）。別AnimNode・ABPで設定を流用したい場合はこちらを推奨
	* Set the bone pairs to be processed by BoneConstraint (DataAsset version). If you want to reuse the settings for another AnimNode or another ABP, this is recommended.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bone Constraint (Experimental)",
		meta = (PinHiddenByDefault))
	TObjectPtr<UKawaiiPhysicsBoneConstraintsDataAsset> BoneConstraintsDataAsset;

	/** 
	* BoneConstraint処理の対象となるボーンのペアのプレビュー
	* Preview of bone pairs that will be processed by BoneConstraint
	*/
	UPROPERTY(VisibleAnywhere, Category = "Bone Constraint (Experimental)", AdvancedDisplay,
		meta=(TitleProperty="{Bone1} - {Bone2}"))
	TArray<FModifyBoneConstraint> BoneConstraintsData;
	UPROPERTY()
	TArray<FModifyBoneConstraint> MergedBoneConstraints;

	/** 
	* 外力（重力など）
	* External forces (gravity, etc.)
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (PinHiddenByDefault, DisplayName="External Force"))
	FVector Gravity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bEnableWind = false;

	/** 
	* WindDirectionalSourceによる風の影響度。ClothやSpeedTreeとの併用目的
	* Influence of wind by WindDirectionalSource. For use with Cloth and SpeedTree
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce", meta = (EditCondition = "bEnableWind"),
		meta = (PinHiddenByDefault))
	float WindScale = 1.0f;

	/** 
	* 外力のプリセット。C++で独自のプリセットを追加可能(Instanced Struct)
	* External force presets. You can add your own presets in C++.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ExternalForce",
		meta = (BaseStruct = "/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce", ExcludeBaseStruct))
	TArray<FInstancedStruct> ExternalForces;

	/**
	* !!! VERY VERY EXPERIMENTAL !!!
	* 外力のプリセット。BP・C++で独自のプリセットを追加可能(Instanced Property)
	* 注意：AnimNodeをクリック or ABPをコンパイルしないと正常に動作しません
	* External force presets. You can add your own presets in BP or C++
	* Note: If you do not click on AnimNode or compile ABP, it will not work properly.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "ExternalForce",
		meta=(DisplayName="CustomExternalForces(EXPERIMENTAL)"))
	TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>> CustomExternalForces;

	/** 
	* レベル上の各コリジョンとの判定を行うフラグ。有効にすると物理処理の負荷が大幅に上がります
	* Flag for collision detection with each collision on the level. Enabling this will significantly increase the load of physics processing.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision", meta = (PinHiddenByDefault))
	bool bAllowWorldCollision = false;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideCollisionParams = false;
	/** 
	* SkeletalMeshComponentが持つコリジョン設定ではなく、独自のコリジョン設定をWorldCollisionで使用する際に設定
	* Use custom collision settings in WorldCollision instead of the collision settings set in SkeletalMeshComponent.
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideCollisionParams", DisplayName=
			"Override SkelComp Collision Params"))
	FBodyInstance CollisionChannelSettings;

	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視するフラグ
	* In WorldCollision, Flag to ignore collisions for SkeletalMeshComponent(PhysicsAsset) in WorldCollision
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bAllowWorldCollision"))
	bool bIgnoreSelfComponent = true;

	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視する設定（骨）
	* In WorldCollision, set to ignore collision (PhysicsAsset) of SkeletalMeshComponent using bone
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FBoneReference> IgnoreBones;
	/** 
	* WorldCollisionにて、SkeletalMeshComponentが持つコリジョン(PhysicsAsset)を無視する設定（骨名のプリフィックス）
	* In WorldCollision, set to ignore collision (PhysicsAsset) of SkeletalMeshComponent using bone name prefix
	*/
	UPROPERTY(EditAnywhere, Category = "World Collision", meta = (EditCondition = "!bIgnoreSelfComponent"))
	TArray<FName> IgnoreBoneNamePrefix;

	UPROPERTY(BlueprintReadWrite, Category = "Bones")
	TArray<FKawaiiPhysicsModifyBone> ModifyBones;

	UPROPERTY(BlueprintReadWrite)
	float DeltaTime;

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
	void SimulateModifyBones(FComponentSpacePoseContext& Output,
	                         const FTransform& ComponentTransform);
	void Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene, const FTransform& ComponentTransform,
	              const FVector& GravityCS, const float& Exponent, const USkeletalMeshComponent* SkelComp,
	              FComponentSpacePoseContext& Output);
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
