// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

#include "KawaiiPhysicsEditorLibrary.generated.h"

class UAnimBlueprint;
class UAnimGraphNode_KawaiiPhysics;
class USkeleton;

UENUM(BlueprintType)
enum class EKawaiiPhysicsEditorAccessResult : uint8
{
	Valid,
	NotValid,
};

/**
 * KawaiiPhysics エディタグラフノードへの弱参照ハンドル。
 * Weak-reference handle to a KawaiiPhysics editor graph node.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsGraphNodeHandle
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UAnimGraphNode_KawaiiPhysics> Node;

	bool IsValid() const
	{
		return Node.IsValid();
	}
};

/**
 * KawaiiPhysics ノード監査の1件分の結果。
 * One audit result entry for a KawaiiPhysics node.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsNodeAuditEntry
{
	GENERATED_BODY()

	/** Anim Blueprint のアセットパス / Asset path of the Anim Blueprint. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FSoftObjectPath AnimBlueprintPath;

	/** ノードを含むグラフ名 / Name of the graph that owns the node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FName GraphName;

	/** エディタグラフノードの GUID / GUID of the editor graph node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FGuid NodeGuid;

	/** RootBone のボーン名 / Bone name of RootBone. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FName RootBoneName;

	/** KawaiiPhysicsTag / KawaiiPhysicsTag. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FGameplayTag KawaiiPhysicsTag;

	/** TargetTags がこのノードのタグにマッチしたプリセット（先頭1件） / Preset whose TargetTags match this node tag (first match). */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FSoftObjectPath MatchedPresetPath;

	/** MatchedPreset と一致するか / Whether the node matches MatchedPreset. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bMatchesPreset = false;

	/** MatchedPreset との差分プロパティ / Properties that differ from MatchedPreset. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	TArray<FName> DiffProperties;

	/** マッチしたプリセット総数（2以上なら TargetTags 設計の重複シグナル） / Total matched preset count (2+ signals overlapping TargetTags design). */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	int32 MatchedPresetCount = 0;

	/** BoneSubdivisionCount の設定値 / Configured BoneSubdivisionCount. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	int32 BoneSubdivisionCount = 0;

	/** BoneConstraintSubdivisionCount の設定値 / Configured BoneConstraintSubdivisionCount. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	int32 BoneConstraintSubdivisionCount = 0;

	/** WorldCollision が有効か / Whether WorldCollision is enabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bAllowWorldCollision = false;

	/** 共有コリジョンを使用するか / Whether shared collision is used. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bUseSharedCollision = false;

	/** 共有コリジョンのSourceか / Whether this node is a shared collision source. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bSharedCollisionSource = false;

	/** Wind が有効か / Whether wind is enabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bEnableWind = false;

	/** ExternalForces と CustomExternalForces の合計数 / Total count of ExternalForces and CustomExternalForces. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	int32 ExternalForceCount = 0;

	/** WarmUpFrames の設定値 / Configured WarmUpFrames. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	int32 WarmUpFrames = 0;
};

/**
 * AnimGraph に KawaiiPhysics ノードを配置するための1件分のリクエスト。
 * One request for placing a KawaiiPhysics node into an AnimGraph.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsNodePlacementRequest
{
	GENERATED_BODY()

	/** 適用するプリセット（未指定ならデフォルト値ノード） / Preset to apply (null creates a default node). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	TObjectPtr<UKawaiiPhysicsPresetDataAsset> Preset = nullptr;

	/** RootBone のボーン名 / Bone name for RootBone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	FName RootBoneName;

	/** ExcludeBones のボーン名リスト / Bone names for ExcludeBones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	TArray<FName> ExcludeBoneNames;

	/** AdditionalRootBones / AdditionalRootBones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	TArray<FKawaiiPhysicsRootBoneSetting> AdditionalRootBones;

	/**
	 * RootBone 用の正規表現。既存 ApplyRegex と同じく、全ボーン名を連結した文字列に対する FindNext() の一致を使う。
	 * Regex for RootBone. Uses FindNext() against a concatenated bone-name string, matching the existing ApplyRegex behavior.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	FString RootBonePattern;

	/**
	 * ExcludeBones 用の正規表現。既存 ApplyRegex と同じく、全ボーン名を連結した文字列に対する FindNext() の一致を使う。
	 * Regex for ExcludeBones. Uses FindNext() against a concatenated bone-name string, matching the existing ApplyRegex behavior.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	FString ExcludeBonePattern;

	/** KawaiiPhysicsTag（未指定ならプリセット側タグを使用） / KawaiiPhysicsTag (falls back to the preset tag when unset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	FGameplayTag KawaiiPhysicsTag;

	/** bAutoPosition が false の場合の配置座標 / Placement position used when bAutoPosition is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	FVector2D NodePosition = FVector2D::ZeroVector;

	/** Result ノード左側へ自動配置する / Automatically place nodes to the left of the Result node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	bool bAutoPosition = true;
};

/** Upsert 時に既存ノードを識別するキー / Key used to identify existing nodes during upsert placement. */
UENUM(BlueprintType)
enum class EKawaiiPhysicsPlacementUpsertKey : uint8
{
	None,
	Tag,
	RootBone,
	TagAndRootBone,
};

/**
 * KawaiiPhysics エディタグラフノードを Blueprint / Python から操作する関数ライブラリ。
 * Function library for controlling KawaiiPhysics editor graph nodes from Blueprint / Python.
 */
UCLASS()
class KAWAIIPHYSICSED_API UKawaiiPhysicsEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * AnimBlueprint 内の KawaiiPhysics グラフノードを収集する（FilterTags が空なら全件）。
	 * Collect KawaiiPhysics graph nodes in an AnimBlueprint (empty FilterTags collects all nodes).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "FilterTags"))
	static TArray<FKawaiiPhysicsGraphNodeHandle> CollectKawaiiPhysicsGraphNodes(
		UAnimBlueprint* AnimBlueprint,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch = false);

	/**
	 * プロジェクト内の KawaiiPhysics プリセットDataAssetを列挙してロードする。
	 * Enumerate and load all KawaiiPhysics preset data assets in the project.
	 */
	static void GetAllPresetAssets(TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& OutPresets);

	/**
	 * 指定 Content パス配下の AnimBlueprint アセットを列挙する（空なら /Game）。
	 * Enumerate AnimBlueprint assets under Content paths (uses /Game when empty).
	 */
	static void GetAnimBlueprintAssets(const TArray<FString>& ContentPaths, TArray<FAssetData>& OutAssets);

	/**
	 * AnimGraph に KawaiiPhysics ノードを追加またはUpsertする。接続は行わない。
	 * Add or upsert KawaiiPhysics nodes into an AnimGraph. This does not connect graph pins.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "Requests"))
	static TArray<FKawaiiPhysicsGraphNodeHandle> AddKawaiiPhysicsNodes(
		UAnimBlueprint* AnimBlueprint,
		const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests,
		EKawaiiPhysicsPlacementUpsertKey UpsertKey = EKawaiiPhysicsPlacementUpsertKey::None,
		FName GraphName = NAME_None);

	/**
	 * 配置リクエストを検証し、エラーと警告を返す。空なら問題なし。Warning: で始まる項目は警告で、配置は継続可能。
	 * Validate placement requests and return errors and warnings. Empty means no issues. Entries prefixed with Warning: are warnings and do not block placement.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FString> ValidatePlacementRequests(
		UAnimBlueprint* AnimBlueprint,
		const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests);

	/**
	 * スケルトンの参照ボーン名を正規表現で解決する。既存 ApplyRegex と同じく部分一致可能な FindNext() を使う。
	 * Resolve reference bone names by regex. Uses the existing ApplyRegex-style FindNext() behavior, so partial matches are possible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FName> ResolveBonesByPattern(USkeleton* Skeleton, const FString& Pattern);

	/** グラフノードハンドルが有効か / Check whether a graph node handle is valid. */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Editor")
	static bool IsGraphNodeHandleValid(const FKawaiiPhysicsGraphNodeHandle& Handle);

	/** ノードプロパティを文字列で設定 / Set a node property from string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetGraphNodePropertyByString(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		const FString& Value);

	/** ノードプロパティを文字列で取得 / Get a node property as string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodePropertyAsString(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		FString& OutValue);

	/** プリセット内ノードプロパティを文字列で設定 / Set a preset node property from string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetPresetNodePropertyByString(
		UKawaiiPhysicsPresetDataAsset* Preset,
		FName PropertyName,
		const FString& Value);

	/** プリセット内ノードプロパティを文字列で取得 / Get a preset node property as string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetPresetNodePropertyAsString(
		UKawaiiPhysicsPresetDataAsset* Preset,
		FName PropertyName,
		FString& OutValue);

	/**
	 * タグ名配列から GameplayTagContainer を作る（未登録タグ名は警告してスキップ。入力非空で全滅した場合は false）。
	 * Make a GameplayTagContainer from tag names (unregistered names are skipped with a warning; returns false when non-empty input resolves to no tags).
	 */
	UFUNCTION(BlueprintCallable, Category = "KawaiiPhysics|Editor|Preset")
	static bool MakeGameplayTagContainerFromNames(
		const TArray<FName>& TagNames,
		FGameplayTagContainer& OutContainer);

	/**
	 * プリセットの TargetTags をタグ名配列から設定する（未登録タグ名は警告してスキップ。入力非空で全滅した場合は変更せず false）。
	 * Set a preset's TargetTags from tag names (unregistered names are skipped with a warning; leaves the preset unchanged and returns false when non-empty input resolves to no tags).
	 */
	UFUNCTION(BlueprintCallable, Category = "KawaiiPhysics|Editor|Preset")
	static bool SetPresetTargetTags(
		UKawaiiPhysicsPresetDataAsset* Preset,
		const TArray<FName>& TagNames,
		bool bExactMatch);

	/** ノードプロパティをワイルドカード値で設定 / Set a node property by wildcard value. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics|Editor",
		meta=(ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void SetGraphNodeWildcardProperty(
		EKawaiiPhysicsEditorAccessResult& ExecResult,
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		const int32& Value)
	{
		checkNoEntry();
	}

	/** ノードプロパティをワイルドカード値で取得 / Get a node property by wildcard value. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Kawaii Physics|Editor",
		meta=(ExpandEnumAsExecs = "ExecResult", CustomStructureParam = "Value"))
	static void GetGraphNodeWildcardProperty(
		EKawaiiPhysicsEditorAccessResult& ExecResult,
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		int32& Value)
	{
		checkNoEntry();
	}

	/** KawaiiPhysicsTag を設定 / Set KawaiiPhysicsTag. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetGraphNodeTag(const FKawaiiPhysicsGraphNodeHandle& Handle, FGameplayTag Tag);

	/** KawaiiPhysicsTag を取得 / Get KawaiiPhysicsTag. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodeTag(const FKawaiiPhysicsGraphNodeHandle& Handle, FGameplayTag& OutTag);

	/** RootBone のボーン名を設定 / Set RootBone bone name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetGraphNodeRootBoneName(const FKawaiiPhysicsGraphNodeHandle& Handle, FName RootBoneName);

	/** RootBone のボーン名を取得 / Get RootBone bone name. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodeRootBoneName(const FKawaiiPhysicsGraphNodeHandle& Handle, FName& OutRootBoneName);

	/** プリセットをグラフノードへ適用 / Apply a preset to a graph node. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool ApplyPresetToGraphNode(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		UKawaiiPhysicsPresetDataAsset* Preset,
		FKawaiiPhysicsPresetApplyOptions Options);

	/** グラフノードとプリセットの差分プロパティを取得。空なら一致 / Get graph node preset diff properties. Empty means the node matches the preset. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FName> GetGraphNodePresetDiffProperties(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		UKawaiiPhysicsPresetDataAsset* Preset,
		FKawaiiPhysicsPresetApplyOptions Options);

	/** グラフノードを既存プリセットアセットへ書き出し / Export a graph node to an existing preset asset. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool ExportGraphNodeToPreset(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		UKawaiiPhysicsPresetDataAsset* TargetAsset);

	/**
	 * Preset の TargetTags がマッチするノードへ再適用する（TargetTags空なら対象なし）
	 * Reapply a preset to nodes whose tags match the preset's TargetTags (empty TargetTags targets nothing).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static int32 ReapplyPresetToProject(
		UKawaiiPhysicsPresetDataAsset* Preset,
		bool bDryRun,
		bool bCheckOutFiles,
		TArray<FKawaiiPhysicsNodeAuditEntry>& OutReport);

	/** 指定 Content パス配下の KawaiiPhysics ノードを監査 / Audit KawaiiPhysics nodes under Content paths. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "ContentPaths,FilterTags"))
	static bool AuditKawaiiPhysicsNodes(
		const TArray<FString>& ContentPaths,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch,
		TArray<FKawaiiPhysicsNodeAuditEntry>& OutEntries);

private:
	DECLARE_FUNCTION(execSetGraphNodeWildcardProperty);
	DECLARE_FUNCTION(execGetGraphNodeWildcardProperty);
};
