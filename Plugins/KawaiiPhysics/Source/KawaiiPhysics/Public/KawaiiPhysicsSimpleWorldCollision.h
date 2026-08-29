// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSharedCollisionTypes.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "KawaiiPhysicsSimpleWorldCollision.generated.h"

/**
 * シンプルワールドコリジョンで未対応の複雑形状を近似する方法（Phase 1 では Convex に適用）
 * simple collision を持たないコンポーネント（Landscape / Complex のみのメッシュ）は収集対象外です。
 * How unsupported complex shapes are approximated for simple world collision (applied to Convex in Phase 1)
 * Components without simple collision (Landscape / complex-only meshes) are not gathered.
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsComplexShapeApproximation : uint8
{
	/** ボックス境界で近似します（既定） / Approximate with bounding box (default). */
	BoxBounds UMETA(ToolTip = "ボックス境界で近似します（既定） / Approximate with bounding box (default)."),
	/** 球境界で近似します / Approximate with bounding sphere. */
	SphereBounds UMETA(ToolTip = "球境界で近似します / Approximate with bounding sphere."),
	/** 形状を無視します / Skip the shape. */
	Ignore UMETA(ToolTip = "形状を無視します / Skip the shape."),
};

/**
 * シンプルワールドコリジョンで収集した SkeletalMeshComponent の扱い
 * How collected SkeletalMeshComponents are handled for simple world collision
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldSkeletalMeshMode : uint8
{
	/** 無視します（既定） / Skip skeletal meshes (default). */
	Ignore UMETA(ToolTip = "無視します（既定） / Skip skeletal meshes (default)."),
	/** Bounds から単一の Box として安価に近似します / Cheaply approximate Bounds as a single Box. */
	BoundsBox UMETA(ToolTip = "Bounds から単一の Box として安価に近似します / Cheaply approximate Bounds as a single Box."),
	/** PhysicsAsset の Body をボーン変換込みで正確に変換します（毎フレーム高コスト） / Convert PhysicsAsset bodies exactly, including per-bone transforms (expensive per frame). */
	PhysicsAsset UMETA(ToolTip = "PhysicsAsset の Body をボーン変換込みで正確に変換します（毎フレーム高コスト） / Convert PhysicsAsset bodies exactly, including per-bone transforms (expensive per frame)."),
};

namespace KawaiiPhysicsSimpleWorldCollision
{
	/**
	 * ワールドAABBをコンポーネントローカル空間の Limit 配列へ変換して追記します。Box はワールドで軸平行になるよう ComponentTM の逆回転を持ちます。
	 * Converts world AABB into component-local-space limits and appends them. Boxes keep ComponentTM inverse rotation so they remain axis-aligned in world space.
	 */
	KAWAIIPHYSICS_API void AppendBoundsLocalLimits(
		const FBoxSphereBounds& Bounds,
		const FTransform& ComponentTM,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits);

	/**
	 * AggGeom をコンポーネントローカル空間の Limit 配列へ変換します。Scale3D は適用済みで、OutLocalLimits へ追記します。
	 * Converts AggGeom into component-local-space limits with Scale3D applied, appending to OutLocalLimits.
	 */
	KAWAIIPHYSICS_API void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits);

	/**
	 * ローカル空間の Limit 配列を ComponentTM でワールド空間へ変換し、OutWorldLimits へ追記します。ComponentTM はスケール除去済みとして扱います。
	 * RadiusScale は Sphere/Capsule/TaperedCapsule の半径にのみ適用します。
	 * Transforms local-space limits by ComponentTM into world-space limits, appending to OutWorldLimits. ComponentTM is assumed to have scale stripped.
	 * RadiusScale is applied only to Sphere/Capsule/TaperedCapsule radii.
	 */
	KAWAIIPHYSICS_API void AppendLocalLimitsTransformed(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		const FTransform& ComponentTM,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float RadiusScale = 1.0f);

	/**
	 * フェード係数を適用したローカル空間の Limit 配列を ComponentTM でワールド空間へ変換し、OutWorldLimits へ追記します。
	 * Sphere/Capsule/TaperedCapsule は半径を FadeAlpha 倍に縮小し、Box は FadeAlpha が BoxEnableThreshold 未満の間は追記しません。
	 * Applies FadeAlpha to local-space limits (shrinking Sphere/Capsule/TaperedCapsule radii, withholding Boxes while
	 * FadeAlpha stays below BoxEnableThreshold) before transforming by ComponentTM into world space and appending to OutWorldLimits.
	 */
	KAWAIIPHYSICS_API void AppendFadedLocalLimits(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		float FadeAlpha,
		const FTransform& ComponentTM,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float BoxEnableThreshold = 0.5f);
}
