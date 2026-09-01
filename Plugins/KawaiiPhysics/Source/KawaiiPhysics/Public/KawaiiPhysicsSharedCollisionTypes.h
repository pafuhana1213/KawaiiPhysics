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
	TArray<FTaperedCapsuleLimit> TaperedCapsuleLimits;
	TArray<FBoxLimit> BoxLimits;
	TArray<FPlanarLimit> PlanarLimits;
	TArray<FKawaiiPhysicsConvexLimit> ConvexLimits;

	void Reset()
	{
		SphericalLimits.Reset();
		CapsuleLimits.Reset();
		TaperedCapsuleLimits.Reset();
		BoxLimits.Reset();
		PlanarLimits.Reset();
		ConvexLimits.Reset();
	}

	bool IsEmpty() const
	{
		return SphericalLimits.Num() == 0
			&& CapsuleLimits.Num() == 0
			&& TaperedCapsuleLimits.Num() == 0
			&& BoxLimits.Num() == 0
			&& PlanarLimits.Num() == 0
			&& ConvexLimits.Num() == 0;
	}
};
