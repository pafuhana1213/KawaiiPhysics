// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "KawaiiPhysicsSharedCollisionTypes.h"
#include "Misc/EngineVersionComparison.h"

/** Rebuild a collision snapshot without destroying the nested arrays of surviving convex shapes.
 * The destination is private scratch until this writer is destroyed. Removed shapes release their
 * nested storage at that point; no inactive shape or member is kept alive as a capacity cache.
 */
struct FKawaiiPhysicsCollisionBufferWriter
{
	explicit FKawaiiPhysicsCollisionBufferWriter(FKawaiiPhysicsSharedCollisionData& InData)
		: Data(InData)
	{
		Data.SphericalLimits.Reset();
		Data.CapsuleLimits.Reset();
		Data.TaperedCapsuleLimits.Reset();
		Data.BoxLimits.Reset();
		Data.PlanarLimits.Reset();
	}

	// Append to a rebuild already in progress; the caller finalizes the convex count.
	FKawaiiPhysicsCollisionBufferWriter(FKawaiiPhysicsSharedCollisionData& InData, int32 InConvexIndex)
		: Data(InData), ConvexIndex(InConvexIndex), bAppendOnly(true) {}

	~FKawaiiPhysicsCollisionBufferWriter()
	{
		if (!bAppendOnly)
		{
			SetConvexCount(Data.ConvexLimits, ConvexIndex);
		}
	}

	FKawaiiPhysicsCollisionBufferWriter(const FKawaiiPhysicsCollisionBufferWriter&) = delete;
	FKawaiiPhysicsCollisionBufferWriter& operator=(const FKawaiiPhysicsCollisionBufferWriter&) = delete;

	static void SetConvexCount(TArray<FKawaiiPhysicsConvexLimit>& Limits, int32 Count)
	{
		Limits.Reserve(Count);
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		Limits.SetNum(Count, false);
#else
		Limits.SetNum(Count, EAllowShrinking::No);
#endif
	}

	static void CopyConvex(const FKawaiiPhysicsConvexLimit& Source, FKawaiiPhysicsConvexLimit& Target)
	{
		static_cast<FCollisionLimitBase&>(Target) = Source;
		Target.LocalBounds = Source.LocalBounds;
		Target.CachedConvexTransform = Source.CachedConvexTransform;
		Target.LocalPlanes.Reset(Source.LocalPlanes.Num());
		Target.LocalPlanes.Append(Source.LocalPlanes);
#if !UE_BUILD_SHIPPING
		Target.LocalVertices.Reset(Source.LocalVertices.Num());
		Target.LocalVertices.Append(Source.LocalVertices);
		Target.LocalEdges.Reset(Source.LocalEdges.Num());
		Target.LocalEdges.Append(Source.LocalEdges);
#endif
	}

	FKawaiiPhysicsConvexLimit& AddConvex(const FKawaiiPhysicsConvexLimit& Source)
	{
		if (ConvexIndex == Data.ConvexLimits.Num())
		{
			Data.ConvexLimits.AddDefaulted();
		}
		FKawaiiPhysicsConvexLimit& Target = Data.ConvexLimits[ConvexIndex++];
		CopyConvex(Source, Target);
		return Target;
	}

	void Append(const FKawaiiPhysicsSharedCollisionData& Source)
	{
		Data.SphericalLimits.Append(Source.SphericalLimits);
		Data.CapsuleLimits.Append(Source.CapsuleLimits);
		Data.TaperedCapsuleLimits.Append(Source.TaperedCapsuleLimits);
		Data.BoxLimits.Append(Source.BoxLimits);
		Data.PlanarLimits.Append(Source.PlanarLimits);
		Data.ConvexLimits.Reserve(ConvexIndex + Source.ConvexLimits.Num());
		for (const FKawaiiPhysicsConvexLimit& Convex : Source.ConvexLimits)
		{
			AddConvex(Convex);
		}
	}

	FKawaiiPhysicsSharedCollisionData& Data;
	int32 ConvexIndex = 0;
	bool bAppendOnly = false;
};

struct FKawaiiPhysicsSimpleWorldBodyBinding;

namespace KawaiiPhysicsSimpleWorldCollision
{
	void AppendFadedLocalLimits(const FKawaiiPhysicsSharedCollisionData& LocalLimits, float FadeAlpha,
		const FTransform& ComponentTM, FKawaiiPhysicsCollisionBufferWriter& Writer, float BoxEnableThreshold);
	void AppendFadedSkeletalLocalLimits(const FKawaiiPhysicsSharedCollisionData& LocalLimits,
		TArrayView<const FKawaiiPhysicsSimpleWorldBodyBinding> Bindings,
		TArrayView<const FTransform> BodyWorldTMs, float FadeAlpha,
		FKawaiiPhysicsCollisionBufferWriter& Writer, float BoxEnableThreshold);
}
