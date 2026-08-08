// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "KawaiiPhysicsEditorLibrary.h"

#include "KawaiiPhysicsAuditCommandlet.generated.h"

class FJsonObject;

UCLASS()
class KAWAIIPHYSICSED_API UKawaiiPhysicsAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UKawaiiPhysicsAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};

namespace KawaiiPhysicsAuditJson
{
	/**
	 * 監査エントリ配列とサマリを、コマンドレット出力と同一スキーマの JSON オブジェクトへ変換する。
	 * SKawaiiPhysicsNodeAuditWindow の Export... からも同一スキーマで共有利用される。
	 * Convert audit entries and a summary into a JSON object using the same schema as the commandlet output.
	 * Also shared and reused by SKawaiiPhysicsNodeAuditWindow's Export... to keep the schema identical.
	 */
	TSharedPtr<FJsonObject> MakeAuditJsonObject(
		const TArray<FKawaiiPhysicsNodeAuditEntry>& Entries,
		int32 TotalAnimBlueprints);
}
