// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "KawaiiPhysicsCollisionLimits.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsTypes.h"

#include "KawaiiPhysicsRuntimeInfoTypes.generated.h"

/**
 * ランタイム情報でのダミーボーンの種別
 * Kind of dummy bone in the runtime info
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsDummyBoneType : uint8
{
	/** 実ボーン / Real bone */
	None,
	/** チェーン末端の先に足したダミー / Dummy added past the chain tip */
	Tip,
	/** 2 本の実ボーンの間に挿入したダミー / Dummy inserted between two real bones */
	InterBone,
	/** 横方向の BoneConstraint 上に挿入したコリジョン用ダミー / Collision dummy inserted along a horizontal bone constraint */
	Bridge,
};

/**
 * ランタイム情報での BoneConstraint の出どころ
 * Source of a bone constraint in the runtime info
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsConstraintSourceType : uint8
{
	/** ノードの BoneConstraints / The node's BoneConstraints */
	AnimNode,
	/** BoneConstraintsDataAsset */
	DataAsset,
	/** ダミーボーン間に自動で追加した拘束 / Constraint added automatically between dummy bones */
	AutoDummy,
};

/**
 * 直近の評価でシミュレーションに使われた 1 本のボーン。位置と寸法はコンポーネント空間
 * One bone used by the simulation in the latest evaluation. Locations and sizes are in component space.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRuntimeBoneInfo
{
	GENERATED_BODY()

	/** ボーン名。ダミーボーンは None / Bone name. None for dummy bones */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName BoneName;

	/** ダミーボーンの種別 / Kind of dummy bone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	EKawaiiPhysicsDummyBoneType DummyType = EKawaiiPhysicsDummyBoneType::None;

	/** Bones 配列での自分の要素番号 / Index of this bone in the Bones array */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 Index = INDEX_NONE;

	/** Bones 配列での親の要素番号。親が無ければ -1 / Index of the parent in the Bones array, or -1 when there is none */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 ParentIndex = INDEX_NONE;

	/** 衝突解決後の位置（cm） / Location after collision resolution (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector Location = FVector::ZeroVector;

	/** 物理適用前の入力ポーズでの位置（cm） / Location in the input pose before physics (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector PoseLocation = FVector::ZeroVector;

	/** 当たり半径（cm）。一時的な物理設定倍率を掛けた後の値 / Collision radius (cm), after temporary physics settings multipliers */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	float Radius = 0.0f;

	/** ルートからの長さの比率（0〜1） / Length rate from the root (0 to 1) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	float LengthRateFromRoot = 0.0f;

	/** シミュレーションと衝突処理から外れて入力ポーズに固定されているか（チェーンのルートなど） / Whether the bone is pinned to the input pose and skipped by simulation and collision (such as a chain root) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	bool bSkipSimulate = false;
};

/**
 * 直近の評価で使われた 1 つのコリジョン。位置と寸法はコンポーネント空間
 * One collision limit used in the latest evaluation. Locations and sizes are in component space.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRuntimeLimitInfo
{
	GENERATED_BODY()

	/** 形状 / Shape */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	ECollisionLimitType LimitType = ECollisionLimitType::None;

	/** 出どころ / Source */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	ECollisionSourceType SourceType = ECollisionSourceType::AnimNode;

	/** 元の配列の名前（例: SphericalLimits、SharedCapsuleLimits） / Name of the source array (e.g. SphericalLimits, SharedCapsuleLimits) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName SourceArrayName;

	/** 元の配列での要素番号 / Index in the source array */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 SourceIndex = INDEX_NONE;

	/** 追従するボーン。共有コリジョンとシンプルワールドコリジョンは None / Driving bone. None for shared and simple world collision */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName DrivingBone;

	/** 直近の評価で衝突処理に使われたか（bEnable が偽、または衝突処理がスキップする寸法・設定のものは偽） / Whether the collision step used the limit in the latest evaluation (false when bEnable is false or when its size or settings make the collision step skip it) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	bool bEnabled = false;

	/** 中心の位置（cm） / Center location (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector Location = FVector::ZeroVector;

	/** 回転 / Rotation */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FQuat Rotation = FQuat::Identity;

	/** カプセルの +Z 端（Radius0 側）。それ以外の形状は Location と同じ / Capsule +Z end (Radius0 side). Same as Location for other shapes */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector Start = FVector::ZeroVector;

	/** カプセルの -Z 端（Radius1 側）。それ以外の形状は Location と同じ / Capsule -Z end (Radius1 side). Same as Location for other shapes */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector End = FVector::ZeroVector;

	/** 球・カプセルの半径、テーパードカプセルの Start 側の半径（cm） / Sphere or capsule radius, or the Start-side radius of a tapered capsule (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	float Radius0 = 0.0f;

	/** テーパードカプセルの End 側の半径（cm）。球・カプセルは Radius0 と同じ / End-side radius of a tapered capsule (cm). Same as Radius0 for spheres and capsules */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	float Radius1 = 0.0f;

	/** ボックスの半分の大きさ（cm） / Half size of the box (cm) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector Extent = FVector::ZeroVector;

	/** 平面の法線。ボーンはこの向きの側へ押し出される / Plane normal. Bones are pushed out to this side */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FVector PlaneNormal = FVector::ZeroVector;

	/** 球の内側に閉じ込めるか（Inner） / Whether the sphere keeps bones inside (Inner) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	bool bInnerSphere = false;
};

/**
 * 直近の評価で使われた 1 つの BoneConstraint
 * One bone constraint used in the latest evaluation
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRuntimeConstraintInfo
{
	GENERATED_BODY()

	/** 1 本目の Bones 配列での要素番号 / Index of the first bone in the Bones array */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 BoneIndex1 = INDEX_NONE;

	/** 2 本目の Bones 配列での要素番号 / Index of the second bone in the Bones array */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 BoneIndex2 = INDEX_NONE;

	/** 1 本目のボーン名。ダミーボーンは None / Name of the first bone. None for dummy bones */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName BoneName1;

	/** 2 本目のボーン名。ダミーボーンは None / Name of the second bone. None for dummy bones */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName BoneName2;

	/** 出どころ / Source */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	EKawaiiPhysicsConstraintSourceType SourceType = EKawaiiPhysicsConstraintSourceType::AnimNode;
};

/**
 * 1 つの KawaiiPhysics ノードのランタイム情報。調整ツール向けの診断用
 * Runtime info of one KawaiiPhysics node, for diagnostics in tuning tools
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsRuntimeNodeInfo
{
	GENERATED_BODY()

	/** ノードを持つ AnimInstance のクラス名 / Class name of the AnimInstance that owns the node */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName AnimInstanceClassName;

	/** AnimInstance のクラス内でのノード番号 / Node index within the AnimInstance class */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 NodeIndex = INDEX_NONE;

	/** RootBone */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FName RootBone;

	/** ノードのタグ / Tag of the node */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FGameplayTag Tag;

	/** シミュレーション空間 / Simulation space */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	EKawaiiPhysicsSimulationSpace SimulationSpace = EKawaiiPhysicsSimulationSpace::ComponentSpace;

	/** 一度でも評価されたか。偽なら Bones と Limits の位置は意味を持たない / Whether the node has been evaluated. When false, locations in Bones and Limits are meaningless */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	bool bEvaluated = false;

	/** シミュレーション空間からの変換が非一様スケールを含むか。真なら半径と寸法は近似 / Whether the transform from simulation space has non-uniform scale. When true, radii and sizes are approximations */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	bool bNonUniformScale = false;

	/** シミュレーション空間からコンポーネント空間への変換 / Transform from simulation space to component space */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	FTransform SimulationToComponent = FTransform::Identity;

	/** シミュレーションされたボーン（ダミーを含む） / Simulated bones, including dummies */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	TArray<FKawaiiPhysicsRuntimeBoneInfo> Bones;

	/** コリジョン（Convex を除く） / Collision limits, excluding convex */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	TArray<FKawaiiPhysicsRuntimeLimitInfo> Limits;

	/** 統合後の BoneConstraint / Merged bone constraints */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	TArray<FKawaiiPhysicsRuntimeConstraintInfo> Constraints;

	/** シンプルワールドコリジョンの Convex の件数（形状は Limits に含めない） / Number of simple world convex limits (their shapes are not in Limits) */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	int32 NumConvexLimits = 0;

	/** Convex の代替形状の設定 / Convex fallback shape setting */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics")
	EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull;
};
