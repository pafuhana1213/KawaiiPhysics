#include "AnimNode_KawaiiPhysics.h"

#include "AnimationRuntime.h"
#include "KawaiiPhysicsBoneConstraintsDataAsset.h"
#include "KawaiiPhysicsCustomExternalForce.h"
#include "KawaiiPhysicsExternalForce.h"
#include "KawaiiPhysicsLimitsDataAsset.h"
#include "Animation/AnimInstanceProxy.h"
#include "Curves/CurveFloat.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SceneInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#endif

#if ENABLE_ANIM_DEBUG
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsEnable(
	TEXT("a.AnimNode.KawaiiPhysics.Enable"), true, TEXT("Enable/Disable KawaiiPhysics"));
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebug(
	TEXT("a.AnimNode.KawaiiPhysics.Debug"), false, TEXT("Turn on visualization debugging for KawaiiPhysics"));
TAutoConsoleVariable<bool> CVarAnimNodeKawaiiPhysicsDebugLengthRate(
	TEXT("a.AnimNode.KawaiiPhysics.Debug.LengthRate"), false,
	TEXT("Turn on visualization debugging for KawaiiPhysics Bone's LengthRate"));
#endif

DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_InitModifyBones"), STAT_KawaiiPhysics_InitModifyBones, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_Eval"), STAT_KawaiiPhysics_Eval, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_SimulatemodifyBones"), STAT_KawaiiPhysics_SimulatemodifyBones, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_Simulate"), STAT_KawaiiPhysics_Simulate, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_GetWindVelocity"), STAT_KawaiiPhysics_GetWindVelocity, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_WorldCollision"), STAT_KawaiiPhysics_WorldCollision, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AdjustByCollision"), STAT_KawaiiPhysics_AdjustByCollision, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_AdjustByBoneConstraint"), STAT_KawaiiPhysics_AdjustByBoneConstraint,
                   STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateSphericalLimit"), STAT_KawaiiPhysics_UpdateSphericalLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdatePlanerLimit"), STAT_KawaiiPhysics_UpdatePlanerLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_WarmUp"), STAT_KawaiiPhysics_WarmUp, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdatePhysicsSetting"), STAT_KawaiiPhysics_UpdatePhysicsSetting, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateCapsuleLimit"), STAT_KawaiiPhysics_UpdateCapsuleLimit, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("KawaiiPhysics_UpdateBoxLimit"), STAT_KawaiiPhysics_UpdateBoxLimit, STATGROUP_Anim);

FAnimNode_KawaiiPhysics::FAnimNode_KawaiiPhysics()
	: DeltaTime(0)
	  , DeltaTimeOld(0)
	  , bResetDynamics(false)
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

	bResetDynamics = false;

	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.Initialize(Context);
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
	bResetDynamics |= (ETeleportType::ResetPhysics == InTeleportType);
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
void FAnimNode_KawaiiPhysics::AnimDrawDebug(const FComponentSpacePoseContext& Output)
{
	if (const UWorld* World = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetWorld(); !World->IsPreviewWorld())
	{
		if (Output.AnimInstanceProxy->GetSkelMeshComponent()->bRecentlyRendered)
		{
			if (CVarAnimNodeKawaiiPhysicsDebug.GetValueOnAnyThread())
			{
				const auto AnimInstanceProxy = Output.AnimInstanceProxy;

				// Modify Bones
				for (const auto& ModifyBone : ModifyBones)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						ModifyBone.Location);
					auto Color = ModifyBone.bDummy ? FColor::Red : FColor::Yellow;
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, ModifyBone.PhysicsSettings.Radius, 8,
					                                       Color, false, -1, 0, SDPG_Foreground);

#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
					// print bone length rate
					// In 5.4, there is an engine issue that AnimDrawDebugInWorldMessage does not respect SkeletalMeshComponent's world coordinates.
					if (CVarAnimNodeKawaiiPhysicsDebugLengthRate.GetValueOnAnyThread())
					{
						AnimInstanceProxy->AnimDrawDebugInWorldMessage(
							FString::Printf(TEXT("%.2f"), ModifyBone.LengthRateFromRoot),
							ModifyBone.Location, FColor::White, 1.0f);
					}
#endif
				}
				// Sphere limit
				for (const auto& SphericalLimit : SphericalLimits)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						SphericalLimit.Location);
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Orange,
					                                       false, -1, 0, SDPG_Foreground);
				}
				for (const auto& SphericalLimit : SphericalLimitsData)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						SphericalLimit.Location);
					AnimInstanceProxy->AnimDrawDebugSphere(LocationWS, SphericalLimit.Radius, 8, FColor::Blue,
					                                       false, -1, 0, SDPG_Foreground);
				}

				// Box limit
				for (const auto& BoxLimit : BoxLimits)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						BoxLimit.Location);

					// TODO
				}
				for (const auto& BoxLimit : BoxLimitsData)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						BoxLimit.Location);

					// TODO
				}

#if	ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
				// Capsule limit
				for (const auto& CapsuleLimit : CapsuleLimits)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						CapsuleLimit.Location);
					const FQuat RotationWS = AnimInstanceProxy->GetComponentTransform().TransformRotation(
						CapsuleLimit.Rotation);

					AnimInstanceProxy->AnimDrawDebugCapsule(LocationWS, CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, RotationWS.Rotator(),
					                                        FColor::Orange);
				}
				for (const auto& CapsuleLimit : CapsuleLimitsData)
				{
					const FVector LocationWS = AnimInstanceProxy->GetComponentTransform().TransformPosition(
						CapsuleLimit.Location);
					const FQuat RotationWS = AnimInstanceProxy->GetComponentTransform().TransformRotation(
						CapsuleLimit.Rotation);

					AnimInstanceProxy->AnimDrawDebugCapsule(LocationWS, CapsuleLimit.Length * 0.5f,
					                                        CapsuleLimit.Radius, RotationWS.Rotator(),
					                                        FColor::Blue);
				}
#endif
			}
		}
	}
}
#endif

void FAnimNode_KawaiiPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                                TArray<FBoneTransform>& OutBoneTransforms)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_Eval);

	check(OutBoneTransforms.Num() == 0);

	if (bResetDynamics)
	{
		ModifyBones.Empty(ModifyBones.Num());
		bResetDynamics = false;
		bInitPhysicsSettings = false;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

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
		InitBoneConstraints();
		PreSkelCompTransform = ComponentTransform;
	}

	// Update each parameters and collision
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

	// Update SkeletalMeshComponent movement in World Space
	UpdateSkelCompMove(ComponentTransform);

	// Simulate Physics and Apply
	if (bNeedWarmUp && WarmUpFrames > 0)
	{
		WarmUp(Output, BoneContainer, ComponentTransform);
		bNeedWarmUp = false;
	}
	SimulateModifyBones(Output, ComponentTransform);
	ApplySimulateResult(Output, BoneContainer, OutBoneTransforms);

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

	Initialize(SphericalLimits);
	Initialize(CapsuleLimits);
	Initialize(BoxLimits);
	Initialize(PlanarLimits);

	for (auto& BoneConstraint : BoneConstraints)
	{
		BoneConstraint.InitializeBone(RequiredBones);
	}
}

void FAnimNode_KawaiiPhysics::InitModifyBones(FComponentSpacePoseContext& Output, const FBoneContainer& BoneContainer)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_InitModifyBones);

	const USkeleton* Skeleton = BoneContainer.GetSkeletonAsset();
	auto& RefSkeleton = Skeleton->GetReferenceSkeleton();

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
		for (auto BodySetup : PhysicsAssetForLimits->SkeletalBodySetups)
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

	auto& RefBonePoseTransform = Output.Pose.GetComponentSpaceTransform(NewModifyBone.BoneRef.CachedCompactPoseIndex);
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

int32 FAnimNode_KawaiiPhysics::CollectChildBones(const FReferenceSkeleton& RefSkeleton, int32 ParentBoneIndex,
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
	}
	else
	{
		if (!Bone.bDummy)
		{
			Bone.LengthFromRoot = InModifyBones[Bone.ParentIndex].LengthFromRoot
				+ RefBonePose[Bone.BoneRef.BoneIndex].GetLocation().Size();
		}
		else
		{
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
		Bone.PhysicsSettings.Damping = PhysicsSettings.Damping;
		if (!DampingCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.Damping *= DampingCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.Damping = FMath::Clamp(Bone.PhysicsSettings.Damping, 0.0f, 1.0f);

		// WorldLocationDamping
		Bone.PhysicsSettings.WorldDampingLocation = PhysicsSettings.WorldDampingLocation;
		if (!WorldDampingLocationCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.WorldDampingLocation *= WorldDampingLocationCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.WorldDampingLocation = FMath::Clamp(Bone.PhysicsSettings.WorldDampingLocation, 0.0f,
		                                                         1.0f);

		// WorldRotationDamping
		Bone.PhysicsSettings.WorldDampingRotation = PhysicsSettings.WorldDampingRotation;
		if (!WorldDampingRotationCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.WorldDampingRotation *= WorldDampingRotationCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.WorldDampingRotation = FMath::Clamp(Bone.PhysicsSettings.WorldDampingRotation, 0.0f,
		                                                         1.0f);

		// Stiffness
		Bone.PhysicsSettings.Stiffness = PhysicsSettings.Stiffness;
		if (!StiffnessCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.Stiffness *= StiffnessCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.Stiffness = FMath::Clamp(Bone.PhysicsSettings.Stiffness, 0.0f, 1.0f);

		// Radius
		Bone.PhysicsSettings.Radius = PhysicsSettings.Radius;
		if (!RadiusCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.Radius *= RadiusCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.Radius = FMath::Max(Bone.PhysicsSettings.Radius, 0.0f);

		// LimitAngle
		Bone.PhysicsSettings.LimitAngle = PhysicsSettings.LimitAngle;
		if (!LimitAngleCurveData.GetRichCurve()->IsEmpty())
		{
			Bone.PhysicsSettings.LimitAngle *= LimitAngleCurveData.GetRichCurve()->Eval(LengthRate);
		}
		Bone.PhysicsSettings.LimitAngle = FMath::Max(Bone.PhysicsSettings.LimitAngle, 0.0f);
	}
}


void FAnimNode_KawaiiPhysics::UpdateSphericalLimits(TArray<FSphericalLimit>& Limits, FComponentSpacePoseContext& Output,
                                                    const FBoneContainer& BoneContainer,
                                                    const FTransform& ComponentTransform)
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
                                                  const FTransform& ComponentTransform)
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
                                              const FTransform& ComponentTransform)
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
                                                 const FTransform& ComponentTransform)
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
			Planar.Location = BoneTransform.GetLocation();
			Planar.Rotation = BoneTransform.GetRotation();
			Planar.Rotation.Normalize();
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());

			Planar.bEnable = true;
		}
		else
		{
			Planar.Location = Planar.OffsetLocation;
			Planar.Rotation = Planar.OffsetRotation.Quaternion();
			Planar.Rotation.Normalize();
			Planar.Plane = FPlane(Planar.Location, Planar.Rotation.GetUpVector());

			// Maybe the DrivingBone is set to empty for the floor, so keep Enable
			// Planar.bEnable = false;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateModifyBonesPoseTransform(FComponentSpacePoseContext& Output,
                                                             const FBoneContainer& BoneContainer)
{
	for (auto& Bone : ModifyBones)
	{
		if (!Bone.bDummy)
		{
			Bone.UpdatePoseTransform(BoneContainer, Output.Pose, ResetBoneTransformWhenBoneNotFound);
		}
		else
		{
			auto ParentBone = ModifyBones[Bone.ParentIndex];
			Bone.PoseLocation = ParentBone.PoseLocation + GetBoneForwardVector(ParentBone.PoseRotation) *
				DummyBoneLength;
			Bone.PoseRotation = ParentBone.PoseRotation;
			Bone.PoseScale = ParentBone.PoseScale;
		}
	}
}

void FAnimNode_KawaiiPhysics::UpdateSkelCompMove(const FTransform& ComponentTransform)
{
	SkelCompMoveVector = ComponentTransform.InverseTransformPosition(PreSkelCompTransform.GetLocation());
	if (SkelCompMoveVector.SizeSquared() > TeleportDistanceThreshold * TeleportDistanceThreshold)
	{
		SkelCompMoveVector = FVector::ZeroVector;
	}

	SkelCompMoveRotation = ComponentTransform.InverseTransformRotation(PreSkelCompTransform.GetRotation());
	if (TeleportRotationThreshold >= 0 && FMath::RadiansToDegrees(SkelCompMoveRotation.GetAngle()) >
		TeleportRotationThreshold)
	{
		SkelCompMoveRotation = FQuat::Identity;
	}

	PreSkelCompTransform = ComponentTransform;
}

void FAnimNode_KawaiiPhysics::SimulateModifyBones(FComponentSpacePoseContext& Output,
                                                  const FTransform& ComponentTransform)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_SimulatemodifyBones);

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
			Force.PreApply(*this, SkelComp);
		}
	}

	// Simulate
	const float Exponent = TargetFramerate * DeltaTime;
	const FVector GravityCS = ComponentTransform.InverseTransformVector(Gravity);
	const UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
	const FSceneInterface* Scene = World ? World->Scene : nullptr;
	for (FKawaiiPhysicsModifyBone& Bone : ModifyBones)
	{
		if (Bone.bSkipSimulate)
		{
			continue;
		}
		Simulate(Bone, Scene, ComponentTransform, GravityCS, Exponent, SkelComp, Output);
	}

	// External Force : PostApply
	for (int i = 0; i < ExternalForces.Num(); ++i)
	{
		if (ExternalForces[i].IsValid())
		{
			auto& Force = ExternalForces[i].GetMutable<FKawaiiPhysics_ExternalForce>();
			Force.PostApply(*this);
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
			AdjustByWorldCollision(Bone, SkelComp);
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
                                       const FTransform& ComponentTransform, const FVector& GravityCS,
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
		Velocity += GetWindVelocity(Scene, ComponentTransform, Bone) * TargetFramerate;
	}
	Bone.Location += Velocity * DeltaTime;

	// Follow Translation
	Bone.Location += SkelCompMoveVector * (1.0f - Bone.PhysicsSettings.WorldDampingLocation);

	// Follow Rotation
	Bone.Location += (SkelCompMoveRotation.RotateVector(Bone.PrevLocation) - Bone.PrevLocation)
		* (1.0f - Bone.PhysicsSettings.WorldDampingRotation);

	// Gravity
	// TODO:Migrate if there are more good method (Currently copying AnimDynamics implementation)
	Bone.Location += 0.5 * GravityCS * DeltaTime * DeltaTime;

	// External Force
	// NOTE: if use foreach, you may get issue ( Array has changed during ranged-for iteration )
	for (int i = 0; i < CustomExternalForces.Num(); ++i)
	{
		if (CustomExternalForces[i] && CustomExternalForces[i]->bIsEnabled)
		{
			FTransform BoneTM = FTransform::Identity;
			if (Bone.bDummy)
			{
				BoneTM = Output.Pose.GetComponentSpaceTransform(
					ParentBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
			}
			else
			{
				BoneTM = Output.Pose.GetComponentSpaceTransform(
					Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
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
						BoneTM = Output.Pose.GetComponentSpaceTransform(
							ParentBone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
					}
					else
					{
						BoneTM = Output.Pose.GetComponentSpaceTransform(
							Bone.BoneRef.GetCompactPoseIndex(Output.Pose.GetPose().GetBoneContainer()));
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

FVector FAnimNode_KawaiiPhysics::GetWindVelocity(const FSceneInterface* Scene, const FTransform& ComponentTransform,
                                                 const FKawaiiPhysicsModifyBone& Bone) const
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_GetWindVelocity);

	FVector WindDirection = FVector::ZeroVector;
	float WindSpeed = 0.0f;
	float WindMinGust = 0.0f;
	float WindMaxGust = 0.0f;

	Scene->GetWindParameters_GameThread(ComponentTransform.TransformPosition(Bone.PoseLocation), WindDirection,
	                                    WindSpeed, WindMinGust, WindMaxGust);
	WindDirection = ComponentTransform.Inverse().TransformVector(WindDirection);
	FVector WindVelocity = WindDirection * WindSpeed * WindScale;

	// TODO:Migrate if there are more good method (Currently copying AnimDynamics implementation)
	WindVelocity *= FMath::FRandRange(0.0f, 2.0f);

	return WindVelocity;
}

void FAnimNode_KawaiiPhysics::AdjustByWorldCollision(FKawaiiPhysicsModifyBone& Bone,
                                                     const USkeletalMeshComponent* OwningComp)
{
	SCOPE_CYCLE_COUNTER(STAT_KawaiiPhysics_WorldCollision);

	if (!OwningComp || Bone.ParentIndex < 0)
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
	auto CompTransform = OwningComp->GetComponentTransform();

	if (const UWorld* World = OwningComp->GetWorld())
	{
		if (bIgnoreSelfComponent)
		{
			// Do sphere sweep
			FHitResult Result;
			bool bHit = World->SweepSingleByChannel(Result, CompTransform.TransformPosition(Bone.PrevLocation),
			                                        CompTransform.TransformPosition(Bone.Location), FQuat::Identity,
			                                        TraceChannel,
			                                        FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius), Params,
			                                        ResponseParams);
			if (bHit)
			{
				if (Result.bStartPenetrating)
				{
					Bone.Location = CompTransform.InverseTransformPosition(
						CompTransform.TransformPosition(Bone.Location) + (Result.Normal * Result.PenetrationDepth));
				}
				else
				{
					Bone.Location = CompTransform.InverseTransformPosition(Result.Location);
				}
			}
		}
		else
		{
			// Do sphere sweep and ignore bones later
			TArray<FHitResult> Results;
			bool bHit = World->SweepMultiByChannel(Results, CompTransform.TransformPosition(Bone.PrevLocation),
			                                       CompTransform.TransformPosition(Bone.Location), FQuat::Identity,
			                                       TraceChannel,
			                                       FCollisionShape::MakeSphere(Bone.PhysicsSettings.Radius), Params,
			                                       ResponseParams);
			if (bHit)
			{
				bool IsIgnoreHit;
				for (const auto& Hit : Results)
				{
					if (Hit.bBlockingHit)
					{
						//should we ignore this hit?
						IsIgnoreHit = false;
						if (Hit.Component == OwningComp && Hit.BoneName != NAME_None)
						{
							IsIgnoreHit = Hit.BoneName == Bone.BoneRef.BoneName;
							if (!IsIgnoreHit)
							{
								for (auto BoneRef : IgnoreBones)
								{
									if (BoneRef.BoneName == Hit.BoneName)
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
									if (Hit.BoneName.ToString().StartsWith(BoneNamePrefix.ToString()))
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
							if (Hit.bStartPenetrating)
							{
								Bone.Location = CompTransform.InverseTransformPosition(
									CompTransform.TransformPosition(Bone.Location) + (Hit.Normal * Hit.
										PenetrationDepth));
							}
							else
							{
								Bone.Location = CompTransform.InverseTransformPosition(Hit.Location);
							}
							break;
						}
					}
				}
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
			Bone.Location = Sphere.Location + (Sphere.Radius - Bone.PhysicsSettings.Radius) * (Bone.Location - Sphere.
				Location).GetSafeNormal();
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
	for (int32 i = 0; i < ModifyBones.Num(); ++i)
	{
		OutBoneTransforms.Add(FBoneTransform(ModifyBones[i].BoneRef.GetCompactPoseIndex(BoneContainer),
		                                     FTransform(ModifyBones[i].PoseRotation, ModifyBones[i].PoseLocation,
		                                                ModifyBones[i].PoseScale)));
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

				FQuat SimulateRotation = FQuat::FindBetweenVectors(PoseVector, SimulateVector) * ParentBone.
					PoseRotation;
				OutBoneTransforms[Bone.ParentIndex].Transform.SetRotation(SimulateRotation);
				ParentBone.PrevRotation = SimulateRotation;
			}
		}

		if (Bone.BoneRef.BoneIndex >= 0 && !Bone.bDummy)
		{
			OutBoneTransforms[i].Transform.SetLocation(Bone.Location);
		}
	}

	OutBoneTransforms.RemoveAll([](const FBoneTransform& BoneTransform)
	{
		return BoneTransform.BoneIndex < 0;
	});

	// for check in FCSPose<PoseType>::LocalBlendCSBoneTransforms
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
