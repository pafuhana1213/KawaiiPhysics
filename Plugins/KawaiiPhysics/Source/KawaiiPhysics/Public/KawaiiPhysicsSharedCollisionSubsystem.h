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
class UWorld;
struct FKawaiiPhysicsSharedPublisherEntry;

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

	/**
	 * Publish ごとに 1 増える単調カウンタ。読み手は AppendTo の**前**に読んで前回値と比較し、一致ならコピーを省略できる（後に読むと Publish が割り込んだとき新しい serial を古いデータに紐付けて更新を取りこぼす）
	 * Monotonic counter incremented per Publish. Readers read it **before** AppendTo and skip the copy when unchanged (reading after could pair a newer serial with older data).
	 */
	uint64 GetPublishSerial() const { return PublishSerial.load(std::memory_order_acquire); }

private:
	FKawaiiPhysicsSharedCollisionData Buffer;

	/** Publish ごとに増える単調カウンタ / Monotonic counter incremented per Publish */
	std::atomic<uint64> PublishSerial{0};

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
 * SimpleWorld Registry のキー。Local は SkelComp + 無効 Tag、Shared はファミリー root Actor + Tag を使う。
 * Key for the SimpleWorld registry. Local uses SkelComp + invalid Tag; Shared uses family-root Actor + Tag.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldRegistryKey
{
	TWeakObjectPtr<const UObject> KeyObject;
	FGameplayTag Tag;

	/**
	 * Worker から呼ばれる FindOrCreateSimpleWorldEntry / FindSimpleWorldEntry の前段判定に使うため、
	 * UObject をデリファレンスしないスレッドセーフ判定（bThreadsafeTest=true）で有効性を見る。
	 * Uses the thread-safe test (bThreadsafeTest=true) so worker-thread callers of
	 * FindOrCreateSimpleWorldEntry / FindSimpleWorldEntry never dereference the UObject.
	 */
	bool IsValid() const { return KeyObject.IsValid(false, true); }

	bool operator==(const FKawaiiPhysicsSimpleWorldRegistryKey& Other) const
	{
		return KeyObject == Other.KeyObject && Tag == Other.Tag;
	}

	friend uint32 GetTypeHash(const FKawaiiPhysicsSimpleWorldRegistryKey& Key)
	{
		return HashCombine(GetTypeHash(Key.KeyObject), GetTypeHash(Key.Tag));
	}

	/**
	 * GameThread 用（DebugInfo / テスト）。生ポインタをそのままキーへ格納する。
	 * For GameThread use (debug info / tests). Stores the raw pointer into the key.
	 */
	static FKawaiiPhysicsSimpleWorldRegistryKey MakeLocalKey(const USkeletalMeshComponent* SkelComp);
	/**
	 * Worker から呼べる弱参照版。弱参照をデリファレンスせずそのままキーへ格納する。
	 * Weak-pointer variant callable from worker threads. Stores the weak pointer without dereferencing it.
	 */
	static FKawaiiPhysicsSimpleWorldRegistryKey MakeLocalKey(const TWeakObjectPtr<const USkeletalMeshComponent>& SkelComp);
	static FKawaiiPhysicsSimpleWorldRegistryKey MakeSharedKey(const AActor* FamilyRoot, const FGameplayTag& Tag);
};

/**
 * シンプルワールドコリジョン収集Entry。provider Desc をマージして1回収集し、reader は同じ Entry を読む。
 * Simple-world collision gather entry. Merges provider Descs and gathers once; readers consume the same Entry.
 *
 * スレッド境界 / Thread boundary:
 * - Worker: SetDesc / RemoveDesc / MarkRead / AddReaderMember / RemoveReaderMember / MarkReaderRead / RequestRegather / AppendFamilyMemberLimits / Slot.AppendTo / GroundSlot.AppendTo
 * - GameThread Tick: RemoveExpiredDescs / BuildMergedDesc / world query, GatheredComponents更新, Slot.Publish / GroundSlot.Publish
 * Worker から呼ぶ API は SkelComp 弱参照をデリファレンスしない（IsValid(false, true) と弱参照同士の比較だけを使う）。
 * GetPrimarySkelComp / CollectMemberSkelComps は生ポインタを取り出すため GameThread（Tick）専用。
 * Worker-callable APIs never dereference the SkelComp weak pointers (they only use IsValid(false, true) and weak-pointer comparison).
 * GetPrimarySkelComp / CollectMemberSkelComps resolve raw pointers and are therefore GameThread (Tick) only.
 * ISM / HISM はインスタンス単位で収集し、MaxGatheredComponents はインスタンス数に対して効きます。
 * ISM / HISM are gathered per instance, and MaxGatheredComponents applies to instance count.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSimpleWorldCollisionEntry
{
	friend class UKawaiiPhysicsSharedCollisionSubsystem;

	void SetDesc(uint64 SourceID, const FKawaiiPhysicsSimpleWorldCollisionDesc& InDesc, uint64 CurrentFrame,
	             const TWeakObjectPtr<const USkeletalMeshComponent>& SkelComp, bool bProvider = true);
	void SetDesc(uint64 SourceID, const FKawaiiPhysicsSimpleWorldCollisionDesc& InDesc);
	void RemoveDesc(uint64 SourceID);

	bool MarkRead(uint64 SourceID);
	// provider の heartbeat。CurrentFrame を明示する版（Publisher など呼び出し側がフレームを持つ場合）/ Provider heartbeat with an explicit frame (for callers such as the Publisher that already hold the frame)
	bool MarkRead(uint64 SourceID, uint64 CurrentFrame);
	void AddReaderMember(uint64 SourceID, const TWeakObjectPtr<const USkeletalMeshComponent>& SkelComp,
	                     uint64 CurrentFrame);
	void RemoveReaderMember(uint64 SourceID);
	bool MarkReaderRead(uint64 SourceID, uint64 CurrentFrame, uint64 ProviderMaxAgeFrames);
	void RemoveExpiredDescs(uint64 CurrentFrame, uint64 MaxAge);
	bool HasAnyDesc() const;
	bool HasProviderDesc() const;
	bool HasAnyReader() const;
	bool IsProviderDisabled() const;
	bool BuildMergedDesc(FKawaiiPhysicsSimpleWorldCollisionDesc& OutMerged) const;
	int32 GetNumDescs() const;
	int32 GetNumReaders() const;
	/** GameThread（Tick）専用。弱参照から生ポインタを解決する / GameThread (Tick) only. Resolves raw pointers from weak pointers. */
	void CollectMemberSkelComps(TArray<TWeakObjectPtr<const USkeletalMeshComponent>>& Out) const;
	/** GameThread（Tick）専用。弱参照から生ポインタを解決する / GameThread (Tick) only. Resolves a raw pointer from a weak pointer. */
	const USkeletalMeshComponent* GetPrimarySkelComp() const;
	uint64 GetLastProviderFrame() const;
	/**
	 * OwnSkelComp 以外のファミリーメンバー Slot を OutData へ追記する。GameThread API は呼ばない。
	 * 自己除外は弱参照同士の比較で行い、OwnSkelComp をデリファレンスしない。
	 * Appends family-member slots other than OwnSkelComp into OutData. Does not call GameThread-only APIs.
	 * Self-exclusion compares weak pointers, so OwnSkelComp is never dereferenced.
	 */
	void AppendFamilyMemberLimits(const TWeakObjectPtr<const USkeletalMeshComponent>& OwnSkelComp,
	                              FKawaiiPhysicsSharedCollisionData& OutData) const;
	/** 全ファミリーメンバー Slot の PublishSerial 合計を返す / Returns the sum of PublishSerial for all family-member slots */
	uint64 GetMemberSlotsPublishSerialSum() const;
	/** ファミリーメンバー Slot 数を返す / Returns the number of family-member slots */
	int32 GetNumMemberSlots() const;
	/**
	 * 指定されたメンバー以外のファミリーメンバー Slot を削除する。空配列なら全削除。
	 * Removes family-member slots except the specified members. An empty array removes all slots.
	 */
	void RemoveMemberSlotsNotIn(const TArray<TWeakObjectPtr<const USkeletalMeshComponent>>& MembersToKeep);

	void RequestRegather();
	bool ConsumeRegatherRequested();

	FKawaiiPhysicsSharedCollisionSourceSlot Slot;
	// 地面 Box 専用 Slot。0 または 1 個の FBoxLimit を BoxLimits に入れて Publish する / Dedicated ground-box slot. Publishes zero or one FBoxLimit in BoxLimits.
	FKawaiiPhysicsSharedCollisionSourceSlot GroundSlot;
	TMap<TWeakObjectPtr<const USkeletalMeshComponent>, TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>> MemberSlots;

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
		 * ファミリーメンバー SkelComp の形状なら設定する。通常コンポーネントは未設定。
		 * Set for shapes that belong to a family-member SkelComp. Unset for regular components.
		 */
		TWeakObjectPtr<const USkeletalMeshComponent> MemberSkelComp;
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
	// Trace ソースの地面 Box を床コンポーネントのローカル空間（スケール除去済み）で保持。Movable な床への追従に使う
	// Trace-source ground box in the floor component's scale-stripped local space; used to follow Movable floors
	FBoxLimit GroundBoxLocal;
	// 直近に GroundBox を作った床コンポーネント（ISM はインスタンス）のスケール除去済み Transform
	// Scale-stripped transform of the floor component (instance for ISM) that produced the current GroundBox
	FTransform GroundComponentTM = FTransform::Identity;
	// ISM/HISM 床のインスタンス index。非 ISM は INDEX_NONE / ISM/HISM floor instance index; INDEX_NONE otherwise
	int32 GroundInstanceIndex = INDEX_NONE;
	// 床コンポーネントが Static なら true（Box を使い回す）。Movable なら毎 Tick Transform を追従する
	// True when the floor component is Static (box is reused); Movable floors are followed every tick
	bool bGroundComponentStatic = true;
	// Tick スレッド専有。地面 Slot の Publish が必要か / Tick-thread only. Whether the ground slot needs publishing
	bool bGroundBoxDirty = true;
	FKawaiiPhysicsSharedCollisionData PublishScratch;
	// Tick スレッド専有。地面 Slot の Publish 用スクラッチ / Tick-thread only. Scratch buffer for publishing the ground slot
	FKawaiiPhysicsSharedCollisionData GroundPublishScratch;
	// Tick スレッド専有。ファミリーメンバー Slot の Publish 用スクラッチ / Tick-thread only. Scratch buffers for publishing family-member slots
	TMap<TWeakObjectPtr<const USkeletalMeshComponent>, FKawaiiPhysicsSharedCollisionData> MemberPublishScratch;
	FKawaiiPhysicsSharedCollisionData EmptyMemberPublishScratch;
	TArray<FOverlapResult> OverlapScratch;
	// Tick スレッド専有。収集上限超過時の距離順インデックス / Tick-thread only. Distance-sorted indices used only when gather results exceed the cap
	TArray<int32> GatherOrderScratch;
	// Tick スレッド専有。収集上限超過時の距離二乗スクラッチ / Tick-thread only. Squared-distance scratch used only when gather results exceed the cap
	TArray<float> GatherDistanceScratch;
	// Tick スレッド専有。ActorFamily 収集用メンバー scratch / Tick-thread only. Member scratch for ActorFamily gathering
	TArray<TWeakObjectPtr<const USkeletalMeshComponent>> MemberSkelCompScratch;
	TSet<TWeakObjectPtr<const USkeletalMeshComponent>> MemberSkelCompSetScratch;
	TSet<TWeakObjectPtr<const AActor>> MemberOwnerScratch;
	TArray<FBoxSphereBounds> MemberBoundsScratch;
	bool bWorldLimitsDirty = true;

private:
	struct FDescSlot
	{
		FKawaiiPhysicsSimpleWorldCollisionDesc Desc;
		TWeakObjectPtr<const USkeletalMeshComponent> SkelComp;
		uint64 LastReadFrame = 0;
		bool bProvider = true;
		// 登録順（Merge の順序依存規則を決定的にする） / Registration order that makes order-dependent merge rules deterministic
		uint64 RegistrationOrdinal = 0;
	};

	TMap<uint64, FDescSlot> DescSlots;
	mutable FRWLock DescLock;
	// 次に登録する Desc へ割り当てる登録順。DescLock 内でのみ触る / Registration order for the next Desc. Touched only under DescLock
	uint64 NextDescRegistrationOrdinal = 1;
	uint64 LastProviderFrame = 0;
	std::atomic<bool> bRegatherRequested{false};

	void RemoveMemberSlotsNotInLocked(const TArray<TWeakObjectPtr<const USkeletalMeshComponent>>& MembersToKeep);
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

	// Entry.bGroundComponentStatic（Trace ソースの床が Static なら true） / Entry.bGroundComponentStatic (true when the trace-source floor is Static)
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bGroundComponentStatic = true;

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

	// 収集スコープ / Gather scope
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	EKawaiiPhysicsSimpleWorldGatherScope GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::SkeletalMeshComponent;

	// Registry キーの Tag。Local は空 / Registry key Tag. Empty for local entries
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	FGameplayTag GroupTag;

	// Registry キーの KeyObject 名。Local は SkelComp 名 / Registry key KeyObject name. Local entries use the SkelComp name
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	FString KeyObjectName;

	// Entry に登録されている reader 数 / Number of readers registered to the entry
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumReaders = 0;

	// マージ済み provider disabled 状態 / Merged provider disabled state
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bProviderDisabled = false;

	// ファミリーメンバー形状を収集するか / Whether family-member shapes are gathered
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	bool bGatherFamilyMembers = false;

	// ファミリーメンバー Slot 数 / Number of family-member slots
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Simple World Collision")
	int32 NumMemberSlots = 0;
};

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedPublisherDebugInfo
{
	GENERATED_BODY()

	// Shared Publisher Entry が存在したか / Whether a Shared Publisher entry exists
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bFound = false;

	// Provider Entry が存在し期限切れではないか / Whether the provider entry exists and has not expired
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bProviderAlive = false;

	// Shared Publisher の Group Tag / Shared Publisher group tag
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	FGameplayTag GroupTag;

	// Actor ファミリー root の名前 / Actor family root name
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	FString FamilyRootName;

	// Publish ごとの単調カウンタ / Monotonic counter incremented on every publish
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	int64 PublishSerial = 0;

	// 最後に publish されたフレーム / Last published frame
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	int64 LastPublishFrame = 0;

	// Provider の実効 Enabled / Provider effective Enabled
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bEnabled = false;

	// Simple World Collision が有効か / Whether Simple World Collision is enabled
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bSimpleWorldEnabled = false;

	// Simple World Collision の収集スコープ / Simple World Collision gather scope
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	EKawaiiPhysicsSimpleWorldGatherScope GatherScope = EKawaiiPhysicsSimpleWorldGatherScope::ActorFamily;

	// ファミリーメンバー形状を収集するか / Whether family-member shapes are gathered
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bGatherFamilyMembers = false;

	// SimpleWorld Entry の reader 数 / Number of readers in the SimpleWorld entry
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	int32 NumReaders = 0;

	// SimpleWorld Entry の収集済みコンポーネント数 / Number of gathered components in the SimpleWorld entry
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	int32 NumGatheredComponents = 0;

	// ファミリーメンバー Slot 数 / Number of family-member slots
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	int32 NumMemberSlots = 0;

	// 共有 Wind が有効か / Whether shared Wind is enabled
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	bool bWindEnabled = false;

	// 共有 Wind の Time / Shared Wind time
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	float WindTime = 0.0f;

	// 共有 Wind の TimeScale / Shared Wind time scale
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Shared Publisher")
	float WindTimeScale = 1.0f;
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
	 * SimpleWorld用: 構築済みキーの Entry を検索、なければ作成する。provider は Desc を登録し、reader は member として登録する。
	 * 任意スレッドから呼べる（Key / SkelComp をdereferenceしない）。
	 * For SimpleWorld: Find or create an entry by an already-built key. Providers register a Desc; readers register membership.
	 * Callable from any thread (does not dereference Key / SkelComp).
	 */
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> FindOrCreateSimpleWorldEntry(
		const FKawaiiPhysicsSimpleWorldRegistryKey& Key, uint64 SourceID,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& InitialDesc,
		const TWeakObjectPtr<const USkeletalMeshComponent>& SkelComp,
		bool bProvider);

	/**
	 * SimpleWorld用: 構築済みキーの Entry を検索する。Entry が無ければ null。
	 * For SimpleWorld: Find an entry by an already-built key. Returns null when no entry exists.
	 */
	TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry> FindSimpleWorldEntry(
		const FKawaiiPhysicsSimpleWorldRegistryKey& Key) const;

	/**
	 * Target用: Actorのファミリーrootのエントリを検索（RegistryLockでスレッドセーフ。任意スレッドから呼べる）
	 * For targets: Find an entry for the actor family root. Thread-safe via RegistryLock; callable from any thread.
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntry(AActor* Actor, const FGameplayTag& Tag) const;

	/**
	 * Shared Publisher用: Actorのファミリーrootの Entry を検索、なければ作成する。
	 * 既存 Entry が MarkExpired 済みの場合は Tick の Cleanup を待たずに新しい Entry へ置き換えるため、返る Entry は常に publish 可能。
	 * For Shared Publisher: Find or create an entry for the actor family root.
	 * An existing entry that has been marked expired is replaced with a fresh one without waiting for the Tick cleanup,
	 * so the returned entry can always accept a publish.
	 */
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> FindOrCreateSharedPublisherEntry(
		AActor* Actor,
		const FGameplayTag& Tag);

	/**
	 * Shared Publisher用: Actorのファミリーrootの Entry を検索する。Entry が無ければ null。
	 * 検索のみなので MarkExpired 済みの Entry も返る。呼び出し側で IsExpired / IsMarkedExpired を確認すること。
	 * For Shared Publisher: Find an entry for the actor family root. Returns null when no entry exists.
	 * This is a pure lookup, so an entry that has been marked expired is returned as-is; callers must check
	 * IsExpired / IsMarkedExpired.
	 */
	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> FindSharedPublisherEntry(
		AActor* Actor,
		const FGameplayTag& Tag) const;

	/**
	 * Entry の Tick 専有データから診断情報を詰める（GameThread 専用。テストから直接呼べるよう static）
	 * Fill diagnostics from an entry's tick-owned data (GameThread only; static so tests can call it directly)
	 */
	static void FillSimpleWorldCollisionDebugInfo(const FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
	                                              FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo,
	                                              const FKawaiiPhysicsSimpleWorldRegistryKey* Key = nullptr);

	/**
	 * Entry の収集済み形状を Shape Slot へ Publish する（GameThread 専用。テストから直接呼べるよう static）
	 * Publish gathered shapes from Entry to the shape slot (GameThread only; static so tests can call it directly)
	 */
	static void PublishSimpleWorldShapeLimits(FKawaiiPhysicsSimpleWorldCollisionEntry& Entry, float BoxEnableThreshold);

	/**
	 * Provider disabled 中に形状 Slot と Ground Slot を必要な場合だけ空 Publish する（GameThread 専用）。
	 * Publishes empty shape and ground slots only when needed while the provider is disabled (GameThread only).
	 */
	static void PublishSimpleWorldEmptyLimits(FKawaiiPhysicsSimpleWorldCollisionEntry& Entry, float BoxEnableThreshold);

	/**
	 * Entry の地面 Box を Ground Slot へ Publish する（GameThread 専用。テストから直接呼べるよう static）
	 * Publish the ground box from Entry to the ground slot (GameThread only; static so tests can call it directly)
	 */
	static void PublishSimpleWorldGroundBox(FKawaiiPhysicsSimpleWorldCollisionEntry& Entry);

	/**
	 * SkelComp の SimpleWorld Entry を検索し診断情報を返す。Entry が無ければ false（OutInfo は既定値＋bHasEntry=false）。
	 * GameThread 専用。Shipping では常に false。
	 * Look up the SimpleWorld entry for SkelComp and return diagnostics. Returns false when no entry exists
	 * (OutInfo is reset with bHasEntry=false). GameThread only. Always false in Shipping builds.
	 */
	bool BuildSimpleWorldCollisionDebugInfo(const USkeletalMeshComponent* SkelComp,
	                                        FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo) const;
	bool BuildSimpleWorldCollisionDebugInfo(const FKawaiiPhysicsSimpleWorldRegistryKey& Key,
	                                        FKawaiiPhysicsSimpleWorldCollisionDebugInfo& OutInfo) const;

	/**
	 * Shared Publisher の診断情報を取得する。Entry が無ければ false（OutInfo は既定値＋bFound=false）。
	 * GameThread 専用。Shipping では常に false。
	 * Get Shared Publisher diagnostics. Returns false when no entry exists (OutInfo is reset with bFound=false).
	 * GameThread only. Always false in Shipping builds.
	 */
	bool BuildSharedPublisherDebugInfo(AActor* Actor, const FGameplayTag& Tag,
	                                   FKawaiiPhysicsSharedPublisherDebugInfo& OutInfo) const;

	// USubsystem interface
	virtual void Deinitialize() override;

	// UWorldSubsystem interface
	/**
	 * Game / Editor / PIE に加えて EditorPreview（Persona プレビュー）でも生成する。CVar a.AnimNode.KawaiiPhysics.SharedCollision.EnableInPreviewWorld=0 で従来挙動。
	 * GamePreview / Inactive は対象外。
	 * Also created for EditorPreview worlds (Persona preview) in addition to Game / Editor / PIE. CVar ...EnableInPreviewWorld=0 restores the legacy behavior.
	 * GamePreview / Inactive stay unsupported.
	 */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

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
	void GatherSimpleWorldEntry(
		UWorld& World,
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		const USkeletalMeshComponent& SkelComp,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		const FVector& Center,
		float Radius,
		float GroundTraceLength,
		ECollisionChannel CollisionChannel,
		int32 EffectiveMaxGatheredComponents,
		int32 EffectiveMaxPhysicsAssetBodies,
		int32 EffectiveMaxConvexPlanes,
		bool bUseMovementGround,
		bool bBuildConvexDebugGeometry,
		const TArray<TWeakObjectPtr<const USkeletalMeshComponent>>& MemberSkelComps,
		const TSet<TWeakObjectPtr<const USkeletalMeshComponent>>& MemberSkelCompSet,
		const TSet<TWeakObjectPtr<const AActor>>& MemberOwners);
	void UpdateSimpleWorldGround(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		const USkeletalMeshComponent& SkelComp,
		const FKawaiiPhysicsSimpleWorldCollisionDesc& Desc,
		float Radius,
		bool bGatherInputValid);
	bool UpdateSimpleWorldTransforms(
		FKawaiiPhysicsSimpleWorldCollisionEntry& Entry,
		float DeltaTime,
		bool bRegatherOnScaleChange,
		float FadeInTime);

	/**
	 * 構築済みキーで Entry を読み取りロック検索する（死んだActorのEntryはスキップ）。
	 * Read-locked lookup of an entry by its already-resolved key (skips entries whose family-root actor has died).
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntryByKey(const FRegistryKey& Key) const;

	TSharedPtr<FKawaiiPhysicsSharedPublisherEntry> FindSharedPublisherEntryByKey(const FRegistryKey& Key) const;

	/** レジストリ: (ActorFamilyRoot, Tag) → Entry / Registry: (ActorFamilyRoot, Tag) -> Entry */
	TMap<FRegistryKey, TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>> Registry;

	/** Registryの構造変更とイテレーションの競合を防ぐロック（Worker初期化とGameThread Tickの両方が触る）
	 *  Lock protecting Registry structural changes vs iteration (touched by both worker-thread init and GameThread Tick).
	 *  ロック順序は Registry → Slots に統一する（Tickは本ロック保持中にEntryのSlotsLockを取る）。デッドロック回避のため逆順は禁止。
	 *  Lock order is always Registry -> Slots (Tick holds this while taking an Entry's SlotsLock); never the reverse. */
	mutable FRWLock RegistryLock;

	/** SimpleWorldレジストリ: (KeyObject, Tag) → Entry / SimpleWorld registry: (KeyObject, Tag) -> Entry
	 *  ロック順序は SimpleWorldRegistryLock → Entry内ロック。既存RegistryLock/SlotsLockとは同時取得しない。
	 *  Lock order is SimpleWorldRegistryLock -> entry-internal locks. Do not hold it with RegistryLock/SlotsLock. */
	TMap<FKawaiiPhysicsSimpleWorldRegistryKey, TSharedPtr<FKawaiiPhysicsSimpleWorldCollisionEntry>> SimpleWorldRegistry;
	mutable FRWLock SimpleWorldRegistryLock;

	/** SharedPublisherレジストリ: (ActorFamilyRoot, Tag) → Entry / SharedPublisher registry: (ActorFamilyRoot, Tag) -> Entry
	 *  ロック順序は SharedPublisherRegistryLock → StateLock / GustMutex。StateLock と GustMutex は同時取得しない。
	 *  既存RegistryLock/SlotsLock/SimpleWorldRegistryLockとは同時取得しない。
	 *  Lock order is SharedPublisherRegistryLock -> StateLock / GustMutex. Do not hold StateLock and GustMutex together.
	 *  Do not hold it with RegistryLock/SlotsLock/SimpleWorldRegistryLock. */
	TMap<FRegistryKey, TSharedPtr<FKawaiiPhysicsSharedPublisherEntry>> SharedPublisherRegistry;
	mutable FRWLock SharedPublisherRegistryLock;
	TMap<FRegistryKey, uint64> SharedPublisherPreviousPublishSerials;

	/** クリーンアップ間隔制御 / Cleanup interval control */
	float CleanupAccumulator = 0.0f;
};
