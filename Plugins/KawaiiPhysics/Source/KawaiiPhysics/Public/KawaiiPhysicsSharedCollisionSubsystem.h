// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"

#include "KawaiiPhysicsSharedCollisionSubsystem.generated.h"

struct FSphericalLimit;
struct FCapsuleLimit;
struct FBoxLimit;
struct FPlanarLimit;

/**
 * 共有コリジョンデータ（ワールド空間、計算済み）
 * Pre-computed collision data in world space for sharing between KawaiiPhysics AnimNodes
 */
USTRUCT()
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionData
{
	GENERATED_BODY()

	TArray<FSphericalLimit> SphericalLimits;
	TArray<FCapsuleLimit> CapsuleLimits;
	TArray<FBoxLimit> BoxLimits;
	TArray<FPlanarLimit> PlanarLimits;

	void Reset()
	{
		SphericalLimits.Reset();
		CapsuleLimits.Reset();
		BoxLimits.Reset();
		PlanarLimits.Reset();
	}

	bool IsEmpty() const
	{
		return SphericalLimits.Num() == 0
			&& CapsuleLimits.Num() == 0
			&& BoxLimits.Num() == 0
			&& PlanarLimits.Num() == 0;
	}
};

/**
 * Source1つ分のダブルバッファ付きスロット
 * Double-buffered slot for a single source (lock-free read/write)
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionSourceSlot
{
	FKawaiiPhysicsSharedCollisionData Buffers[2];
	std::atomic<int32> ReadBufferIndex{0};

	/** 最終Publishフレーム番号（ロックフリー鮮度チェック用） / Last published frame number for expiration detection */
	std::atomic<uint64> LastPublishFrame{0};

	/** ワーカースレッドから呼び出し可能（ロックフリー） / Can be called from any thread (lock-free) */
	void Publish(const FKawaiiPhysicsSharedCollisionData& Data);

	/** ワーカースレッドから呼び出し可能（ロックフリー） / Can be called from any thread (lock-free) */
	const FKawaiiPhysicsSharedCollisionData& Read() const;

	/** スロットが古くなっているか判定 / Check if this slot has not been published to recently */
	bool IsExpired(uint64 CurrentFrame, uint64 MaxAge) const;
};

/**
 * (Actor, Tag) 単位のエントリ。複数Sourceのスロットを保持
 * Entry per (Actor, Tag) pair. Holds slots for multiple sources.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionEntry
{
	/**
	 * Source用: 自分専用スロットを取得/作成（GameThreadで呼ぶ）
	 * For sources: Get or create a dedicated slot (call from GameThread)
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
 * KawaiiPhysics AnimNode間でコリジョンデータを共有するためのWorldSubsystem
 * WorldSubsystem for sharing collision data between KawaiiPhysics AnimNodes across SkeletalMeshComponents
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsSharedCollisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Source用: エントリを検索、なければ作成（GameThreadで呼ぶ）
	 * For sources: Find or create an entry (call from GameThread)
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindOrCreateEntry(AActor* Actor, const FGameplayTag& Tag);

	/**
	 * Target用: Actor→親Actorを辿ってエントリを検索
	 * For targets: Find an entry, traversing parent Actors via attachment hierarchy
	 */
	TSharedPtr<FKawaiiPhysicsSharedCollisionEntry> FindEntry(AActor* Actor, const FGameplayTag& Tag) const;

	// USubsystem interface
	virtual void Deinitialize() override;

	// FTickableGameObject interface (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !Registry.IsEmpty(); }
	virtual bool IsTickableInEditor() const override { return true; }

private:
	/** レジストリ: (Actor, Tag) → Entry / Registry: (Actor, Tag) -> Entry */
	TMap<TPair<TWeakObjectPtr<AActor>, FGameplayTag>, TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>> Registry;

	/** クリーンアップ間隔制御 / Cleanup interval control */
	float CleanupAccumulator = 0.0f;
};
