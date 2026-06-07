// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "KawaiiPhysicsSharedCollisionSubsystem.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsSettings.h"

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
#include "Animation/AnimInstance.h"
#endif

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#include "KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysicsInternal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_KawaiiPhysics)

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
TAutoConsoleVariable<float> CVarSharedCollisionCleanupInterval(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.CleanupInterval"), 1.0f,
	TEXT("クリーンアップ間隔（秒） / Cleanup interval in seconds."));
TAutoConsoleVariable<bool> CVarSharedCollisionUseLockFree(
	TEXT("a.AnimNode.KawaiiPhysics.SharedCollision.UseLockFree"), false,
	TEXT("Use the lock-free double-buffer path for SharedCollision slots. false uses short per-slot locks for safer reads/writes."));

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
DEFINE_STAT(STAT_KawaiiPhysics_UpdatePlanerLimit);
DEFINE_STAT(STAT_KawaiiPhysics_WarmUp);
DEFINE_STAT(STAT_KawaiiPhysics_UpdatePhysicsSetting);
DEFINE_STAT(STAT_KawaiiPhysics_UpdateCapsuleLimit);
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
DEFINE_STAT(STAT_KawaiiPhysics_NumModifyBones);
DEFINE_STAT(STAT_KawaiiPhysics_NumInterBoneDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_NumBridgeDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_InsertInterBoneDummyBones);
DEFINE_STAT(STAT_KawaiiPhysics_BridgeDummy);
DEFINE_STAT(STAT_KawaiiPhysics_AdjustByLimitsAndLength);
DEFINE_STAT(STAT_KawaiiPhysics_PreUpdate);
DEFINE_STAT(STAT_KawaiiPhysics_NumSphereColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumCapsuleColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumBoxColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumPlanarColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumSharedColliders);
DEFINE_STAT(STAT_KawaiiPhysics_NumMergedBoneConstraints);
DEFINE_STAT(STAT_KawaiiPhysics_NumWorldCollisionChecks);
DEFINE_STAT(STAT_KawaiiPhysics_ModifyBonesMemory);

FAnimNode_KawaiiPhysics::FAnimNode_KawaiiPhysics()
{
}

void FAnimNode_KawaiiPhysics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	SphericalLimitsData.Empty();
	CapsuleLimitsData.Empty();
	BoxLimitsData.Empty();
	PlanarLimitsData.Empty();

	// 旧Slotを即座に期限切れ化 / Mark old slot as immediately expired
	if (CachedSourceSlot.IsValid())
	{
		CachedSourceSlot->LastPublishFrame.store(0, std::memory_order_release);
	}

	// 共有コリジョンのキャッシュをリセット / Reset shared collision cache
	bSharedCollisionInitialized = false;
	CachedSharedCollisionEntry.Reset();
	CachedSourceSlot.Reset();
	SharedCollisionInitRetryCount = 0;
	bSharedCollisionInitWarningLogged = false;
	bSharedCollisionNeedsReinit = false;
	bModifyBonesNeedsReinit = false;

	// 共有コリジョンワーク配列をリセット / Reset shared collision working arrays
	SharedCollisionMergedData.Reset();
	SharedSphericalLimits.Reset();
	SharedCapsuleLimits.Reset();
	SharedBoxLimits.Reset();
	SharedPlanarLimits.Reset();

	ApplyLimitsDataAsset(RequiredBones);
	ApplyPhysicsAsset(RequiredBones);
	ApplyBoneConstraintDataAsset(RequiredBones);

	ModifyBones.Empty();

	// For Avoiding Zero Divide in the first frame
	DeltaTimeOld = 1.0f / static_cast<float>(GetEffectiveTargetFramerate());

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

#if ENABLE_ANIM_DEBUG
void FAnimNode_KawaiiPhysics::AnimDrawDebug(FComponentSpacePoseContext& Output)
{
	if (const UWorld* World = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetWorld(); !World->IsPreviewWorld())
	{
		if (Output.AnimInstanceProxy->GetSkelMeshComponent()->bRecentlyRendered)
		{
			if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
			{
				const auto AnimInstanceProxy = Output.AnimInstanceProxy;
				const float LineThickness = FMath::Max(
					0.0f, CVarAnimNodeKawaiiPhysicsDebugDrawThickness.GetValueOnAnyThread());

				// Modify Bones
				for (const auto& ModifyBone : ModifyBones)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace, ModifyBone.Location);

					auto Color = ModifyBone.bBridgeDummy
					             ? FColor::Green
					             : (ModifyBone.bInterBoneDummy ? FColor::Cyan : (ModifyBone.bDummy ? FColor::Red : FColor::Yellow));
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, ModifyBone.PhysicsSettings.Radius, 8,
					                                       Color, false, -1, LineThickness, SDPG_Foreground);

					AnimInstanceProxy->AnimDrawDebugInWorldMessage(
						FString::Printf(TEXT("%.2f"), ModifyBone.LengthRateFromRoot),
						ModifyBone.Location, FColor::White, 1.0f);
					
				}
				// Sphere limit
				for (const auto& SphericalLimit : SphericalLimits)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SphericalLimit.Location);

					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Orange,
					                                       false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& SphericalLimit : SphericalLimitsData)
				{
					const FVector LocationWS =
						ConvertSimulationSpaceLocation(Output, SimulationSpace,
						                               EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SphericalLimit.Location);
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Blue,
					                                       false, -1, LineThickness, SDPG_Foreground);
				}

				// Box limit
				for (const auto& BoxLimit : BoxLimits)
				{
					this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
					                       FColor::Orange, LineThickness);
				}
				for (const auto& BoxLimit : BoxLimitsData)
				{
					this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
					                       FColor::Blue, LineThickness);
				}

				// Planar limit
				for (const auto& PlanarLimit : PlanarLimits)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
					AnimInstanceProxy->AnimDrawDebugPlane(TransformWS, 50.0f,
					                                      FColor::Orange, false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& PlanarLimit : PlanarLimitsData)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
					AnimInstanceProxy->AnimDrawDebugPlane(TransformWS, 50.0f,
					                                      FColor::Blue, false, -1, LineThickness, SDPG_Foreground);
				}

#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				// Capsule limit
				for (const auto& CapsuleLimit : CapsuleLimits)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));

					AnimInstanceProxy->AnimDrawDebugCapsule(TransformWS.GetTranslation(), CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, TransformWS.GetRotation().Rotator(),
					                                        FColor::Orange, false, -1, LineThickness, SDPG_Foreground);
				}
				for (const auto& CapsuleLimit : CapsuleLimitsData)
				{
					FTransform TransformWS =
						ConvertSimulationSpaceTransform(Output, SimulationSpace,
						                                EKawaiiPhysicsSimulationSpace::WorldSpace,
						                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));

					AnimInstanceProxy->AnimDrawDebugCapsule(TransformWS.GetTranslation(), CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, TransformWS.GetRotation().Rotator(),
					                                        FColor::Blue, false, -1, LineThickness, SDPG_Foreground);
				}
#endif

				// Shared collision limits (green) / 共有コリジョン（緑）
				if (bUseSharedCollision && !bSharedCollisionSource)
				{
					for (const auto& SphericalLimit : SharedSphericalLimits)
					{
						const FVector LocationWS =
							ConvertSimulationSpaceLocation(Output, SimulationSpace,
							                               EKawaiiPhysicsSimulationSpace::WorldSpace,
							                               SphericalLimit.Location);
						AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Green,
						                                       false, -1, LineThickness, SDPG_Foreground);
					}

					for (const auto& BoxLimit : SharedBoxLimits)
					{
						this->AnimDrawDebugBox(Output, BoxLimit.Location, BoxLimit.Rotation, BoxLimit.Extent,
						                       FColor::Green, LineThickness);
					}

					for (const auto& PlanarLimit : SharedPlanarLimits)
					{
						FTransform PlanarTransformWS =
							ConvertSimulationSpaceTransform(Output, SimulationSpace,
							                                EKawaiiPhysicsSimulationSpace::WorldSpace,
							                                FTransform(PlanarLimit.Rotation, PlanarLimit.Location));
						AnimInstanceProxy->AnimDrawDebugPlane(PlanarTransformWS, 50.0f,
						                                      FColor::Green, false, -1, LineThickness, SDPG_Foreground);
					}

#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
					for (const auto& CapsuleLimit : SharedCapsuleLimits)
					{
						FTransform CapsuleTransformWS =
							ConvertSimulationSpaceTransform(Output, SimulationSpace,
							                                EKawaiiPhysicsSimulationSpace::WorldSpace,
							                                FTransform(CapsuleLimit.Rotation, CapsuleLimit.Location));
						AnimInstanceProxy->AnimDrawDebugCapsule(CapsuleTransformWS.GetTranslation(),
						                                        CapsuleLimit.Length * 0.5f,
						                                        CapsuleLimit.Radius,
						                                        CapsuleTransformWS.GetRotation().Rotator(),
						                                        FColor::Green, false, -1, LineThickness,
						                                        SDPG_Foreground);
					}
#endif
				}
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AnimDrawDebugBox(FComponentSpacePoseContext& Output, const FVector& CenterLocationSim,
                                               const FQuat& RotationSim, const FVector& Extent,
                                               const FColor& Color, float LineThickness) const
{
	const auto AnimInstanceProxy = Output.AnimInstanceProxy;
	if (!AnimInstanceProxy)
	{
		return;
	}

	const FVector LocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace,
		                               EKawaiiPhysicsSimulationSpace::WorldSpace, CenterLocationSim);
	const FQuat RotationWS =
		ConvertSimulationSpaceRotation(Output, SimulationSpace,
		                               EKawaiiPhysicsSimulationSpace::WorldSpace, RotationSim);

	const FTransform BoxTransformWS(RotationWS, LocationWS);
	const FVector E(FMath::Abs(Extent.X), FMath::Abs(Extent.Y), FMath::Abs(Extent.Z));

	auto DrawFaceRect = [&](const FVector& FaceCenterLS, const FVector& FaceNormalLS, float HalfWidth, float HalfHeight)
	{
		const FVector FaceCenterWS = BoxTransformWS.TransformPosition(FaceCenterLS);
		const FVector NormalWS = RotationWS.RotateVector(FaceNormalLS).GetSafeNormal();

		const FVector AnyUpWS = (FMath::Abs(NormalWS.Z) < 0.999f) ? FVector::UpVector : FVector::RightVector;
		const FVector XAxisWS = FVector::CrossProduct(AnyUpWS, NormalWS).GetSafeNormal();
		const FVector YAxisWS = FVector::CrossProduct(NormalWS, XAxisWS).GetSafeNormal();

		const FVector P0 = FaceCenterWS + (XAxisWS * HalfWidth) + (YAxisWS * HalfHeight);
		const FVector P1 = FaceCenterWS - (XAxisWS * HalfWidth) + (YAxisWS * HalfHeight);
		const FVector P2 = FaceCenterWS - (XAxisWS * HalfWidth) - (YAxisWS * HalfHeight);
		const FVector P3 = FaceCenterWS + (XAxisWS * HalfWidth) - (YAxisWS * HalfHeight);

		AnimInstanceProxy->AnimDrawDebugLine(P0, P1, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P1, P2, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P2, P3, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
		AnimInstanceProxy->AnimDrawDebugLine(P3, P0, Color, false, -1.0f,
		                                     LineThickness, SDPG_Foreground);
	};

	// +X / -X faces: cover YZ
	DrawFaceRect(FVector(E.X, 0, 0), FVector(1, 0, 0), E.Y, E.Z);
	DrawFaceRect(FVector(-E.X, 0, 0), FVector(-1, 0, 0), E.Y, E.Z);

	// +Y / -Y faces: cover XZ
	DrawFaceRect(FVector(0, E.Y, 0), FVector(0, 1, 0), E.X, E.Z);
	DrawFaceRect(FVector(0, -E.Y, 0), FVector(0, -1, 0), E.X, E.Z);

	// +Z / -Z faces: cover XY
	DrawFaceRect(FVector(0, 0, E.Z), FVector(0, 0, 1), E.X, E.Y);
	DrawFaceRect(FVector(0, 0, -E.Z), FVector(0, 0, -1), E.X, E.Y);
}
#endif

void FAnimNode_KawaiiPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                                TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Eval);

	check(OutBoneTransforms.Num() == 0);

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

	// save prev frame BaseBoneSpace2ComponentSpace
	if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
	{
		if (SimulationBaseBone.IsValidToEvaluate(BoneContainer))
		{
			PrevBaseBoneSpace2ComponentSpace = CurrentEvalSimSpaceCache.TargetSpaceToComponent;
		}
		else
		{
			PrevBaseBoneSpace2ComponentSpace = FTransform::Identity;
			UE_LOG(LogKawaiiPhysics, Warning, TEXT("SimulationBaseBone is invalid. Reverting to Identity transform."));
		}
	}

	// Build per-evaluate caches AFTER PrevBaseBoneSpace2ComponentSpace is updated.
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

	if (ModifyBones.Num() > 0 &&
		(bModifyBonesNeedsReinit ||
		 LastInitializedBoneSubdivisionCount != BoneSubdivisionCount ||
		 LastInitializedBoneConstraintSubdivisionCount != BoneConstraintSubdivisionCount ||
		 LastInitializedBoneSubdivisionCollisionOnly != bBoneSubdivisionCollisionOnly ||
		 !FMath::IsNearlyEqual(LastInitializedDummyBoneLength, DummyBoneLength)))
	{
		ModifyBones.Empty(ModifyBones.Num());
		bInitPhysicsSettings = false;
		bModifyBonesNeedsReinit = false;
	}

#if WITH_EDITOR
	// sync editing on other Nodes
	ApplyLimitsDataAsset(BoneContainer);
	ApplyPhysicsAsset(BoneContainer);
	ApplyBoneConstraintDataAsset(BoneContainer);


	if (GUnrealEd && !GUnrealEd->IsPlayingSessionInEditor())
	{
		// for live editing ( sync before compile )
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
		LastInitializedBoneSubdivisionCollisionOnly = bBoneSubdivisionCollisionOnly;
		LastInitializedDummyBoneLength = DummyBoneLength;
		bModifyBonesNeedsReinit = false;
		PreSkelCompTransform = ComponentTransform;

		// STAT更新 & パフォーマンス警告 / STAT update & performance warning
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumModifyBones, ModifyBones.Num());
		int32 InterBoneDummyCount = 0;
		int32 BridgeDummyCount = 0;
		for (const auto& Bone : ModifyBones)
		{
			if (Bone.bInterBoneDummy) InterBoneDummyCount++;
			if (Bone.bBridgeDummy) BridgeDummyCount++;
		}
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumInterBoneDummyBones, InterBoneDummyCount);
		SET_DWORD_STAT(STAT_KawaiiPhysics_NumBridgeDummyBones, BridgeDummyCount);
		if (InterBoneDummyCount > 50)
		{
			UE_LOG(LogAnimation, Warning,
				TEXT("KawaiiPhysics: %d inter-bone dummy bones generated. This may impact performance. Consider reducing BoneSubdivisionCount."),
				InterBoneDummyCount);
		}
		if (BridgeDummyCount > 100)
		{
			UE_LOG(LogAnimation, Warning,
				TEXT("KawaiiPhysics: %d bridge collision-proxy dummy bones and %d merged bone constraints generated. This may impact performance. Consider reducing BoneConstraintSubdivisionCount."),
				BridgeDummyCount, MergedBoneConstraints.Num());
		}

	}

	// Update each parameter and collision
	if (!bInitPhysicsSettings || bUpdatePhysicsSettingsInGame)
	{
		UpdatePhysicsSettingsOfModifyBones();

#if WITH_EDITORONLY_DATA
		if (!bEditing)
#endif
		{
			bInitPhysicsSettings = true;
		}
	}
	UpdateSphericalLimits(SphericalLimits, Output, BoneContainer, ComponentTransform);
	UpdateSphericalLimits(SphericalLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimits, Output, BoneContainer, ComponentTransform);
	UpdateCapsuleLimits(CapsuleLimitsData, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimits, Output, BoneContainer, ComponentTransform);
	UpdateBoxLimits(BoxLimitsData, Output, BoneContainer, ComponentTransform);
	UpdatePlanerLimits(PlanarLimits, Output, BoneContainer, ComponentTransform);
	UpdatePlanerLimits(PlanarLimitsData, Output, BoneContainer, ComponentTransform);

	// 共有コリジョンの更新（初期化はPreUpdateでGameThread実行済み）
	// Update shared collision (initialization done in PreUpdate on GameThread)
	if ((bSharedCollisionSource || bUseSharedCollision) && SharedCollisionGroupTag.IsValid())
	{
		if (bUseSharedCollision && !bSharedCollisionSource && CachedSharedCollisionEntry.IsValid())
		{
			UpdateSharedCollisionLimits(Output);
		}
	}

	// 入力規模カウンタ & メモリの更新（毎フレーム。負荷=N×L等の相関とダミー膨張の可視化用）。
	// コライダ配列はここまでに更新済み（Update*Limits / UpdateSharedCollisionLimits）。
	// 注: 既存のNum*系と同じくSET（複数ノード時は最後にSETしたノード値が表示される）。
	// Per-frame input-size counters & memory (correlate load = N×L, visualize dummy growth).
	// Collider arrays are already updated above (Update*Limits / UpdateSharedCollisionLimits).
	// Note: SET semantics like the existing Num* stats (last node wins when multiple nodes exist).
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumSphereColliders, SphericalLimits.Num() + SphericalLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumCapsuleColliders, CapsuleLimits.Num() + CapsuleLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumBoxColliders, BoxLimits.Num() + BoxLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumPlanarColliders, PlanarLimits.Num() + PlanarLimitsData.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumSharedColliders,
	               SharedSphericalLimits.Num() + SharedCapsuleLimits.Num() + SharedBoxLimits.Num() +
	               SharedPlanarLimits.Num());
	SET_DWORD_STAT(STAT_KawaiiPhysics_NumMergedBoneConstraints, MergedBoneConstraints.Num());
	SET_MEMORY_STAT(STAT_KawaiiPhysics_ModifyBonesMemory,
	                ModifyBones.GetAllocatedSize() + MergedBoneConstraints.GetAllocatedSize());

	// Update Bone Pose Transform
	UpdateModifyBonesPoseTransform(Output, BoneContainer);

	// Apply Sync Bones
	ApplySyncBones(Output, BoneContainer);

	// Update SkeletalMeshComponent movement in World Space
	UpdateSkelCompMove(Output, ComponentTransform);

	// Simulate Physics and Apply
	if (bNeedWarmUp && WarmUpFrames > 0)
	{
		WarmUp(Output, BoneContainer, ComponentTransform);
		bNeedWarmUp = false;
	}

	// SkipSimulate if Teleport in WorldSpace
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
	}
	else
	{
		SimulateModifyBones(Output, ComponentTransform);
	}

	// 計算済みコリジョンをSubsystemに書き込み / Write computed collision to subsystem
	if (bSharedCollisionSource && SharedCollisionGroupTag.IsValid() && CachedSourceSlot.IsValid())
	{
		WriteSharedCollisionToSubsystem(Output, ComponentTransform);
	}

	ApplySimulateResult(Output, BoneContainer, OutBoneTransforms);

	TeleportType = ETeleportType::None;
	PreSkelCompTransform = ComponentTransform;

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

bool FAnimNode_KawaiiPhysics::HasPreUpdate() const
{
	// CDO上でキャッシュされるため無条件true（共有コリジョン初期化にGameThread PreUpdateが必要）
	// Must return true unconditionally: cached on CDO at load time (AnimBlueprintGeneratedClass).
	// Shared collision initialization requires GameThread PreUpdate.
	return true;
}

void FAnimNode_KawaiiPhysics::PreUpdate(const UAnimInstance* InAnimInstance)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_PreUpdate);

#if WITH_EDITOR
	if (const UWorld* World = InAnimInstance->GetWorld())
	{
		if (World->WorldType == EWorldType::Editor ||
			World->WorldType == EWorldType::EditorPreview)
		{
			bEditing = true;
		}
	}
#endif

	// BP APIランタイム変更時の遅延リセット / Deferred reinit from Blueprint API runtime changes
	if (bSharedCollisionNeedsReinit)
	{
		// 旧Slotを即座に期限切れ化 / Mark old slot as immediately expired
		if (CachedSourceSlot.IsValid())
		{
			CachedSourceSlot->LastPublishFrame.store(0, std::memory_order_release);
		}
		bSharedCollisionInitialized = false;
		CachedSharedCollisionEntry.Reset();
		CachedSourceSlot.Reset();
		SharedCollisionInitRetryCount = 0;
		bSharedCollisionInitWarningLogged = false;
		bSharedCollisionNeedsReinit = false;
	}

	// 共有コリジョンの初期化（GameThreadで実行、TMapへの書き込みはスレッドセーフでないため）
	// Initialize shared collision on GameThread (TMap mutation is not thread-safe)
	if ((bSharedCollisionSource || bUseSharedCollision) && SharedCollisionGroupTag.IsValid())
	{
		if (!bSharedCollisionInitialized)
		{
			InitializeSharedCollision(InAnimInstance);

			// 初期化リトライが続く場合に警告ログ（1回のみ）
			// Warn once if target init keeps retrying (source not found)
			if (!bSharedCollisionInitialized && !bSharedCollisionInitWarningLogged)
			{
				SharedCollisionInitRetryCount++;
				if (SharedCollisionInitRetryCount > CVarSharedCollisionInitRetryThreshold.GetValueOnGameThread())
				{
					UE_LOG(LogKawaiiPhysics, Warning,
						TEXT("SharedCollision: Target could not find source entry for tag [%s]. "
							"Ensure a source node with matching tag exists on this actor."),
						*SharedCollisionGroupTag.ToString());
					bSharedCollisionInitWarningLogged = true;
				}
			}
		}
	}
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
