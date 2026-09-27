// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "KawaiiPhysicsPresetDataAsset.h"
#include "KawaiiPhysicsSimpleWorldCollision.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

#include "KawaiiPhysicsEditorLibrary.generated.h"

class UAnimBlueprint;
class UAnimSequenceBase;
class UAnimGraphNode_KawaiiPhysics;
class UAnimGraphNode_KawaiiPhysicsSharedPublisher;
class UEdGraphNode_Comment;
class USkeleton;
class UWorld;

UENUM(BlueprintType)
enum class EKawaiiPhysicsEditorAccessResult : uint8
{
	Valid,
	NotValid,
};

/** 自動配置方向のリクエスト単位上書き。Default はプロジェクト設定に従う / Per-request override for automatic placement direction. Default follows project settings. */
UENUM(BlueprintType)
enum class EKawaiiPhysicsNodePlacementDirectionOverride : uint8
{
	/** プロジェクト設定に従う / Follow project settings. */
	Default,
	/** 縦方向へ配置 / Place vertically. */
	Vertical,
	/** 横方向へ配置 / Place horizontally. */
	Horizontal,
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
 * KawaiiPhysics Shared Publisher エディタグラフノードへのハンドル。
 * Handle to a KawaiiPhysics Shared Publisher editor graph node.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsSharedPublisherGraphNodeHandle
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UAnimGraphNode_KawaiiPhysicsSharedPublisher> Node;

	/** ノードを含む AnimBlueprint / AnimBlueprint that owns the node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	TObjectPtr<UAnimBlueprint> AnimBlueprint = nullptr;

	/** エディタグラフノードの GUID / GUID of the editor graph node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FGuid NodeGuid;

	/** Shared Group Tag / Shared Group Tag. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FGameplayTag SharedGroupTag;

	/** ノードを含むグラフ名 / Name of the graph that owns the node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FName GraphName;

	bool IsValid() const
	{
		return Node.IsValid();
	}
};

/**
 * AnimGraph コメントノード情報。
 * Information about a comment node in an AnimGraph.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsAnimGraphCommentInfo
{
	GENERATED_BODY()

	/** 枠タイトル（NodeComment） / Comment frame title (NodeComment). */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FString Title;

	/** MCPコメントの指示プロンプト全文（手動コメントは空） / Full prompt for MCP comments (empty for manual comments). */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FString Prompt;

	/** MCPコメント枠の作成日時 / Creation time for MCP comment frames. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FDateTime CreatedAt;

	/** MCPコメント枠の最終更新日時 / Last update time for MCP comment frames. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FDateTime UpdatedAt;

	/** MCPコメント枠か / Whether this is an MCP comment frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bMcpComment = false;

	/** コメントノード本体 / The comment node itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	TObjectPtr<UEdGraphNode_Comment> CommentNode = nullptr;

	/** コメント枠の左上位置（グラフ座標） / Top-left position of the comment frame in graph coordinates. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FVector2D NodePosition = FVector2D::ZeroVector;

	/** コメント枠の幅と高さ / Width and height of the comment frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FVector2D NodeSize = FVector2D::ZeroVector;
};

/**
 * プリセットとノードの差分1プロパティ分の値ペア。
 * One value pair for a single differing property between a node and a preset.
 */
USTRUCT(BlueprintType)
struct KAWAIIPHYSICSED_API FKawaiiPhysicsPresetDiffValue
{
	GENERATED_BODY()

	/** 差分プロパティの内部 FName / Internal FName of the differing property. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FName PropertyName;

	/** 対象ノード側の値文字列 / Value string from the target node. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FString NodeValue;

	/** プリセット側の値文字列 / Value string from the preset. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	FString PresetValue;
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

	/** DiffProperties の値ペア（既定は空。AuditKawaiiPhysicsNodes に bIncludeDiffValues=true を渡した場合のみ充填） / Value pairs for DiffProperties (empty by default; filled only when AuditKawaiiPhysicsNodes is called with bIncludeDiffValues=true). */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	TArray<FKawaiiPhysicsPresetDiffValue> DiffValues;

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

	/** シンプルワールドコリジョンが有効か / Whether Simple World Collision is enabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	bool bUseSimpleWorldCollision = false;

	/** シンプルワールドコリジョンで収集した SkeletalMeshComponent との当たり方 / How Simple World Collision collides with gathered SkeletalMeshComponents. */
	UPROPERTY(BlueprintReadOnly, Category = "Kawaii Physics|Editor")
	EKawaiiPhysicsSimpleWorldSkeletalMeshCollision SimpleWorldCollisionSkeletalMeshCollision =
		EKawaiiPhysicsSimpleWorldSkeletalMeshCollision::None;

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

	/** Result ノード直前へ直列に自動接続する / Automatically connect in series just before the Result node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	bool bAutoConnect = false;

	/** 自動配置方向の上書き。Default はプロジェクト設定に従う / Override for automatic placement direction. Default follows project settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kawaii Physics|Editor")
	EKawaiiPhysicsNodePlacementDirectionOverride PlacementDirection =
		EKawaiiPhysicsNodePlacementDirectionOverride::Default;
};

/** Match 時に既存ノードを識別するキー / Key used to identify existing nodes to match during placement. */
UENUM(BlueprintType)
enum class EKawaiiPhysicsPlacementMatchKey : uint8
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
	 * AnimBlueprint 内の KawaiiPhysics Shared Publisher グラフノードを収集する。
	 * Collect KawaiiPhysics Shared Publisher graph nodes in an AnimBlueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FKawaiiPhysicsSharedPublisherGraphNodeHandle> CollectKawaiiPhysicsSharedPublisherGraphNodes(
		UAnimBlueprint* AnimBlueprint);

	/** AnimBlueprint 内の KawaiiPhysics グラフノードを NodeGuid で検索する（タグフィルタなしで全グラフを走査） / Finds a KawaiiPhysics graph node inside an AnimBlueprint by NodeGuid, scanning all graphs with no tag filter. */
	static UAnimGraphNode_KawaiiPhysics* FindGraphNodeByGuid(
		const FSoftObjectPath& AnimBlueprintPath,
		const FGuid& NodeGuid);

	/**
	 * プロジェクト内の KawaiiPhysics プリセットDataAssetを列挙してロードする。
	 * Enumerate and load all KawaiiPhysics preset data assets in the project.
	 */
	static void FindAllPresetAssetData(TArray<TStrongObjectPtr<UKawaiiPhysicsPresetDataAsset>>& OutPresets);

	/**
	 * プロジェクト内の KawaiiPhysics プリセットDataAssetを Blueprint / Python 向けの生ポインタ配列で返す。C++ 呼び出し側は GC 安全性のため FindAllPresetAssetData (TStrongObjectPtr) を使用すること。
	 * Return all KawaiiPhysics preset data assets as raw pointers for Blueprint / Python. C++ callers should use FindAllPresetAssetData (TStrongObjectPtr) for GC safety.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor|Preset")
	static TArray<UKawaiiPhysicsPresetDataAsset*> FindAllPresetAssets();

	/**
	 * 指定 Content パス配下の AnimBlueprint アセットを列挙する（空なら /Game）。
	 * Enumerate AnimBlueprint assets under Content paths (uses /Game when empty).
	 */
	static void FindAnimBlueprintAssetData(const TArray<FString>& ContentPaths, TArray<FAssetData>& OutAssets);

	/**
	 * 指定 Content パス配下の AnimBlueprint を、GameplayTag の SearchableName 依存関係でロードなしに事前絞り込みする。FilterTags が空なら全 AnimBlueprint を返す。非 Exact では保存タグ名のプレフィックス一致で子タグ（タグ辞書未登録を含む）も対象にし、未保存の dirty パッケージは常に候補へ含める。ロード済みの AnimBlueprint は AssetRegistry ではなくメモリ上の KawaiiPhysics ノードのタグで判定する。既知の限界: UE4.15 未満保存の極端に古いパッケージ。
	 * Pre-filter AnimBlueprint assets under Content paths without loading them by GameplayTag SearchableName dependencies. Empty FilterTags returns all AnimBlueprint assets. Non-exact matching includes child tags by saved tag-name prefix matching, including tags not registered in the current dictionary, and unsaved dirty packages are always kept as candidates. Loaded AnimBlueprint assets are matched by the tags of their in-memory KawaiiPhysics nodes instead of the AssetRegistry. Known limit: extremely old packages saved before UE4.15.
	 */
	static void FindAnimBlueprintAssetDataReferencingTags(
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch,
		const TArray<FString>& ContentPaths,
		TArray<FAssetData>& OutAssets);

	/**
	 * 指定 Content パス配下の AnimBlueprint アセットパスを返す（ContentPaths が空なら /Game）。
	 * Return AnimBlueprint asset paths under Content paths (uses /Game when ContentPaths is empty).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "ContentPaths"))
	static TArray<FSoftObjectPath> FindAnimBlueprintAssets(const TArray<FString>& ContentPaths);

	/**
	 * GameplayTag の SearchableName 依存関係でロードなしに絞り込んだ AnimBlueprint アセットパスを返す（非 Exact は子タグ含む。FilterTags が空なら全件）。
	 * Return AnimBlueprint asset paths pre-filtered without loading by GameplayTag SearchableName dependencies (non-exact includes child tags; empty FilterTags returns all).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta = (AutoCreateRefTerm = "FilterTags,ContentPaths"))
	static TArray<FSoftObjectPath> FindAnimBlueprintAssetsReferencingTags(
		const FGameplayTagContainer& FilterTags, bool bFilterExactMatch,
		const TArray<FString>& ContentPaths);

	/**
	 * AnimGraph に KawaiiPhysics ノードを追加または更新する。bAutoConnect 指定時は Result ノード直前へ直列に接続する。Comment 指定時は MCP コメント枠を追加する。bAutoPosition と bAutoConnect を両方指定したリクエストがあれば、最後に LayoutKawaiiPhysicsAnimGraph と同じレイアウトで Result 上流のチェーンを1行に並べ直す。
	 * Add or update KawaiiPhysics nodes into an AnimGraph. When bAutoConnect is set, nodes are connected in series just before the Result node. A non-empty Comment adds an MCP comment frame. When any request sets both bAutoPosition and bAutoConnect, the pose chain upstream of Result is finally laid out on one row, as LayoutKawaiiPhysicsAnimGraph does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "Requests,Comment,Prompt"))
	static TArray<FKawaiiPhysicsGraphNodeHandle> AddKawaiiPhysicsNodes(
		UAnimBlueprint* AnimBlueprint,
		const TArray<FKawaiiPhysicsNodePlacementRequest>& Requests,
		EKawaiiPhysicsPlacementMatchKey MatchKey = EKawaiiPhysicsPlacementMatchKey::None,
		FName GraphName = NAME_None,
		const FString& Comment = TEXT(""),
		const FString& Prompt = TEXT(""));

	/**
	 * AnimGraph に KawaiiPhysics Shared Publisher ノードを追加する。bReuseExisting 指定時は同 Tag の既存ノードを返す。
	 * Add a KawaiiPhysics Shared Publisher node to the AnimGraph. With bReuseExisting, returns an existing node with the same tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static FKawaiiPhysicsSharedPublisherGraphNodeHandle AddKawaiiPhysicsSharedPublisherNode(
		UAnimBlueprint* AnimBlueprint,
		FGameplayTag SharedGroupTag,
		bool bReuseExisting = true,
		bool bAutoConnect = true);

	/**
	 * AnimGraph の入力ポーズとしてアニメーションを設定する。Result から上流のポーズ入力を辿り、既存の SequencePlayer があればそのシーケンスを差し替え、未接続のポーズ入力に達したら SequencePlayer を追加して接続する（コンポーネント空間の入力には変換ノードを自動挿入）。コンパイルは行わない。
	 * Set an animation as the input pose of the AnimGraph. Follows the pose inputs upstream from Result; replaces the sequence of an existing SequencePlayer, or adds and connects a SequencePlayer at the first unlinked pose input (a space conversion node is inserted automatically for component-space inputs). Does not compile.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetAnimGraphInputAnimation(
		UAnimBlueprint* AnimBlueprint,
		UAnimSequenceBase* Animation,
		FName GraphName = NAME_None);

	/**
	 * AnimGraph のノード配置を整える。Result から各ノードの先頭の接続済みポーズ入力を上流へ辿り、そのチェーンを Result の行へ上流から下流の順に左から右へ並べる（Result は動かさず、間隔はノード幅に基づく）。チェーン外のノードは動かさない。MCP コメント枠は、紐付いた KawaiiPhysics ノード（紐付けが無ければレイアウト前に枠と重なっていた KawaiiPhysics ノード、それも無く MCP コメント枠が1つだけならグラフ内の全 KawaiiPhysics ノード）を囲むよう再調整する。コンパイルは行わない。入力が不正なら false。
	 * Tidy the node layout of an AnimGraph. Follows the first linked pose input of each node upstream from Result and places that chain on Result's row, left to right from upstream to downstream (Result stays in place; spacing is based on node widths). Nodes off the chain are left in place. Each MCP comment frame is refit around its associated KawaiiPhysics nodes (or, without an association, the KawaiiPhysics nodes that overlapped it before the layout; failing that, all KawaiiPhysics nodes in the graph when it is the only MCP comment frame). Does not compile. Returns false for invalid input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool LayoutKawaiiPhysicsAnimGraph(
		UAnimBlueprint* AnimBlueprint,
		FName GraphName = NAME_None);

	/**
	 * AnimBlueprint をコンパイルし、コンパイラメッセージを "Error: " / "Warning: " / "Note: " 付きの文字列で返す。戻り値はエラー数（0 なら成功、AnimBlueprint が null なら -1）。
	 * Compile an AnimBlueprint and return compiler messages prefixed with "Error: ", "Warning: " or "Note: ". Returns the error count (0 means success, -1 when AnimBlueprint is null).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static int32 CompileAnimBlueprintWithMessages(
		UAnimBlueprint* AnimBlueprint,
		TArray<FString>& OutMessages);

	/**
	 * AnimGraph 上のコメントノード一覧を返す。
	 * Returns comment nodes in the AnimGraph.
	 */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Editor")
	static TArray<FKawaiiPhysicsAnimGraphCommentInfo> GetAnimGraphComments(
		UAnimBlueprint* AnimBlueprint,
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
	 * スケルトンの参照ボーン名を正規表現で検索する。既存 ApplyRegex と同じく部分一致可能な FindNext() を使う。
	 * Find reference bone names by regex. Uses the existing ApplyRegex-style FindNext() behavior, so partial matches are possible.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FName> FindBonesByPattern(USkeleton* Skeleton, const FString& Pattern);

	/** グラフノードハンドルが有効か / Check whether a graph node handle is valid. */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Editor")
	static bool IsGraphNodeHandleValid(const FKawaiiPhysicsGraphNodeHandle& Handle);

	/** Shared Publisher グラフノードハンドルが有効か / Check whether a Shared Publisher graph node handle is valid. */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Editor")
	static bool IsSharedPublisherGraphNodeHandleValid(const FKawaiiPhysicsSharedPublisherGraphNodeHandle& Handle);

	/** ノードプロパティを文字列で設定 / Set a node property from string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetGraphNodePropertyFromString(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		const FString& Value);

	/** ノードプロパティを文字列で取得 / Get a node property as string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodePropertyAsString(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName PropertyName,
		FString& OutValue);

	/**
	 * ノードの ExternalForces を JSON 配列で取得する。各要素は "_structType"（構造体パス）と編集可能なフィールドを持つオブジェクト（空スロットは null）。
	 * Get the node's ExternalForces as a JSON array. Each element is an object with "_structType" (struct path) and the editable fields (an empty slot is null).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodeExternalForcesAsJson(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FString& OutJson);

	/**
	 * ノードの ExternalForces を GetGraphNodeExternalForcesAsJson と同じ形式の JSON 配列で置き換える。"_structType" は FKawaiiPhysics_ExternalForce の派生構造体（パスまたは名前）であること。省略したフィールドは既定値。戻り値は設定した要素数（失敗時は -1 で OutError に理由、ノードは変更しない）。
	 * Replace the node's ExternalForces from a JSON array in the GetGraphNodeExternalForcesAsJson format. "_structType" must name a struct derived from FKawaiiPhysics_ExternalForce (path or name). Omitted fields keep their defaults. Returns the element count (-1 on failure with the reason in OutError; the node is left unchanged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static int32 SetGraphNodeExternalForcesFromJson(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		const FString& ForcesJson,
		FString& OutError);

	/**
	 * ノードを含む AnimBlueprint のターゲットスケルトンについて、ボーンの参照ポーズのコンポーネント空間トランスフォームを取得する。
	 * Get the component-space reference pose transform of a bone in the target skeleton of the AnimBlueprint that owns the node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool GetGraphNodeReferenceBoneTransform(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		FName BoneName,
		FTransform& OutComponentTransform);

	/**
	 * エディタワールド（PIE ではない側）を返す。PIE 実行中でもプレイモードのエラーを出さずに返す（取得できなければ null）。
	 * Return the editor (non-PIE) world. Works while PIE runs without logging the play-mode error (null when unavailable).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static UWorld* GetEditorWorldIgnoringPlayMode();

	/** Shared Publisher ノードプロパティを文字列で設定 / Set a Shared Publisher node property from string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetSharedPublisherNodePropertyFromString(
		const FKawaiiPhysicsSharedPublisherGraphNodeHandle& Handle,
		FName PropertyName,
		const FString& Value);

	/** Shared Publisher ノードプロパティを文字列で取得 / Get a Shared Publisher node property as string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static FString GetSharedPublisherNodePropertyAsString(
		const FKawaiiPhysicsSharedPublisherGraphNodeHandle& Handle,
		FName PropertyName);

	/** プリセット内ノードプロパティを文字列で設定 / Set a preset node property from string. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool SetPresetNodePropertyFromString(
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
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor|Preset")
	static bool MakeGameplayTagContainerFromNames(
		const TArray<FName>& TagNames,
		FGameplayTagContainer& OutContainer);

	/**
	 * プリセットの TargetTags をタグ名配列から設定する（未登録タグ名は警告してスキップ。入力非空で全滅した場合は変更せず false）。
	 * Set a preset's TargetTags from tag names (unregistered names are skipped with a warning; leaves the preset unchanged and returns false when non-empty input resolves to no tags).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor|Preset")
	static bool SetPresetTargetTags(
		UKawaiiPhysicsPresetDataAsset* Preset,
		const TArray<FName>& TagNames,
		bool bExactMatch);

	/** プリセットの説明文を設定 / Set the preset description. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor|Preset")
	static bool SetPresetDescription(UKawaiiPhysicsPresetDataAsset* Preset, const FText& Description);

	/** プリセットの説明文を取得 / Get the preset description. */
	UFUNCTION(BlueprintPure, Category = "Kawaii Physics|Editor")
	static FText GetPresetDescription(const UKawaiiPhysicsPresetDataAsset* Preset);

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

	/** グラフノードとプリセットの差分を値付きで取得。空なら一致 / Get graph node preset diff values. Empty means the node matches the preset. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static TArray<FKawaiiPhysicsPresetDiffValue> GetGraphNodePresetDiffValues(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		UKawaiiPhysicsPresetDataAsset* Preset,
		FKawaiiPhysicsPresetApplyOptions Options);

	/** グラフノードを既存プリセットアセットへ書き出し / Export a graph node to an existing preset asset. */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static bool ExportGraphNodeToPreset(
		const FKawaiiPhysicsGraphNodeHandle& Handle,
		UKawaiiPhysicsPresetDataAsset* TargetAsset);

	/**
	 * Preset の TargetTags がマッチするノードへ適用する（TargetTags空なら対象なし）
	 * Apply a preset to nodes whose tags match the preset's TargetTags (empty TargetTags targets nothing).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor")
	static int32 ApplyPresetToProject(
		UKawaiiPhysicsPresetDataAsset* Preset,
		bool bDryRun,
		bool bCheckOutFiles,
		TArray<FKawaiiPhysicsNodeAuditEntry>& OutReport);

	/**
	 * 指定 Content パス配下の KawaiiPhysics ノードを監査する。bIncludeDiffValues=true で不一致ノードの DiffValues を充填する（既定 false は後方互換のため空のまま）。
	 * Audit KawaiiPhysics nodes under Content paths. When bIncludeDiffValues is true, DiffValues is filled for non-matching nodes (default false leaves it empty for backward compatibility).
	 */
	UFUNCTION(BlueprintCallable, Category = "Kawaii Physics|Editor",
		meta=(AutoCreateRefTerm = "ContentPaths,FilterTags"))
	static bool AuditKawaiiPhysicsNodes(
		const TArray<FString>& ContentPaths,
		const FGameplayTagContainer& FilterTags,
		bool bFilterExactMatch,
		TArray<FKawaiiPhysicsNodeAuditEntry>& OutEntries,
		bool bIncludeDiffValues = false);

private:
	DECLARE_FUNCTION(execSetGraphNodeWildcardProperty);
	DECLARE_FUNCTION(execGetGraphNodeWildcardProperty);
};
