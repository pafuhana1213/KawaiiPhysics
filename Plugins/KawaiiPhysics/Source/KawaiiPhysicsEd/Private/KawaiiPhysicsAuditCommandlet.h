// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "KawaiiPhysicsAuditCommandlet.generated.h"

UCLASS()
class KAWAIIPHYSICSED_API UKawaiiPhysicsAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UKawaiiPhysicsAuditCommandlet();

	virtual int32 Main(const FString& Params) override;
};
