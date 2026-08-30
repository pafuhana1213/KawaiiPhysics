// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "KawaiiPhysicsGroundProvider.generated.h"

class UPrimitiveComponent;
class USkeletalMeshComponent;

/** 地面 1 点の情報 / One ground sample */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsGroundHit
{
	GENERATED_BODY()

	/** 地面あり。false なら次のソース（CharacterMovement → トレース）へ落ちる / Ground found; false falls through to the next source (CharacterMovement, then trace) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	bool bHit = false;

	/** 接地点（ワールド） / Impact point (world) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FVector Location = FVector::ZeroVector;

	/** 地面法線（ワールド） / Surface normal (world) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	FVector Normal = FVector::UpVector;

	/** ヒットしたコンポーネント（任意） / Hit component (optional) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics")
	TObjectPtr<UPrimitiveComponent> Component = nullptr;
};

UINTERFACE(BlueprintType, Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsGroundProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Simple World Collision の地面情報を供給する。Mover / カスタム Movement Component / BP Pawn はこれを実装する。
 * 毎フレーム呼ばれるのでキャッシュ済みの床情報を返す軽量な実装にすること（内部でトレースしない）。
 * Supplies ground info to Simple World Collision. Implement on Mover / custom movement components / BP pawns.
 * Called every frame: return cached floor data; do not trace inside.
 */
class KAWAIIPHYSICS_API IKawaiiPhysicsGroundProvider
{
	GENERATED_BODY()

public:
	/** SkelComp は問い合わせ元のメッシュ / SkelComp is the querying mesh */
	UFUNCTION(BlueprintNativeEvent, Category = "Kawaii Physics|Simple World Collision")
	FKawaiiPhysicsGroundHit GetKawaiiPhysicsGround(const USkeletalMeshComponent* SkelComp);
};
