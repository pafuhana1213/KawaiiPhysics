/** Copy from FAnimNodeEditMode to override **/

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "KawaiiPhysicsEditModeBase.h"
#include "EditorViewportClient.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "EngineUtils.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AssetEditorModeManager.h"

#define LOCTEXT_NAMESPACE "AnimNodeEditMode"

FKawaiiPhysicsEditModeBase::FKawaiiPhysicsEditModeBase()
	: AnimNode(nullptr)
	, RuntimeAnimNode(nullptr)
	, bManipulating(false)
	, bInTransaction(false)
{
	// Disable grid drawing for this mode as the viewport handles this
	bDrawGrid = false;
}

bool FKawaiiPhysicsEditModeBase::GetCameraTarget(FSphere& OutTarget) const
{
	FVector Location(GetWidgetLocation());
	OutTarget.Center = Location;
	OutTarget.W = 50.0f;

	return true;
}

IPersonaPreviewScene& FKawaiiPhysicsEditModeBase::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FKawaiiPhysicsEditModeBase::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
	if (AnimNode != nullptr)
	{
		AnimNode->GetOnScreenDebugInfo(OutDebugInfo, RuntimeAnimNode, GetAnimPreviewScene().GetPreviewMeshComponent());
	}
}

ECoordSystem FKawaiiPhysicsEditModeBase::GetWidgetCoordinateSystem() const
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return (ECoordSystem)SkelControl->GetWidgetCoordinateSystem(GetAnimPreviewScene().GetPreviewMeshComponent());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return ECoordSystem::COORD_None;
}

UE_WIDGET::EWidgetMode FKawaiiPhysicsEditModeBase::GetWidgetMode() const
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return (UE_WIDGET::EWidgetMode)SkelControl->GetWidgetMode(GetAnimPreviewScene().GetPreviewMeshComponent());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return UE_WIDGET::EWidgetMode::WM_None;
}

UE_WIDGET::EWidgetMode FKawaiiPhysicsEditModeBase::ChangeToNextWidgetMode(UE_WIDGET::EWidgetMode CurWidgetMode)
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return (UE_WIDGET::EWidgetMode)SkelControl->ChangeToNextWidgetMode(GetAnimPreviewScene().GetPreviewMeshComponent(), CurWidgetMode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return UE_WIDGET::EWidgetMode::WM_None;
}

bool FKawaiiPhysicsEditModeBase::SetWidgetMode(UE_WIDGET::EWidgetMode InWidgetMode)
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return SkelControl->SetWidgetMode(GetAnimPreviewScene().GetPreviewMeshComponent(), InWidgetMode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return false;
}

FName FKawaiiPhysicsEditModeBase::GetSelectedBone() const
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return SkelControl->FindSelectedBone();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return NAME_None;
}

void FKawaiiPhysicsEditModeBase::EnterMode(UAnimGraphNode_Base* InEditorNode, FAnimNode_Base* InRuntimeNode)
{
	AnimNode = InEditorNode;
	RuntimeAnimNode = InRuntimeNode;

	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->MoveSelectActorLocation(GetAnimPreviewScene().GetPreviewMeshComponent(), (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		SkelControl->CopyNodeDataTo(RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	GetModeManager()->SetCoordSystem(GetWidgetCoordinateSystem());
	GetModeManager()->SetWidgetMode(GetWidgetMode());
}

void FKawaiiPhysicsEditModeBase::ExitMode()
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->DeselectActor(GetAnimPreviewScene().GetPreviewMeshComponent());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	AnimNode = nullptr;
	RuntimeAnimNode = nullptr;
}

void FKawaiiPhysicsEditModeBase::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	if (AnimNode != nullptr)
	{
		AnimNode->Draw(PDI, GetAnimPreviewScene().GetPreviewMeshComponent());
	}
}

void FKawaiiPhysicsEditModeBase::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (AnimNode != nullptr)
	{
		AnimNode->DrawCanvas(*Viewport, *const_cast<FSceneView*>(View), *Canvas, GetAnimPreviewScene().GetPreviewMeshComponent());
	}
}

bool FKawaiiPhysicsEditModeBase::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HitProxy != nullptr && HitProxy->IsA(HActor::StaticGetType()))
	{
		HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
		GetAnimPreviewScene().SetSelectedActor(ActorHitProxy->Actor);

		UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
		if (SkelControl != nullptr)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
				SkelControl->ProcessActorClick(ActorHitProxy);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		return true;
	}

	return false;
}

FVector FKawaiiPhysicsEditModeBase::GetWidgetLocation() const
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			return SkelControl->GetWidgetLocation(GetAnimPreviewScene().GetPreviewMeshComponent(), (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	return FVector::ZeroVector;
}

bool FKawaiiPhysicsEditModeBase::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (!bInTransaction)
	{
		GEditor->BeginTransaction(LOCTEXT("EditSkelControlNodeTransaction", "Edit Skeletal Control Node"));
		AnimNode->SetFlags(RF_Transactional);
		AnimNode->Modify();
		bInTransaction = true;
	}

	bManipulating = true;

	return true;
}

bool FKawaiiPhysicsEditModeBase::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (bManipulating)
	{
		bManipulating = false;
	}

	if (bInTransaction)
	{
		GEditor->EndTransaction();
		bInTransaction = false;
	}

	return true;
}

bool FKawaiiPhysicsEditModeBase::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	bool bHandled = false;

	// Handle switching modes - only allowed when not already manipulating
	if ((InEvent == IE_Pressed) && (InKey == EKeys::SpaceBar) && !bManipulating)
	{
		UE_WIDGET::EWidgetMode WidgetMode = (UE_WIDGET::EWidgetMode)ChangeToNextWidgetMode(GetModeManager()->GetWidgetMode());
		GetModeManager()->SetWidgetMode(WidgetMode);
		if (WidgetMode == UE_WIDGET::EWidgetMode::WM_Scale)
		{
			GetModeManager()->SetCoordSystem(COORD_Local);
		}
		else
		{
			GetModeManager()->SetCoordSystem(COORD_World);
		}

		bHandled = true;
		InViewportClient->Invalidate();
	}

	return bHandled;
}

bool FKawaiiPhysicsEditModeBase::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	const EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();
	const UE_WIDGET::EWidgetMode WidgetMode = InViewportClient->GetWidgetMode();

	bool bHandled = false;

	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();

	if (bManipulating && CurrentAxis != EAxisList::None)
	{
		bHandled = true;

		const bool bDoRotation = WidgetMode == UE_WIDGET::EWidgetMode::WM_Rotate || WidgetMode == UE_WIDGET::EWidgetMode::WM_TranslateRotateZ;
		const bool bDoTranslation = WidgetMode == UE_WIDGET::EWidgetMode::WM_Translate || WidgetMode == UE_WIDGET::EWidgetMode::WM_TranslateRotateZ;
		const bool bDoScale = WidgetMode == UE_WIDGET::EWidgetMode::WM_Scale;

		if (bDoRotation)
		{
			DoRotation(InRot);
		}

		if (bDoTranslation)
		{
			DoTranslation(InDrag);
		}

		if (bDoScale)
		{
			DoScale(InScale);
		}

		InViewport->Invalidate();
	}

	return bHandled;
}

bool FKawaiiPhysicsEditModeBase::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	UDebugSkelMeshComponent* PreviewMeshComponent = GetAnimPreviewScene().GetPreviewMeshComponent();
	FName BoneName = GetSelectedBone();
	int32 BoneIndex = PreviewMeshComponent->GetBoneIndex(BoneName);
	if (BoneIndex != INDEX_NONE)
	{
		FTransform BoneMatrix = PreviewMeshComponent->GetBoneTransform(BoneIndex);
		InMatrix = BoneMatrix.ToMatrixNoScale().RemoveTranslation();
		return true;
	}

	return false;
}

bool FKawaiiPhysicsEditModeBase::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

bool FKawaiiPhysicsEditModeBase::ShouldDrawWidget() const
{
	return true;
}

void FKawaiiPhysicsEditModeBase::DoTranslation(FVector& InTranslation)
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->DoTranslation(GetAnimPreviewScene().GetPreviewMeshComponent(), InTranslation, (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void FKawaiiPhysicsEditModeBase::DoRotation(FRotator& InRotation)
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->DoRotation(GetAnimPreviewScene().GetPreviewMeshComponent(), InRotation, (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void FKawaiiPhysicsEditModeBase::DoScale(FVector& InScale)
{
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->DoScale(GetAnimPreviewScene().GetPreviewMeshComponent(), InScale, (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void FKawaiiPhysicsEditModeBase::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	IAnimNodeEditMode::Tick(ViewportClient, DeltaTime);

	// Keep actor location in sync with animation
	UAnimGraphNode_SkeletalControlBase* SkelControl = Cast<UAnimGraphNode_SkeletalControlBase>(AnimNode);
	if (SkelControl != nullptr)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SkelControl->MoveSelectActorLocation(GetAnimPreviewScene().GetPreviewMeshComponent(), (FAnimNode_SkeletalControlBase*)RuntimeAnimNode);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void FKawaiiPhysicsEditModeBase::ConvertToComponentSpaceTransform(const USkeletalMeshComponent* SkelComp, const FTransform & InTransform, FTransform & OutCSTransform, int32 BoneIndex, EBoneControlSpace Space)
{
	USkeleton* Skeleton = SkelComp->SkeletalMesh->GetSkeleton();

	switch (Space)
	{
	case BCS_WorldSpace:
	{
		OutCSTransform = InTransform;
		OutCSTransform.SetToRelativeTransform(SkelComp->GetComponentTransform());
	}
	break;

	case BCS_ComponentSpace:
	{
		// Component Space, no change.
		OutCSTransform = InTransform;
	}
	break;

	case BCS_ParentBoneSpace:
		if (BoneIndex != INDEX_NONE)
		{
			const int32 ParentIndex = Skeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				const int32 MeshParentIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkelComp->SkeletalMesh, ParentIndex);
				if (MeshParentIndex != INDEX_NONE)
				{
					const FTransform ParentTM = SkelComp->GetBoneTransform(MeshParentIndex);
					OutCSTransform = InTransform * ParentTM;
				}
				else
				{
					OutCSTransform = InTransform;
				}
			}
		}
		break;

	case BCS_BoneSpace:
		if (BoneIndex != INDEX_NONE)
		{
			const int32 MeshBoneIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkelComp->SkeletalMesh, BoneIndex);
			if (MeshBoneIndex != INDEX_NONE)
			{
				const FTransform BoneTM = SkelComp->GetBoneTransform(MeshBoneIndex);
				OutCSTransform = InTransform * BoneTM;
			}
			else
			{
				OutCSTransform = InTransform;
			}
		}
		break;

	default:
		if (SkelComp->SkeletalMesh)
		{
			UE_LOG(LogAnimation, Warning, TEXT("ConvertToComponentSpaceTransform: Unknown BoneSpace %d  for Mesh: %s"), (uint8)Space, *SkelComp->SkeletalMesh->GetFName().ToString());
		}
		else
		{
			UE_LOG(LogAnimation, Warning, TEXT("ConvertToComponentSpaceTransform: Unknown BoneSpace %d  for Skeleton: %s"), (uint8)Space, *Skeleton->GetFName().ToString());
		}
		break;
	}
}


void FKawaiiPhysicsEditModeBase::ConvertToBoneSpaceTransform(const USkeletalMeshComponent* SkelComp, const FTransform & InCSTransform, FTransform & OutBSTransform, int32 BoneIndex, EBoneControlSpace Space)
{
	USkeleton* Skeleton = SkelComp->SkeletalMesh->GetSkeleton();

	switch (Space)
	{
	case BCS_WorldSpace:
	{
		OutBSTransform = InCSTransform * SkelComp->GetComponentTransform();
		break;
	}

	case BCS_ComponentSpace:
	{
		// Component Space, no change.
		OutBSTransform = InCSTransform;
		break;
	}

	case BCS_ParentBoneSpace:
	{
		if (BoneIndex != INDEX_NONE)
		{
			const int32 ParentIndex = Skeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				const int32 MeshParentIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkelComp->SkeletalMesh, ParentIndex);
				if (MeshParentIndex != INDEX_NONE)
				{
					const FTransform ParentTM = SkelComp->GetBoneTransform(MeshParentIndex);
					OutBSTransform = InCSTransform.GetRelativeTransform(ParentTM);
				}
				else
				{
					OutBSTransform = InCSTransform;
				}
			}
		}
		break;
	}

	case BCS_BoneSpace:
	{
		if (BoneIndex != INDEX_NONE)
		{
			const int32 MeshBoneIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkelComp->SkeletalMesh, BoneIndex);
			if (MeshBoneIndex != INDEX_NONE)
			{
				FTransform BoneCSTransform = SkelComp->GetBoneTransform(MeshBoneIndex);
				OutBSTransform = InCSTransform.GetRelativeTransform(BoneCSTransform);
			}
			else
			{
				OutBSTransform = InCSTransform;
			}
		}
		break;
	}

	default:
	{
		UE_LOG(LogAnimation, Warning, TEXT("ConvertToBoneSpaceTransform: Unknown BoneSpace %d  for Mesh: %s"), (int32)Space, *GetNameSafe(SkelComp->SkeletalMesh));
		break;
	}
	}
}

FVector FKawaiiPhysicsEditModeBase::ConvertCSVectorToBoneSpace(const USkeletalMeshComponent* SkelComp, FVector& InCSVector, FCSPose<FCompactHeapPose>& MeshBases, const FBoneSocketTarget& InTarget, const EBoneControlSpace Space)
{
	FVector OutVector = FVector::ZeroVector;

	if (MeshBases.GetPose().IsValid())
	{
		const FCompactPoseBoneIndex BoneIndex = InTarget.GetCompactPoseBoneIndex();

		switch (Space)
		{
			// World Space, no change in preview window
		case BCS_WorldSpace:
		case BCS_ComponentSpace:
			// Component Space, no change.
			OutVector = InCSVector;
			break;

		case BCS_ParentBoneSpace:
		{
			if (BoneIndex != INDEX_NONE)
			{
				const FCompactPoseBoneIndex ParentIndex = MeshBases.GetPose().GetParentBoneIndex(BoneIndex);
				if (ParentIndex != INDEX_NONE)
				{
					const FTransform& ParentTM = MeshBases.GetComponentSpaceTransform(ParentIndex);
					OutVector = ParentTM.InverseTransformVector(InCSVector);
				}
			}
		}
		break;

		case BCS_BoneSpace:
		{
			FTransform BoneTransform = InTarget.GetTargetTransform(FVector::ZeroVector, MeshBases, SkelComp->GetComponentToWorld());
			OutVector = BoneTransform.InverseTransformVector(InCSVector);
		}
		break;
		}
	}

	return OutVector;
}

FVector FKawaiiPhysicsEditModeBase::ConvertCSVectorToBoneSpace(const USkeletalMeshComponent* SkelComp, FVector& InCSVector, FCSPose<FCompactHeapPose>& MeshBases, const FName& BoneName, const EBoneControlSpace Space)
{
	FVector OutVector = FVector::ZeroVector;

	if (MeshBases.GetPose().IsValid())
	{
		const FMeshPoseBoneIndex MeshBoneIndex(SkelComp->GetBoneIndex(BoneName));
		const FCompactPoseBoneIndex BoneIndex = MeshBases.GetPose().GetBoneContainer().MakeCompactPoseIndex(MeshBoneIndex);

		switch (Space)
		{
			// World Space, no change in preview window
		case BCS_WorldSpace:
		case BCS_ComponentSpace:
			// Component Space, no change.
			OutVector = InCSVector;
			break;

		case BCS_ParentBoneSpace:
		{
			const FCompactPoseBoneIndex ParentIndex = MeshBases.GetPose().GetParentBoneIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				const FTransform& ParentTM = MeshBases.GetComponentSpaceTransform(ParentIndex);
				OutVector = ParentTM.InverseTransformVector(InCSVector);
			}
		}
		break;

		case BCS_BoneSpace:
		{
			if (BoneIndex != INDEX_NONE)
			{
				const FTransform& BoneTM = MeshBases.GetComponentSpaceTransform(BoneIndex);
				OutVector = BoneTM.InverseTransformVector(InCSVector);
			}
		}
		break;
		}
	}

	return OutVector;
}

FQuat FKawaiiPhysicsEditModeBase::ConvertCSRotationToBoneSpace(const USkeletalMeshComponent* SkelComp, FRotator& InCSRotator, FCSPose<FCompactHeapPose>& MeshBases, const FName& BoneName, const EBoneControlSpace Space)
{
	FQuat OutQuat = FQuat::Identity;

	if (MeshBases.GetPose().IsValid())
	{
		const FMeshPoseBoneIndex MeshBoneIndex(SkelComp->GetBoneIndex(BoneName));
		const FCompactPoseBoneIndex BoneIndex = MeshBases.GetPose().GetBoneContainer().MakeCompactPoseIndex(MeshBoneIndex);

		FVector RotAxis;
		float RotAngle;
		InCSRotator.Quaternion().ToAxisAndAngle(RotAxis, RotAngle);

		switch (Space)
		{
			// World Space, no change in preview window
		case BCS_WorldSpace:
		case BCS_ComponentSpace:
			// Component Space, no change.
			OutQuat = InCSRotator.Quaternion();
			break;

		case BCS_ParentBoneSpace:
		{
			const FCompactPoseBoneIndex ParentIndex = MeshBases.GetPose().GetParentBoneIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				const FTransform& ParentTM = MeshBases.GetComponentSpaceTransform(ParentIndex);
				FTransform InverseParentTM = ParentTM.Inverse();
				//Calculate the new delta rotation
				FVector4 BoneSpaceAxis = InverseParentTM.TransformVector(RotAxis);
				FQuat DeltaQuat(BoneSpaceAxis, RotAngle);
				DeltaQuat.Normalize();
				OutQuat = DeltaQuat;
			}
		}
		break;

		case BCS_BoneSpace:
		{
			const FTransform& BoneTM = BoneIndex >= 0 ? MeshBases.GetComponentSpaceTransform(BoneIndex) : FTransform::Identity;
			FTransform InverseBoneTM = BoneTM.Inverse();
			FVector4 BoneSpaceAxis = InverseBoneTM.TransformVector(RotAxis);
			//Calculate the new delta rotation
			FQuat DeltaQuat(BoneSpaceAxis, RotAngle);
			DeltaQuat.Normalize();
			OutQuat = DeltaQuat;
		}
		break;
		}
	}

	return OutQuat;
}

FVector FKawaiiPhysicsEditModeBase::ConvertWidgetLocation(const USkeletalMeshComponent* InSkelComp, FCSPose<FCompactHeapPose>& InMeshBases, const FBoneSocketTarget& Target, const FVector& InLocation, const EBoneControlSpace Space)
{
	FVector WidgetLoc = FVector::ZeroVector;

	switch (Space)
	{
		// GetComponentTransform() must be Identity in preview window so same as ComponentSpace
	case BCS_WorldSpace:
	case BCS_ComponentSpace:
	{
		// Component Space, no change.
		WidgetLoc = InLocation;
	}
	break;

	case BCS_ParentBoneSpace:
	{
		const FCompactPoseBoneIndex CompactBoneIndex = Target.GetCompactPoseBoneIndex();

		if (CompactBoneIndex != INDEX_NONE)
		{
			if (ensure(InMeshBases.GetPose().IsValidIndex(CompactBoneIndex)))
			{
				const FCompactPoseBoneIndex CompactParentIndex = InMeshBases.GetPose().GetParentBoneIndex(CompactBoneIndex);
				if (CompactParentIndex != INDEX_NONE)
				{
					const FTransform& ParentTM = InMeshBases.GetComponentSpaceTransform(CompactParentIndex);
					WidgetLoc = ParentTM.TransformPosition(InLocation);
				}
			}
			else
			{
				UE_LOG(LogAnimation, Warning, TEXT("Using socket(%d), Socket name(%s), Bone name(%s)"),
					Target.bUseSocket, *Target.SocketReference.SocketName.ToString(), *Target.BoneReference.BoneName.ToString());
			}
		}
	}
	break;

	case BCS_BoneSpace:
	{
		FTransform BoneTM = Target.GetTargetTransform(FVector::ZeroVector, InMeshBases, InSkelComp->GetComponentToWorld());
		WidgetLoc = BoneTM.TransformPosition(InLocation);
	}
	break;
	}

	return WidgetLoc;
}
FVector FKawaiiPhysicsEditModeBase::ConvertWidgetLocation(const USkeletalMeshComponent* SkelComp, FCSPose<FCompactHeapPose>& MeshBases, const FName& BoneName, const FVector& Location, const EBoneControlSpace Space)
{
	FVector WidgetLoc = FVector::ZeroVector;

	auto GetCompactBoneIndex = [](const USkeletalMeshComponent* InSkelComp, FCSPose<FCompactHeapPose>& InMeshBases, const FName& InBoneName)
	{
		if (InMeshBases.GetPose().IsValid())
		{
			USkeleton* Skeleton = InSkelComp->SkeletalMesh->GetSkeleton();
			const int32 MeshBoneIndex = InSkelComp->GetBoneIndex(InBoneName);
			if (MeshBoneIndex != INDEX_NONE)
			{
				return InMeshBases.GetPose().GetBoneContainer().MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
			}
		}

		return FCompactPoseBoneIndex(INDEX_NONE);
	};

	switch (Space)
	{
		// GetComponentTransform() must be Identity in preview window so same as ComponentSpace
	case BCS_WorldSpace:
	case BCS_ComponentSpace:
	{
		// Component Space, no change.
		WidgetLoc = Location;
	}
	break;

	case BCS_ParentBoneSpace:
	{
		const FCompactPoseBoneIndex CompactBoneIndex = GetCompactBoneIndex(SkelComp, MeshBases, BoneName);
		if (CompactBoneIndex != INDEX_NONE)
		{
			const FCompactPoseBoneIndex CompactParentIndex = MeshBases.GetPose().GetParentBoneIndex(CompactBoneIndex);
			if (CompactParentIndex != INDEX_NONE)
			{
				const FTransform& ParentTM = MeshBases.GetComponentSpaceTransform(CompactParentIndex);
				WidgetLoc = ParentTM.TransformPosition(Location);
			}
		}
	}
	break;

	case BCS_BoneSpace:
	{
		const FCompactPoseBoneIndex CompactBoneIndex = GetCompactBoneIndex(SkelComp, MeshBases, BoneName);
		if (CompactBoneIndex != INDEX_NONE)
		{
			const FTransform& BoneTM = MeshBases.GetComponentSpaceTransform(CompactBoneIndex);
			WidgetLoc = BoneTM.TransformPosition(Location);
		}
	}
	break;
	}

	return WidgetLoc;
}

#undef LOCTEXT_NAMESPACE
