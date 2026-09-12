// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "HAL/PlatformTLS.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"
#include <atomic>

// Diagnostic captures run separately from CPU timing. Memory trace timestamps are sampled, so
// tiny sentinel allocations provide exact event-index boundaries instead of approximate times.
// Keep sentinels alive until process shutdown to prevent address reuse; the analyzer excludes them.
struct FKawaiiPhysicsMemoryTraceRegion
{
	FKawaiiPhysicsMemoryTraceRegion(const TCHAR* InScenario, int32 InTrial)
		: Scenario(InScenario), Trial(InTrial)
	{
		Mark(TEXT("setup"));
	}
	void Warmup() const { Mark(TEXT("warmup")); }
	void End() const { Mark(TEXT("end")); }

private:
	struct FSentinels
	{
		void* Addresses[512] = {};
		std::atomic<uint32> Count{0};
		~FSentinels()
		{
			for (void* Address : Addresses)
			{
				FMemory::Free(Address);
			}
		}
	};
	void Mark(const TCHAR* Phase) const
	{
		static const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("KawaiiMemoryCapture"));
		if (!bCapture)
		{
			return;
		}
		static FSentinels Sentinels;
		const uint32 Index = Sentinels.Count.fetch_add(1, std::memory_order_relaxed);
		check(Index < UE_ARRAY_COUNT(Sentinels.Addresses));
		void* Address = FMemory::Malloc(1);
		Sentinels.Addresses[Index] = Address;
		TRACE_BOOKMARK(TEXT("KAWAII_MEMORY scenario=%s trial=%d phase=%s thread=%u marker=%llu"),
			Scenario, Trial, Phase, FPlatformTLS::GetCurrentThreadId(), reinterpret_cast<uint64>(Address));
	}
	const TCHAR* Scenario;
	int32 Trial;
};
