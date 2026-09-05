// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#if !UE_BUILD_SHIPPING && WITH_EDITORONLY_DATA
#include "KawaiiPhysicsDeveloperSettings.h"
#endif
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "KawaiiPhysicsSharedTags.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if !UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"
#include "KawaiiPhysicsNodeWarning.h"

#include <atomic>

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_KawaiiPhysics)

namespace
{
	void DropOldestPendingTransientForceIfFull(FKawaiiPhysicsTransientForceQueue& Queue, const bool bAppendingGust)
	{
		if (Queue.PendingForces.Num() + Queue.PendingGusts.Num() < FAnimNode_KawaiiPhysics::MaxTransientExternalForces)
		{
			return;
		}

		// 評価が走らないノードへの連打でも pending 合計が MaxTransientExternalForces を超えないよう最古から破棄する。
		if (bAppendingGust)
		{
			if (Queue.PendingGusts.Num() > 0)
			{
				Queue.PendingGusts.RemoveAt(0);
			}
			else
			{
				Queue.PendingForces.RemoveAt(0);
			}
		}
		else
		{
			if (Queue.PendingForces.Num() > 0)
			{
				Queue.PendingForces.RemoveAt(0);
			}
			else
			{
				Queue.PendingGusts.RemoveAt(0);
			}
		}
	}
}

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsEnable(
	TEXT("a.AnimNode.KawaiiPhysics.Enable"), true, TEXT("Enable/Disable KawaiiPhysics"));
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebug(
	TEXT("a.AnimNode.KawaiiPhysics.Debug"), false, TEXT("Turn on visualization debugging for KawaiiPhysics"));
TAutoConsoleVariable<float> CVarAnimNodeKawaiiPhysicsDebugDrawThickness(
	TEXT("a.AnimNode.KawaiiPhysics.DebugDrawThickness"), 1.0f,
	TEXT("Override debug draw thickness used by KawaiiPhysics (<=0 uses per-call thickness)."));
#endif

TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsUseBoneContainerRefSkeletonWhenInit(
	TEXT("a.AnimNode.KawaiiPhysics.UseBoneContainerRefSkeletonWhenInit"), true, TEXT(
		"flag to revert the behavior of RefSkeleton in InitModifyBones to its previous implementation."));

// SharedCollision CVars
TAutoConsoleVariable<int32> CVarSharedCollisionReadMaxAge(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.ReadMaxAge"), 10,
	TEXT("ReadMergedでの鮮度判定フレーム数。URO/LODスキップを考慮した値に設定 / Max frame age for ReadMerged freshness check. Accounts for URO/LOD skips."));
TAutoConsoleVariable<int32> CVarSharedCollisionCleanupMaxAge(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupMaxAge"), 60,
	TEXT("Tickでのスロット除去猶予フレーム数 / Grace period in frames before expired slots are removed during Tick cleanup."));
TAutoConsoleVariable<int32> CVarSharedCollisionInitRetryThreshold(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThreshold"), 60,
	TEXT("警告ログを出すまでの初期化リトライ回数 / Number of init retries before logging a warning."));
TAutoConsoleVariable<int32> CVarSharedCollisionInitRetryThrottleInterval(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.InitRetryThrottleInterval"), 60,
	TEXT("警告しきい値到達後の再初期化リトライ間隔（フレーム）。誤設定タグでの毎フレーム再試行を間引く / "
		"Retry interval (frames) after the warning threshold; throttles per-frame re-init for misconfigured tags."));
TAutoConsoleVariable<float> CVarSharedCollisionCleanupInterval(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupInterval"), 1.0f,
	TEXT("クリーンアップ間隔（秒） / Cleanup interval in seconds."));
TAutoConsoleVariable<int32> CVarSharedCollisionEnableInPreviewWorld(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.EnableInPreviewWorld"), 1,
	TEXT("1でPersona等のプレビューワールド（EditorPreview）でもSharedCollision Subsystemを生成する。0で従来どおり生成しない / 1 creates the SharedCollision subsystem in preview worlds (EditorPreview, e.g. Persona) too; 0 keeps the legacy behavior (not created)."));

// SimpleWorldCollision CVars
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionEnable(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.Enable"), 1,
	TEXT("0でシンプルワールドコリジョンを全体無効化（収集・適用の両方を停止） / 0 disables Simple World Collision entirely (both gathering and application)."));
TAutoConsoleVariable<float> CVarSimpleWorldCollisionGatherIntervalScale(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.GatherIntervalScale"), 1.0f,
	TEXT("実効収集間隔に乗算するスケール値 / Scale multiplied into the effective gather interval."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxComponents(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxComponents"), -1,
	TEXT("-1でDeveloperSettingsの値を使用。0以上でSkelComp単位の最大収集コンポーネント数を上書き / "
		"-1 uses the DeveloperSettings value; >=0 overrides the max gathered components per SkelComp."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxPhysicsAssetBodies(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxPhysicsAssetBodies"), -1,
	TEXT("-1でDeveloperSettingsの値を使用。0でPhysicsAssetモードのSkeletalMeshを収集しない。1以上でSkelComp単位最大body数を上書き / "
		"-1 uses the DeveloperSettings value; 0 does not gather SkeletalMeshes in PhysicsAsset mode; >=1 overrides the max PhysicsAsset bodies per SkelComp."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionMaxConvexPlanes(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.MaxConvexPlanes"), -1,
	TEXT("-1でDeveloperSettingsの値を使用。0以上でConvex Hull 1つあたりの最大平面数を上書き / "
		"-1 uses the DeveloperSettings value; >=0 overrides the max planes per convex hull."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionRegatherOnScaleChange(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.RegatherOnScaleChange"), -1,
	TEXT("-1でDeveloperSettingsの値を使用。0で無効、1でスケール変化時に再収集 / "
		"-1 uses the DeveloperSettings value; 0 disables, 1 re-gathers on scale change."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionCleanupMaxAge(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.CleanupMaxAge"), 60,
	TEXT("SimpleWorld Entry/Descのエイジアウト閾値（フレーム数） / Age-out threshold (in frames) for SimpleWorld entries/descs."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionDebugDraw(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.DebugDraw"), 0,
	TEXT("Subsystem側(GameThread)で収集半径・収集済み形状・地面Boxを可視化 / "
		"Visualize the gather radius, gathered shapes and ground box on the subsystem side (GameThread)."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionForceEnableOnServer(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.ForceEnableOnServer"), 0,
	TEXT("1でDedicated Serverでも収集を行う（既定は見た目専用機能のため収集しない） / "
		"1 gathers even on a Dedicated Server (by default it is skipped since this is a visual-only feature)."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionUseMovementGround(
	TEXT("a.AnimNode.KawaiiPhysics.SimpleWorldCollision.UseMovementGround"), 1,
	TEXT("1で所有Actorの地面情報（IKawaiiPhysicsGroundProvider / CharacterMovementComponent）を地面Boxに使う。0で従来の下方向トレースのみ / "
		"1 uses the owner's ground info (IKawaiiPhysicsGroundProvider / CharacterMovementComponent) for the ground box; 0 uses only the legacy downward trace."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionReaderReleaseMaxAge(
	TEXT("a.AnimNode.KawaiiPhysics.SharedPublisher.ReaderReleaseMaxAge"), 60,
	TEXT("共有 Entry の provider が更新を止めてから reader が Entry を解放するまでの猶予フレーム数 / "
		"Frames a reader keeps a shared entry after its provider stopped updating before releasing it."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionAutoResolveInterval(
	TEXT("a.AnimNode.KawaiiPhysics.SharedPublisher.AutoResolveInterval"), 30,
	TEXT("Frames between Auto Simple World Collision checks while running as a local provider."));
TAutoConsoleVariable<int32> CVarSimpleWorldCollisionSharedPublisherDebugDraw(
	TEXT("a.AnimNode.KawaiiPhysics.SharedPublisher.DebugDraw"), 0,
	TEXT("Draw Shared Publisher Simple World Collision labels from the subsystem tick."));

DEFINE_STAT(STAT_KawaiiPhysics_InitModifyBones);
DEFINE_STAT(STAT_KawaiiPhysics_Eval);
DEFINE_STAT(STAT_KawaiiPhysics_SimulateModifyBones);
DEFINE_STAT(STAT_KawaiiPhysics_Simulate);
DEFINE_STAT(STAT_KawaiiPhysics_GetWindVelocity);
DEFINE_STAT(STAT_KawaiiPhysics_WorldCollision);
DEFINE_STAT(STAT_KawaiiPhysics_InitSyncBone);
DEFINE_STAT(STAT_KawaiiPhysics_ApplySyncBone);
DEFINE_STAT(STAT_KawaiiPhysics_AdjustByCollision);
DEFINE_STAT(STAT_KawaiiPhysics_AdjustByBoneConstraint);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateSphericalLimit);
DEFINE_STAT(STAT_KawaiiPhysics_UpdatePlanarLimit);
DEFINE_STAT(STAT_KawaiiPhysics_WarmUp);
DEFINE_STAT(STAT_KawaiiPhysics_UpdatePhysicsSetting);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateCapsuleLimit);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateTaperedCapsuleLimit);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateBoxLimit);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform);
DEFINE_STAT(STAT_KawaiiPhysics_ApplySimulateResult);
DEFINE_STAT(STAT_KawaiiPhysics_ConvertSimulationSpaceTransform);
DEFINE_STAT(STAT_KawaiiPhysics_ConvertSimulationSpaceVector);
DEFINE_STAT(STAT_KawaiiPhysics_ConvertSimulationSpaceLocation);
DEFINE_STAT(STAT_KawaiiPhysics_ConvertSimulationSpaceRotation);
DEFINE_STAT(STAT_KawaiiPhysics_ConvertSimulationSpace);
DEFINE_STAT(STAT_KawaiiPhysics_InitializeSharedCollision);
DEFINE_STAT(STAT_KawaiiPhysics_WriteSharedCollisionToSubsystem);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateSharedCollisionLimits);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateSimpleWorldCollisionLimits);
DEFINE_STAT(STAT_KawaiiPhysics_NumSimpleWorldColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumModifyBones);
DEFINE_STAT(STAT_KawaiiPhysics_NumInterBoneDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_NumBridgeDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_InsertInterBoneDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_BridgeDummy);
DEFINE_STAT(STAT_KawaiiPhysics_AdjustByLimitsAndLength);
DEFINE_STAT(STAT_KawaiiPhysics_NumSphereColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumCapsuleColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumTaperedCapsuleColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumBoxColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumPlanarColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumSharedColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumMergedBoneConstraints);
DEFINE_STAT(STAT_KawaiiPhysics_NumWorldCollisionChecks);
DEFINE_STAT(STAT_KawaiiPhysics_ModifyBonesMemory);

int32 GetKawaiiPhysicsSharedPublisherReaderReleaseMaxAge()
{
	return CVarSimpleWorldCollisionReaderReleaseMaxAge.GetValueOnAnyThread();
}

int32 GetKawaiiPhysicsSharedPublisherAutoResolveInterval()
{
	return CVarSimpleWorldCollisionAutoResolveInterval.GetValueOnAnyThread();
}

FAnimNode_KawaiiPhysics::FAnimNode_KawaiiPhysics()
{
	SimpleWorldCollisionSharedTag = TAG_KawaiiPhysics_Shared_Default;
}

void FAnimNode_KawaiiPhysics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	SphericalLimitsData.Empty();
	CapsuleLimitsData.Empty();
	TaperedCapsuleLimitsData.Empty();
	BoxLimitsData.Empty();
	PlanarLimitsData.Empty();

	// 旧Slotを即座に期限切れ化
	if (CachedSourceSlot.IsValid())
	{
		CachedSourceSlot->MarkExpired();
	}

	// 共有コリジョンのキャッシュをリセット
	bSharedCollisionInitialized = false;
	CachedSharedCollisionEntry.Reset();
	CachedSourceSlot.Reset();
	SharedCollisionInitRetryCount = 0;
	bSharedCollisionInitWarningLogged = false;
	bSharedCollisionNeedsReinit = false;
	bModifyBonesNeedsReinit = false;

	// 共有コリジョンワーク配列をリセット
	SharedCollisionMergedData.Reset();
	SharedSphericalLimits.Reset();
	SharedCapsuleLimits.Reset();
	SharedTaperedCapsuleLimits.Reset();
	SharedBoxLimits.Reset();
	SharedPlanarLimits.Reset();

	// シンプルワールドコリジョンのキャッシュをリセット
	ReleaseSimpleWorldCollision();
	SimpleWorldMergedScratch.Reset();
	SimpleWorldGroundScratch.Reset();
	SimpleWorldSphericalLimits.Reset();
	SimpleWorldCapsuleLimits.Reset();
	SimpleWorldTaperedCapsuleLimits.Reset();
	SimpleWorldBoxLimits.Reset();
	SimpleWorldGroundBoxLimits.Reset();
	SimpleWorldConvexLimits.Reset();
	LastReadSimpleWorldShapeSerial = 0;
	LastReadSimpleWorldGroundSerial = 0;
	LastReadSimpleWorldMemberSerialSum = 0;
	SimpleWorldReaderRetryCount = 0;
	bSimpleWorldReaderWarningLogged = false;
	SimpleWorldResolvedSource = EKawaiiPhysicsSimpleWorldCollisionSource::Local;
	SimpleWorldAutoResolveCountdown = 0;
#if !UE_BUILD_SHIPPING
	bSimpleWorldInvalidSharedTagWarningLogged = false;
#endif
	bSimpleWorldRadiusChecked = false;
	SimpleWorldRadiusCheckDeferrals = 0;

	ApplyLimitsDataAsset(RequiredBones);
	ApplyPhysicsAsset(RequiredBones);
	ApplyMirrorLimits(RequiredBones);
	ApplyBoneConstraintDataAsset(RequiredBones);

	ModifyBones.Empty();

	// 最初のフレームでのゼロ除算を回避するため
	DeltaTimeOld = 1.0f / static_cast<float>(GetEffectiveTargetFramerate());

	// サブステップ状態をリセット
	SubstepAccumulator = 0.0f;
	bSubstepPoseInitialized = false;

	// ノード再初期化時は実行時専用の一時外力と物理設定倍率を破棄する
	ResetTransientRuntimeState();

	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.Initialize(Context);
		}
	}

	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
	{
		if (SimulationBaseBone.Initialize(RequiredBones))
		{
			PrevBaseBoneSpace2ComponentSpace =
				FAnimationRuntime::GetComponentSpaceTransformRefPose(RequiredBones.GetReferenceSkeleton(),
				                                                     SimulationBaseBone.BoneIndex);
			CurrentEvalSimSpaceCache.TargetSpaceToComponent = PrevBaseBoneSpace2ComponentSpace;
		}
	}

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif
}

void FAnimNode_KawaiiPhysics::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_SkeletalControlBase::CacheBones_AnyThread(Context);
}

void FAnimNode_KawaiiPhysics::ResetDynamics(ETeleportType InTeleportType)
{
	TeleportType = InTeleportType;
	if (bUseWarmUpWhenResetDynamics)
	{
		bNeedWarmUp = true;
	}

	// サブステップ：未消費時間を破棄し、ポーズ補間の前フレーム値を次フレームで再初期化させる
	SubstepAccumulator = 0.0f;
	bSubstepPoseInitialized = false;
}

int64 FAnimNode_KawaiiPhysics::GenerateTransientHandleId()
{
	static std::atomic<int64> NextHandleId{1};
	return NextHandleId.fetch_add(1, std::memory_order_relaxed);
}

int64 FAnimNode_KawaiiPhysics::RequestTransientExternalForce(FInstancedStruct&& InForce, const float InLifetimeSeconds,
                                                             const int64 InHandleId)
{
	const int64 HandleId = InHandleId != 0 ? InHandleId : GenerateTransientHandleId();

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	DropOldestPendingTransientForceIfFull(*TransientForceStore.Queue, false);

	FKawaiiPhysicsTransientExternalForce Entry;
	Entry.Force = MoveTemp(InForce);
	Entry.RemainingLifetime = InLifetimeSeconds;
	Entry.HandleId = HandleId;

	TransientForceStore.Queue->PendingForces.Emplace(MoveTemp(Entry));
	return HandleId;
}

int64 FAnimNode_KawaiiPhysics::RequestTransientGust(const float Strength, const float RiseTime, const float DecayTime,
                                                    const FVector& GustDirection, const int32 InheritForceIndex,
                                                    const float HoldTime, const int64 InHandleId,
                                                    const bool bRealTimeEnvelope)
{
	FKawaiiPhysicsTransientGustRequest Request;
	Request.Strength = Strength;
	Request.RiseTime = RiseTime;
	Request.DecayTime = DecayTime;
	Request.HoldTime = HoldTime;
	Request.Direction = GustDirection;
	Request.InheritForceIndex = InheritForceIndex;
	Request.HandleId = InHandleId;
	Request.bRealTimeEnvelope = bRealTimeEnvelope;

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	DropOldestPendingTransientForceIfFull(*TransientForceStore.Queue, true);
	TransientForceStore.Queue->PendingGusts.Emplace(Request);
	return InHandleId;
}

void FAnimNode_KawaiiPhysics::RequestStopTransientExternalForce(const int64 HandleId, const float BlendOutTime)
{
	if (HandleId == 0 || !TransientForceStore.Queue.IsValid())
	{
		return;
	}

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	for (FKawaiiPhysicsTransientForceStopRequest& PendingStop : TransientForceStore.Queue->PendingStops)
	{
		if (PendingStop.HandleId == HandleId)
		{
			PendingStop.BlendOutTime = BlendOutTime;
			return;
		}
	}

	if (TransientForceStore.Queue->PendingStops.Num() >= MaxTransientExternalForces)
	{
		// 評価が走らないノードへの連打でも停止要求が MaxTransientExternalForces を超えないよう最古から破棄する。
		TransientForceStore.Queue->PendingStops.RemoveAt(0);
	}

	FKawaiiPhysicsTransientForceStopRequest Request;
	Request.HandleId = HandleId;
	Request.BlendOutTime = BlendOutTime;
	TransientForceStore.Queue->PendingStops.Emplace(Request);
}

int64 FAnimNode_KawaiiPhysics::RequestStartPhysicsSettingsMultiplier(const FKawaiiPhysicsSettingsMultiplier& InScale,
                                                              const float RiseTime, const float HoldTime,
                                                              const float DecayTime, const int64 InHandleId,
                                                              const bool bInfiniteHold)
{
	const int64 HandleId = InHandleId != 0 ? InHandleId : GenerateTransientHandleId();

	FKawaiiPhysicsSettingsMultiplierRequest Request;
	Request.Scale = InScale;
	Request.RiseTime = RiseTime;
	Request.HoldTime = HoldTime;
	Request.DecayTime = DecayTime;
	Request.HandleId = HandleId;
	Request.bInfiniteHold = bInfiniteHold;

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	if (TransientForceStore.Queue->PendingSettingsMultipliers.Num() >= MaxPhysicsSettingsMultipliers)
	{
		// 評価が走らないノードへの連打でも pending が MaxPhysicsSettingsMultipliers を超えないよう最古から破棄する。
		TransientForceStore.Queue->PendingSettingsMultipliers.RemoveAt(0);
	}

	TransientForceStore.Queue->PendingSettingsMultipliers.Emplace(Request);
	return HandleId;
}

bool FAnimNode_KawaiiPhysics::RequestPushPhysicsSettingsMultiplier(const FKawaiiPhysicsSettingsMultiplier& InScale,
                                                                const float InAlpha, const int64 InHandleId,
                                                                const int32 LeaseEvaluations,
                                                                const float LeaseExpireBlendOutTime)
{
	if (InHandleId == 0 || !TransientForceStore.Queue.IsValid())
	{
		return false;
	}

	FKawaiiPhysicsSettingsMultiplierPushRequest Request;
	Request.Scale = InScale;
	Request.Alpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
	Request.HandleId = InHandleId;
	Request.LeaseEvaluations = FMath::Max(0, LeaseEvaluations);
	Request.LeaseExpireBlendOutTime = FMath::Max(0.0f, LeaseExpireBlendOutTime);

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	TransientForceStore.Queue->PendingSettingsMultiplierStops.RemoveAll(
		[InHandleId](const FKawaiiPhysicsTransientForceStopRequest& PendingStop)
		{
			return PendingStop.HandleId == InHandleId;
		});
	TransientForceStore.Queue->PendingSettingsMultipliers.RemoveAll(
		[InHandleId](const FKawaiiPhysicsSettingsMultiplierRequest& PendingOverride)
		{
			return PendingOverride.HandleId == InHandleId;
		});

	for (FKawaiiPhysicsSettingsMultiplierPushRequest& PendingSet : TransientForceStore.Queue->PendingSettingsMultiplierPushes)
	{
		if (PendingSet.HandleId == InHandleId)
		{
			PendingSet = Request;
			return true;
		}
	}

	if (TransientForceStore.Queue->PendingSettingsMultiplierPushes.Num() >= MaxPhysicsSettingsMultipliers)
	{
		// 評価が走らないノードへの連打でも Push 要求が MaxPhysicsSettingsMultipliers を超えないよう最古から破棄する。
		TransientForceStore.Queue->PendingSettingsMultiplierPushes.RemoveAt(0);
	}

	TransientForceStore.Queue->PendingSettingsMultiplierPushes.Emplace(Request);
	return true;
}

void FAnimNode_KawaiiPhysics::RequestStopPhysicsSettingsMultiplier(const int64 HandleId, const float BlendOutTime)
{
	if (HandleId == 0 || !TransientForceStore.Queue.IsValid())
	{
		return;
	}

	FScopeLock Lock(&TransientForceStore.Queue->Mutex);
	for (FKawaiiPhysicsTransientForceStopRequest& PendingStop : TransientForceStore.Queue->PendingSettingsMultiplierStops)
	{
		if (PendingStop.HandleId == HandleId)
		{
			PendingStop.BlendOutTime = BlendOutTime;
			return;
		}
	}

	if (TransientForceStore.Queue->PendingSettingsMultiplierStops.Num() >= MaxPhysicsSettingsMultipliers)
	{
		// 評価が走らないノードへの連打でも停止要求が MaxPhysicsSettingsMultipliers を超えないよう最古から破棄する。
		TransientForceStore.Queue->PendingSettingsMultiplierStops.RemoveAt(0);
	}

	FKawaiiPhysicsTransientForceStopRequest Request;
	Request.HandleId = HandleId;
	Request.BlendOutTime = BlendOutTime;
	TransientForceStore.Queue->PendingSettingsMultiplierStops.Emplace(Request);
}

void FAnimNode_KawaiiPhysics::ResetTransientRuntimeState()
{
	TransientForceStore.Items.Reset();
	TransientForceStore.SettingsMultiplierItems.Reset();
#if !UE_BUILD_SHIPPING
	WarnedInfiniteHoldSettingsMultiplierEvictionHandleIds.Reset();
#endif
	bPhysicsSettingsMultiplierAppliedLastUpdate = false;
	if (TransientForceStore.Queue.IsValid())
	{
		FScopeLock Lock(&TransientForceStore.Queue->Mutex);
		TransientForceStore.Queue->PendingForces.Reset();
		TransientForceStore.Queue->PendingGusts.Reset();
		TransientForceStore.Queue->PendingStops.Reset();
		TransientForceStore.Queue->PendingSettingsMultipliers.Reset();
		TransientForceStore.Queue->PendingSettingsMultiplierPushes.Reset();
		TransientForceStore.Queue->PendingSettingsMultiplierStops.Reset();
	}
}

void FAnimNode_KawaiiPhysics::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);

	DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_KawaiiPhysics::GatherDebugData(FNodeDebugData& DebugData)
{
#if ENABLE_ANIM_DEBUG
	// TODO
#endif
	Super::GatherDebugData(DebugData);
}

bool FAnimNode_KawaiiPhysics::ShouldReinitModifyBones() const
{
	// 明示的なreinit要求
	if (bModifyBonesNeedsReinit)
	{
		return true;
	}

	// 配置トポロジ（生成されるダミー数）を左右する設定の変更
	if (LastInitializedBoneSubdivisionCount != BoneSubdivisionCount ||
		LastInitializedBoneConstraintSubdivisionCount != BoneConstraintSubdivisionCount ||
		LastInitializedBoneSubdivisionDensifyByRadius != bBoneSubdivisionDensifyByRadius)
	{
		return true;
	}

	// Densify=true時のみ、node Radius変更でダミー数が変わる
	if (bBoneSubdivisionDensifyByRadius && !FMath::IsNearlyEqual(LastInitializedRadius, PhysicsSettings.Radius))
	{
		return true;
	}

	// DummyBoneLength変更
	if (!FMath::IsNearlyEqual(LastInitializedDummyBoneLength, DummyBoneLength))
	{
		return true;
	}

	return false;
}

void FAnimNode_KawaiiPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                                TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Eval);

	check(OutBoneTransforms.Num() == 0);

	// ランタイム変更（BP setter等でのreinit要求）時の遅延リセット。ポーズに依存せず、無効ルートボーン等での
	// 早期returnより前に必ず実行する（無効化/タグクリア時に旧Source Slotと古いマージ配列を確実に始末するため）。
	if (bSharedCollisionNeedsReinit)
	{
		// 旧Slotを即座に期限切れ化（Targetが無効化したSourceのコリジョンを次フレームで参照しないように）
		if (CachedSourceSlot.IsValid())
		{
			CachedSourceSlot->MarkExpired();
		}
		bSharedCollisionInitialized = false;
		CachedSharedCollisionEntry.Reset();
		CachedSourceSlot.Reset();
		SharedCollisionInitRetryCount = 0;
		bSharedCollisionInitWarningLogged = false;
		bSharedCollisionNeedsReinit = false;

		// マージ済み共有コリジョン配列もクリア。無効化/タグクリア後はUpdateSharedCollisionLimitsが呼ばれず
		// 再populateされないため、ここでクリアしないと古いコリジョン形状がSimulateで使われ続ける。
		SharedCollisionMergedData.Reset();
		SharedSphericalLimits.Reset();
		SharedCapsuleLimits.Reset();
		SharedTaperedCapsuleLimits.Reset();
		SharedBoxLimits.Reset();
		SharedPlanarLimits.Reset();
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

	// 前フレームの BaseBoneSpace2ComponentSpace を保存
	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
	{
		if (SimulationBaseBone.IsValidToEvaluate(BoneContainer))
		{
			PrevBaseBoneSpace2ComponentSpace = CurrentEvalSimSpaceCache.TargetSpaceToComponent;
		}
		else
		{
			PrevBaseBoneSpace2ComponentSpace = FTransform::Identity;
			KAWAII_LOG_NODE_WARNING_ONCE(bSimBaseBoneInvalidWarned, LogKawaiiPhysics,
				TEXT("SimulationBaseBone [%s] is invalid. Reverting to Identity transform."),
				*SimulationBaseBone.BoneName.ToString());
		}
	}

	// PrevBaseBoneSpace2ComponentSpace の更新後に評価ごとのキャッシュを構築する
	CurrentEvalSimSpaceCache = BuildSimulationSpaceCache(Output, SimulationSpace);
	bHasCurrentEvalSimSpaceCache = true;
	CurrentEvalWorldSpaceCache = BuildSimulationSpaceCache(Output, EKawaiiPhysicsSimulationSpace::WorldSpace);
	bHasCurrentEvalWorldSpaceCache = true;

	BoneSubdivisionCount = FMath::Clamp(BoneSubdivisionCount, 0, 10);
	BoneConstraintSubdivisionCount = FMath::Clamp(BoneConstraintSubdivisionCount, 0, 10);
	BoneConstraintSubdivisionFeedbackScale = FMath::Clamp(BoneConstraintSubdivisionFeedbackScale, 0.0f, 2.0f);
	DummyBoneLength = FMath::Max(DummyBoneLength, 0.0f);

	if (TeleportType == ETeleportType::ResetPhysics)
	{
		ModifyBones.Empty(ModifyBones.Num());
		TeleportType = ETeleportType::None;
		bInitPhysicsSettings = false;
		bModifyBonesNeedsReinit = false;
	}

	if (SimulationSpace != LastSimulationSpace)
	{
		ConvertSimulationSpace(Output, LastSimulationSpace, SimulationSpace);
	}
	LastSimulationSpace = SimulationSpace;

	if (ModifyBones.Num() > 0 && ShouldReinitModifyBones())
	{
		ModifyBones.Empty(ModifyBones.Num());
		bInitPhysicsSettings = false;
		bModifyBonesNeedsReinit = false;

		// 再構築で新規ボーンのPrevPoseLocationが0に戻りsubstep補間が原点へ引かれチラつくため、
		// flagを戻し次フレーム冒頭で現在ポーズから再開させる
		bSubstepPoseInitialized = false;

		// 再初期化（設定変更）時はノード警告を再通知できるようガードを戻す
		KAWAII_RESET_NODE_WARNING_ONCE(bSimBaseBoneInvalidWarned);
		KAWAII_RESET_NODE_WARNING_ONCE(bMirrorSkeletonMissingWarned);
	}

#if WITH_EDITOR
	// 他のNodeでの編集を同期する
	ApplyLimitsDataAsset(BoneContainer);
	ApplyPhysicsAsset(BoneContainer);
	ApplyMirrorLimits(BoneContainer);
	ApplyBoneConstraintDataAsset(BoneContainer);

	// ライブ編集用（コンパイル前に同期）
	if (GUnrealEd && !GUnrealEd->IsPlayingSessionInEditor())
	{
		InitializeBoneReferences(BoneContainer);
	}

#endif

	if (!RootBone.IsValidToEvaluate(BoneContainer))
	{
		return;
	}
	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		if (!AdditionalRootBone.RootBone.IsValidToEvaluate(BoneContainer))
		{
			return;
		}
	}

	if (ModifyBones.Num() == 0)
	{
		InitModifyBones(Output, BoneContainer);
		InitSyncBones(Output);
		InitBoneConstraints();
		LastInitializedBoneSubdivisionCount = BoneSubdivisionCount;
		LastInitializedBoneConstraintSubdivisionCount = BoneConstraintSubdivisionCount;
		LastInitializedBoneSubdivisionDensifyByRadius = bBoneSubdivisionDensifyByRadius;
		LastInitializedRadius = PhysicsSettings.Radius;
		LastInitializedDummyBoneLength = DummyBoneLength;
		bModifyBonesNeedsReinit = false;
		PreSkelCompTransform = ComponentTransform;

#if !UE_BUILD_SHIPPING
		// STAT更新 & パフォーマンス警告
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumModifyBones, ModifyBones.Num());
		int32 InterBoneDummyCount = 0;
		int32 BridgeDummyCount = 0;
		for (const auto& Bone : ModifyBones)
		{
			if (Bone.bInterBoneDummy)
			{
				InterBoneDummyCount++;
			}
			if (Bone.bBridgeDummy)
			{
				BridgeDummyCount++;
			}
		}
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumInterBoneDummyBones, InterBoneDummyCount);
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumBridgeDummyBones, BridgeDummyCount);
		
#if WITH_EDITORONLY_DATA
		int32 InterBoneDummyWarningThreshold = 100;
		int32 BridgeDummyWarningThreshold = 200;

		if (const UKawaiiPhysicsDeveloperSettings* KawaiiSettings = GetDefault<UKawaiiPhysicsDeveloperSettings>())
		{
			InterBoneDummyWarningThreshold = KawaiiSettings->InterBoneDummyWarningThreshold;
			BridgeDummyWarningThreshold = KawaiiSettings->BridgeDummyWarningThreshold;
		}

		if (InterBoneDummyWarningThreshold > 0 && InterBoneDummyCount > InterBoneDummyWarningThreshold)
		{
			KAWAII_LOG_NODE_WARNING(LogAnimation,
				TEXT("KawaiiPhysics: %d inter-bone dummy bones generated (warning threshold: %d). This may impact performance. Consider reducing BoneSubdivisionCount or raising InterBoneDummyWarningThreshold in Kawaii Physics project settings."),
				InterBoneDummyCount, InterBoneDummyWarningThreshold);
		}
		if (BridgeDummyWarningThreshold > 0 && BridgeDummyCount > BridgeDummyWarningThreshold)
		{
			KAWAII_LOG_NODE_WARNING(LogAnimation,
				TEXT("KawaiiPhysics: %d bridge collision-proxy dummy bones and %d merged bone constraints generated (warning threshold: %d). This may impact performance. Consider reducing BoneConstraintSubdivisionCount or raising BridgeDummyWarningThreshold in Kawaii Physics project settings."),
				BridgeDummyCount, MergedBoneConstraints.Num(), BridgeDummyWarningThreshold);
		}
#endif
#endif

	}

	// 倍率は UpdatePhysicsSettingsOfModifyBones より前に取り込み、このフレームの倍率を確定させる
	const bool bHasActiveSettingsMultiplier = ConsumeAndAdvancePhysicsSettingsMultipliers(DeltaTime);

	// 各パラメータとコリジョンを更新する
	if (ShouldUpdatePhysicsSettings(bHasActiveSettingsMultiplier))
	{
		UpdatePhysicsSettingsOfModifyBones();
		bPhysicsSettingsMultiplierAppliedLastUpdate = bHasActiveSettingsMultiplier;

#if WITH_EDITORONLY_DATA
		if (!bEditing)
#endif
		{
			bInitPhysicsSettings = true;
		}
	}

	// 各コリジョンの更新
	UpdateSphericalLimits(SphericalLimits, Output, BoneContainer, ComponentTransform);
	UpdateSphericalLimits(SphericalLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimits, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateTaperedCapsuleLimits(TaperedCapsuleLimits, Output, BoneContainer, ComponentTransform);
	UpdateTaperedCapsuleLimits(TaperedCapsuleLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimits, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimitsData, Output, BoneContainer, ComponentTransform);
	UpdatePlanarLimits(PlanarLimits, Output, BoneContainer, ComponentTransform);
	UpdatePlanarLimits(PlanarLimitsData, Output, BoneContainer, ComponentTransform);

	// 共有コリジョンの初期化と更新（有効時のみ）。reinit処理は関数冒頭で実行済み。
	// subsystemはロックでスレッドセーフ化済みのためWorker(AnyThread)で実行でき、PreUpdate(GameThread)を介さない。
	// これによりランタイム有効化(BP setter)も全ビルド構成で正しく動作する。
	if ((bSharedCollisionSource || bUseSharedCollision) && SharedCollisionGroupTag.IsValid())
	{
		// 初期化（未初期化時のみ。Subsystem側のロックでWorkerから安全に呼べる）
		if (!bSharedCollisionInitialized)
		{
			const int32 RetryThreshold = CVarSharedCollisionInitRetryThreshold.GetValueOnAnyThread();
			const int32 ThrottleInterval = FMath::Max(1, CVarSharedCollisionInitRetryThrottleInterval.GetValueOnAnyThread());

			if (!bSharedCollisionInitWarningLogged)
			{
				// 警告前: 毎フレーム試行（正常な起動ウィンドウでの即接続を維持）。しきい値到達で1回だけ警告し間引きへ移行
				InitializeSharedCollision();
				if (!bSharedCollisionInitialized)
				{
					SharedCollisionInitRetryCount++;
					if (SharedCollisionInitRetryCount > RetryThreshold)
					{
						KAWAII_LOG_NODE_WARNING(LogKawaiiPhysics,
							TEXT("SharedCollision: Target could not find source entry for tag [%s]. "
								"Ensure a source node with matching tag exists in the same actor/child-actor family."),
							*SharedCollisionGroupTag.ToString());
						bSharedCollisionInitWarningLogged = true;
						SharedCollisionInitRetryCount = 0; // 間引きフェーズ用にカウンタを0起点で再利用
					}
				}
			}
			else
			{
				// 警告後(Sourceが見つからない誤設定の可能性大): ThrottleInterval間隔で試行。カウンタは[0, ThrottleInterval]に
				// 収まりオーバーフローしない。誤設定タグでの毎フレームのfamily-root walk/registry lock取得を削減しつつ、
				// Sourceが後から現れた場合の接続は維持する（最大ThrottleInterval遅れ）。
				if (++SharedCollisionInitRetryCount >= ThrottleInterval)
				{
					SharedCollisionInitRetryCount = 0;
					InitializeSharedCollision();
				}
			}
		}

		// Target: 全Sourceのコリジョンをマージして取得
		if (bUseSharedCollision && !bSharedCollisionSource && CachedSharedCollisionEntry.IsValid())
		{
			UpdateSharedCollisionLimits(Output);
		}
	}

	// シンプルワールドコリジョン（Subsystemが収集したレベル上のsimple collisionをWorkerから読むだけ）
	// CVarで全体無効化されている間はUpdateを行わず、else側でSimpleWorldXxxLimitsをResetして適用もスキップする
	if (bUseSimpleWorldCollision && CVarSimpleWorldCollisionEnable.GetValueOnAnyThread())
	{
		if (!bSimpleWorldCollisionInitialized)
		{
			InitializeSimpleWorldCollision();
		}
		if (CachedSimpleWorldEntry.IsValid() || bSimpleWorldReaderMode)
		{
			UpdateSimpleWorldCollisionLimits(Output);
			if (TeleportType == ETeleportType::TeleportPhysics && CachedSimpleWorldEntry.IsValid())
			{
				CachedSimpleWorldEntry->RequestRegather();
			}
		}
	}
	else if (bSimpleWorldCollisionInitialized || CachedSimpleWorldEntry.IsValid())
	{
		// ランタイム無効化時は自Descを即時解除し、古い押し出しを残さない
		ReleaseSimpleWorldCollision();
		SimpleWorldMergedScratch.Reset();
		SimpleWorldGroundScratch.Reset();
		SimpleWorldSphericalLimits.Reset();
		SimpleWorldCapsuleLimits.Reset();
		SimpleWorldTaperedCapsuleLimits.Reset();
		SimpleWorldBoxLimits.Reset();
		SimpleWorldGroundBoxLimits.Reset();
		SimpleWorldConvexLimits.Reset();
		LastReadSimpleWorldShapeSerial = 0;
		LastReadSimpleWorldGroundSerial = 0;
		LastReadSimpleWorldMemberSerialSum = 0;
		SimpleWorldReaderRetryCount = 0;
		bSimpleWorldReaderWarningLogged = false;
	}

	// 入力規模カウンタ & メモリの更新（毎フレーム。負荷=N×L等の相関とダミー膨張の可視化用）
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumSphereColliders, SphericalLimits.Num() + SphericalLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumCapsuleColliders, CapsuleLimits.Num() + CapsuleLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumTaperedCapsuleColliders,
	               TaperedCapsuleLimits.Num() + TaperedCapsuleLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumBoxColliders, BoxLimits.Num() + BoxLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumPlanarColliders, PlanarLimits.Num() + PlanarLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumSharedColliders,
	               SharedSphericalLimits.Num() + SharedCapsuleLimits.Num() + SharedTaperedCapsuleLimits.Num() +
	               SharedBoxLimits.Num() + SharedPlanarLimits.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumSimpleWorldColliders,
	               SimpleWorldSphericalLimits.Num() + SimpleWorldCapsuleLimits.Num() +
	               SimpleWorldTaperedCapsuleLimits.Num() + SimpleWorldBoxLimits.Num() +
	               SimpleWorldGroundBoxLimits.Num() + SimpleWorldConvexLimits.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumMergedBoneConstraints, MergedBoneConstraints.Num());
	SET_MEMORY_STAT(STAT_KawaiiPhysics_ModifyBonesMemory,
	                ModifyBones.GetAllocatedSize() + MergedBoneConstraints.GetAllocatedSize());

	UpdateModifyBonesPoseTransform(Output, BoneContainer);
	ApplySyncBones(Output, BoneContainer);

	// World SpaceでのSkeletalMeshComponentの移動を更新する
	UpdateSkelCompMove(Output, ComponentTransform);

	// 一時外力は評価ごとに1回だけ取り込み、WarmUpの反復やTeleport時のSimulateスキップに左右されないようにする
	ConsumeAndRemoveExpiredTransientExternalForces(DeltaTime);

	// 物理の荒ぶりを回避するための空回し処理
	if (bNeedWarmUp && WarmUpFrames > 0)
	{
		WarmUp(Output, BoneContainer, ComponentTransform);
		bNeedWarmUp = false;
	}

	// WorldSpaceでテレポートした場合はシミュレートをスキップする
	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::WorldSpace &&
		TeleportType == ETeleportType::TeleportPhysics)
	{
		for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
		{
			FVector PrevLocationCS = PreSkelCompTransform.InverseTransformPosition(Bone.PrevLocation);
			Bone.Location = ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                               SimulationSpace, PrevLocationCS);
			Bone.PrevLocation = Bone.Location;
		}

		// テレポート時はサブステップの未消費時間を破棄し、ポーズ補間を次フレームで再初期化
		SubstepAccumulator = 0.0f;
		bSubstepPoseInitialized = false;
		PreSkelCompTransformConsumeFraction = 1.0f;
	}
	else
	{
		SimulateModifyBones(Output, ComponentTransform);
	}

	// 計算済みコリジョンをSubsystemに書き込み
	if (bSharedCollisionSource && SharedCollisionGroupTag.IsValid() && CachedSourceSlot.IsValid())
	{
		WriteSharedCollisionToSubsystem(Output, ComponentTransform);
	}

	ApplySimulateResult(Output, BoneContainer, OutBoneTransforms);

	TeleportType = ETeleportType::None;
	// サブステップで未消費の実時間がある場合、PreSkelCompTransform を消費割合だけ前進させ、
	// 未適用のComponent移動を次にステップが走るフレームへ繰り越す（NumSteps==0 では割合0で据え置き）。
	const float PreSkelCompConsumeFrac = FMath::Clamp(PreSkelCompTransformConsumeFraction, 0.0f, 1.0f);
	if (PreSkelCompConsumeFrac >= 1.0f - KINDA_SMALL_NUMBER)
	{
		PreSkelCompTransform = ComponentTransform;
	}
	else
	{
		PreSkelCompTransform.SetLocation(
			FMath::Lerp(PreSkelCompTransform.GetLocation(), ComponentTransform.GetLocation(), PreSkelCompConsumeFrac));
		PreSkelCompTransform.SetRotation(
			FQuat::Slerp(PreSkelCompTransform.GetRotation(), ComponentTransform.GetRotation(), PreSkelCompConsumeFrac).GetNormalized());
		PreSkelCompTransform.SetScale3D(
			FMath::Lerp(PreSkelCompTransform.GetScale3D(), ComponentTransform.GetScale3D(), PreSkelCompConsumeFrac));
	}

#if ENABLE_ANIM_DEBUG

	AnimDrawDebug(Output);

#endif

#if WITH_EDITORONLY_DATA
	LastEvaluatedTime = FPlatformTime::Seconds();
#endif
}

bool FAnimNode_KawaiiPhysics::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
#if ENABLE_ANIM_DEBUG
	if (!CVarAnimNodeKawaiiPhysicsEnable.GetValueOnAnyThread())
	{
		return false;
	}
#endif

	if (!RootBone.BoneName.IsValid())
	{
		return false;
	}

	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		if (!AdditionalRootBone.RootBone.BoneName.IsValid())
		{
			return false;
		}
	}

	return true;
}

void FAnimNode_KawaiiPhysics::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	FAnimNode_SkeletalControlBase::OnInitializeAnimInstance(InProxy, InAnimInstance);

	CachedSimpleWorldCollisionSkelComp.Reset();

	// 共有コリジョン初期化で使うSubsystemとowner ActorをGameThreadで1回だけ解決してキャッシュする。
	// （Evaluate(AnyThread)でのGetWorld/GetSubsystem/GetOwner回避。ファミリーrootはアタッチ変更追従のためEvaluate側でownerから都度解決）
	if (InAnimInstance)
	{
		if (const UWorld* World = InAnimInstance->GetWorld())
		{
			CachedSharedCollisionSubsystem = World->GetSubsystem<UKawaiiPhysicsSharedCollisionSubsystem>();
		}
		if (const USkeletalMeshComponent* SkelComp = InAnimInstance->GetSkelMeshComponent())
		{
			CachedSharedCollisionOwnerActor = SkelComp->GetOwner();
			CachedSimpleWorldCollisionSkelComp = SkelComp;
		}
	}

#if !UE_BUILD_SHIPPING
	// 警告ログ用のノード識別名を収集（名前はノード生存中に不変なのでGameThread初期化時に1回だけ取得）
	if (InAnimInstance)
	{
		CachedAnimInstanceClassName = InAnimInstance->GetClass()->GetFName();
		if (const USkeletalMeshComponent* SkelComp = InAnimInstance->GetSkelMeshComponent())
		{
			CachedComponentName = SkelComp->GetFName();
			const AActor* OwnerActor = SkelComp->GetOwner();
			CachedOwnerActorName = OwnerActor ? OwnerActor->GetFName() : NAME_None;
		}
	}
#endif

#if WITH_EDITOR
	if (InAnimInstance)
	{
		if (const UWorld* World = InAnimInstance->GetWorld())
		{
			if (World->WorldType == EWorldType::Editor ||
				World->WorldType == EWorldType::EditorPreview)
			{
				bEditing = true;
			}
		}
	}
#endif
}

const FVector& FAnimNode_KawaiiPhysics::GetSkelCompMoveVector() const
{
	return this->SkelCompMoveVector;
}

const FQuat& FAnimNode_KawaiiPhysics::GetSkelCompMoveRotation() const
{
	return this->SkelCompMoveRotation;
}

float FAnimNode_KawaiiPhysics::GetDeltaTimeOld() const
{
	return this->DeltaTimeOld;
}

FVector FAnimNode_KawaiiPhysics::GetBoneForwardVector(const FQuat& Rotation) const
{
	switch (BoneForwardAxis)
	{
	default:
	case EBoneForwardAxis::X_Positive:
		return Rotation.GetAxisX();
	case EBoneForwardAxis::X_Negative:
		return -Rotation.GetAxisX();
	case EBoneForwardAxis::Y_Positive:
		return Rotation.GetAxisY();
	case EBoneForwardAxis::Y_Negative:
		return -Rotation.GetAxisY();
	case EBoneForwardAxis::Z_Positive:
		return Rotation.GetAxisZ();
	case EBoneForwardAxis::Z_Negative:
		return -Rotation.GetAxisZ();
	}
}
