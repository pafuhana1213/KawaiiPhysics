// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KawaiiPhysicsCollisionLimits.h"

#include "KawaiiPhysicsSharedCollisionTypes.generated.h"

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
