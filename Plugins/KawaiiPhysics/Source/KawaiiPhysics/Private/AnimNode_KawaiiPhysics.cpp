// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
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

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_InitModifyBones"), STAT_KawaiiPhysics_InitModifyBones, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_Eval"), STAT_KawaiiPhysics_Eval, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SimulateModifyBones"), STAT_KawaiiPhysics_SimulateModifyBones, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_Simulate"), STAT_KawaiiPhysics_Simulate, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_GetWindVelocity"), STAT_KawaiiPhysics_GetWindVelocity, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_WorldCollision"), STAT_KawaiiPhysics_WorldCollision, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_InitSyncBone"), STAT_KawaiiPhysics_InitSyncBone, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ApplySyncBone"), STAT_KawaiiPhysics_ApplySyncBone, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AdjustByCollision"), STAT_KawaiiPhysics_AdjustByCollision, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AdjustByBoneConstraint"), STAT_KawaiiPhysics_AdjustByBoneConstraint,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateSphericalLimit"), STAT_KawaiiPhysics_UpdateSphericalLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdatePlanerLimit"), STAT_KawaiiPhysics_UpdatePlanerLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_WarmUp"), STAT_KawaiiPhysics_WarmUp, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdatePhysicsSetting"), STAT_KawaiiPhysics_UpdatePhysicsSetting, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateCapsuleLimit"), STAT_KawaiiPhysics_UpdateCapsuleLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateBoxLimit"), STAT_KawaiiPhysics_UpdateBoxLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateModifyBonesPoseTransform"),
                   STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ApplySimulateResult"), STAT_KawaiiPhysics_ApplySimulateResult, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ConvertSimulationSpaceTransform"),
                   STAT_KawaiiPhysics_ConvertSimulationSpaceTransform, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ConvertSimulationSpaceVector"), STAT_KawaiiPhysics_ConvertSimulationSpaceVector,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ConvertSimulationSpaceLocation"),
                   STAT_KawaiiPhysics_ConvertSimulationSpaceLocation, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ConvertSimulationSpaceRotation"),
                   STAT_KawaiiPhysics_ConvertSimulationSpaceRotation, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_ConvertSimulationSpace"), STAT_KawaiiPhysics_ConvertSimulationSpace,
                   STATGROUP_Anim);

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

	ApplyLimitsDataAsset(RequiredBones);
	ApplyPhysicsAsset(RequiredBones);
	ApplyBoneConstraintDataAsset(RequiredBones);

	ModifyBones.Empty();

	// For Avoiding Zero Divide in the first frame
	DeltaTimeOld = 1.0f / TargetFramerate;

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

					auto Color = ModifyBone.bDummy ? FColor::Red : FColor::Yellow;
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

	if (TeleportType == ETeleportType::ResetPhysics)
	{
		ModifyBones.Empty(ModifyBones.Num());
		TeleportType = ETeleportType::None;
		bInitPhysicsSettings = false;
	}

	if (SimulationSpace != LastSimulationSpace)
	{
		ConvertSimulationSpace(Output, LastSimulationSpace, SimulationSpace);
	}
	LastSimulationSpace = SimulationSpace;

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
		PreSkelCompTransform = ComponentTransform;
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
#if WITH_EDITOR
	return true;
#else
	return false;
#endif
}

void FAnimNode_KawaiiPhysics::PreUpdate(const UAnimInstance* InAnimInstance)
{
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

void FAnimNode_KawaiiPhysics::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
		return true;
	};

	RootBone.Initialize(RequiredBones);
	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		AdditionalRootBone.RootBone.Initialize(RequiredBones);
	}
	for (auto& Bone : ModifyBones)
	{
		Bone.BoneRef.Initialize(RequiredBones);
	}

	SimulationBaseBone.Initialize(RequiredBones);

	Initialize(SphericalLimits);
	Initialize(CapsuleLimits);
	Initialize(BoxLimits);
	Initialize(PlanarLimits);

	for (auto& BoneConstraint : BoneConstraints)
	{
		BoneConstraint.InitializeBone(RequiredBones);
	}

	for (auto& SyncBone : SyncBones)
	{
		SyncBone.Bone.Initialize(RequiredBones);
		for (auto& Target : SyncBone.TargetRoots)
		{
			Target.Bone.Initialize(RequiredBones);
		}
	}
}

void FAnimNode_KawaiiPhysics::InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitModifyBones);

	// https://github.com/pafuhana1213/KawaiiPhysics/issues/174
	const FReferenceSkeleton& RefSkeleton = (CVarAnimNodeKawaiiPhysicsUseBoneContainerRefSkeletonWhenInit.
		                                        GetValueOnAnyThread())
		                                        ? BoneContainer.GetReferenceSkeleton()
		                                        : BoneContainer.GetSkeletonAsset()->GetReferenceSkeleton();

	auto InitRootBone = [&](const FName& RootBoneName, const TArray<FBoneReference>& InExcludeBones)
	{
		TArray<FKawaiiPhysicsModifyBone> Bones;
		AddModifyBone(Bones, Output, BoneContainer, RefSkeleton, RefSkeleton.FindBoneIndex(RootBoneName),
		              InExcludeBones);
		if (Bones.Num() > 0)
		{
			float TotalBoneLength = 0.0f;
			CalcBoneLength(Bones[0], Bones, BoneContainer.GetRefPoseArray(), TotalBoneLength);

			for (auto& Bone : Bones)
			{
				if (Bone.LengthFromRoot > 0.0f)
				{
					Bone.LengthRateFromRoot = Bone.LengthFromRoot / TotalBoneLength;
				}

				Bone.Index += ModifyBones.Num();
				if (Bone.ParentIndex >= 0)
				{
					Bone.ParentIndex += ModifyBones.Num();
				}
				for (auto& ChildIndex : Bone.ChildIndices)
				{
					ChildIndex += ModifyBones.Num();
				}
			}
			ModifyBones.Append(Bones);
		}
	};

	ModifyBones.Empty();
	InitRootBone(RootBone.BoneName, ExcludeBones);
	for (auto& AdditionalRootBone : AdditionalRootBones)
	{
		InitRootBone(AdditionalRootBone.RootBone.BoneName,
		             AdditionalRootBone.bUseOverrideExcludeBones
			             ? AdditionalRootBone.OverrideExcludeBones
			             : ExcludeBones);
	}
}

void FAnimNode_KawaiiPhysics::ApplyLimitsDataAsset(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
	};
	auto RemoveAllSourceDataAssets = [](auto& Targets)
	{
		Targets.RemoveAll([](const FCollisionLimitBase& Limit)
		{
			return Limit.SourceType == ECollisionSourceType::DataAsset;
		});
	};

	RemoveAllSourceDataAssets(SphericalLimitsData);
	RemoveAllSourceDataAssets(CapsuleLimitsData);
	RemoveAllSourceDataAssets(BoxLimitsData);
	RemoveAllSourceDataAssets(PlanarLimitsData);

	if (LimitsDataAsset)
	{
		SphericalLimitsData.Append(LimitsDataAsset->SphericalLimits);
		CapsuleLimitsData.Append(LimitsDataAsset->CapsuleLimits);
		BoxLimitsData.Append(LimitsDataAsset->BoxLimits);
		PlanarLimitsData.Append(LimitsDataAsset->PlanarLimits);

		Initialize(SphericalLimitsData);
		Initialize(CapsuleLimitsData);
		Initialize(BoxLimitsData);
		Initialize(PlanarLimitsData);
	}
}

void FAnimNode_KawaiiPhysics::ApplyPhysicsAsset(const FBoneContainer& RequiredBones)
{
	auto Initialize = [&RequiredBones](auto& Targets)
	{
		for (auto& Target : Targets)
		{
			Target.DrivingBone.Initialize(RequiredBones);
		}
	};
	auto RemoveAllSourcePhysicsAssets = [](auto& Targets)
	{
		Targets.RemoveAll([](const FCollisionLimitBase& Limit)
		{
			return Limit.SourceType == ECollisionSourceType::PhysicsAsset;
		});
	};

	RemoveAllSourcePhysicsAssets(SphericalLimitsData);
	RemoveAllSourcePhysicsAssets(CapsuleLimitsData);
	RemoveAllSourcePhysicsAssets(BoxLimitsData);

	if (PhysicsAssetForLimits)
	{
		for (const auto& BodySetup : PhysicsAssetForLimits->SkeletalBodySetups)
		{
			FBoneReference DrivingBone = BodySetup->BoneName;
			DrivingBone.Initialize(RequiredBones);
			if (!DrivingBone.IsValidToEvaluate(RequiredBones))
			{
				continue;
			}

			const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
			for (const auto& SphereElem : AggGeom.SphereElems)
			{
				FSphericalLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = SphereElem.Center;
				NewLimit.Radius = SphereElem.Radius;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				SphericalLimitsData.Add(NewLimit);
			}
			for (const auto& CapsuleElem : AggGeom.SphylElems)
			{
				FCapsuleLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = CapsuleElem.Center;
				NewLimit.OffsetRotation = CapsuleElem.Rotation;
				NewLimit.Length = CapsuleElem.Length;
				NewLimit.Radius = CapsuleElem.Radius;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				CapsuleLimitsData.Add(NewLimit);
			}
			for (const auto& BoxElem : AggGeom.BoxElems)
			{
				FBoxLimit NewLimit;
				NewLimit.DrivingBone = DrivingBone;
				NewLimit.OffsetLocation = BoxElem.Center;
				NewLimit.OffsetRotation = BoxElem.Rotation;
				NewLimit.Extent = FVector(BoxElem.X, BoxElem.Y, BoxElem.Z) / 2.0f;
				NewLimit.SourceType = ECollisionSourceType::PhysicsAsset;
				BoxLimitsData.Add(NewLimit);
			}
		}

		Initialize(SphericalLimitsData);
		Initialize(CapsuleLimitsData);
		Initialize(BoxLimitsData);
	}
}

void FAnimNode_KawaiiPhysics::ApplyBoneConstraintDataAsset(const FBoneContainer& RequiredBones)
{
	BoneConstraintsData.Empty();
	if (BoneConstraintsDataAsset)
	{
		BoneConstraintsData = BoneConstraintsDataAsset->GenerateBoneConstraints();
		for (auto& BoneConstraint : BoneConstraintsData)
		{
			BoneConstraint.InitializeBone(RequiredBones);
		}
	}
}

int32 FAnimNode_KawaiiPhysics::AddModifyBone(TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                             FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                             const FReferenceSkeleton& RefSkeleton, int32 BoneIndex,
                                             const TArray<FBoneReference>& InExcludeBones)
{
	if (BoneIndex < 0 || RefSkeleton.GetNum() < BoneIndex)
	{
		return INDEX_NONE;
	}

	FBoneReference BoneRef;
	BoneRef.BoneName = RefSkeleton.GetBoneName(BoneIndex);

	if (InExcludeBones.Num() > 0 && InExcludeBones.Find(BoneRef) >= 0)
	{
		return INDEX_NONE;
	}

	FKawaiiPhysicsModifyBone NewModifyBone;
	NewModifyBone.BoneRef = BoneRef;
	NewModifyBone.BoneRef.Initialize(BoneContainer);
	if (NewModifyBone.BoneRef.CachedCompactPoseIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	FTransform RefBonePoseTransform =
		GetBoneTransformInSimSpace(Output, NewModifyBone.BoneRef.CachedCompactPoseIndex);

	NewModifyBone.Location = RefBonePoseTransform.GetLocation();
	NewModifyBone.PrevLocation = NewModifyBone.Location;
	NewModifyBone.PoseLocation = NewModifyBone.Location;
	NewModifyBone.PrevRotation = RefBonePoseTransform.GetRotation();
	NewModifyBone.PoseRotation = NewModifyBone.PrevRotation;
	NewModifyBone.PoseScale = RefBonePoseTransform.GetScale3D();

	int32 ModifyBoneIndex = InModifyBones.Add(NewModifyBone);
	InModifyBones[ModifyBoneIndex].Index = ModifyBoneIndex;

	TArray<int32> ChildBoneIndices;
	CollectChildBones(RefSkeleton, BoneIndex, ChildBoneIndices);
	bool AddedChildBone = false;
	if (ChildBoneIndices.Num() > 0)
	{
		//for some mesh where tip bone is empty (without any skinning weight in the mesh), ChildBoneIndices > 0 but no actual child bones are created
		for (auto ChildBoneIndex : ChildBoneIndices)
		{
			int32 ChildModifyBoneIndex = AddModifyBone(InModifyBones, Output, BoneContainer, RefSkeleton,
			                                           ChildBoneIndex,
			                                           InExcludeBones);
			if (ChildModifyBoneIndex >= 0)
			{
				InModifyBones[ModifyBoneIndex].ChildIndices.Add(ChildModifyBoneIndex);
				InModifyBones[ChildModifyBoneIndex].ParentIndex = ModifyBoneIndex;
				AddedChildBone = true;
			}
		}
	}

	if (!AddedChildBone && DummyBoneLength > 0.0f)
	{
		// Add dummy modify bone
		FKawaiiPhysicsModifyBone DummyModifyBone;
		DummyModifyBone.bDummy = true;
		DummyModifyBone.Location = NewModifyBone.Location + GetBoneForwardVector(NewModifyBone.PrevRotation) *
			DummyBoneLength;
		DummyModifyBone.PrevLocation = DummyModifyBone.Location;
		DummyModifyBone.PoseLocation = DummyModifyBone.Location;
		DummyModifyBone.PrevRotation = NewModifyBone.PrevRotation;
		DummyModifyBone.PoseRotation = DummyModifyBone.PrevRotation;
		DummyModifyBone.PoseScale = RefBonePoseTransform.GetScale3D();

		int32 DummyBoneIndex = InModifyBones.Add(DummyModifyBone);
		InModifyBones[ModifyBoneIndex].ChildIndices.Add(DummyBoneIndex);
		InModifyBones[DummyBoneIndex].Index = DummyBoneIndex;
		InModifyBones[DummyBoneIndex].ParentIndex = ModifyBoneIndex;
	}


	return ModifyBoneIndex;
}

int32 FAnimNode_KawaiiPhysics::CollectChildBones(const FReferenceSkeleton& RefSkeleton, const int32 ParentBoneIndex,
                                                 TArray<int32>& Children) const
{
	Children.Reset();

	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 ChildIndex = ParentBoneIndex + 1; ChildIndex < NumBones; ChildIndex++)
	{
		if (ParentBoneIndex == RefSkeleton.GetParentIndex(ChildIndex))
		{
			Children.Add(ChildIndex);
		}
	}

	return Children.Num();
}

void FAnimNode_KawaiiPhysics::CalcBoneLength(FKawaiiPhysicsModifyBone& Bone,
                                             TArray<FKawaiiPhysicsModifyBone>& InModifyBones,
                                             const TArray<FTransform>& RefBonePose,
                                             float& TotalBoneLength)
{
	if (Bone.ParentIndex < 0)
	{
		Bone.LengthFromRoot = 0.0f;
		Bone.BoneLength = 0.0f;
	}
	else
	{
		if (!Bone.bDummy)
		{
			Bone.BoneLength = RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
			Bone.LengthFromRoot = InModifyBones[Bone.ParentIndex].LengthFromRoot + Bone.BoneLength;
		}
		else
		{
			Bone.BoneLength = DummyBoneLength;
			Bone.LengthFromRoot = InModifyBones[Bone.ParentIndex].LengthFromRoot + DummyBoneLength;
		}

		TotalBoneLength = FMath::Max(TotalBoneLength, Bone.LengthFromRoot);
	}

	for (const int32 ChildIndex : Bone.ChildIndices)
	{
		CalcBoneLength(InModifyBones[ChildIndex], InModifyBones, RefBonePose, TotalBoneLength);
	}
}


void FAnimNode_KawaiiPhysics::UpdatePhysicsSettingsOfModifyBones()
{
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePhysicsSetting);

		const float LengthRate = Bone.LengthRateFromRoot;

		// Damping
		Bone.PhysicsSettings.Damping = FMath::Clamp(
			PhysicsSettings.Damping * DampingCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldLocationDamping
		Bone.PhysicsSettings.WorldDampingLocation = FMath::Clamp(
			PhysicsSettings.WorldDampingLocation * WorldDampingLocationCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// WorldRotationDamping
		Bone.PhysicsSettings.WorldDampingRotation = FMath::Clamp(
			PhysicsSettings.WorldDampingRotation * WorldDampingRotationCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Stiffness
		Bone.PhysicsSettings.Stiffness = FMath::Clamp(
			PhysicsSettings.Stiffness * StiffnessCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f, 1.0f);
		
		// Radius
		Bone.PhysicsSettings.Radius = FMath::Max(
			PhysicsSettings.Radius * RadiusCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f);
		
		// LimitAngle
		Bone.PhysicsSettings.LimitAngle = FMath::Max(
			PhysicsSettings.LimitAngle * LimitAngleCurveData.GetRichCurveConst()->Eval(
				LengthRate, 1.0f), 0.0f);
	}
}


void FAnimNode_KawaiiPhysics::UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
                                                    const FBoneContainer& BoneContainer,
                                                    const FTransform& ComponentTransform) const
{
	for (auto& Sphere : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateSphericalLimit);

		if (Sphere.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Sphere.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Sphere.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Sphere.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Sphere.Location = BoneTransform.GetLocation();
			Sphere.Rotation = BoneTransform.GetRotation();

			Sphere.bEnable = true;
		}
		else
		{
			Sphere.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateCapsuleLimits(TArray<FCapsuleLimit>& Limits, FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  const FTransform& ComponentTransform) const
{
	for (auto& Capsule : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateCapsuleLimit);

		if (Capsule.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Capsule.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Capsule.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Capsule.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Capsule.Location = BoneTransform.GetLocation();
			Capsule.Rotation = BoneTransform.GetRotation();

			Capsule.bEnable = true;
		}
		else
		{
			Capsule.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateBoxLimits(TArray<FBoxLimit>& Limits, FComponentSpacePoseContext& Output,
                                              const FBoneContainer& BoneContainer,
                                              const FTransform& ComponentTransform) const
{
	for (auto& Box : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateBoxLimit);

		if (Box.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Box.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Box.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Box.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform =
				//GetSimSpaceTransformFromComponentSpace(SimulationSpace, Output, BoneTransform);
				ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulationSpace,
				                                BoneTransform);
			Box.Location = BoneTransform.GetLocation();
			Box.Rotation = BoneTransform.GetRotation();

			Box.bEnable = true;
		}
		else
		{
			Box.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdatePlanerLimits(TArray<FPlanarLimit>& Limits, FComponentSpacePoseContext& Output,
                                                 const FBoneContainer& BoneContainer,
                                                 const FTransform& ComponentTransform) const
{
	for (auto& Planar : Limits)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdatePlanerLimit);

		if (Planar.DrivingBone.IsValidToEvaluate(BoneContainer))
		{
			const FCompactPoseBoneIndex CompactPoseIndex = Planar.DrivingBone.GetCompactPoseIndex(BoneContainer);
			FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactPoseIndex);

			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);
			BoneTransform.SetRotation(Planar.OffsetRotation.Quaternion() * BoneTransform.GetRotation());
			BoneTransform.AddToTranslation(Planar.OffsetLocation);

			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, BoneTransform,
			                                                 CompactPoseIndex, BCS_BoneSpace);

			BoneTransform = ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                                SimulationSpace, BoneTransform);
			Planar.Location = BoneTransform.GetLocation();
			Planar.Rotation = BoneTransform.GetRotation();
			Planar.Rotation.Normalize();
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());

			Planar.bEnable = true;
		}
		else
		{
			// Maybe the DrivingBone is set to empty for the floor
			FTransform OffsetTransform(Planar.OffsetRotation, Planar.OffsetLocation);
			OffsetTransform = ConvertSimulationSpaceTransform(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                                  SimulationSpace, OffsetTransform);

			Planar.Location = OffsetTransform.GetLocation();
			Planar.Rotation = OffsetTransform.GetRotation();
			Planar.Rotation.Normalize();
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	for (auto& Bone : ModifyBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_UpdateModifyBonesPoseTransform);
		
		if (Bone.bDummy)
		{
			auto ParentBone = ModifyBones[Bone.ParentIndex];
			Bone.PoseLocation = ParentBone.PoseLocation +
				GetBoneForwardVector(ParentBone.PoseRotation) * DummyBoneLength;
			Bone.PoseRotation = ParentBone.PoseRotation;
			Bone.PoseScale = ParentBone.PoseScale;
		}
		else
		{
			const auto CompactPoseIndex = Bone.BoneRef.GetCompactPoseIndex(BoneContainer);
			if (CompactPoseIndex < 0)
			{
				// Reset bone location and rotation may cause trouble when switching between skeleton LODs #44
				if (ResetBoneTransformWhenBoneNotFound)
				{
					Bone.PoseLocation = FVector::ZeroVector;
					Bone.PoseRotation = FQuat::Identity;
					Bone.PoseScale = FVector::OneVector;
				}
				continue;
			}

			const FTransform BoneTransform = GetBoneTransformInSimSpace(Output, CompactPoseIndex);
			Bone.PoseLocation = BoneTransform.GetLocation();
			Bone.PoseRotation = BoneTransform.GetRotation();
			Bone.PoseScale = BoneTransform.GetScale3D();
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateSkelCompMove(FComponentSpacePoseContext& Output,
                                                 const FTransform& ComponentTransform)
{
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	SkelCompMoveVector *= SkelCompMoveScale;
	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());

	if (TeleportDistanceThreshold > 0 &&
		SkelCompMoveVector.SizeSquared() > TeleportDistanceThreshold * TeleportDistanceThreshold)
	{
		TeleportType = ETeleportType::TeleportPhysics;
	}

	if (TeleportRotationThreshold > 0 &&
		FMath::RadiansToDegrees(SkelCompMoveRotation.GetAngle()) > TeleportRotationThreshold)
	{
		TeleportType = ETeleportType::TeleportPhysics;
	}
}

void FAnimNode_KawaiiPhysics::SimulateModifyBones(FComponentSpacePoseContext& Output,
                                                  const FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimulateModifyBones);

	if (DeltaTime <= 0.0f)
	{
		return;
	}

	const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();

	// Save Prev/Pose Info , Check SkipSimulate
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.BoneRef.BoneIndex < 0 && !Bone.bDummy)
		{
			Bone.bSkipSimulate = true;
			continue;
		}

		if (Bone.ParentIndex < 0)
		{
			Bone.bSkipSimulate = true;
			Bone.PrevLocation = Bone.Location;
			Bone.Location = Bone.PoseLocation;
			continue;
		}

		Bone.bSkipSimulate = false;
	}

	// Gravity
	GravityInSimSpace = ConvertSimulationSpaceVector(Output,
	                                                 bUseWorldSpaceGravity
		                                                 ? EKawaiiPhysicsSimulationSpace::WorldSpace
		                                                 : EKawaiiPhysicsSimulationSpace::ComponentSpace,
	                                                 SimulationSpace, Gravity);
	if (bUseDefaultGravityZProjectSetting)
	{
		GravityInSimSpace *= FMath::Abs(UPhysicsSettings::Get()->DefaultGravityZ);
	}

	// SimpleExternalForce: compute once in SimulationSpace (avoid per-bone conversions)
	if (!SimpleExternalForce.IsNearlyZero())
	{
		if (bUseWorldSpaceSimpleExternalForce)
		{
			SimpleExternalForceInSimSpace = ConvertSimulationSpaceVector(
				Output,
				EKawaiiPhysicsSimulationSpace::WorldSpace,
				SimulationSpace,
				SimpleExternalForce);
		}
		else
		{
			SimpleExternalForceInSimSpace = SimpleExternalForce;
		}
	}
	else
	{
		SimpleExternalForceInSimSpace = FVector::ZeroVector;
	}

	// External Force : PreApply
	// NOTE: if use foreach, you may get issue ( Array has changed during ranged-for iteration )
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i])
		{
			CustomExternalForces[i]->PreApply(*this, SkelComp);
		}
	}
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.PreApply(*this, Output);
		}
	}

	// Simulate
	const float Exponent = TargetFramerate * DeltaTime;
	const UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
	const FSceneInterface* Scene = World ? World->Scene : nullptr;
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}
		Simulate(Bone, Scene, ComponentTransform, Exponent, SkelComp, Output);
	}

	// External Force : PostApply
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.PostApply(*this, Output);
		}
	}

	// Adjust by collisions
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AdjustByCollision);

		AdjustBySphereCollision(Bone, SphericalLimits);
		AdjustBySphereCollision(Bone, SphericalLimitsData);
		AdjustByCapsuleCollision(Bone, CapsuleLimits);
		AdjustByCapsuleCollision(Bone, CapsuleLimitsData);
		AdjustByBoxCollision(Bone, BoxLimits);
		AdjustByBoxCollision(Bone, BoxLimitsData);
		AdjustByPlanerCollision(Bone, PlanarLimits);
		AdjustByPlanerCollision(Bone, PlanarLimitsData);
		if (bAllowWorldCollision)
		{
			AdjustByWorldCollision(Output, Bone, SkelComp);
		}
	}

	// Adjust by Bone Constraints After Collision
	if (BoneConstraintIterationCountAfterCollision > 0)
	{
		for (FModifyBoneConstraint& BoneConstraint : MergedBoneConstraints)
		{
			BoneConstraint.Lambda = 0.0f;
		}
		for (int i = 0; i < BoneConstraintIterationCountAfterCollision; ++i)
		{
			AdjustByBoneConstraints();
		}
	}

	// Adjust by Limits ane Bone Length
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}

		auto& ParentBone = ModifyBones[Bone.ParentIndex];

		// Adjust by angle limit
		AdjustByAngleLimit(Bone, ParentBone);

		// Adjust by Planar Constraint
		AdjustByPlanarConstraint(Bone, ParentBone);

		// Restore Bone Length
		const float BoneLength = (Bone.PoseLocation - ParentBone.PoseLocation).Size();
		Bone.Location = (Bone.Location - ParentBone.Location).GetSafeNormal() * BoneLength + ParentBone.Location;
	}

	DeltaTimeOld = DeltaTime;
}

void FAnimNode_KawaiiPhysics::Simulate(FKawaiiPhysicsModifyBone& Bone, const FSceneInterface* Scene,
                                       const FTransform& ComponentTransform,
                                       const float& Exponent, const USkeletalMeshComponent* SkelComp,
                                       FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Simulate);

	const FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];

	// Move using Velocity( = movement amount in pre frame ) and Damping
	FVector Velocity = (Bone.Location - Bone.PrevLocation) / DeltaTimeOld;
	Bone.PrevLocation = Bone.Location;
	Velocity *= (1.0f - Bone.PhysicsSettings.Damping);

	// wind
	if (bEnableWind && Scene)
	{
		Velocity += GetWindVelocity(Output, Scene, Bone) * TargetFramerate;
	}

	// Gravity (apply just after wind; keep legacy compatibility via separate position term)
	if (!bUseLegacyGravity)
	{
		// AnimDynamics-like: integrate acceleration into velocity
		Velocity += GravityInSimSpace * DeltaTime;
	}
	else
	{
		// Legacy gravity: add 0.5 * g * dt^2 to position
		Bone.Location += 0.5 * GravityInSimSpace * DeltaTime * DeltaTime;
	}

	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			if (const auto ExForce = ExternalForces[i].GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				ExForce->bIsEnabled)
			{
				ExForce->ApplyToVelocity(Bone, *this, Output, Velocity);
			}
		}
	}

	// Integrate position from velocity
	Bone.Location += Velocity * DeltaTime;

	// Simple External Force (cached in SimulateModifyBones)
	if (!SimpleExternalForceInSimSpace.IsNearlyZero())
	{
		Bone.Location += SimpleExternalForceInSimSpace * DeltaTime;
	}

	// Follow World Movement
	if (SimulationSpace != EKawaiiPhysicsSimulationSpace::WorldSpace && TeleportType != ETeleportType::TeleportPhysics)
	{
		// Follow Translation
		if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FVector SkelCompMoveVectorBBS =
				ConvertSimulationSpaceVector(Output, EKawaiiPhysicsSimulationSpace::ComponentSpace,
				                             EKawaiiPhysicsSimulationSpace::BaseBoneSpace, SkelCompMoveVector);
			Bone.Location += SkelCompMoveVectorBBS * (1.0f - Bone.PhysicsSettings.WorldDampingLocation);
		}
		else
		{
			Bone.Location += SkelCompMoveVector * (1.0f - Bone.PhysicsSettings.WorldDampingLocation);
		}

		// Follow Rotation
		if (SimulationSpace == EKawaiiPhysicsSimulationSpace::BaseBoneSpace)
		{
			const FVector PrevLocationCS = PrevBaseBoneSpace2ComponentSpace.TransformPosition(Bone.PrevLocation);
			const FVector RotatedLocationCS = SkelCompMoveRotation.RotateVector(PrevLocationCS);
			const FVector RotatedLocationBase = ConvertSimulationSpaceLocationCached(
				FSimulationSpaceCache(), CurrentEvalSimSpaceCache, RotatedLocationCS);

			Bone.Location += (RotatedLocationBase - Bone.PrevLocation) * (1.0f - Bone.PhysicsSettings.
				WorldDampingRotation);
		}
		else
		{
			Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation)
				* (1.0f - Bone.PhysicsSettings.WorldDampingRotation);
		}
	}

	// External Force
	// NOTE: if use foreach, you may get issue ( Array has changed during ranged-for iteration )
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i] && CustomExternalForces[i]->bIsEnabled)
		{
			FTransform BoneTM = FTransform::Identity;
			if (Bone.bDummy)
			{
				BoneTM = GetBoneTransformInSimSpace(
					Output, ParentBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
			}
			else
			{
				BoneTM = GetBoneTransformInSimSpace(
					Output, Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
			}

			CustomExternalForces[i]->Apply(*this, Bone.Index, SkelComp, BoneTM);
		}
	}

	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			if (const auto ExForce = ExternalForces[i].GetMutablePtr<FKawaiiPhysics_ExternalForce>();
				ExForce->bIsEnabled)
			{
				if (ExForce->ExternalForceSpace == EExternalForceSpace::BoneSpace)
				{
					FTransform BoneTM = FTransform::Identity;
					if (Bone.bDummy)
					{
						BoneTM = GetBoneTransformInSimSpace(
							Output, ParentBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
					}
					else
					{
						BoneTM = GetBoneTransformInSimSpace(
							Output, Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
					}

					ExForce->Apply(Bone, *this, Output, BoneTM);
				}
				else
				{
					ExForce->Apply(Bone, *this, Output);
				}
			}
		}
	}

	// // Pull to Pose Location
	const FVector BaseLocation = ParentBone.Location + (Bone.PoseLocation - ParentBone.PoseLocation);
	Bone.Location += (BaseLocation - Bone.Location) *
		(1.0f - FMath::Pow(1.0f - Bone.PhysicsSettings.Stiffness, Exponent));
}

FVector FAnimNode_KawaiiPhysics::GetWindVelocity(FComponentSpacePoseContext& Output, const FSceneInterface* Scene,
                                                 const FKawaiiPhysicsModifyBone& Bone) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_GetWindVelocity);

	if (WindScale == 0.0f || !Scene)
	{
		return FVector::ZeroVector;
	}

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;

	Scene->GetWindParameters_GameThread(
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.PoseLocation),
		WindDirection, WindSpeed, WindMinGust, WindMaxGust);

	WindDirection =
		ConvertSimulationSpaceVector(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace, WindDirection);
	if (WindDirectionNoiseAngle > 0)
	{
		WindDirection = FMath::VRandCone(WindDirection, FMath::DegreesToRadians(WindDirectionNoiseAngle));
	}

	FVector WindVelocity = WindDirection * WindSpeed * WindScale;

	// TODO:Migrate if there are more good method (Currently copying AnimDynamics implementation)
	WindVelocity *= FMath::FRandRange(0.0f, 2.0f);

	return WindVelocity;
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(FComponentSpacePoseContext& Output, FKawaiiPhysicsModifyBone& Bone,
                                                     const USkeletalMeshComponent* OwningComp)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WorldCollision);

	if (!OwningComp || !OwningComp->GetWorld() || Bone.ParentIndex < 0)
	{
		return;
	}


	/** the trace is not done in game thread, so TraceTag does not draw debug traces*/
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KawaiiCollision));

	if (bIgnoreSelfComponent)
	{
		Params.AddIgnoredComponent(OwningComp);
	}

	// Get collision settings from component	
	ECollisionChannel TraceChannel = bOverrideCollisionParams
		                                 ? CollisionChannelSettings.GetObjectType()
		                                 : OwningComp->GetCollisionObjectType();
	FCollisionResponseParams ResponseParams = bOverrideCollisionParams
		                                          ? FCollisionResponseParams(
			                                          CollisionChannelSettings.GetResponseToChannels())
		                                          : FCollisionResponseParams(
			                                          OwningComp->GetCollisionResponseToChannels());
	const UWorld* World = OwningComp->GetWorld();

	const FVector TraceStartLocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.PrevLocation);
	const FVector TraceEndLocationWS =
		ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::WorldSpace,
		                               Bone.Location);

	if (bIgnoreSelfComponent)
	{
		// Do sphere sweep
		FHitResult Result;
		bool bHit = World->SweepSingleByChannel(
			Result, TraceStartLocationWS, TraceEndLocationWS, FQuat::Identity,
			TraceChannel, FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius), Params, ResponseParams);
		if (bHit)
		{
			if (Result.bStartPenetrating)
			{
				Bone.Location =
					ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace,
					                               TraceEndLocationWS + Result.Normal * Result.PenetrationDepth);
			}
			else
			{
				Bone.Location =
					ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace, SimulationSpace,
					                               Result.Location);
			}
		}
	}
	else
	{
		// Do sphere sweep and ignore bones later
		TArray<FHitResult> Results;
		bool bHit = World->SweepMultiByChannel(Results, TraceStartLocationWS,
		                                       TraceEndLocationWS, FQuat::Identity, TraceChannel,
		                                       FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius), Params,
		                                       ResponseParams);
		if (!bHit)
		{
			return;
		}

		bool IsIgnoreHit;
		for (const auto& Result : Results)
		{
			if (!Result.bBlockingHit)
			{
				continue;
			}

			//should we ignore this hit?
			IsIgnoreHit = false;
			if (Result.Component == OwningComp && Result.BoneName != NAME_None)
			{
				IsIgnoreHit = Result.BoneName == Bone.BoneRef.BoneName;
				if (!IsIgnoreHit)
				{
					for (auto BoneRef : IgnoreBones)
					{
						if (BoneRef.BoneName == Result.BoneName)
						{
							IsIgnoreHit = true;
							break;
						}
					}
				}
				if (!IsIgnoreHit)
				{
					for (auto BoneNamePrefix : IgnoreBoneNamePrefix)
					{
						if (Result.BoneName.ToString().StartsWith(BoneNamePrefix.ToString()))
						{
							IsIgnoreHit = true;
							break;
						}
					}
				}
			}

			//found the blocking hit we shouldn't ignore!
			if (!IsIgnoreHit)
			{
				if (Result.bStartPenetrating)
				{
					Bone.Location =
						ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SimulationSpace,
						                               TraceEndLocationWS + Result.Normal * Result.PenetrationDepth);
				}
				else
				{
					Bone.Location =
						ConvertSimulationSpaceLocation(Output, EKawaiiPhysicsSimulationSpace::WorldSpace,
						                               SimulationSpace,
						                               Result.Location);
				}
				break;
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustBySphereCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FSphericalLimit>& Limits)
{
	for (auto& Sphere : Limits)
	{
		if (!Sphere.bEnable || Sphere.Radius <= 0.0f)
		{
			continue;
		}

		const float LimitDistance = Bone.PhysicsSettings.Radius + Sphere.Radius;
		if (Sphere.LimitType == ESphericalLimitType::Outer)
		{
			if ((Bone.Location - Sphere.Location).SizeSquared() > LimitDistance * LimitDistance)
			{
				continue;
			}
			Bone.Location += (LimitDistance - (Bone.Location - Sphere.Location).Size())
				* (Bone.Location - Sphere.Location).GetSafeNormal();
		}
		else
		{
			if ((Bone.Location - Sphere.Location).SizeSquared() < LimitDistance * LimitDistance)
			{
				continue;
			}
			Bone.Location = Sphere.Location +
				(Sphere.Radius - Bone.PhysicsSettings.Radius) * (Bone.Location - Sphere.Location).GetSafeNormal();
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByCapsuleCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FCapsuleLimit>& Limits)
{
	for (auto& Capsule : Limits)
	{
		if (!Capsule.bEnable || Capsule.Radius <= 0 || Capsule.Length <= 0)
		{
			continue;
		}

		FVector StartPoint = Capsule.Location + Capsule.Rotation.GetAxisZ() * Capsule.Length * 0.5f;
		FVector EndPoint = Capsule.Location + Capsule.Rotation.GetAxisZ() * Capsule.Length * -0.5f;
		const float DistSquared = FMath::PointDistToSegmentSquared(Bone.Location, StartPoint, EndPoint);

		const float LimitDistance = Bone.PhysicsSettings.Radius + Capsule.Radius;
		if (DistSquared < LimitDistance * LimitDistance)
		{
			FVector ClosestPoint = FMath::ClosestPointOnSegment(Bone.Location, StartPoint, EndPoint);
			Bone.Location = ClosestPoint + (Bone.Location - ClosestPoint).GetSafeNormal() * LimitDistance;
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByBoxCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FBoxLimit>& Limits)
{
	for (auto& Box : Limits)
	{
		FTransform BoxTransform(Box.Rotation, Box.Location);
		float SphereRadius = Bone.PhysicsSettings.Radius;

		FVector LocalSphereCenter = BoxTransform.InverseTransformPosition(Bone.Location);
		FBox LocalBox(-Box.Extent, Box.Extent);
		if (FMath::SphereAABBIntersection(FSphere(LocalSphereCenter, SphereRadius), LocalBox))
		{
			// Calculate the point of the Box closest to the center of the Sphere
			FVector ClosestPoint = LocalSphereCenter;
			ClosestPoint.X = FMath::Clamp(ClosestPoint.X, LocalBox.Min.X, LocalBox.Max.X);
			ClosestPoint.Y = FMath::Clamp(ClosestPoint.Y, LocalBox.Min.Y, LocalBox.Max.Y);
			ClosestPoint.Z = FMath::Clamp(ClosestPoint.Z, LocalBox.Min.Z, LocalBox.Max.Z);

			FVector PushOutVector = LocalSphereCenter - ClosestPoint;
			float Distance = PushOutVector.Size();

			// When the bone sphere is completely buried inside the box, forced to push.
			if (PushOutVector.IsNearlyZero())
			{
				PushOutVector = LocalSphereCenter;
				Distance = SphereRadius;
			}

			// push
			if (Distance <= SphereRadius)
			{
				FVector PushOutDirection = PushOutVector.GetSafeNormal();
				FVector NewLocalSphereCenter = ClosestPoint + PushOutDirection * SphereRadius;
				Bone.Location = BoxTransform.TransformPosition(NewLocalSphereCenter);
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByPlanerCollision(FKawaiiPhysicsModifyBone& Bone, TArray<FPlanarLimit>& Limits)
{
	for (auto& Planar : Limits)
	{
		if (!Planar.bEnable)
		{
			continue;
		}

		FVector PointOnPlane = FVector::PointPlaneProject(Bone.Location, Planar.Plane);
		const float DistSquared = (Bone.Location - PointOnPlane).SizeSquared();

		FVector IntersectionPoint;
		if (DistSquared < Bone.PhysicsSettings.Radius * Bone.PhysicsSettings.Radius ||
			FMath::SegmentPlaneIntersection(Bone.Location, Bone.PrevLocation, Planar.Plane, IntersectionPoint))
		{
			Bone.Location = PointOnPlane + Planar.Rotation.GetUpVector() * Bone.PhysicsSettings.Radius;
		}
	}
}

void FAnimNode_KawaiiPhysics::AdjustByAngleLimit(
	FKawaiiPhysicsModifyBone& Bone,
	const FKawaiiPhysicsModifyBone& ParentBone)
{
	if (Bone.PhysicsSettings.LimitAngle == 0.0f)
	{
		return;
	}

	FVector BoneDir = (Bone.Location - ParentBone.Location).GetSafeNormal();
	const FVector PoseDir = (Bone.PoseLocation - ParentBone.PoseLocation).GetSafeNormal();
	const FVector Axis = FVector::CrossProduct(PoseDir, BoneDir);
	const float Angle = FMath::Atan2(Axis.Size(), FVector::DotProduct(PoseDir, BoneDir));
	const float AngleOverLimit = FMath::RadiansToDegrees(Angle) - Bone.PhysicsSettings.LimitAngle;

	if (AngleOverLimit > 0.0f)
	{
		BoneDir = BoneDir.RotateAngleAxis(-AngleOverLimit, Axis.GetSafeNormal());
		Bone.Location = BoneDir * (Bone.Location - ParentBone.Location).Size() + ParentBone.Location;
	}
}

void FAnimNode_KawaiiPhysics::AdjustByPlanarConstraint(FKawaiiPhysicsModifyBone& Bone,
                                                       const FKawaiiPhysicsModifyBone& ParentBone)
{
	if (PlanarConstraint != EPlanarConstraint::None)
	{
		FPlane Plane;
		switch (PlanarConstraint)
		{
		case EPlanarConstraint::X:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisX());
			break;
		case EPlanarConstraint::Y:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisY());
			break;
		case EPlanarConstraint::Z:
			Plane = FPlane(ParentBone.Location, ParentBone.PoseRotation.GetAxisZ());
			break;
		case EPlanarConstraint::None:
			break;
		default: ;
		}
		Bone.Location = FVector::PointPlaneProject(Bone.Location, Plane);
	}
}

const TArray<float> XPBDComplianceValues =
{
	0.00000000004f, // 0.04 x 10^(-9) (M^2/N) Concrete
	0.00000000016f, // 0.16 x 10^(-9) (M^2/N) Wood
	0.000000001f, // 1.0  x 10^(-8) (M^2/N) Leather
	0.000000002f, // 0.2  x 10^(-7) (M^2/N) Tendon
	0.0000001f, // 1.0  x 10^(-6) (M^2/N) Rubber
	0.00002f, // 0.2  x 10^(-3) (M^2/N) Muscle
	0.0001f, // 1.0  x 10^(-3) (M^2/N) Fat
};

void FAnimNode_KawaiiPhysics::AdjustByBoneConstraints()
{
	for (FModifyBoneConstraint& BoneConstraint : MergedBoneConstraints)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_AdjustByBoneConstraint);

		if (!BoneConstraint.IsValid())
		{
			continue;
		}

		FKawaiiPhysicsModifyBone& ModifyBone1 = ModifyBones[BoneConstraint.ModifyBoneIndex1];
		FKawaiiPhysicsModifyBone& ModifyBone2 = ModifyBones[BoneConstraint.ModifyBoneIndex2];
		EXPBDComplianceType ComplianceType = BoneConstraint.bOverrideCompliance
			                                     ? BoneConstraint.ComplianceType
			                                     : BoneConstraintGlobalComplianceType;

		FVector Delta = ModifyBone2.Location - ModifyBone1.Location;
		float DeltaLength = Delta.Size();
		if (DeltaLength <= 0.0f)
		{
			continue;
		}

		// PBD
		// Delta *= (DeltaLength - BoneConstraint.Length) / DeltaLength * 0.5f;
		// ModifyBone1.Location += Delta * Stiffness;
		// ModifyBone2.Location -= Delta * Stiffness;

		// XBPD
		float Constraint = DeltaLength - BoneConstraint.Length;
		float Compliance = XPBDComplianceValues[static_cast<int32>(ComplianceType)];
		Compliance /= DeltaTime * DeltaTime;
		float DeltaLambda = (Constraint - Compliance * BoneConstraint.Lambda) / (2 + Compliance); // 2 = SumMass
		Delta = (Delta / DeltaLength) * DeltaLambda;

		ModifyBone1.Location += Delta;
		ModifyBone2.Location -= Delta;
		BoneConstraint.Lambda += DeltaLambda;
	}
}

void FAnimNode_KawaiiPhysics::WarmUp(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                     FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WarmUp);

	for (int32 i = 0; i < WarmUpFrames; ++i)
	{
		SimulateModifyBones(Output, ComponentTransform);
	}
}

void FAnimNode_KawaiiPhysics::InitBoneConstraints()
{
	MergedBoneConstraints = BoneConstraints;
	MergedBoneConstraints.Append(BoneConstraintsData);

	TArray<FModifyBoneConstraint> DummyBoneConstraint;
	for (FModifyBoneConstraint& Constraint : MergedBoneConstraints)
	{
		Constraint.ModifyBoneIndex1 =
			ModifyBones.IndexOfByPredicate([Constraint](const FKawaiiPhysicsModifyBone& ModifyBone)
			{
				return ModifyBone.BoneRef == Constraint.Bone1;
			});
		if (Constraint.ModifyBoneIndex1 < 0)
		{
			continue;
		}

		Constraint.ModifyBoneIndex2 =
			ModifyBones.IndexOfByPredicate([Constraint](const FKawaiiPhysicsModifyBone& ModifyBone)
			{
				return ModifyBone.BoneRef == Constraint.Bone2;
			});
		if (Constraint.ModifyBoneIndex2 < 0)
		{
			continue;
		}

		Constraint.Length =
			(ModifyBones[Constraint.ModifyBoneIndex1].Location - ModifyBones[Constraint.ModifyBoneIndex2].Location).
			Size();

		// DummyBone"s constraint
		if (bAutoAddChildDummyBoneConstraint)
		{
			const int32 ChildDummyBoneIndex1 = ModifyBones[Constraint.ModifyBoneIndex1].ChildIndices.IndexOfByPredicate(
				[&](int32 Index)
				{
					return Index >= 0 && ModifyBones[Index].bDummy == true;
				});
			const int32 ChildDummyBoneIndex2 = ModifyBones[Constraint.ModifyBoneIndex2].ChildIndices.IndexOfByPredicate(
				[&](int32 Index)
				{
					return Index >= 0 && ModifyBones[Index].bDummy == true;
				});

			if (ChildDummyBoneIndex1 >= 0 && ChildDummyBoneIndex2 >= 0)
			{
				FModifyBoneConstraint NewDummyBoneConstraint;
				NewDummyBoneConstraint.ModifyBoneIndex1 = ModifyBones[Constraint.ModifyBoneIndex1].ChildIndices[
					ChildDummyBoneIndex1];
				NewDummyBoneConstraint.ModifyBoneIndex2 = ModifyBones[Constraint.ModifyBoneIndex2].ChildIndices[
					ChildDummyBoneIndex2];
				NewDummyBoneConstraint.Length =
					(ModifyBones[NewDummyBoneConstraint.ModifyBoneIndex1].Location - ModifyBones[NewDummyBoneConstraint.
						ModifyBoneIndex2].Location).
					Size();
				NewDummyBoneConstraint.bIsDummy = true;
				DummyBoneConstraint.Add(NewDummyBoneConstraint);
			}
		}
	}

	MergedBoneConstraints.Append(DummyBoneConstraint);
}

void FAnimNode_KawaiiPhysics::ApplySimulateResult(FComponentSpacePoseContext& Output,
                                                  const FBoneContainer& BoneContainer,
                                                  TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ApplySimulateResult);

	for (int32 i = 0; i < ModifyBones.Num(); ++i)
	{
		FTransform PoseTransform = FTransform(ModifyBones[i].PoseRotation, ModifyBones[i].PoseLocation,
		                                      ModifyBones[i].PoseScale);
		PoseTransform =
			ConvertSimulationSpaceTransform(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace,
			                                PoseTransform);
		OutBoneTransforms.Add(FBoneTransform(ModifyBones[i].BoneRef.GetCompactPoseIndex(BoneContainer), PoseTransform));
	}


	for (int32 i = 0; i < ModifyBones.Num(); ++i)
	{
		FKawaiiPhysicsModifyBone& Bone = ModifyBones[i];
		if (!Bone.HasParent())
		{
			continue;
		}

		FKawaiiPhysicsModifyBone& ParentBone = ModifyBones[Bone.ParentIndex];

		if (ParentBone.ChildIndices.Num() <= 1)
		{
			if (ParentBone.BoneRef.BoneIndex >= 0)
			{
				FVector PoseVector = Bone.PoseLocation - ParentBone.PoseLocation;
				FVector SimulateVector = Bone.Location - ParentBone.Location;

				if (PoseVector.GetSafeNormal() == SimulateVector.GetSafeNormal())
				{
					continue;
				}

				if (BoneForwardAxis == EBoneForwardAxis::X_Negative || BoneForwardAxis == EBoneForwardAxis::Y_Negative
					|| BoneForwardAxis == EBoneForwardAxis::Z_Negative)
				{
					PoseVector *= -1;
					SimulateVector *= -1;
				}

				FQuat SimulateRotation =
					FQuat::FindBetweenVectors(PoseVector, SimulateVector) * ParentBone.PoseRotation;
				ParentBone.PrevRotation = SimulateRotation;

				SimulateRotation =
					ConvertSimulationSpaceRotation(Output, SimulationSpace,
					                               EKawaiiPhysicsSimulationSpace::ComponentSpace, SimulateRotation);
				OutBoneTransforms[Bone.ParentIndex].Transform.SetRotation(SimulateRotation);
			}
		}

		if (Bone.BoneRef.BoneIndex >= 0 && !Bone.bDummy)
		{
			OutBoneTransforms[i].Transform.SetLocation(
				ConvertSimulationSpaceLocation(Output, SimulationSpace, EKawaiiPhysicsSimulationSpace::ComponentSpace,
				                               Bone.Location));
		}
	}

	OutBoneTransforms.RemoveAll([](const FBoneTransform& BoneTransform)
	{
		return BoneTransform.BoneIndex < 0;
	});

	// for check in FCSPose<PoseType>::LocalBlendCSBoneTransforms
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

FTransform FAnimNode_KawaiiPhysics::GetBoneTransformInSimSpace(FComponentSpacePoseContext& Output,
                                                               const FCompactPoseBoneIndex& BoneIndex) const
{
	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(
		Output, EKawaiiPhysicsSimulationSpace::ComponentSpace);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, SimulationSpace);
	return ConvertSimulationSpaceTransformCached(CacheFrom, CacheTo, Output.Pose.GetComponentSpaceTransform(BoneIndex));
}

FAnimNode_KawaiiPhysics::FSimulationSpaceCache FAnimNode_KawaiiPhysics::GetSimulationSpaceCacheFor(
	FComponentSpacePoseContext& Output,
	const EKawaiiPhysicsSimulationSpace Space) const
{
	if (Space == EKawaiiPhysicsSimulationSpace::ComponentSpace)
	{
		return FSimulationSpaceCache();
	}
	if (bHasCurrentEvalSimSpaceCache && Space == SimulationSpace)
	{
		return CurrentEvalSimSpaceCache;
	}
	if (bHasCurrentEvalWorldSpaceCache && Space == EKawaiiPhysicsSimulationSpace::WorldSpace)
	{
		return CurrentEvalWorldSpaceCache;
	}

	const UEnum* EnumPtr = StaticEnum<EKawaiiPhysicsSimulationSpace>();
	UE_LOG(LogKawaiiPhysics, Warning, TEXT("Building Simulation Space Cache for %s"),
	       *EnumPtr->GetNameStringByValue(static_cast<int64>(Space)));
	return BuildSimulationSpaceCache(Output, Space);
}

FTransform FAnimNode_KawaiiPhysics::ConvertSimulationSpaceTransformCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FTransform& InTransform) const
{
	FTransform ResultTransform = InTransform;
	ResultTransform = ResultTransform * CacheFrom.TargetSpaceToComponent;
	ResultTransform = ResultTransform * CacheTo.ComponentToTargetSpace;
	return ResultTransform;
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceVectorCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FVector& InVector) const
{
	FVector ResultVector = InVector;
	ResultVector = CacheFrom.TargetSpaceToComponent.TransformVector(ResultVector);
	ResultVector = CacheTo.ComponentToTargetSpace.TransformVector(ResultVector);
	return ResultVector;
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceLocationCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FVector& InLocation) const
{
	FVector ResultLocation = InLocation;
	ResultLocation = CacheFrom.TargetSpaceToComponent.TransformPosition(ResultLocation);
	ResultLocation = CacheTo.ComponentToTargetSpace.TransformPosition(ResultLocation);
	return ResultLocation;
}

FQuat FAnimNode_KawaiiPhysics::ConvertSimulationSpaceRotationCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	const FQuat& InRotation) const
{
	FQuat ResultRotation = InRotation;
	ResultRotation = CacheFrom.TargetSpaceToComponent.TransformRotation(ResultRotation);
	ResultRotation = CacheTo.ComponentToTargetSpace.TransformRotation(ResultRotation);
	return ResultRotation;
}

void FAnimNode_KawaiiPhysics::ConvertSimulationSpaceCached(
	const FSimulationSpaceCache& CacheFrom,
	const FSimulationSpaceCache& CacheTo,
	EKawaiiPhysicsSimulationSpace From,
	EKawaiiPhysicsSimulationSpace To)
{
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		Bone.Location = CacheTo.ComponentToTargetSpace.TransformPosition(
			CacheFrom.TargetSpaceToComponent.TransformPosition(Bone.Location));
		Bone.PrevLocation = CacheTo.ComponentToTargetSpace.TransformPosition(
			CacheFrom.TargetSpaceToComponent.TransformPosition(Bone.PrevLocation));

		Bone.PrevRotation = CacheTo.ComponentToTargetSpace.TransformRotation(
			CacheFrom.TargetSpaceToComponent.TransformRotation(Bone.PrevRotation));
	}
}

FTransform FAnimNode_KawaiiPhysics::ConvertSimulationSpaceTransform(FComponentSpacePoseContext& Output,
                                                                    const EKawaiiPhysicsSimulationSpace From,
                                                                    const EKawaiiPhysicsSimulationSpace To,
                                                                    const FTransform& InTransform) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceTransform);
	if (From == To)
	{
		return InTransform;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceTransformCached(CacheFrom, CacheTo, InTransform);
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceVector(FComponentSpacePoseContext& Output,
                                                              const EKawaiiPhysicsSimulationSpace From,
                                                              const EKawaiiPhysicsSimulationSpace To,
                                                              const FVector& InVector) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceVector);
	if (From == To)
	{
		return InVector;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceVectorCached(CacheFrom, CacheTo, InVector);
}

FVector FAnimNode_KawaiiPhysics::ConvertSimulationSpaceLocation(FComponentSpacePoseContext& Output,
                                                                const EKawaiiPhysicsSimulationSpace From,
                                                                const EKawaiiPhysicsSimulationSpace To,
                                                                const FVector& InLocation) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceLocation);
	if (From == To)
	{
		return InLocation;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceLocationCached(CacheFrom, CacheTo, InLocation);
}

FQuat FAnimNode_KawaiiPhysics::ConvertSimulationSpaceRotation(FComponentSpacePoseContext& Output,
                                                              const EKawaiiPhysicsSimulationSpace From,
                                                              const EKawaiiPhysicsSimulationSpace To,
                                                              const FQuat& InRotation) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpaceRotation);
	if (From == To)
	{
		return InRotation;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	return ConvertSimulationSpaceRotationCached(CacheFrom, CacheTo, InRotation);
}

void FAnimNode_KawaiiPhysics::ConvertSimulationSpace(FComponentSpacePoseContext& Output,
                                                     const EKawaiiPhysicsSimulationSpace From,
                                                     const EKawaiiPhysicsSimulationSpace To)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ConvertSimulationSpace);
	if (From == To)
	{
		return;
	}

	const FSimulationSpaceCache CacheFrom = GetSimulationSpaceCacheFor(Output, From);
	const FSimulationSpaceCache CacheTo = GetSimulationSpaceCacheFor(Output, To);
	ConvertSimulationSpaceCached(CacheFrom, CacheTo, From, To);
}

FAnimNode_KawaiiPhysics::FSimulationSpaceCache FAnimNode_KawaiiPhysics::BuildSimulationSpaceCache(
	FComponentSpacePoseContext& Output,
	const EKawaiiPhysicsSimulationSpace SimulationSpaceForCache) const
{
	FSimulationSpaceCache Cache;

	switch (SimulationSpaceForCache)
	{
	default:
	case EKawaiiPhysicsSimulationSpace::ComponentSpace:
		Cache.ComponentToTargetSpace = FTransform::Identity;
		Cache.TargetSpaceToComponent = FTransform::Identity;
		break;

	case EKawaiiPhysicsSimulationSpace::WorldSpace:
		{
			const FTransform& ComponentToWorld = Output.AnimInstanceProxy->GetComponentTransform();
			Cache.ComponentToTargetSpace = ComponentToWorld; // Component -> World
			Cache.TargetSpaceToComponent = ComponentToWorld.Inverse(); // World -> Component
		}
		break;

	case EKawaiiPhysicsSimulationSpace::BaseBoneSpace:

		if (SimulationBaseBone.IsValidToEvaluate())
		{
			const FCompactPoseBoneIndex BaseBoneIndex =
				SimulationBaseBone.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer());
			Cache.TargetSpaceToComponent =
				Output.Pose.GetComponentSpaceTransform(BaseBoneIndex); // Base -> Component
			Cache.ComponentToTargetSpace = Cache.TargetSpaceToComponent.Inverse(); // Component -> Base
		}
		break;
	}

	return Cache;
}

void FAnimNode_KawaiiPhysics::InitSyncBones(FComponentSpacePoseContext& Output)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	for (FKawaiiPhysicsSyncBone& SyncBone : SyncBones)
	{
		InitSyncBone(Output, BoneContainer, SyncBone);
	}
}

void FAnimNode_KawaiiPhysics::InitSyncBone(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer,
                                           FKawaiiPhysicsSyncBone& SyncBone)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitSyncBone);

	SyncBone.Bone.Initialize(BoneContainer);
	if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
	{
		SyncBone.InitialPoseLocation = FVector::ZeroVector;
		return;
	}
	SyncBone.InitialPoseLocation =
			FAnimationRuntime::GetComponentSpaceTransformRefPose(BoneContainer.GetReferenceSkeleton(),
			                                                     SyncBone.Bone.BoneIndex).GetLocation();

	// cleanup
	SyncBone.TargetRoots.RemoveAll([&](const FKawaiiPhysicsSyncTarget& Target)
	{
		return !Target.IsValid(BoneContainer);
	});

	for (auto& TargetRoot : SyncBone.TargetRoots)
	{
		TargetRoot.ChildTargets.Empty();

		TargetRoot.ModifyBoneIndex = ModifyBones.IndexOfByPredicate(
			[&](const FKawaiiPhysicsModifyBone& ModifyBone) { return ModifyBone.BoneRef == TargetRoot.Bone; });
		if (TargetRoot.ModifyBoneIndex == INDEX_NONE || !TargetRoot.bIncludeChildBones)
		{
			continue;
		}

		// For Calculate LengthRateFromSyncTargetRoot
		const float StartLength = ModifyBones[TargetRoot.ModifyBoneIndex].LengthFromRoot;
		float MaxLength = StartLength;

		// Collect Child Bones
		TArray<int32> IndicesToProcess = ModifyBones[TargetRoot.ModifyBoneIndex].ChildIndices;
		while (!IndicesToProcess.IsEmpty())
		{
			const int32 CurrentIndex = IndicesToProcess.Pop();
			if (!ModifyBones.IsValidIndex(CurrentIndex))
			{
				continue;
			}

			TargetRoot.ChildTargets.AddUnique({CurrentIndex});

#if WITH_EDITORONLY_DATA
			TargetRoot.ChildTargets.Last().PreviewBone = ModifyBones[CurrentIndex].BoneRef;
#endif

			IndicesToProcess.Append(ModifyBones[CurrentIndex].ChildIndices);
			MaxLength = FMath::Max(MaxLength, ModifyBones[CurrentIndex].LengthFromRoot);
		}

		// Calculate LengthRateFromSyncTargetRoot
		const float LengthRange = MaxLength - StartLength;
		TargetRoot.LengthRateFromSyncTargetRoot = 0.0f;
		for (auto& Target : TargetRoot.ChildTargets)
		{
			if (LengthRange > KINDA_SMALL_NUMBER)
			{
				Target.LengthRateFromSyncTargetRoot =
					(ModifyBones[Target.ModifyBoneIndex].LengthFromRoot - StartLength) / LengthRange;
			}
			else
			{
				Target.LengthRateFromSyncTargetRoot = 0.0f;
			}
		}

		// Update Alpha by Length Rate & Curve
		if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
			ScaleCurve && !ScaleCurve->IsEmpty())
		{
			TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
			for (auto& Target : TargetRoot.ChildTargets)
			{
				Target.UpdateScaleByLengthRate(ScaleCurve);
			}
		}
	}
}

void FAnimNode_KawaiiPhysics::ApplySyncBones(FComponentSpacePoseContext& Output,
                                             const FBoneContainer& BoneContainer)
{
	if (SyncBones.Num() == 0)
	{
		return;
	}

	for (auto& SyncBone : SyncBones)
	{
		SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_ApplySyncBone);

		if (!SyncBone.Bone.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		// Calculate Delta Movement in Component Space
		const FCompactPoseBoneIndex SyncBoneIndex = SyncBone.Bone.GetCompactPoseIndex(BoneContainer);
		FVector DeltaMovement = Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation() - SyncBone.
			InitialPoseLocation;

#if WITH_EDITORONLY_DATA
		SyncBone.DeltaDistance = DeltaMovement;
#endif

		// Apply Curve
		if (const FRichCurve* ScaleCurve = SyncBone.ScaleCurveByDeltaDistance.GetRichCurveConst();
			ScaleCurve && !ScaleCurve->IsEmpty())
		{
			DeltaMovement *= ScaleCurve->Eval(DeltaMovement.Length());
		}

		// Apply Global Alpha
		DeltaMovement *= SyncBone.GlobalScale;

#if WITH_EDITORONLY_DATA
		SyncBone.ScaledDeltaDistance = DeltaMovement;
#endif

		// Filter direction once per SyncBone in Component Space
		auto CheckDirection = [](const float Val, const ESyncBoneDirection Dir)
		{
			return (Dir == ESyncBoneDirection::Both) ||
				(Dir == ESyncBoneDirection::Positive && Val > 0.0) ||
				(Dir == ESyncBoneDirection::Negative && Val < 0.0);
		};

		FVector FilteredDeltaMovement(
			CheckDirection(DeltaMovement.X, SyncBone.ApplyDirectionX) ? DeltaMovement.X : 0.0,
			CheckDirection(DeltaMovement.Y, SyncBone.ApplyDirectionY) ? DeltaMovement.Y : 0.0,
			CheckDirection(DeltaMovement.Z, SyncBone.ApplyDirectionZ) ? DeltaMovement.Z : 0.0
		);

		if (FilteredDeltaMovement.IsNearlyZero())
		{
			continue;
		}

		// Convert to Simulation Space
		FilteredDeltaMovement = ConvertSimulationSpaceVector(Output,
		                                                     EKawaiiPhysicsSimulationSpace::ComponentSpace,
		                                                     SimulationSpace, FilteredDeltaMovement);

		// Cache SyncBone location in Simulation Space for distance attenuation
		const FVector SyncBoneLocationInSimulationSpace = ConvertSimulationSpaceLocation(
			Output,
			EKawaiiPhysicsSimulationSpace::ComponentSpace,
			SimulationSpace,
			Output.Pose.GetComponentSpaceTransform(SyncBoneIndex).GetLocation()
		);

		// Helper: compute attenuation alpha for a distance
		auto CalcAttenuationAlpha = [&](const float Distance) -> float
		{
			if (!SyncBone.bEnableDistanceAttenuation)
			{
				return 1.0f;
			}

			const float Inner = SyncBone.AttenuationInnerRadius;
			const float Outer = SyncBone.AttenuationOuterRadius;
			const float MaxAtten = SyncBone.MaxAttenuationRate;

			// Safety: if outer <= inner, treat as step function at inner
			const float EffectiveOuter = FMath::Max(Outer, Inner);

			float AttenAmount;
			if (Distance <= Inner)
			{
				AttenAmount = 0.0f;
			}
			else if (Distance >= EffectiveOuter)
			{
				AttenAmount = MaxAtten;
			}
			else
			{
				const float Denom = EffectiveOuter - Inner;
				const float T = (Denom > KINDA_SMALL_NUMBER) ? ((Distance - Inner) / Denom) : 1.0f;
				AttenAmount = T * MaxAtten;
			}

			// Convert attenuation amount to alpha multiplier
			return FMath::Max(0.0f, 1.0f - AttenAmount);
		};

		// Apply to Targets
		for (auto& TargetRoot : SyncBone.TargetRoots)
		{
			// Update Alpha by Length Rate & Curve
			// TODO : Need flag to optimize for skip updating Scale after InitSyncBone
			if (const FRichCurve* ScaleCurve = TargetRoot.ScaleCurveByBoneLengthRate.GetRichCurveConst();
				ScaleCurve && !ScaleCurve->IsEmpty())
			{
				TargetRoot.UpdateScaleByLengthRate(ScaleCurve);
				for (auto& Target : TargetRoot.ChildTargets)
				{
					Target.UpdateScaleByLengthRate(ScaleCurve);
				}
			}

			// Root target
			{
				const int32 ModifyBoneIndex = TargetRoot.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					TargetRoot.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}

			// Child targets
			for (auto& Target : TargetRoot.ChildTargets)
			{
				const int32 ModifyBoneIndex = Target.ModifyBoneIndex;
				if (ModifyBones.IsValidIndex(ModifyBoneIndex))
				{
					const float Dist = FVector::Dist(SyncBoneLocationInSimulationSpace,
					                                 ModifyBones[ModifyBoneIndex].Location);
					const float AlphaMul = CalcAttenuationAlpha(Dist);
					Target.Apply(ModifyBones, FilteredDeltaMovement * AlphaMul);
				}
				else
				{
					Target.Apply(ModifyBones, FilteredDeltaMovement);
				}
			}
		}
	}
}

