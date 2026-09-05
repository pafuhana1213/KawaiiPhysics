// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSharedCollisionTypes.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "KawaiiPhysicsSimpleWorldCollision.generated.h"

class UPhysicsAsset;
struct FKawaiiPhysicsSimpleWorldCollisionEntry;
struct FReferenceSkeleton;

/**
 * Simple World Collision で Convex コリジョンに使う形状。既定の Convex Hull は実形状の平面セットで当たります。宣言順は Merge 優先度（高精度が先）です。Landscape や Complex コリジョンのみのメッシュはもともと収集対象外です。
 * Shape used for convex collision in Simple World Collision. The default Convex Hull collides against the actual plane set. Declaration order is the Merge priority (more accurate first). Landscapes and complex-collision-only meshes are never gathered.
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldConvexFallbackShape : uint8
{
	/** Convex Hull の平面セットを使います（既定） / Use the convex hull plane set (default). */
	ConvexHull UMETA(DisplayName = "Convex Hull", ToolTip = "Convex Hull の平面セットを使います（既定） / Use the convex hull plane set (default)."),
	/** Convex の境界ボックスで代用します / Use the bounding box of the convex. */
	BoundingBox UMETA(DisplayName = "Bounding Box", ToolTip = "Convex の境界ボックスで代用します / Use the bounding box of the convex."),
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

/**
 * Simple World Collision の収集中心と半径を決める範囲。SkeletalMeshComponent はそのメッシュの Bounds、ActorFamily は同じ Entry に参加する全メッシュの Bounds の合成を使います。
 * Scope used to choose the Simple World Collision gather center and radius. SkeletalMeshComponent uses that mesh's Bounds; ActorFamily uses the combined Bounds of all meshes participating in the same Entry.
 */
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldGatherScope : uint8
{
	/** その SkeletalMeshComponent の Bounds を使います / Use the owning SkeletalMeshComponent Bounds. */
	SkeletalMeshComponent = 0 UMETA(DisplayName = "Skeletal Mesh Component"),
	/** 同じ Entry に参加する全メッシュの Bounds 合成を使います / Use the combined Bounds of all meshes participating in the same Entry. */
	ActorFamily = 1 UMETA(DisplayName = "Actor Family"),
};

struct FKawaiiPhysicsSimpleWorldCollisionSettings;

/**
 * シンプルワールドコリジョン収集設定
 * Simple-world collision gather settings
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionDesc
{
	float GatherIntervalSec = 0.2f;
	float GatherRadiusOverride = 0.0f;
	// Merge の出力: 全 Desc が GatherRadiusOverride を指定していれば true。ノード側は false のまま / Merge output: true when every Desc specifies GatherRadiusOverride. Node-built Descs leave it false
	bool bGatherRadiusAllOverridden = false;
	// ECC_MAX は所有 SkeletalMeshComponent の ObjectType を使う指定 / ECC_MAX means using the owning SkeletalMeshComponent's ObjectType.
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_MAX;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull;
	EKawaiiPhysicsSimpleWorldSkeletalMeshCollision SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None;
	bool bGroundCollision = true;
	EKawaiiPhysicsSimpleWorldGatherScope GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::SkeletalMeshComponent;
	bool bGatherFamilyMembers = false;
	bool bProviderDisabled = false;

	/**
	 * 複数ノードの収集設定を SkelComp 単位の1設定へマージする。
	 * Merge multiple node gather settings into one SkelComp-level setting.
	 *
	 * 規則 / Rules:
	 * - GatherIntervalSec は最小値。0以下は毎フレーム収集で最優先。
	 * - GatherIntervalSec uses the minimum value. <= 0 means gather every frame and has highest priority.
	 * - GatherRadiusOverride は Override 指定の最大値（指定なしは 0）。bGatherRadiusAllOverridden は全 Desc が Override 指定の時だけ true。
	 * - GatherRadiusOverride is the max of the specified overrides (0 when none). bGatherRadiusAllOverridden is true only when every Desc specifies an override.
	 * - CollisionChannel は ECC_MAX 以外（World Collision の Override 指定）を持つ Desc のうち、最も早く登録されたノードの値を採用。全て ECC_MAX なら ECC_MAX。
	 * - CollisionChannel uses the value of the earliest-registered Desc whose value is not ECC_MAX (World Collision override). If all values are ECC_MAX, it stays ECC_MAX.
	 * - ObjectTypes は union。空配列は WorldStatic + WorldDynamic の意味なので、空 Desc がある場合はそれらを明示的に union へ含める。
	 * - ObjectTypes are unioned. Empty means WorldStatic + WorldDynamic, so those are explicitly included when any Desc is empty.
	 * - ConvexFallbackShape は宣言順（高精度が先）の優先。
	 * - ConvexFallbackShape priority follows declaration order (more accurate first).
	 * - SkeletalMeshCollision は PhysicsAsset > BoundingBox > None の優先。
	 * - SkeletalMeshCollision priority is PhysicsAsset > BoundingBox > None.
	 * - bGroundCollision は OR。
	 * - bGroundCollision is OR.
	 * - GatherScope は ActorFamily 優先、bGatherFamilyMembers は OR、bProviderDisabled は AND。
	 * - GatherScope prefers ActorFamily, bGatherFamilyMembers uses OR, and bProviderDisabled uses AND.
	 */
	static FKawaiiPhysicsSimpleWorldCollisionDesc Merge(const TArray<FKawaiiPhysicsSimpleWorldCollisionDesc>& Descs);

	/**
	 * Desc の変更が収集結果（Overlap 対象・形状変換・地面・半径）に影響するか。GatherIntervalSec だけの変化は false。
	 * Whether a Desc change affects the gather result (overlap set, shape conversion, ground, radius). Interval-only changes return false.
	 */
	static bool DoesChangeRequireRegather(const FKawaiiPhysicsSimpleWorldCollisionDesc& Old, const FKawaiiPhysicsSimpleWorldCollisionDesc& New);

	bool operator==(const FKawaiiPhysicsSimpleWorldCollisionDesc& Other) const;
};

namespace KawaiiPhysicsSimpleWorldCollision
{
	/**
	 * 地面 Box の厚み半分（cm）。調整の必要性が薄いため定数
	 * Half thickness of the ground box in cm; intentionally a constant
	 */
	inline constexpr float GroundBoxHalfThickness = 10.0f;

	/**
	 * ObjectTypes を Block、それ以外を Ignore にした問い合わせ側レスポンスを作ります。空配列は WorldStatic + WorldDynamic を Block します。
	 * Builds querier response params that Block ObjectTypes and Ignore everything else. An empty array blocks WorldStatic + WorldDynamic.
	 */
	KAWAIIPHYSICS_API FCollisionResponseParams BuildSimpleWorldResponseParams(
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes);

	/**
	 * 収集中心と半径が物理クエリに渡せる値か判定します。NaN / 非有限 / KINDA_SMALL_NUMBER 以下の半径は false です。
	 * Returns whether the gather center and radius are safe for physics queries. NaN / non-finite values and radii at or below KINDA_SMALL_NUMBER return false.
	 */
	KAWAIIPHYSICS_API bool IsSimpleWorldGatherInputValid(const FVector& Center, float Radius);

	/**
	 * 上限が正で、かつ Overlap 数が上限を超えるときだけ距離順を使うかどうかを判定します。上限 0 は形状を収集しない設定なので並べ替えも省きます。
	 * Returns whether to use distance-order sorting: only when the cap is positive and the overlap count exceeds it. A cap of 0 means gathering no shapes, so sorting is skipped too.
	 */
	KAWAIIPHYSICS_API bool ShouldUseSimpleWorldGatherOrder(int32 NumOverlaps, int32 MaxGatheredComponents);

	/**
	 * 距離二乗の昇順で収集順インデックスを作ります。同距離は元の順序を保ちます。
	 * Builds gather-order indices sorted by ascending squared distance. Equal distances keep their original order.
	 */
	KAWAIIPHYSICS_API void SortSimpleWorldGatherOrderByDistance(
		TArrayView<const float> DistanceSquared,
		TArray<int32>& OutOrder);

	/**
	 * SimpleWorld Entry の provider が有効期間内に更新されているか判定します。
	 * Returns whether the SimpleWorld Entry provider has updated within the allowed age.
	 */
	KAWAIIPHYSICS_API bool IsSimpleWorldProviderAlive(
		const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		uint64 CurrentFrame,
		uint64 ProviderMaxAgeFrames);

	struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldBodyBinding
	{
		int32 BoneIndex = INDEX_NONE;
		int32 NumSphericalLimits = 0;
		int32 NumCapsuleLimits = 0;
		int32 NumTaperedCapsuleLimits = 0;
		int32 NumBoxLimits = 0;
		int32 NumConvexLimits = 0;
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
	 * Convex Elem の平面セットと頂点をコンポーネントローカル空間の Convex Limit へ変換して追記します。
	 * Converts a convex elem plane set and vertices into a component-local-space convex limit and appends it.
	 */
	KAWAIIPHYSICS_API bool AppendConvexElemLocalLimit(
		TArrayView<const FPlane> BodySpacePlanes,
		TArrayView<const FVector> ElemLocalVertices,
		TArrayView<const int32> Indices,
		const FTransform& ElemTM,
		const FVector& Scale3D,
		int32 MaxConvexPlanes,
		bool bBuildDebugGeometry,
		FKawaiiPhysicsSharedCollisionData& OutLocalLimits);

	/**
	 * AggGeom をコンポーネントローカル空間の Limit 配列へ変換します。Scale3D は適用済みで、OutLocalLimits へ追記します。
	 * Converts AggGeom into component-local-space limits with Scale3D applied, appending to OutLocalLimits.
	 */
	KAWAIIPHYSICS_API void ConvertAggGeomToLocalLimits(
		const FKAggregateGeom& AggGeom,
		const FVector& Scale3D,
		EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape,
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
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
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
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
	 * ワールド空間の地面 Box をコンポーネントローカル空間へ変換します。ComponentTM はスケール除去済みとして扱い、Extent は変えません。
	 * Converts a world-space ground box into component-local space. ComponentTM is treated as scale-stripped; Extent is unchanged.
	 */
	KAWAIIPHYSICS_API FBoxLimit MakeSimpleWorldGroundBoxLocal(const FBoxLimit& WorldBox, const FTransform& ComponentTM);

	/**
	 * コンポーネントローカル空間の地面 Box を ComponentTM でワールド空間へ戻します。Extent は変えません。
	 * Transforms a component-local ground box back into world space by ComponentTM. Extent is unchanged.
	 */
	KAWAIIPHYSICS_API FBoxLimit TransformSimpleWorldGroundBox(const FBoxLimit& LocalBox, const FTransform& ComponentTM);

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
		int32 MaxConvexPlanes,
		bool bBuildConvexDebugGeometry,
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

	/**
	 * メンバー Bounds 配列を SimpleWorld 収集用 Bounds に合成します。空なら false を返します。
	 * Combines member Bounds into gather Bounds for SimpleWorld. Returns false for an empty input.
	 */
	KAWAIIPHYSICS_API bool ComputeSimpleWorldGatherBounds(
		TArrayView<const FBoxSphereBounds> MemberBounds,
		FBoxSphereBounds& OutBounds);

	/**
	 * Shared Publisher の設定から Simple World Collision Desc を作ります。
	 * Builds a Simple World Collision Desc from Shared Publisher settings.
	 */
	KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionDesc BuildSimpleWorldCollisionDesc(
		const FKawaiiPhysicsSimpleWorldCollisionSettings& Settings);
}
