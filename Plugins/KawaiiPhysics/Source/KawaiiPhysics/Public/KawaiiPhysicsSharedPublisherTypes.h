// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/Optional.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include <atomic>

#include "KawaiiPhysicsSharedPublisherTypes.generated.h"

/**
 * Shared Publisher が持つ Simple World Collision の収集設定。
 * Simple World Collision gather settings owned by a Shared Publisher.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionSettings
{
	GENERATED_BODY()

	/**
	 * GameThreadで定期収集したレベル上のsimple collisionをトレースなしで共有 publish します。World Collisionより大幅に低負荷ですが、薄い壁・高速移動では World Collision(sweep) の併用を推奨します。
	 * Publishes level simple collision gathered periodically on the GameThread without traces. Much cheaper than World Collision, but using it with World Collision (sweep) is recommended for thin walls or fast movement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/**
	 * Simple World Collision の収集中心と半径を決める範囲。SkeletalMeshComponent はそのメッシュの Bounds、ActorFamily は同じ Entry に参加する全メッシュの Bounds の合成を使います。
	 * Scope used to choose the Simple World Collision gather center and radius. SkeletalMeshComponent uses that mesh's Bounds; ActorFamily uses the combined Bounds of all meshes participating in the same Entry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision", meta = (EditCondition = "bEnabled", DisplayName = "Gather Scope"))
	EKawaiiPhysicsSimpleWorldGatherScope GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;

	/**
	 * シンプルワールドコリジョンの収集間隔（秒）。0の場合は毎フレーム収集します。収集済みコンポーネントの位置更新は毎フレーム行われます。
	 * Gather interval for Simple World Collision in seconds. 0 gathers every frame. Already gathered component transforms are updated every frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", ClampMin = "0.0", ClampMax = "10.0",
		DisplayName = "Gather Interval", Units = "s"))
	float GatherInterval = 0.2f;

	/**
	 * シンプルワールドコリジョンが反応するオブジェクトタイプ。空の場合は WorldStatic + WorldDynamic を使用します。収集されるのは、これらのタイプに属し、かつ所有 SkeletalMeshComponent の ObjectType（Override SkelComp Collision Params 有効時はその ObjectType）を Block するコンポーネントだけです。Overlap / Ignore 応答（トリガー等）は収集しません。bGatherFamilyMembers を使うときは Pawn を含める必要があります。
	 * Object types used by Simple World Collision. Empty means WorldStatic + WorldDynamic. Only components of these types that Block the owning SkeletalMeshComponent's ObjectType (or the ObjectType from Override SkelComp Collision Params when enabled) are gathered. Overlap / Ignore responses (triggers, etc.) are never gathered. Include Pawn when using bGatherFamilyMembers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", DisplayName = "Object Types"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;

	/**
	 * Simple World Collision で Convex コリジョンに使う形状。既定の Convex Hull は実形状の平面セットをそのまま使い、ハル情報を取得できない場合や平面数が Max Convex Planes を超える場合は Bounding Box で代用します。
	 * Shape used for convex collision in Simple World Collision. The default Convex Hull uses the actual plane set, falling back to the bounding box when hull data is unavailable or the plane count exceeds Max Convex Planes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", DisplayName = "Convex Shape"))
	EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape =
		EKawaiiPhysicsSimpleWorldConvexFallbackShape::ConvexHull;

	/**
	 * シンプルワールドコリジョンの収集半径を上書きする。
	 * Overrides the Simple World Collision gather radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideGatherRadius = false;

	/**
	 * シンプルワールドコリジョンの収集半径のオーバーライド。無効時は SkeletalMesh の Bounds から自動算出します。
	 * Override for the Simple World Collision gather radius. When disabled, it is calculated automatically from SkeletalMesh bounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideGatherRadius", ClampMin = "0",
		DisplayName = "Gather Radius", Units = "cm"))
	float GatherRadius = 200.0f;

	/**
	 * 所有 Actor の地面情報（IKawaiiPhysicsGroundProvider → CharacterMovementComponent の CurrentFloor）があればそれを使い、無ければ下方向トレース 1 本で地面を求め、薄い Box コリジョンとして扱います。Landscape や Complex コリジョンのみの床でも有効です。
	 * Uses the owner's ground info when available (IKawaiiPhysicsGroundProvider, then CharacterMovementComponent CurrentFloor); otherwise fires one downward trace. The ground is treated as a thin box collision. Works on Landscape and complex-collision-only floors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", DisplayName = "Ground Collision"))
	bool bGroundCollision = true;

	/**
	 * Simple World Collision で収集した周囲の SkeletalMeshComponent との当たり方。None / Bounding Box / Physics Asset から選びます。
	 * How Simple World Collision collides with gathered SkeletalMeshComponents: None, Bounding Box, or Physics Asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", DisplayName = "Skeletal Mesh Collision"))
	EKawaiiPhysicsSimpleWorldSkeletalMeshCollision SkeletalMeshCollision =
		EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None;

	/**
	 * Simple World Collision の Collision Channel を上書きする。
	 * Overrides the Simple World Collision collision channel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision", meta = (PinHiddenByDefault, InlineEditConditionToggle))
	bool bOverrideCollisionChannel = false;

	/**
	 * Simple World Collision の問い合わせ側 Collision Channel。上書きが無効な場合は所有 SkeletalMeshComponent の ObjectType を使います。
	 * Query-side collision channel for Simple World Collision. When the override is disabled, the owning SkeletalMeshComponent ObjectType is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bOverrideCollisionChannel", DisplayName = "Collision Channel"))
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Pawn;

	/**
	 * 同じ Entry に参加する他のメッシュ（ファミリー内メンバー）も Skeletal Mesh Collision のモードで収集し、各ノードは自分以外のメンバーの形状と衝突します。Object Types に Pawn を含める必要があります。
	 * Also gathers other meshes participating in the same Entry (family members) with the Skeletal Mesh Collision mode, so each node collides with member shapes except its own. Object Types must include Pawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shared Publisher|Simple World Collision",
		meta = (PinHiddenByDefault, EditCondition = "bEnabled", DisplayName = "Gather Family Members"))
	bool bGatherFamilyMembers = false;
};

/**
 * Shared Publisher が共有する風の状態。
 * Wind state shared by a Shared Publisher.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedWindState
{
	bool bPublisherWindEnabled = false;
	FKawaiiProceduralWindDynamicParams Params;
	float Time = 0.0f;
	float PublisherTimeScale = 1.0f;
	FKawaiiProceduralWindActiveGust ActiveGust;
};

/**
 * Shared Publisher が 1 フレームに publish する全状態。
 * Complete state published by a Shared Publisher for one frame.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedPublisherState
{
	// Publisher の実効 Enabled（bEnabled UPROPERTY と BP 上書きの結果）
	// Effective publisher Enabled (UPROPERTY plus Blueprint override)
	bool bPublisherEnabled = false;
	bool bSimpleWorldEnabled = false;
	EKawaiiPhysicsSimpleWorldGatherScope GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;
	FKawaiiPhysicsSimpleWorldCollisionDesc SimpleWorldDesc;
	FKawaiiPhysicsSimpleWorldCollisionSettings SimpleWorldSettings;
	FKawaiiPhysicsSharedWindState Wind;
};

/**
 * Shared Publisher の突風開始 / 停止リクエスト。
 * Gust start / stop request for a Shared Publisher.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedPublisherGustRequest
{
	bool bStop = false;
	float Strength = 0.0f;
	float RiseTime = 0.0f;
	float DecayTime = 0.0f;
	float HoldTime = 0.0f;
	float BlendOutTime = 0.0f;
};

/**
 * Shared Publisher が GameplayTag ごとに publish する状態 Entry。
 * State entry published by a Shared Publisher for a GameplayTag.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedPublisherEntry
{
	struct FPendingPublisherRequests
	{
		TOptional<bool> Enabled;
		TOptional<FKawaiiPhysicsSimpleWorldCollisionSettings> SimpleWorldSettings;
	};

	/**
	 * 1 フレーム分の状態を publish する。期限切れ（MarkExpired 済み）の Entry では何も書き換えずに false を返す。
	 * provider ID 0 は拒否する。
	 * Publishes one frame of state. Returns false without touching an entry that has already been expired (MarkExpired).
	 * A provider ID of 0 is rejected.
	 */
	bool PublishState(const FKawaiiPhysicsSharedPublisherState& InState, uint64 InProviderID, uint64 CurrentFrame,
	                  uint64 ProviderMaxAgeFrames);
	uint64 ReadState(FKawaiiPhysicsSharedPublisherState& Out) const;
	uint64 ReadWindState(FKawaiiPhysicsSharedWindState& Out) const;
	uint64 GetPublishSerial() const;
	uint64 GetProviderID() const;
	uint64 GetLastPublishFrame() const;
	bool IsExpired(uint64 CurrentFrame, uint64 MaxAgeFrames) const;
	/**
	 * MarkExpired 済みか（フレーム経過による期限切れは含まない）。
	 * Whether MarkExpired has been called (frame-age expiry is not included).
	 */
	bool IsMarkedExpired() const;
	void MarkExpired();

	void RequestGust(float Strength, float RiseTime, float DecayTime, float HoldTime);
	void RequestGustStop(float BlendOutTime);
	void ConsumePendingGustRequests(TArray<FKawaiiPhysicsSharedPublisherGustRequest>& Out);

	void RequestPublisherEnabled(bool bEnabled);
	void RequestSimpleWorldSettings(const FKawaiiPhysicsSimpleWorldCollisionSettings& Settings);
	bool ConsumePendingPublisherRequests(FPendingPublisherRequests& Out);

private:
	FKawaiiPhysicsSharedPublisherState State;
	mutable FRWLock StateLock;
	std::atomic<uint64> PublishSerial{0};
	std::atomic<uint64> LastPublishFrame{0};
	uint64 ProviderID = 0;
	bool bExpired = false;

	FCriticalSection GustMutex;
	TArray<FKawaiiPhysicsSharedPublisherGustRequest> PendingGusts;
	TOptional<bool> PendingPublisherEnabled;
	TOptional<FKawaiiPhysicsSimpleWorldCollisionSettings> PendingSimpleWorldSettings;
};
