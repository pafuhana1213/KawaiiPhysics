// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"

#include <atomic>

#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "KawaiiPhysicsSharedCollisionTypes.h"

#include "KawaiiPhysicsSharedCollisionSubsystem.generated.h"

class AActor;
class UPrimitiveComponent;
class UPhysicsAsset;
class USkeletalMeshComponent;
class USkinnedAsset;
class UCharacterMovementComponent;

// 地面ソースの種類（DebugDraw の色分けにも使う） / Ground source kind (also used for debug draw colors)
UENUM(BlueprintType)
enum class EKawaiiPhysicsSimpleWorldGroundSource : uint8
{
	None UMETA(DisplayName = "None"),
	Provider UMETA(DisplayName = "Provider"),
	CharacterMovement UMETA(DisplayName = "Character Movement"),
	Trace UMETA(DisplayName = "Trace"),
};

/**
 * Source1つ分の共有コリジョンスロット
 * Shared collision slot for a single source
 *
 * BufferはBufferLockで保護する。 / All access to Buffer must hold BufferLock.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionSourceSlot
{
	/**
	 * ワーカースレッドから呼び出し可能 / Can be called from any thread.
	 * InOutDataとBufferをSwapする。呼び出し側は受け取った旧Buffer(=InOutData)を次フレームの一時バッファとして再利用でき、
	 * 書き込みロック区間内のディープコピーと毎フレームのメモリ確保を避けられる。
	 * Swaps InOutData with Buffer. The caller can reuse the returned old buffer as next frame's scratch,
	 * avoiding a deep copy inside the write-lock critical section and per-frame allocation.
	 */
	void Publish(FKawaiiPhysicsSharedCollisionData& InOutData);

	/** ワーカースレッドから呼び出し可能 / Can be called from any thread */
	void AppendTo(FKawaiiPhysicsSharedCollisionData& OutData) const;

	/** スロットが古くなっているか判定 / Check if this slot has not been published to recently */
	bool IsExpired(uint64 CurrentFrame, uint64 MaxAge) const;

	/** スロットを即座に期限切れ化 / Mark this slot as immediately expired */
	void MarkExpired();

private:
	FKawaiiPhysicsSharedCollisionData Buffer;

	/** 最終Publishフレーム番号（鮮度チェック用） / Last published frame number for expiration detection */
	std::atomic<uint64> LastPublishFrame{0};

	/** バッファ内容の読み書きを保護。 / Protects buffer contents. */
	mutable FRWLock BufferLock;
};

/**
 * (ActorFamilyRoot, Tag) 単位のエントリ。複数Sourceのスロットを保持
 * Entry per (ActorFamilyRoot, Tag) pair. Holds slots for multiple sources.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionEntry
{
	/**
	 * Source用: 自分専用スロットを取得/作成（SlotsLockでスレッドセーフ。任意スレッドから呼べる）
	 * For sources: Get or create a dedicated slot (thread-safe via SlotsLock; callable from any thread)
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot> GetOrCreateSlot(uint64 SourceID);

	/**
	 * Target用: 全スロットのコリジョンをマージして読み取り
	 * For targets: Read merged collision data from all source slots
	 */
	void ReadMerged(FKawaiiPhysicsSharedCollisionData& OutData) const;

	/**
	 * 期限切れスロットを除去（書き込みロック内で実行）
	 * Remove expired slots under write lock
	 */
	void RemoveExpiredSlots(uint64 CurrentFrame, uint64 MaxAge);

	/** スロット数を取得（読み取りロック内） / Get slot count under read lock */
	int32 GetSlotCount() const;

	/** スロットが空か判定（読み取りロック内） / Check if empty under read lock */
	bool IsEmpty() const;

private:
	/** SourceID（AnimNodeアドレス等）→ 専用スロット / Source ID -> dedicated slot */
	TMap<uint64, TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>> Slots;

	/** TMap構造変更とイテレーションの競合を防ぐロック / Lock to protect TMap structural changes vs iteration */
	mutable FRWLock SlotsLock;
};

/**
 * シンプルワールドコリジョン収集設定
 * Simple-world collision gather settings
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionDesc
{
	float GatherIntervalSec = 0.2f;
	float GatherRadiusOverride = 0.0f;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	EKawaiiPhysicsSimpleWorldConvexFallbackShape ConvexFallbackShape = EKawaiiPhysicsSimpleWorldConvexFallbackShape::BoundingBox;
	EKawaiiPhysicsSimpleWorldSkeletalMeshCollision SkeletalMeshCollision = EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None;
	bool bGroundCollision = true;

	/**
	 * 複数ノードの収集設定を SkelComp 単位の1設定へマージする。
	 * Merge multiple node gather settings into one SkelComp-level setting.
	 *
	 * 規則 / Rules:
	 * - GatherIntervalSec は最小値。0以下は毎フレーム収集で最優先。
	 * - GatherIntervalSec uses the minimum value. <= 0 means gather every frame and has highest priority.
	 * - GatherRadiusOverride は「全 Desc が Override 指定」の場合だけ最大値。1つでも未指定なら 0 のままにし、Tick 側で自動半径を使う。
	 * - GatherRadiusOverride is the max only when every Desc specifies an override. If any Desc is automatic, it stays 0 and Tick uses the automatic radius.
	 * - ObjectTypes は union。空配列は WorldStatic + WorldDynamic の意味なので、空 Desc がある場合はそれらを明示的に union へ含める。
	 * - ObjectTypes are unioned. Empty means WorldStatic + WorldDynamic, so those are explicitly included when any Desc is empty.
	 * - ConvexFallbackShape は BoundingBox > BoundingSphere > None の優先。
	 * - ConvexFallbackShape priority is BoundingBox > BoundingSphere > None.
	 * - SkeletalMeshCollision は PhysicsAsset > BoundingBox > None の優先。
	 * - SkeletalMeshCollision priority is PhysicsAsset > BoundingBox > None.
	 * - bGroundCollision は OR。
	 * - bGroundCollision is OR.
	 */
	static FKawaiiPhysicsSimpleWorldCollisionDesc Merge(const TArray<FKawaiiPhysicsSimpleWorldCollisionDesc>& Descs);

	bool operator==(const FKawaiiPhysicsSimpleWorldCollisionDesc& Other) const;
};

/**
 * シンプルワールドコリジョン収集Entry（SkelComp単位。複数SourceのDescをマージして1回収集）
 * Simple-world collision gather entry (per SkelComp. Merges multiple source Descs and gathers once)
 *
 * スレッド境界 / Thread boundary:
 * - Worker: SetDesc / RemoveDesc / MarkRead / RequestRegather / Slot.AppendTo
 * - GameThread Tick: RemoveExpiredDescs / BuildMergedDesc / world query, GatheredComponents更新, Slot.Publish
 * ISM / HISM はインスタンス単位で収集し、MaxGatheredComponents はインスタンス数に対して効きます。
 * ISM / HISM are gathered per instance, and MaxGatheredComponents applies to instance count.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionEntry
{
	void SetDesc(uint64 SourceID, const FKawaiiPhysicsSimpleWorldCollisionDesc& InDesc);
	void RemoveDesc(uint64 SourceID);

	bool MarkRead(uint64 SourceID);
	void RemoveExpiredDescs(uint64 CurrentFrame, uint64 MaxAge);
	bool HasAnyDesc() const;
	bool BuildMergedDesc(FKawaiiPhysicsSimpleWorldCollisionDesc& OutMerged) const;
	int32 GetNumDescs() const;

	void RequestRegather();
	bool ConsumeRegatherRequested();

	FKawaiiPhysicsSharedCollisionSourceSlot Slot;

	// Tick スレッド専有・ロック不要 / Tick-thread only; no lock required.
	float TimeSinceLastGather = FLT_MAX;
	// 直近の収集で使った実効半径 / Effective radius used by the latest gather
	float LastGatherRadius = 0.0f;

	struct FGatheredComponent
	{
		TWeakObjectPtr<const UPrimitiveComponent> Component;
		/**
		 * PhysicsAsset 厳密モード用の SkeletalMeshComponent キャッシュ。未設定なら通常/Bounds 収集。
		 * Cached SkeletalMeshComponent for exact PhysicsAsset mode. Unset for regular/bounds gathering.
		 */
		TWeakObjectPtr<const USkeletalMeshComponent> SkeletalComponent;
		/**
		 * 収集時の SkinnedAsset。差し替え検知に使う。
		 * SkinnedAsset at gather time, used for replacement detection.
		 */
		TWeakObjectPtr<const USkinnedAsset> GatheredSkinnedAsset;
		/**
		 * 収集時の PhysicsAsset。Override を含む差し替え検知に使う。
		 * PhysicsAsset at gather time, including overrides, used for replacement detection.
		 */
		TWeakObjectPtr<const UPhysicsAsset> GatheredPhysicsAsset;
		/**
		 * 収集時に Limit へ焼き込んだスケール。ISM はインスタンススケール、PhysicsAsset モードは bone-local に焼き込んだスケール。
		 * Scale baked into limits at gather time. Instance scale for ISM; the baked bone-local scale for PhysicsAsset mode.
		 */
		FVector GatheredScale3D = FVector::OneVector;
		/**
		 * ISM/HISM のインスタンス index。非 ISM は INDEX_NONE
		 * ISM/HISM instance index. INDEX_NONE for non-ISM.
		 */
		int32 InstanceIndex = INDEX_NONE;
		bool bStatic = false;
		float FadeAlpha = 1.0f;
		FTransform LastComponentTM;
		FKawaiiPhysicsSharedCollisionData LocalLimits;
		/**
		 * PhysicsAsset 厳密モードの body binding。空なら通常コンポーネント扱い。
		 * Body bindings for exact PhysicsAsset mode. Empty means regular component handling.
		 */
		TArray<KawaiiPhysicsSimpleWorldCollision::FKawaiiPhysicsSimpleWorldBodyBinding> BodyBindings;
		/**
		 * 前回 publish 対象にした body world transform。デバッグ描画にも使う。
		 * Last body world transforms used for publish. Also used for debug drawing.
		 */
		TArray<FTransform> LastBodyWorldTMs;
		/**
		 * 毎フレーム body world transform 更新用の一時配列。
		 * Scratch array for per-frame body world transform updates.
		 */
		TArray<FTransform> BodyWorldTMScratch;
	};

	// Tick スレッド専有・ロック不要 / Tick-thread only; no lock required.
	TArray<FGatheredComponent> GatheredComponents;
	bool bHasGatheredOnce = false;
	bool bHasGroundBox = false;
	FBoxLimit GroundBox;
	// 収集フレームごとにアタッチ連鎖から解決した、利用可能な最上位ソース（Provider > CharacterMovement > Trace）
	// Highest-priority source available, resolved by walking the attach chain every gather frame (Provider > CharacterMovement > Trace)
	EKawaiiPhysicsSimpleWorldGroundSource GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
	// 現在の GroundBox を作ったソース（DebugDraw の色分け用。GroundSource は選択可能な最上位ソース） / Source that produced the current GroundBox (for debug draw colors; GroundSource is the highest-priority source available)
	EKawaiiPhysicsSimpleWorldGroundSource GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;
	// 収集フレームでアタッチ連鎖から見つかった Provider。GroundCharacterMovement とは独立にキャッシュする / Provider found while walking the attach chain during the gather frame; cached independently of GroundCharacterMovement
	TWeakObjectPtr<UObject> GroundProvider;
	// 収集フレームでアタッチ連鎖から見つかった CharacterMovementComponent。Provider が bHit=false を返した場合のフォールバック先 / CharacterMovementComponent found while walking the attach chain during the gather frame; used as the fallback when Provider returns bHit=false
	TWeakObjectPtr<UCharacterMovementComponent> GroundCharacterMovement;
	TWeakObjectPtr<const UPrimitiveComponent> GroundComponent;
	FKawaiiPhysicsSharedCollisionData PublishScratch;
	TArray<FOverlapResult> OverlapScratch;
	bool bWorldLimitsDirty = true;

private:
	struct FDescSlot
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		uint64 LastReadFrame = 0;
	};

	TMap<uint64, FDescSlot> DescSlots;
	mutable FRWLock DescLock;
	std::atomic<bool> bRegatherRequested{false};
};

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionDebugInfo
{
	GENERATED_BODY()

	// SkelComp に対応する SimpleWorld Entry が存在したか / Whether a SimpleWorld entry exists for SkelComp
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bHasEntry = false;

	// Entry に登録されている Desc 数（ノード数） / Number of descs registered to the entry (node count)
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumDescs = 0;

	// GatheredComponents.Num() / GatheredComponents.Num()
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumGatheredComponents = 0;

	// bStatic == true の件数 / Number of entries with bStatic == true
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumStaticComponents = 0;

	// bStatic == false の件数 / Number of entries with bStatic == false
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumMovableComponents = 0;

	// 全 GatheredComponents の BodyBindings.Num() 合計 / Sum of BodyBindings.Num() across all GatheredComponents
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumSkeletalBodies = 0;

	// 収集順のコンポーネント名 / Component names in gathered order
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	TArray<FString> GatheredComponentNames;

	// 収集コンポーネントの FadeAlpha の最小値（0 件なら 1.0） / Minimum FadeAlpha of gathered components (1.0 when empty)
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	float MinFadeAlpha = 1.0f;

	// Entry.bHasGroundBox / Entry.bHasGroundBox
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bHasGroundBox = false;

	// Entry.GroundSource / Entry.GroundSource
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	EKawaiiPhysicsSimpleWorldGroundSource GroundSource = EKawaiiPhysicsSimpleWorldGroundSource::None;

	// Entry.GroundBoxSource / Entry.GroundBoxSource
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	EKawaiiPhysicsSimpleWorldGroundSource GroundBoxSource = EKawaiiPhysicsSimpleWorldGroundSource::None;

	// Entry.GroundBox.Location（bHasGroundBox 時のみ意味あり） / Entry.GroundBox.Location (meaningful only when bHasGroundBox)
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	FVector GroundBoxLocation = FVector::ZeroVector;

	// Entry.GroundBox.Rotation.Rotator() / Entry.GroundBox.Rotation.Rotator()
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	FRotator GroundBoxRotation = FRotator::ZeroRotator;

	// Entry.GroundBox.Extent / Entry.GroundBox.Extent
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	FVector GroundBoxExtent = FVector::ZeroVector;

	// 直近の収集で使った実効半径 / Effective radius used by the latest gather
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	float GatherRadius = 0.0f;

	// Entry.TimeSinceLastGather（FLT_MAX のときは -1.0） / Entry.TimeSinceLastGather (-1.0 when FLT_MAX)
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	float TimeSinceLastGather = 0.0f;

	// Entry.bHasGatheredOnce / Entry.bHasGatheredOnce
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bHasGatheredOnce = false;
};

/**
 * KawaiiPhysics AnimNode間でコリジョンデータを共有するためのWorldSubsystem
 * WorldSubsystem for sharing collision data between KawaiiPhysics AnimNodes in an attached actor family
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsSharedCollisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Actorのアタッチ階層を遡ってファミリーrootを求める。アタッチポインタを辿るだけのread-only処理で、
	 * UObjectの変更やGCに触れないため任意スレッドから呼べる（並列eval中はアタッチが不変である前提）。
	 * Resolve the actor-family root by walking the attach hierarchy. Read-only pointer chase (no UObject mutation/GC),
	 * callable from any thread (assumes attachment is stable during parallel evaluation).
	 */
	static AActor* GetFamilyRoot(AActor* Actor);

	/**
	 * Source用: Actorのファミリーrootのエントリを検索、なければ作成（RegistryLockでスレッドセーフ。任意スレッドから呼べる）
	 * For sources: Find or create an entry for the actor family root. Thread-safe via RegistryLock; callable from any thread.
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindOrCreateEntry(AActor* Actor, const FGameplayTag& Tag);

	/**
	 * SimpleWorld Source用: SkelComp単位のEntryを検索、なければ作成し、作成と初回Desc登録を同一SimpleWorldRegistryLock write lock内で行う。
	 * cleanupのHasAnyDesc()==false除去と割り込まない（任意スレッドから呼べる。SkelCompをdereferenceしない）。
	 * For SimpleWorld sources: Find or create a SkelComp-level entry and register the initial Desc under the same
	 * SimpleWorldRegistryLock write lock, preventing cleanup's HasAnyDesc()==false removal from interleaving
	 * (callable from any thread; does not dereference SkelComp).
	 */
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> FindOrCreateSimpleWorldEntry(
		TWeakObjectPtr<const USkeletalMeshComponent> SkelComp, uint64 SourceID,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& InitialDesc);

	/**
	 * Target用: Actorのファミリーrootのエントリを検索（RegistryLockでスレッドセーフ。任意スレッドから呼べる）
	 * For targets: Find an entry for the actor family root. Thread-safe via RegistryLock; callable from any thread.
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntry(AActor* Actor, const FGameplayTag& Tag) const;

	/**
	 * Entry の Tick 専有データから診断情報を詰める（GameThread 専用。テストから直接呼べるよう static）
	 * Fill diagnostics from an entry's tick-owned data (GameThread only; static so tests can call it directly)
	 */
	static void FillSimpleWorldCollisionDebugInfo(const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	                                              FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo);

	/**
	 * SkelComp の SimpleWorld Entry を検索し診断情報を返す。Entry が無ければ false（OutInfo は既定値＋bHasEntry=false）。
	 * GameThread 専用。Shipping では常に false。
	 * Look up the SimpleWorld entry for SkelComp and return diagnostics. Returns false when no entry exists
	 * (OutInfo is reset with bHasEntry=false). GameThread only. Always false in Shipping builds.
	 */
	bool BuildSimpleWorldCollisionDebugInfo(const USkeletalMeshComponent* SkelComp,
	                                        FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo) const;

	// USubsystem interface
	virtual void Deinitialize() override;

	// FTickableGameObject interface (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	// RegistryはWorkerスレッドからも変更されるため、空判定も各RegistryLockで保護する（.cppで定義）
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }

private:
	/** レジストリのキー型: (ActorFamilyRoot, Tag) / Registry key type */
	using FRegistryKey = TPair<TWeakObjectPtr<AActor>, FGameplayTag>;

	/**
	 * Actor/Tagからレジストリキーを構築する（GetFamilyRootでファミリーroot解決込み）。
	 * Actor/Tagが無効、またはファミリーrootが取れない場合は false。FindOrCreateEntry/FindEntryの共通前処理。
	 * Build the registry key from Actor/Tag (resolving the family root). Returns false if invalid. Shared by FindOrCreateEntry/FindEntry.
	 */
	static bool TryResolveRegistryKey(AActor* Actor, const FGameplayTag& Tag, FRegistryKey& OutKey);

	void TickSimpleWorldCollision(float DeltaTime);

	/**
	 * 構築済みキーで Entry を読み取りロック検索する（死んだActorのEntryはスキップ）。
	 * Read-locked lookup of an entry by its already-resolved key (skips entries whose family-root actor has died).
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntryByKey(const FRegistryKey& Key) const;

	/** レジストリ: (ActorFamilyRoot, Tag) → Entry / Registry: (ActorFamilyRoot, Tag) -> Entry */
	TMap<FRegistryKey, TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>> Registry;

	/** Registryの構造変更とイテレーションの競合を防ぐロック（Worker初期化とGameThread Tickの両方が触る）
	 *  Lock protecting Registry structural changes vs iteration (touched by both worker-thread init and GameThread Tick).
	 *  ロック順序は Registry → Slots に統一する（Tickは本ロック保持中にEntryのSlotsLockを取る）。デッドロック回避のため逆順は禁止。
	 *  Lock order is always Registry -> Slots (Tick holds this while taking an Entry's SlotsLock); never the reverse. */
	mutable FRWLock RegistryLock;

	/** SimpleWorldレジストリ: SkelComp → Entry / SimpleWorld registry: SkelComp -> Entry
	 *  ロック順序は SimpleWorldRegistryLock → Entry内ロック。既存RegistryLock/SlotsLockとは同時取得しない。
	 *  Lock order is SimpleWorldRegistryLock -> entry-internal locks. Do not hold it with RegistryLock/SlotsLock. */
	TMap<TWeakObjectPtr<const USkeletalMeshComponent>, TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>> SimpleWorldRegistry;
	mutable FRWLock SimpleWorldRegistryLock;

	/** クリーンアップ間隔制御 / Cleanup interval control */
	float CleanupAccumulator = 0.0f;
};
