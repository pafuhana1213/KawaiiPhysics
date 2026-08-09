// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "KawaiiPhysicsPresetDataAsset.generated.h"

class FProperty;

USTRUCT(BlueprintType)
struct KAWAIIPHYSICS_API FKawaiiPhysicsPresetApplyOptions
{
	GENERATED_BODY()

	/**
	 * ボーン割り当て（RootBone / ExcludeBones / AdditionalRootBones）を上書きする
	 * Overwrite bone assignments (RootBone / ExcludeBones / AdditionalRootBones)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bApplyBoneAssignment = false;

	/**
	 * KawaiiPhysicsTag を上書きする
	 * Overwrite KawaiiPhysicsTag
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	bool bApplyTag = false;
};

enum class EKawaiiPhysicsPresetPropertyClass : uint8
{
	Unknown,
	CopyTarget,
	Deny,
	BoneAssignment,
	Tag,
};

/**
 * KawaiiPhysics ノード設定をまとめて保存し、他ノードへ適用するための DataAsset。
 * DataAsset that stores KawaiiPhysics node settings and applies them to other nodes.
 */
UCLASS(Blueprintable)
class KAWAIIPHYSICS_API UKawaiiPhysicsPresetDataAsset : public UDataAsset, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Description", meta = (MultiLine = true))
	FText Description;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeleton")
	TObjectPtr<USkeleton> Skeleton;
#endif

	/**
	 * このプリセットの適用対象ノードを示すタグ（Reapply/Audit の対象決定に使用。空なら対象なし）
	 * Tags that identify target nodes for this preset (used by Reapply/Audit; empty = no targets)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FGameplayTagContainer TargetTags;

	/**
	 * TargetTags の照合を完全一致にする
	 * Use exact tag matching for TargetTags
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	bool bTargetTagsExactMatch = false;

	UPROPERTY(EditAnywhere, Category = "Preset", meta = (ShowOnlyInnerProperties))
	FAnimNode_KawaiiPhysics Node;

	void CopyFromNode(const FAnimNode_KawaiiPhysics& SourceNode);
	/**
	 * CustomExternalForces を指定Outerへ複製してコピーする。
	 * Duplicate and copy CustomExternalForces with the specified Outer.
	 */
	static void DuplicateCustomExternalForces(
		const TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>>& SourceForces,
		TArray<TObjectPtr<UKawaiiPhysics_CustomExternalForce>>& DestinationForces,
		UObject* Outer);
	/**
	 * プリセットを対象ノードへ適用する。TargetOuter が nullptr の場合は ExternalForces と CustomExternalForces を複製しない。
	 * Apply this preset to a target node. ExternalForces and CustomExternalForces are not duplicated when TargetOuter is nullptr.
	 */
	void ApplyToNode(FAnimNode_KawaiiPhysics& TargetNode,
	                 const FKawaiiPhysicsPresetApplyOptions& Options,
	                 UObject* TargetOuter) const;
	/**
	 * プリセット内容と対象ノードを比較し、差分プロパティ名と比較対象プロパティ名を返す。
	 * Compare this preset with a target node and return differing property names and optionally compared property names.
	 */
	bool MatchesNode(const FAnimNode_KawaiiPhysics& TargetNode,
	                 const FKawaiiPhysicsPresetApplyOptions& Options,
	                 TArray<FName>& OutDiffProperties,
	                 TArray<FName>* OutComparedProperties = nullptr) const;
	/**
	 * NodeTag がこのプリセットの対象タグに一致するかを返す。
	 * Returns whether NodeTag matches this preset's target tags.
	 */
	bool TargetsNodeTag(const FGameplayTag& NodeTag) const;

	static EKawaiiPhysicsPresetPropertyClass ClassifyNodeProperty(const FProperty& Property);
	static bool ShouldApplyNodeProperty(const FProperty& Property,
	                                    const FKawaiiPhysicsPresetApplyOptions& Options);

	// Begin UObject Interface.
	virtual void Serialize(FStructuredArchiveRecord Record) override;
	// End UObject Interface.

	// IBoneReferenceSkeletonProvider interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle) override;
};
