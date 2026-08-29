// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSharedCollisionTypes.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "KawaiiPhysicsSimpleWorldCollision.generated.h"

class UPhysicsAsset;
struct FReferenceSkeleton;

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
	/** PhysicsAsset の Body をボーン追従で変換します。上限は Max PhysicsAsset Bodies で、PhysicsAsset 無しは BoundsBox 相当、PhysicsAsset があって有効な body が無い場合は収集しません。ポーズは 1 フレーム遅れ得ます。アニメ由来のボーンスケールは形状サイズへ反映しません。 / Transform PhysicsAsset bodies by following bones. Limited by Max PhysicsAsset Bodies; no PhysicsAsset behaves like BoundsBox, and when a PhysicsAsset exists but has no valid body, it is not gathered. Pose data may be one frame late. Bone scale from animation is not applied to shape sizes. */
	PhysicsAsset UMETA(ToolTip = "PhysicsAsset の Body をボーン追従で変換します。上限は Max PhysicsAsset Bodies で、PhysicsAsset 無しは BoundsBox 相当、PhysicsAsset があって有効な body が無い場合は収集しません。ポーズは 1 フレーム遅れ得ます。アニメ由来のボーンスケールは形状サイズへ反映しません。 / Transform PhysicsAsset bodies by following bones. Limited by Max PhysicsAsset Bodies; no PhysicsAsset behaves like BoundsBox, and when a PhysicsAsset exists but has no valid body, it is not gathered. Pose data may be one frame late. Bone scale from animation is not applied to shape sizes."),
};

namespace KawaiiPhysicsSimpleWorldCollision
{
	struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldBodyBinding
	{
		int32 BoneIndex = INDEX_NONE;
		int32 NumSphericalLimits = 0;
		int32 NumCapsuleLimits = 0;
		int32 NumTaperedCapsuleLimits = 0;
		int32 NumBoxLimits = 0;
	};

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
	 * 1 body 分の AggGeom をボーンローカル Limit として追記し、各配列内の連続要素数を Binding に記録します。
	 * Binding は開始 offset を持たず先頭からの累積で解決するため、OutLocalLimits と OutBindings は同時に空の状態から使い始めてください。
	 * Appends one body's AggGeom as bone-local limits and records contiguous element counts in the binding.
	 * Bindings do not store start offsets and are resolved by accumulation from the beginning, so start using OutLocalLimits and OutBindings when both are empty.
	 */
	KAWAIIPHYSICS_API bool AppendBodyLocalLimits(
		const FKAggregateGeom& AggGeom,
		int32 BoneIndex,
		const FVector& Scale3D,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		int32 MaxBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding>& OutBindings);

	/**
	 * PhysicsAsset の Body を RefSkeleton で解決し、ボーン index 昇順で上限までボーンローカル Limit として追記します。
	 * Binding は開始 offset を持たず先頭からの累積で解決するため、OutLocalLimits と OutBindings は同時に空の状態から使い始めてください。
	 * Resolves PhysicsAsset bodies against the RefSkeleton and appends them as bone-local limits, sorted by bone index and capped by MaxBodies.
	 * Bindings do not store start offsets and are resolved by accumulation from the beginning, so start using OutLocalLimits and OutBindings when both are empty.
	 */
	KAWAIIPHYSICS_API int32 AppendPhysicsAssetLocalLimits(
		const UPhysicsAsset& PhysicsAsset,
		const FReferenceSkeleton& RefSkeleton,
		const FVector& Scale3D,
		EKawaiiPhysicsComplexShapeApproximation ApproxMode,
		int32 MaxBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding>& OutBindings);

	/**
	 * BodyBinding ごとの bone component-space transform と ComponentTM を合成し、スケールを除去した World Transform を更新します。
	 * Updates scale-stripped world transforms by composing each binding's bone component-space transform with ComponentTM.
	 */
	KAWAIIPHYSICS_API int32 UpdateSkeletalBodyWorldTransforms(
		TArrayView<const FKawaiiPhysicsSimpleWorldBodyBinding> Bindings,
		TArrayView<const FTransform> ComponentSpaceTransforms,
		const FTransform& ComponentTM,
		TArray<FTransform>& OutBodyWorldTMs);

	/**
	 * Binding ごとの LocalLimits スライスを BodyWorldTMs でワールド空間へ変換し、FadeAlpha を適用して追記します。
	 * Transforms each binding's LocalLimits slice by BodyWorldTMs, applies FadeAlpha, and appends into OutWorldLimits.
	 */
	KAWAIIPHYSICS_API void AppendFadedSkeletalLocalLimits(
		const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		TArrayView<const FKawaiiPhysicsSimpleWorldBodyBinding> Bindings,
		TArrayView<const FTransform> BodyWorldTMs,
		float FadeAlpha,
		FKawaiiPhysicsSharedCollisionData& OutWorldLimits,
		float BoxEnableThreshold = 0.5f);

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
