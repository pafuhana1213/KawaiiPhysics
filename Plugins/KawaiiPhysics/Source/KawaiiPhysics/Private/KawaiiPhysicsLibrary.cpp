// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsLibrary.h"

#include "AnimNode_KawaiiPhysics.h"

DEFINE_LOG_CATEGORY_STATIC(LogKawaiiPhysicsLibrary, Verbose, All);

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
	EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsReference>(Node, Result);
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics,
	UPARAM(ref) FKawaiiPhysicsSettings& PhysicsSettings)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetPhysicsSettings"),
		[PhysicsSettings](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.PhysicsSettings = PhysicsSettings;
		});

	return KawaiiPhysics;
}

FKawaiiPhysicsSettings UKawaiiPhysicsLibrary::GetPhysicsSettings(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	FKawaiiPhysicsSettings PhysicsSettings;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetPhysicsSettings"),
	[&PhysicsSettings](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		PhysicsSettings = InKawaiiPhysics.PhysicsSettings;
	});

	return PhysicsSettings;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetDummyBoneLength(
	const FKawaiiPhysicsReference& KawaiiPhysics, float DummyBoneLength)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetDummyBoneLength"),
		[DummyBoneLength](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.DummyBoneLength = DummyBoneLength;
		});

	return KawaiiPhysics;
}

float UKawaiiPhysicsLibrary::GetDummyBoneLength(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	float DummyBoneLength;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetDummyBoneLength"),
	[&DummyBoneLength](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		DummyBoneLength = InKawaiiPhysics.DummyBoneLength;
	});

	return DummyBoneLength;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetTeleportDistanceThreshold(
	const FKawaiiPhysicsReference& KawaiiPhysics, float TeleportDistanceThreshold)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetTeleportDistanceThreshold"),
		[TeleportDistanceThreshold](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.TeleportDistanceThreshold = TeleportDistanceThreshold;
		});

	return KawaiiPhysics;
}

float UKawaiiPhysicsLibrary::GetTeleportDistanceThreshold(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	float TeleportDistanceThreshold;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetTeleportDistanceThreshold"),
	[&TeleportDistanceThreshold](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		TeleportDistanceThreshold = InKawaiiPhysics.TeleportDistanceThreshold;
	});

	return TeleportDistanceThreshold;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetTeleportRotationThreshold(
	const FKawaiiPhysicsReference& KawaiiPhysics, float TeleportRotationThreshold)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetTeleportRotationThreshold"),
		[TeleportRotationThreshold](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.TeleportRotationThreshold = TeleportRotationThreshold;
		});

	return KawaiiPhysics;
}

float UKawaiiPhysicsLibrary::GetTeleportRotationThreshold(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	float TeleportRotationThreshold;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetTeleportRotationThreshold"),
	[&TeleportRotationThreshold](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		TeleportRotationThreshold = InKawaiiPhysics.TeleportRotationThreshold;
	});

	return TeleportRotationThreshold;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetGravity(
	const FKawaiiPhysicsReference& KawaiiPhysics, FVector Gravity)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetGravity"),
		[Gravity](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.Gravity = Gravity;
		});

	return KawaiiPhysics;
}

FVector UKawaiiPhysicsLibrary::GetGravity(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	FVector Gravity;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetGravity"),
	[&Gravity](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		Gravity = InKawaiiPhysics.Gravity;
	});

	return Gravity;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetEnableWind(
	const FKawaiiPhysicsReference& KawaiiPhysics, bool EnableWind)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetEnableWind"),
		[EnableWind](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.bEnableWind = EnableWind;
		});

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::GetEnableWind(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	bool EnableWind;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetEnableWind"),
	[&EnableWind](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		EnableWind = InKawaiiPhysics.bEnableWind;
	});

	return EnableWind;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetWindScale(
	const FKawaiiPhysicsReference& KawaiiPhysics, float WindScale)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetWindScale"),
		[WindScale](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.WindScale = WindScale;
		});

	return KawaiiPhysics;
}

float UKawaiiPhysicsLibrary::GetWindScale(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	float WindScale;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetWindScale"),
	[&WindScale](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		WindScale = InKawaiiPhysics.WindScale;
	});

	return WindScale;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetAllowWorldCollision(
	const FKawaiiPhysicsReference& KawaiiPhysics, bool AllowWorldCollision)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetAllowWorldCollision"),
		[AllowWorldCollision](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.bAllowWorldCollision = AllowWorldCollision;
		});

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::GetAllowWorldCollision(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	bool AllowWorldCollision;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetAllowWorldCollision"),
	[&AllowWorldCollision](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		AllowWorldCollision = InKawaiiPhysics.bAllowWorldCollision;
	});

	return AllowWorldCollision;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics,
	bool NeedWarmUp)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetNeedWarmup"),
		[NeedWarmUp](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.bNeedWarmUp = NeedWarmUp;
		});

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::GetNeedWarmUp(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	bool NeedWarmUp;
	
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
	TEXT("GetNeedWarmup"),
	[&NeedWarmUp](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
	{
		NeedWarmUp = InKawaiiPhysics.bNeedWarmUp;
	});

	return NeedWarmUp;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ResetDynamics"),
		[](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.ResetDynamics(ETeleportType::ResetPhysics);
		});

	return KawaiiPhysics;
}
