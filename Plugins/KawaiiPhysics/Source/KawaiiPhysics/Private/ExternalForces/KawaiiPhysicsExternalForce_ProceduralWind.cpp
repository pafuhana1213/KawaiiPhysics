// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "ExternalForces/KawaiiPhysicsExternalForce_ProceduralWind.h"

#include "Math/RotationMatrix.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(KawaiiPhysicsExternalForce_ProceduralWind)

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ExternalForce_ProceduralWind_Apply"),
                   STAT_KawaiiPhysics_ExternalForce_ProceduralWind_Apply, STATGROUP_Anim);

namespace
{
constexpr float TwoPi = UE_PI * 2.0f;
constexpr int32 ScopeBufferSize = 512;

// 正規化に失敗した場合は前方ベクトルへフォールバックする（GetSafeNormal の2引数版は UE バージョン間で挙動差があるため不使用）
FVector SafeDirectionOrForward(const FVector& InVector)
{
	const FVector Normalized = InVector.GetSafeNormal();
	return Normalized.IsNearlyZero() ? FVector::ForwardVector : Normalized;
}

float EvaluateActiveGust(const FKawaiiProceduralWindActiveGust& ActiveGust, const float InTime)
{
	if (!ActiveGust.bIsActive)
	{
		return 0.0f;
	}

	const float ElapsedTime = InTime - ActiveGust.StartTime;
	if (ElapsedTime < 0.0f)
	{
		return 0.0f;
	}

	const float RiseTime = FMath::Max(ActiveGust.RiseTime, 0.0f);
	const float DecayTime = FMath::Max(ActiveGust.DecayTime, 0.0f);
	if (RiseTime > KINDA_SMALL_NUMBER && ElapsedTime < RiseTime)
	{
		return ActiveGust.Strength * (ElapsedTime / RiseTime);
	}

	if (DecayTime <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float DecayElapsedTime = ElapsedTime - RiseTime;
	if (DecayElapsedTime < DecayTime)
	{
		return ActiveGust.Strength * (1.0f - DecayElapsedTime / DecayTime);
	}

	return 0.0f;
}

FVector ApplyConeNoiseToDirection(const FVector& InDirection, const float NoiseX, const float NoiseY,
                                  const float ConeHalfAngleDegrees)
{
	const float ConeHalfAngleRadians = FMath::DegreesToRadians(FMath::Max(ConeHalfAngleDegrees, 0.0f));
	if (ConeHalfAngleRadians <= KINDA_SMALL_NUMBER)
	{
		return SafeDirectionOrForward(InDirection);
	}

	FVector UnitDisk = FVector(NoiseX, NoiseY, 0.0f);
	const float DiskLength = UnitDisk.Size2D();
	if (DiskLength <= KINDA_SMALL_NUMBER)
	{
		return SafeDirectionOrForward(InDirection);
	}

	if (DiskLength > 1.0f)
	{
		UnitDisk /= DiskLength;
	}

	const FVector BaseDirection = SafeDirectionOrForward(InDirection);
	FVector AxisX;
	FVector AxisY;
	BaseDirection.FindBestAxisVectors(AxisX, AxisY);

	const FVector OffsetDirection = (AxisX * UnitDisk.X + AxisY * UnitDisk.Y).GetSafeNormal();
	const float NoiseAngle = ConeHalfAngleRadians * FMath::Min(DiskLength, 1.0f);
	return (BaseDirection * FMath::Cos(NoiseAngle) + OffsetDirection * FMath::Sin(NoiseAngle)).GetSafeNormal();
}

float ComputeDebugTotalRate(const FKawaiiPhysics_ExternalForce_ProceduralWind& Force, const float Total)
{
	const float MaxSine = FMath::Abs(Force.SteadyForce) + FMath::Abs(Force.OscillationForce) +
		FMath::Abs(Force.WaveAmplitude);
	const float MaxEnvelope = FMath::Max(FMath::Abs(Force.EnvelopeMin), FMath::Abs(Force.EnvelopeMax));
	const float MaxRandom = FMath::Abs(Force.RandomForce);
	const float Reference = FMath::Max(MaxSine * MaxEnvelope + MaxRandom, 1.0f);
	return FMath::Abs(Total) / Reference;
}
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::ResetRuntimeState()
{
	RuntimeState = MakeShared<FKawaiiProceduralWindRuntimeState, ESPMode::ThreadSafe>();

#if WITH_EDITOR
	RuntimeState->ScopeBuffer.SetNum(FMath::Max(ScopeBufferSize, 1));
	RuntimeState->ScopeWriteIndex = 0;
	RuntimeState->ScopeSampleCount = 0;
#endif
}

FKawaiiPhysicsProceduralWindSample FKawaiiPhysics_ExternalForce_ProceduralWind::ComputeWindSample(
	const float InTime, const float InLengthRate) const
{
	FKawaiiPhysicsProceduralWindSample Sample;

	const float SafeOscillationPeriod = FMath::Max(OscillationPeriod, 0.01f);
	const float SafeWavePeriod = FMath::Max(WavePeriod, 0.01f);
	const float SafeRandomPeriod = FMath::Max(RandomPeriod, 0.01f);

	Sample.Steady = SteadyForce;
	Sample.Oscillation = OscillationForce * FMath::Sin(TwoPi * InTime / SafeOscillationPeriod);
	Sample.Wave = WaveAmplitude * FMath::Sin(TwoPi * InTime / SafeWavePeriod -
		FMath::DegreesToRadians(InLengthRate * WaveSpatialOffset) + FMath::DegreesToRadians(WavePhase));
	Sample.Envelope = FMath::Lerp(EnvelopeMin, EnvelopeMax,
		0.5f * (1.0f + FMath::Sin(TwoPi * EnvelopeFrequency * InTime + FMath::DegreesToRadians(EnvelopePhase))));
	Sample.Random = RandomForce * SampleSmoothNoise(InTime / SafeRandomPeriod, RandomSeed, 0);

	if (RuntimeState.IsValid())
	{
		Sample.Gust = EvaluateActiveGust(RuntimeState->ActiveGust, InTime);
	}

	Sample.Total = (Sample.Steady + Sample.Oscillation + Sample.Wave) * Sample.Envelope +
		Sample.Random + Sample.Gust;
	return Sample;
}

uint32 FKawaiiPhysics_ExternalForce_ProceduralWind::StableHash(const int32 Seed, const int32 GridIndex,
                                                               const int32 Channel)
{
	uint32 Hash = 0x811C9DC5u;
	Hash ^= static_cast<uint32>(Seed);
	Hash *= 0x01000193u;
	Hash ^= static_cast<uint32>(GridIndex);
	Hash *= 0x01000193u;
	Hash ^= static_cast<uint32>(Channel);
	Hash *= 0x01000193u;

	Hash ^= Hash >> 16;
	Hash *= 0x7FEB352Du;
	Hash ^= Hash >> 15;
	Hash *= 0x846CA68Bu;
	Hash ^= Hash >> 16;
	return Hash;
}

float FKawaiiPhysics_ExternalForce_ProceduralWind::NoiseValueAt(const int32 GridIndex, const int32 Seed,
                                                                const int32 Channel)
{
	FRandomStream RandomStream(StableHash(Seed, GridIndex, Channel));
	return RandomStream.FRandRange(-1.0f, 1.0f);
}

float FKawaiiPhysics_ExternalForce_ProceduralWind::SampleSmoothNoise(const float U, const int32 Seed,
                                                                     const int32 Channel)
{
	const float GridFloat = FMath::FloorToFloat(U);
	const int32 GridIndex = static_cast<int32>(GridFloat);
	const float Alpha = U - GridFloat;
	const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

	return FMath::Lerp(NoiseValueAt(GridIndex, Seed, Channel),
	                   NoiseValueAt(GridIndex + 1, Seed, Channel), SmoothAlpha);
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Initialize(const FAnimationInitializeContext& Context)
{
	Super::Initialize(Context);

	ResetRuntimeState();
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::PreApply(FAnimNode_KawaiiPhysics& Node,
                                                           FComponentSpacePoseContext& PoseContext)
{
	Super::PreApply(Node, PoseContext);

	if (!RuntimeState.IsValid())
	{
		ResetRuntimeState();
	}

	{
		FScopeLock Lock(&RuntimeState->Mutex);
		if (RuntimeState->PendingGust.IsSet())
		{
			const FKawaiiProceduralWindGustRequest& PendingGust = RuntimeState->PendingGust.GetValue();
			RuntimeState->ActiveGust.StartTime = RuntimeState->Time;
			RuntimeState->ActiveGust.Strength = PendingGust.Strength;
			RuntimeState->ActiveGust.RiseTime = PendingGust.RiseTime;
			RuntimeState->ActiveGust.DecayTime = PendingGust.DecayTime;
			RuntimeState->ActiveGust.bIsActive = true;
			RuntimeState->PendingGust.Reset();
		}
	}

	RuntimeState->Time += Node.GetStepDeltaTime() * TimeScale;

	const FKawaiiPhysicsProceduralWindSample Sample = ComputeWindSample(RuntimeState->Time, 0.0f);
	RuntimeState->CachedSinesWithoutWave = Sample.Steady + Sample.Oscillation;
	RuntimeState->CachedEnvelope = Sample.Envelope;
	RuntimeState->CachedRandom = Sample.Random;
	RuntimeState->CachedGust = Sample.Gust;

	const FVector BaseWindDirection = SafeDirectionOrForward(WindDirection.Vector());
	FVector NoisyWindDirection = BaseWindDirection;
	if (DirectionNoiseAngle > 0.0f)
	{
		const float SafeDirectionNoisePeriod = FMath::Max(DirectionNoisePeriod, 0.01f);
		const float DirectionNoiseU = RuntimeState->Time / SafeDirectionNoisePeriod;
		const float NoiseX = SampleSmoothNoise(DirectionNoiseU, RandomSeed, 1);
		const float NoiseY = SampleSmoothNoise(DirectionNoiseU, RandomSeed, 2);
		NoisyWindDirection = ApplyConeNoiseToDirection(BaseWindDirection, NoiseX, NoiseY, DirectionNoiseAngle);
	}
	RuntimeState->CachedWindVector = ConvertExternalForceToSimulationSpace(Node, PoseContext, NoisyWindDirection);

#if WITH_EDITOR
	{
		FScopeLock Lock(&RuntimeState->Mutex);
		if (RuntimeState->ScopeBuffer.Num() != ScopeBufferSize)
		{
			RuntimeState->ScopeBuffer.SetNum(ScopeBufferSize);
			RuntimeState->ScopeWriteIndex = 0;
			RuntimeState->ScopeSampleCount = 0;
		}

		RuntimeState->ScopeBuffer[RuntimeState->ScopeWriteIndex] = {RuntimeState->Time, Sample};
		RuntimeState->ScopeWriteIndex = (RuntimeState->ScopeWriteIndex + 1) % ScopeBufferSize;
		++RuntimeState->ScopeSampleCount;
	}
#endif
}

void FKawaiiPhysics_ExternalForce_ProceduralWind::Apply(FKawaiiPhysicsModifyBone& Bone, FAnimNode_KawaiiPhysics& Node,
                                                        FComponentSpacePoseContext& PoseContext,
                                                        const FTransform& BoneTM)
{
	if (!CanApply(Bone) || !RuntimeState.IsValid())
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ExternalForce_ProceduralWind_Apply);

	const float Wave = WaveAmplitude * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(WavePeriod, 0.01f) -
		FMath::DegreesToRadians(Bone.LengthRateFromRoot * WaveSpatialOffset) + FMath::DegreesToRadians(WavePhase));
	const float Total = (RuntimeState->CachedSinesWithoutWave + Wave) * RuntimeState->CachedEnvelope +
		RuntimeState->CachedRandom + RuntimeState->CachedGust;

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(Bone.LengthRateFromRoot);
	}

	// RandomizedForceScale は _Wind と同じく Apply 側でスカラーにだけ掛け、方向キャッシュとの二重掛けを避ける。
	if (ExternalForceSpace == EExternalForceSpace::BoneSpace)
	{
		const FVector BoneForce = BoneTM.TransformVector(RuntimeState->CachedWindVector);
		Bone.Location += BoneForce * Total * ForceRate * RandomizedForceScale * Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName, BoneForce * Total * ForceRate * RandomizedForceScale);
#endif
	}
	else
	{
		Bone.Location += RuntimeState->CachedWindVector * Total * ForceRate * RandomizedForceScale *
			Node.GetStepDeltaTime();

#if ENABLE_ANIM_DEBUG
		BoneForceMap.Add(Bone.BoneRef.BoneName,
		                 RuntimeState->CachedWindVector * Total * ForceRate * RandomizedForceScale);
#endif
	}

#if ENABLE_ANIM_DEBUG
	AnimDrawDebug(Bone, Node, PoseContext);
#endif
}

#if WITH_EDITOR
void FKawaiiPhysics_ExternalForce_ProceduralWind::AnimDrawDebugForEditMode(
	const FKawaiiPhysicsModifyBone& ModifyBone, const FAnimNode_KawaiiPhysics& Node, FPrimitiveDrawInterface* PDI)
{
	if (!IsDebugEnabled(true) || !CanApply(ModifyBone) || !RuntimeState.IsValid() ||
		RuntimeState->CachedWindVector.IsNearlyZero())
	{
		return;
	}

	const float Wave = WaveAmplitude * FMath::Sin(TwoPi * RuntimeState->Time / FMath::Max(WavePeriod, 0.01f) -
		FMath::DegreesToRadians(ModifyBone.LengthRateFromRoot * WaveSpatialOffset) +
		FMath::DegreesToRadians(WavePhase));
	const float Total = (RuntimeState->CachedSinesWithoutWave + Wave) * RuntimeState->CachedEnvelope +
		RuntimeState->CachedRandom + RuntimeState->CachedGust;

	float ForceRate = 1.0f;
	if (const auto Curve = ForceRateByBoneLengthRate.GetRichCurve(); !Curve->IsEmpty())
	{
		ForceRate = Curve->Eval(ModifyBone.LengthRateFromRoot);
	}

	FVector ArrowLocation = ModifyBone.Location + DebugArrowOffset;
	FQuat ArrowRotation = RuntimeState->CachedWindVector.GetSafeNormal().ToOrientationQuat();
	if (Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
	{
		const FTransform& BaseBoneSpace2ComponentSpace = Node.GetBaseBoneSpace2ComponentSpace();
		ArrowLocation = BaseBoneSpace2ComponentSpace.TransformPosition(ArrowLocation);
		ArrowRotation = BaseBoneSpace2ComponentSpace.TransformRotation(ArrowRotation);
	}

	const float TotalRate = ComputeDebugTotalRate(*this, Total);
	const FTransform ArrowTransform(ArrowRotation, ArrowLocation);
	DrawDirectionalArrow(PDI, ArrowTransform.ToMatrixNoScale(), FColor::Cyan,
	                     DebugArrowLength * ForceRate * TotalRate, DebugArrowSize, SDPG_Foreground);

	if (ModifyBone.Index == 0)
	{
		FVector RootArrowLocation = ModifyBone.Location + DebugArrowOffset * 2.0f;
		FQuat RootArrowRotation = RuntimeState->CachedWindVector.GetSafeNormal().ToOrientationQuat();
		if (Node.SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FTransform& BaseBoneSpace2ComponentSpace = Node.GetBaseBoneSpace2ComponentSpace();
			RootArrowLocation = BaseBoneSpace2ComponentSpace.TransformPosition(RootArrowLocation);
			RootArrowRotation = BaseBoneSpace2ComponentSpace.TransformRotation(RootArrowRotation);
		}

		const FTransform RootArrowTransform(RootArrowRotation, RootArrowLocation);
		DrawDirectionalArrow(PDI, RootArrowTransform.ToMatrixNoScale(), FColor::Cyan,
		                     DebugArrowLength * TotalRate, DebugArrowSize * 2.0f, SDPG_Foreground);
	}
}
#endif
