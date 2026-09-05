// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_KawaiiPhysicsSharedPublisher.h"
#include "EdGraph/EdGraphNodeUtils.h"

#include "AnimGraphNode_KawaiiPhysicsSharedPublisher.generated.h"

class FCompilerResultsLog;
class IDetailLayoutBuilder;
class UGraphNodeContextMenuContext;
class USkeleton;
class UToolMenu;

UCLASS()
class UAnimGraphNode_KawaiiPhysicsSharedPublisher : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_KawaiiPhysicsSharedPublisher Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetNodeCategory() const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog) override;
	virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void FindConsumers();

	/** Constructing FText strings can be costly, so we cache the node's title */
	FNodeTitleTextTable CachedNodeTitles;
};
