// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "KawaiiPhysicsTypes.generated.h"

UENUM(BlueprintType)
enum class EKawaiiPhysicsSimulationSpace : uint8
{
	/** Simulate in component space */
	ComponentSpace,
	/** Simulate in world space. This fixes the issues of root bones moving suddenly */
	WorldSpace,
	/** Simulate in another bone space */
	BaseBoneSpace,
};

/**
 * Enum representing the planar constraint axis in KawaiiPhysics.
 */
UENUM(meta=(ScriptName = "KP_PlanarConstraint"))
enum class EPlanarConstraint : uint8
{
	/** No planar constraint */
	None,
	/** Constrain to the X axis */
	X,
	/** Constrain to the Y axis */
	Y,
	/** Constrain to the Z axis */
	Z,
};

/**
 * 一時効果（一時外力・突風・物理設定倍率）の停止用ハンドル。Id=0 は未設定。ノード再初期化や上限超過破棄で対象が消えた後も値は残り、その場合の停止要求は何もしない
 * Handle used to stop transient effects (transient external forces, gusts, physics settings multipliers). Id=0 means unset. The value survives after the target is lost (node re-init or cap eviction); stop requests then do nothing.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsTransientHandle
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Id = 0;

	bool IsSet() const { return Id != 0; }
};

/**
 * 物理設定への一時的な倍率。全項目の既定値 1.0 は「変更なし」。ベースの設定値は書き換えず、毎フレーム再計算される各ボーンの実効値に乗算する
 * Temporary multipliers for physics settings. Every value defaults to 1.0 (no change). The base settings are never rewritten;
 * the multipliers are applied to each bone's effective values, which are recomputed every frame.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettingsMultiplier
{
	GENERATED_BODY()

	/**
	* Dampingへの倍率。1未満で減衰が弱まり、揺れが大きくなる
	* Multiplier for Damping. Below 1 the damping weakens and the sway grows.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float Damping = 1.0f;

	/**
	* Stiffnessへの倍率。1未満で元の形状への引き戻しが弱まり、揺れが大きくなる
	* Multiplier for Stiffness. Below 1 the pull back to the pre-physics shape weakens and the sway grows.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float Stiffness = 1.0f;

	/**
	* WorldDampingLocationへの倍率。実際の反映率は (1 - WorldDampingLocation) のため意味が反転し、
	* 1未満の倍率ではコンポーネントの移動量がより強く反映されて揺れが大きくなる
	* Multiplier for WorldDampingLocation. The semantics are inverted because the actual reflection factor is
	* (1 - WorldDampingLocation): a multiplier below 1 reflects more of the component movement and increases the sway.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float WorldDampingLocation = 1.0f;

	/**
	* WorldDampingRotationへの倍率。実際の反映率は (1 - WorldDampingRotation) のため意味が反転し、
	* 1未満の倍率ではコンポーネントの回転量がより強く反映されて揺れが大きくなる
	* Multiplier for WorldDampingRotation. The semantics are inverted because the actual reflection factor is
	* (1 - WorldDampingRotation): a multiplier below 1 reflects more of the component rotation and increases the sway.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float WorldDampingRotation = 1.0f;

	/**
	* コリジョン半径への倍率。0にするとワールドコリジョンのスイープと押し出しが実質無効になる。
	* 半径によるダミーボーンの本数はベースの半径から決まるため、1未満ではコリジョンの被覆に隙間が生じうる
	* Multiplier for the collision radius. At 0 the world sweep and push-out are effectively disabled.
	* The radius-based dummy bone count comes from the base radius, so below 1 the collision coverage can leave gaps.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float Radius = 1.0f;

	/**
	* LimitAngleへの倍率。ベースが0（無制限）のボーンは倍率に関わらず無制限のまま。ベースが0より大きいボーンは
	* 極小値で下限クランプされ、倍率0でも無制限へは反転せず、ほぼ完全にポーズへ追従する。Per Axis モードでは全軸の Min/Max に掛かり、倍率0で全軸ロックになる
	* Multiplier for LimitAngle. Bones whose base value is 0 (unlimited) stay unlimited whatever the multiplier is.
	* Bones with a base above 0 are clamped to a tiny positive minimum, so even a multiplier of 0 never flips them back to
	* unlimited; they follow the pose almost exactly instead. In Per Axis mode it scales Min/Max of every axis, and a multiplier of 0 locks them all.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float LimitAngle = 1.0f;
};

namespace KawaiiPhysics
{
	struct FWindGustEnvelope
	{
		float RiseTime = 0.f;
		float HoldTime = 0.f;
		float DecayTime = 0.f;
	};

	// Duration 合計実秒から台形エンベロープを解決する。Hold = max(0, Duration - Rise - Decay)。
	// Rise+Decay > Duration の場合は Rise/Decay を Duration/(Rise+Decay) で比例圧縮し合計を Duration に一致させる。Duration <= 0 は全て 0
	KAWAIIPHYSICS_API FWindGustEnvelope ResolveWindGustEnvelope(float Duration, float RiseTime, float DecayTime);

	// 台形エンベロープ（線形 rise → hold → 線形 decay）を 0..1 で評価する。総時間を超えた ElapsedTime は 0
	KAWAIIPHYSICS_API float EvaluateEnvelopeAlpha01(float RiseTime, float HoldTime, float DecayTime, float ElapsedTime);

	// GC追跡外ストレージへ持ち込めないlive UObject参照の検出用。
	KAWAIIPHYSICS_API bool StructInstanceHasLiveObjectReference(const UScriptStruct* Struct, const void* StructMemory);
}

/**
 * Enum representing the forward axis of a bone in KawaiiPhysics.
 */
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

/**
 * 追加のRootBone設定を表す構造体。
 * Structure representing the root bone settings for KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRootBoneSetting
{
	GENERATED_BODY()

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
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (EditCondition = "bUseOverrideExcludeBones"))
	TArray<FBoneReference> OverrideExcludeBones;
	/** OverrideExcludeBonesを使用するフラグ / Flag to use OverrideExcludeBones */
	UPROPERTY(EditAnywhere, Category = "Bones", meta = (InlineEditConditionToggle))
	bool bUseOverrideExcludeBones = false;
};


/**
 * 角度制限の形状。
 * Shape of the angle limit.
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsLimitAngleMode : uint8
{
	/** ポーズ方向を中心とする円錐で制限。LimitAngle を使用 / Limit with a cone around the pose direction. Uses LimitAngle */
	Cone,
	/** 親ボーンのローカル軸ごとに制限。PerAxisLimitAngle を使用 / Limit per local axis of the parent bone. Uses PerAxisLimitAngle */
	PerAxis UMETA(DisplayName = "Per Axis"),
};

/**
 * 1軸まわりの許容回転範囲。Min と Max をともに0にするとその軸をロック、±180で制限なし
 * Allowed rotation range around one axis. Setting both Min and Max to 0 locks the axis; ±180 leaves it free.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsAxisAngleRange
{
	GENERATED_BODY()

	/**
	* 負方向の許容角度
	* Allowed angle in the negative direction
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics",
		meta = (ClampMin = "-180", ClampMax = "0", UIMin = "-180", UIMax = "0", Units = "Degrees"))
	float Min = -30.0f;

	/**
	* 正方向の許容角度
	* Allowed angle in the positive direction
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics",
		meta = (ClampMin = "0", ClampMax = "180", UIMin = "0", UIMax = "180", Units = "Degrees"))
	float Max = 30.0f;
};

/**
 * 親ボーンのローカル軸ごとの角度制限。BoneForwardAxis と同じ軸はツイストにあたり、KawaiiPhysics はツイストを生成しないため無視される。
 * 有効な2軸の値は楕円状につながり、値が等しければ円錐と一致する
 * Angle limit per local axis of the parent bone. The axis matching BoneForwardAxis is the twist axis; KawaiiPhysics never produces twist, so it is ignored.
 * The two active axes form an elliptical boundary that matches a cone when their values are equal.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsPerAxisLimitAngle
{
	GENERATED_BODY()

	/**
	* X軸まわりの許容範囲
	* Allowed range around the X axis
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FKawaiiPhysicsAxisAngleRange X;

	/**
	* Y軸まわりの許容範囲
	* Allowed range around the Y axis
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FKawaiiPhysicsAxisAngleRange Y;

	/**
	* Z軸まわりの許容範囲
	* Allowed range around the Z axis
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FKawaiiPhysicsAxisAngleRange Z;
};

/**
 * 物理制御の設定を表す構造体。
 * Structure representing the settings for KawaiiPhysics.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettings
{
	GENERATED_BODY()

	/** 
	* 減衰度：揺れの強さを制御。値が小さいほど、加速度を物理挙動に反映
	* Damping physical behavior. As the value is smaller, the acceleration is more reflected to the physical behavior
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float Damping = 0.1f;

	/** 
	* 剛性度：値が大きいほど、元の形状を維持
	* Stiffness of physical behavior. As the value is larger, pre-physics shape is more respected
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float Stiffness = 0.05f;

	/**
	* ワールド座標系における Skeletal Mesh Component の移動量のダンピング(抑制)量。
	* 0 = 移動量をフル反映(揺れ最大) / 1 = コンポーネントに追従(反映なし)。実際の反映率 = (1 - WorldDampingLocation)
	* Damping (suppression) of the Skeletal Mesh Component's world-space movement.
	* 0 = movement fully reflected (max sway) / 1 = follows component (no reflection). Actual reflection factor = (1 - WorldDampingLocation).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float WorldDampingLocation = 0.8f;

	/**
	* ワールド座標系における Skeletal Mesh Component の回転量のダンピング(抑制)量。
	* 0 = 回転量をフル反映(揺れ最大) / 1 = コンポーネントに追従(反映なし)。実際の反映率 = (1 - WorldDampingRotation)
	* Damping (suppression) of the Skeletal Mesh Component's world-space rotation.
	* 0 = rotation fully reflected (max sway) / 1 = follows component (no reflection). Actual reflection factor = (1 - WorldDampingRotation).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float WorldDampingRotation = 0.8f;

	/** 
	* 各ボーンのコリジョン半径
	* Radius of bone's collision
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", DisplayName="Collision Radius", Units = "cm"),
		Category = "Kawaii Physics")
	float Radius = 3.0f;

	/**
	* 角度制限の形状。Per Axis ではヒンジのように特定の軸だけ曲げることが可能
	* Shape of the angle limit. Per Axis can restrict bending to specific axes, such as a hinge
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	EKawaiiPhysicsLimitAngleMode LimitAngleMode = EKawaiiPhysicsLimitAngleMode::Cone;

	/**
	* 物理挙動による回転制限。適切に設定することで荒ぶりを抑制。0で制限なし
	* Rotational limitations in physical behavior. Setting the value properly can suppress excessive shaking. 0 means no limit
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (ClampMin = "0", Units = "Degrees", EditCondition = "LimitAngleMode == EKawaiiPhysicsLimitAngleMode::Cone",
			EditConditionHides),
		Category = "Kawaii Physics")
	float LimitAngle = 0.0f;

	/**
	* 親ボーンのローカル軸ごとの回転制限。0はその方向をロックし、ポーズ方向を基準に判定する
	* Rotational limit per local axis of the parent bone. 0 locks that direction; angles are measured from the pose direction
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		meta = (EditCondition = "LimitAngleMode == EKawaiiPhysicsLimitAngleMode::PerAxis", EditConditionHides),
		Category = "Kawaii Physics")
	FKawaiiPhysicsPerAxisLimitAngle PerAxisLimitAngle;
};

/**
 * Structure representing a bone that can be modified by the KawaiiPhysics system.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsModifyBone
{
	GENERATED_USTRUCT_BODY()

	/** Reference to the bone */
	UPROPERTY()
	FBoneReference BoneRef;

	/** Index of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	int32 Index = -1;

	/** Index of the parent bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	int32 ParentIndex = -1;

	/** Indices of the child bones */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	TArray<int32> ChildIndices;

	/** Physics settings for the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FKawaiiPhysicsSettings PhysicsSettings;

	/** Current location of the bone */
	UPROPERTY(BlueprintReadWrite, Category = "Kawaii Physics|ModifyBone")
	FVector Location = FVector::ZeroVector;

	/** Previous location of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FVector PrevLocation = FVector::ZeroVector;

	/** Previous rotation of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FQuat PrevRotation = FQuat::Identity;

	/** Pose location of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FVector PoseLocation = FVector::ZeroVector;

	/** Pose rotation of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FQuat PoseRotation = FQuat::Identity;

	// ===== 固定サブステップのポーズ補間用（Transient, 非シリアライズ） =====
	// For fixed-substep pose interpolation (Transient, not serialized)
	/** 前フレームのポーズ目標位置（補間の始点） / Previous-frame pose target location (lerp start) */
	FVector PrevPoseLocation = FVector::ZeroVector;
	/** 前フレームのポーズ目標回転 / Previous-frame pose target rotation */
	FQuat PrevPoseRotation = FQuat::Identity;
	/** 現フレームのポーズ目標位置のスナップショット（補間の終点）。サブステップ中 PoseLocation を上書きするため退避 */
	/** Snapshot of this frame's pose target location (lerp end); stashed because PoseLocation is overwritten during substeps */
	FVector CurrentPoseLocation = FVector::ZeroVector;
	/** 現フレームのポーズ目標回転のスナップショット / Snapshot of this frame's pose target rotation */
	FQuat CurrentPoseRotation = FQuat::Identity;

	/** Pose scale of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	FVector PoseScale = FVector::OneVector;

	/** Length of the bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	float BoneLength = 0.0f;

	/** Length from the root bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	float LengthFromRoot = 0.0f;

	/** Length rate from the root bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	float LengthRateFromRoot = 0.0f;

	/** Flag indicating if this is a dummy bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	bool bDummy = false;

	/**
	* ボーン間ダミーボーンフラグ（2つの実ボーン間に挿入されたダミー）
	* Flag: this is an inter-bone dummy (inserted between two real bones)
	*/
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	bool bInterBoneDummy = false;

	/**
	* ボーン間ダミーの補間先（実子ボーン）のインデックス / Real child bone index for PoseLocation interpolation
	*/
	UPROPERTY()
	int32 InterBoneRealChildIndex = -1;

	/**
	* ボーン間ダミーの補間元（実親ボーン）のインデックス / Real parent bone index for PoseLocation interpolation
	*/
	UPROPERTY()
	int32 InterBoneRealParentIndex = -1;

	/**
	* ボーン間ダミーの補間アルファ（0.0=実親, 1.0=実子）/ Interpolation alpha between real parent and child
	*/
	UPROPERTY()
	float InterBoneAlpha = 0.0f;

	/**
	* 横方向BoneConstraint上に挿入されたコリジョン代理ダミー（縦階層に属さず、常にコリジョン専用）
	* Collision-proxy dummy inserted along a horizontal BoneConstraint (not in the vertical hierarchy; always collision-only).
	* 配置用に InterBoneRealParentIndex/InterBoneRealChildIndex/InterBoneAlpha を端点1/端点2/補間率として流用する。
	* Reuses InterBoneRealParentIndex/InterBoneRealChildIndex/InterBoneAlpha as endpoint1/endpoint2/lerp-alpha for placement.
	*/
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	bool bBridgeDummy = false;

	/** Flag indicating if simulation should be skipped for this bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|ModifyBone")
	bool bSkipSimulate = false;

	/**
	 * Checks if the bone has a parent.
	 *
	 * @return True if the bone has a parent, false otherwise.
	 */
	bool HasParent() const { return ParentIndex >= 0; }

	/** Default constructor */
	FKawaiiPhysicsModifyBone()
	{
	}
};
