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

	/** ワーカースレッドから呼び出し可能（ロックフリー） / Can be called from any thread (lock-free) */
	void Publish(const FKawaiiPhysicsSharedCollisionData& Data);

	/** ワーカースレッドから呼び出し可能（ロックフリー） / Can be called from any thread (lock-free) */
	const FKawaiiPhysicsSharedCollisionData& Read() const;
};

/**
 * (Actor, Tag) 単位のエントリ。複数Sourceのスロットを保持
 * Entry per (Actor, Tag) pair. Holds slots for multiple sources.
 */
struct KAWAIIPHYSICS_API FKawaiiPhysicsSharedCollisionEntry
{
	/** SourceID（AnimNodeアドレス等）→ 専用スロット / Source ID -> dedicated slot */
	TMap<uint64, TSharedPtr<FKawaiiPhysicsSharedCollisionSourceSlot>> Slots;

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
};

/**
 * KawaiiPhysics AnimNode間でコリジョンデータを共有するためのWorldSubsystem
 * WorldSubsystem for sharing collision data between KawaiiPhysics AnimNodes across SkeletalMeshComponents
 */
UCLASS()
class KAWAIIPHYSICS_API UKawaiiPhysicsSharedCollisionSubsystem : public UWorldSubsystem
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

private:
	/** レジストリ: (Actor, Tag) → Entry / Registry: (Actor, Tag) -> Entry */
	TMap<TPair<TWeakObjectPtr<AActor>, FGameplayTag>, TSharedPtr<FKawaiiPhysicsSharedCollisionEntry>> Registry;
};
