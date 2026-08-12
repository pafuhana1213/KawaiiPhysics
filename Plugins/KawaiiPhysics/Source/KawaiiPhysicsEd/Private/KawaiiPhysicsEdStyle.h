// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

class FSlateStyleSet;
class ISlateStyle;

class FKawaiiPhysicsEdStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
