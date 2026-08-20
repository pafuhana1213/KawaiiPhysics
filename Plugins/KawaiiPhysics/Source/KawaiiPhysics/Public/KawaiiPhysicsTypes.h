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
 * 一時外力（Blow など）の停止用ハンドル。Id=0 は未設定。ノード再初期化や上限超過破棄で対象が消えた後も値は残り、その場合の停止要求は何もしない
 * Handle used to stop transient external forces (blows). Id=0 means unset. The value survives after the target is lost (node re-init or cap eviction); stop requests then do nothing.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsTransientForceHandle
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
struct KAWAIIPHYSICS_API FKawaiiPhysicsSettingsScale
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
	* 極小値で下限クランプされ、倍率0でも無制限へは反転せず、ほぼ完全にポーズへ追従する
	* Multiplier for LimitAngle. Bones whose base value is 0 (unlimited) stay unlimited whatever the multiplier is.
	* Bones with a base above 0 are clamped to a tiny positive minimum, so even a multiplier of 0 never flips them back to
	* unlimited; they follow the pose almost exactly instead.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float LimitAngle = 1.0f;
};

namespace KawaiiPhysics
{
	struct FWindBlowEnvelope
	{
		float RiseTime = 0.f;
		float HoldTime = 0.f;
		float DecayTime = 0.f;
	};

	// Duration 合計実秒から台形エンベロープを解決する。Hold = max(0, Duration - Rise - Decay)。
	// Rise+Decay > Duration の場合は Rise/Decay を Duration/(Rise+Decay) で比例圧縮し合計を Duration に一致させる。Duration <= 0 は全て 0
	KAWAIIPHYSICS_API FWindBlowEnvelope ResolveWindBlowEnvelope(float Duration, float RiseTime, float DecayTime);

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", DisplayName="Collision Radius"),
		Category = "Kawaii Physics")
	float Radius = 3.0f;

	/** 
	* 物理挙動による回転制限。適切に設定することで荒ぶりを抑制
	* Rotational limitations in physical behavior. Setting the value properly can suppress excessive shaking
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "Kawaii Physics")
	float LimitAngle = 0.0f;
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
