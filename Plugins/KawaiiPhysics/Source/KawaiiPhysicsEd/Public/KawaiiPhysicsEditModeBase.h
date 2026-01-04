
// In UE4, FAnimNodeEditMode didn't have a module API, so I had to copy-paste the contents of FAnimNodeEditMode to an external module
// I will remove this class in the future when I turn off UE4 support.
#if	ENGINE_MAJOR_VERSION == 4

/** Copy from FAnimNodeEditMode to override **/

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UnrealWidget.h"
#include "IAnimNodeEditMode.h"
#include "BonePose.h"
#include "Runtime/Launch/Resources/Version.h"

#define UE_WIDGET FWidget

class FCanvas;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class USkeletalMeshComponent;
struct FViewportClick;
struct FBoneSocketTarget;

/** Base implementation for anim node edit modes */
class FKawaiiPhysicsEditModeBase : public IAnimNodeEditMode
{
public:
	FKawaiiPhysicsEditModeBase();

	/** IAnimNodeEditMode interface */
	virtual ECoordSystem GetWidgetCoordinateSystem() const override;

	virtual UE_WIDGET::EWidgetMode GetWidgetMode() const override;
	virtual UE_WIDGET::EWidgetMode ChangeToNextWidgetMode(UE_WIDGET::EWidgetMode CurWidgetMode) override;

	virtual bool SetWidgetMode(UE_WIDGET::EWidgetMode InWidgetMode) override;
	virtual FName GetSelectedBone() const override;
	virtual void DoTranslation(FVector& InTranslation) override;
	virtual void DoRotation(FRotator& InRotation) override;
	virtual void DoScale(FVector& InScale) override;
	virtual void EnterMode(class UAnimGraphNode_Base* InEditorNode, struct FAnimNode_Base* InRuntimeNode) override;
	virtual void ExitMode() override;

	/** IPersonaEditMode interface */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override;

	/** FEdMode interface */
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool ShouldDrawWidget() const override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

protected:
	// local conversion functions for drawing
	static void ConvertToComponentSpaceTransform(const USkeletalMeshComponent* SkelComp, const FTransform & InTransform, FTransform & OutCSTransform, int32 BoneIndex, EBoneControlSpace Space);
	static void ConvertToBoneSpaceTransform(const USkeletalMeshComponent* SkelComp, const FTransform & InCSTransform, FTransform & OutBSTransform, int32 BoneIndex, EBoneControlSpace Space);
	// convert drag vector in component space to bone space 
	static FVector ConvertCSVectorToBoneSpace(const USkeletalMeshComponent* SkelComp, FVector& InCSVector, FCSPose<FCompactHeapPose>& MeshBases, const FName& BoneName, const EBoneControlSpace Space);
	static FVector ConvertCSVectorToBoneSpace(const USkeletalMeshComponent* SkelComp, FVector& InCSVector, FCSPose<FCompactHeapPose>& MeshBases, const FBoneSocketTarget& InTarget, const EBoneControlSpace Space);
	// convert rotator in component space to bone space 
	static FQuat ConvertCSRotationToBoneSpace(const USkeletalMeshComponent* SkelComp, FRotator& InCSRotator, FCSPose<FCompactHeapPose>& MeshBases, const FName& BoneName, const EBoneControlSpace Space);
	// convert widget location according to bone control space
	static FVector ConvertWidgetLocation(const USkeletalMeshComponent* InSkelComp, FCSPose<FCompactHeapPose>& InMeshBases, const FName& BoneName, const FVector& InLocation, const EBoneControlSpace Space);
	static FVector ConvertWidgetLocation(const USkeletalMeshComponent* InSkelComp, FCSPose<FCompactHeapPose>& InMeshBases, const FBoneSocketTarget& Target, const FVector& InLocation, const EBoneControlSpace Space);

protected:
	/** The node we are operating on */
	class UAnimGraphNode_Base* AnimNode;

	/** The runtime node in the preview scene */
	struct FAnimNode_Base* RuntimeAnimNode;

	bool bManipulating;

private:

	bool bInTransaction;
};
#endif