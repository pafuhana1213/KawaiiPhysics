// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSharedCollisionTypes.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "KawaiiPhysicsSimpleWorldCollision.generated.h"

class UPhysicsAsset;
struct FReferenceSkeleton;

/**
 * Simple World Collision で Convex コリジョンの代わりに使う形状。Convex は Sphere / Capsule / Box の Limit へ直接変換できないため、境界形状で代用するか無視するかを選びます。Landscape や Complex コリジョンのみのメッシュはもともと収集対象外です。
 * Shape used in place of convex collision in Simple World Collision. Convex collision cannot be converted directly into Sphere / Capsule / Box limits, so choose a bounding shape as a substitute or skip it. Landscapes and complex-collision-only meshes are never gathered.
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldConvexFallbackShape : uint8
{
	/** Convex の境界ボックスで代用します（既定） / Use the bounding box of the convex (default). */
	BoundingBox UMETA(DisplayName = "Bounding Box", ToolTip = "Convex の境界ボックスで代用します（既定） / Use the bounding box of the convex (default)."),
	/** Convex の境界球で代用します / Use the bounding sphere of the convex. */
	BoundingSphere UMETA(DisplayName = "Bounding Sphere", ToolTip = "Convex の境界球で代用します / Use the bounding sphere of the convex."),
	/** Convex を無視します / Skip convex collision. */
	None UMETA(DisplayName = "None", ToolTip = "Convex を無視します / Skip convex collision."),
};

/**
 * Simple World Collision で収集した周囲の SkeletalMeshComponent との当たり方
 * How Simple World Collision collides with gathered SkeletalMeshComponents
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldSkeletalMeshCollision : uint8
{
	/** 当たりを作りません（既定） / No collision (default). */
	None UMETA(DisplayName = "None", ToolTip = "当たりを作りません（既定） / No collision (default)."),
	/** Bounds から単一の Box として安価に近似します / Cheaply approximate Bounds as a single Box. */
	BoundingBox UMETA(DisplayName = "Bounding Box", ToolTip = "Bounds から単一の Box として安価に近似します / Cheaply approximate Bounds as a single Box."),
	/** PhysicsAsset の Body をボーン追従で変換します。上限は Max PhysicsAsset Bodies で、PhysicsAsset 無しは Bounding Box 相当、PhysicsAsset があって有効な body が無い場合は収集しません。ポーズは 1 フレーム遅れ得ます。アニメ由来のボーンスケールは形状サイズへ反映しません。 / Transform PhysicsAsset bodies by following bones. Limited by Max PhysicsAsset Bodies; no PhysicsAsset behaves like Bounding Box, and when a PhysicsAsset exists but has no valid body, it is not gathered. Pose data may be one frame late. Bone scale from animation is not applied to shape sizes. */
	PhysicsAsset UMETA(DisplayName = "Physics Asset", ToolTip = "PhysicsAsset の Body をボーン追従で変換します。上限は Max PhysicsAsset Bodies で、PhysicsAsset 無しは Bounding Box 相当、PhysicsAsset があって有効な body が無い場合は収集しません。ポーズは 1 フレーム遅れ得ます。アニメ由来のボーンスケールは形状サイズへ反映しません。 / Transform PhysicsAsset bodies by following bones. Limited by Max PhysicsAsset Bodies; no PhysicsAsset behaves like Bounding Box, and when a PhysicsAsset exists but has no valid body, it is not gathered. Pose data may be one frame late. Bone scale from animation is not applied to shape sizes."),
};

namespace KawaiiPhysicsSimpleWorldCollision
{
	/**
	 * 地面 Box の厚み半分（cm）。調整の必要性が薄いため定数
	 * Half thickness of the ground box in cm; intentionally a constant
	 */
	inline constexpr float GroundBoxHalfThickness = 10.0f;

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
		EKawaiiPhysicsSimpleWorldConvexFallbackShape BoundsShape,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits);

	/**
	 * AggGeom をコンポーネントローカル空間の Limit 配列へ変換します。Scale3D は適用済みで、OutLocalLimits へ追記します。
	 * Converts AggGeom into component-local-space limits with Scale3D applied, appending to OutLocalLimits.
	 */
	KAWAIIPHYSICS_API void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
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
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
		int32 MaxBodies,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits,
		TArray<FKawaiiPhysicsSimpleWorldBodyBinding>& OutBindings);

	/**
	 * 接地点と法線から有界の薄い地面 Box を作る。入力が不正（NaN / 非有限半径）なら false を返し OutBox を変更しない。
	 * ゼロ法線は上向き、負の半径は 0 として扱う。
	 * Builds a bounded thin ground box from an impact point and normal. Returns false and leaves OutBox untouched on invalid input (NaN / non-finite radius).
	 * A zero normal is treated as up, and a negative radius as 0.
	 */
	KAWAIIPHYSICS_API bool BuildSimpleWorldGroundBox(
		const FVector& ImpactPoint,
		const FVector& ImpactNormal,
		float Radius,
		FBoxLimit& OutBox);

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
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
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
