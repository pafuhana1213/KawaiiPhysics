#include "AnimGraphNode_KawaiiPhysics.h"

#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "KawaiiPhysics"

// ----------------------------------------------------------------------------
UAnimGraphNode_KawaiiPhysics::UAnimGraphNode_KawaiiPhysics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UAnimGraphNode_KawaiiPhysics::GetControllerDescription() const
{
	return LOCTEXT("Kawaii Physics", "Kawaii Physics");
}


// ----------------------------------------------------------------------------
FText UAnimGraphNode_KawaiiPhysics::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle))
	{
		return GetControllerDescription();
	}
	// @TODO: the bone can be altered in the property editor, so we have to 
	//        choose to mark this dirty when that happens for this to properly work
	else //if (!CachedNodeTitles.IsTitleCached(TitleType, this))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
		for (auto& RootBone : Node.RootBones)
		{
			Args.Add(TEXT("RootBoneName"), FText::FromName(RootBone.BoneName));
		}

		// FText::Format() is slow, so we cache this to save on performance
		if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_KawaiiPhysics_ListTitle", "{ControllerDescription} - Root: {RootBoneName}"), Args), this);
		}
		else
		{
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_KawaiiPhysics_Title", "{ControllerDescription}\nRoot: {RootBoneName} "), Args), this);
		}
	}
	return CachedNodeTitles[TitleType];
}

void UAnimGraphNode_KawaiiPhysics::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	Node.ModifyBones.Empty();
	ReconstructNode();
}

FEditorModeID UAnimGraphNode_KawaiiPhysics::GetEditorMode() const
{
	return "AnimGraph.SkeletalControl.KawaiiPhysics";
}

void UAnimGraphNode_KawaiiPhysics::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* SkelMeshComp) const
{
	if (!SkelMeshComp)
	{
		return;
	}

	FAnimNode_KawaiiPhysics* ActiveNode = GetActiveInstanceNode<FAnimNode_KawaiiPhysics>(SkelMeshComp->GetAnimInstance());
	if (!ActiveNode)
	{
		return;
	}

	if (bEnableDebugDrawBone)
	{
		for (auto& Bone : ActiveNode->ModifyBones)
		{
			PDI->DrawPoint(Bone.Location, FLinearColor::White, 5.0f, SDPG_Foreground);

			if (Bone.PhysicsSettings.Radius > 0)
			{
				auto Color = Bone.bDummy ? FColor::Red : FColor::Yellow;
				DrawWireSphere(PDI, Bone.Location, Color, Bone.PhysicsSettings.Radius, 16, SDPG_Foreground);
			}

			for (int ChildIndex : Bone.ChildIndexs)
			{
				DrawDashedLine(PDI, Bone.Location, ActiveNode->ModifyBones[ChildIndex].Location,
					FLinearColor::White, 1, SDPG_Foreground);
			}
		}
	}

	if (bEnableDebugDrawAngleLimit)
	{
		for (auto& Bone : ActiveNode->ModifyBones)
		{
			if (Bone.ParentIndex < 0 || Bone.PhysicsSettings.LimitAngle <= 0)
			{
				continue;
			}

			auto& ParentBone = ActiveNode->ModifyBones[Bone.ParentIndex];
			FTransform ParentBoneTransform =
				FTransform(FQuat::FindBetween(FVector::ForwardVector, Bone.PoseLocation - ParentBone.PoseLocation), ParentBone.Location);
			TArray<FVector> Verts;
			DrawWireCone(PDI, Verts, ParentBoneTransform, (Bone.PoseLocation - ParentBone.PoseLocation).Size(),
				Bone.PhysicsSettings.LimitAngle, 16, FColor::Green, SDPG_World);
		}
	}
}


#undef LOCTEXT_NAMESPACE