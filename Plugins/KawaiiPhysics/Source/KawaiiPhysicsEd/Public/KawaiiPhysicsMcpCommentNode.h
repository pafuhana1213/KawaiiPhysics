// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphNode_Comment.h"

#include "KawaiiPhysicsMcpCommentNode.generated.h"

// UHTはUCLASSの任意#if囲みを拒否するため、5.5未満ではgen.cppを含めず使用箇所も消して休眠状態にする
UCLASS()
class UKawaiiPhysicsMcpCommentNode : public UEdGraphNode_Comment
{
	GENERATED_BODY()

public:
	/** MCP経由でノードを追加した際の指示プロンプト全文 / Full instruction prompt used when nodes were added via MCP */
	UPROPERTY(VisibleAnywhere, Category = "Kawaii Physics|MCP", meta = (MultiLine = true))
	FString Prompt;

	/** 枠の作成日時 / When this comment frame was created */
	UPROPERTY(VisibleAnywhere, Category = "Kawaii Physics|MCP")
	FDateTime CreatedAt;

	/** 最終更新日時（FindOrAdd再実行で更新） / Last updated (refreshed on find-or-add re-run) */
	UPROPERTY(VisibleAnywhere, Category = "Kawaii Physics|MCP")
	FDateTime UpdatedAt;
};
