// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsEditorCategoryNames.h"
#include "Misc/AutomationTest.h"
#include "UObject/FieldIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	void CollectCategoryMetadata(const UStruct* Struct, TSet<FName>& OutCategories)
	{
		const FName CategoryKey(TEXT("Category"));
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (Property && Property->HasMetaData(CategoryKey))
			{
				OutCategories.Add(FName(*Property->GetMetaData(CategoryKey)));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsEditorCategoryConsistencyTest,
                                 "KawaiiPhysics.Editor.CategoryConsistency",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsEditorCategoryConsistencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

#if WITH_EDITORONLY_DATA
	TSet<FName> ExistingCategories;
	CollectCategoryMetadata(FAnimNode_KawaiiPhysics::StaticStruct(), ExistingCategories);
	CollectCategoryMetadata(UAnimGraphNode_KawaiiPhysics::StaticClass(), ExistingCategories);
	ExistingCategories.Add(KawaiiPhysicsEditorCategoryNames::KawaiiPhysicsTools);
	ExistingCategories.Add(KawaiiPhysicsEditorCategoryNames::DebugVisualization);
	ExistingCategories.Add(KawaiiPhysicsEditorCategoryNames::CategoryFilter);

	bool bOk = true;
	for (const FName& CategoryName : KawaiiPhysicsEditorCategoryNames::GetCategorySortOrderNames())
	{
		bOk &= TestTrue(
			FString::Printf(TEXT("ソート対象カテゴリが実在すること: %s"), *CategoryName.ToString()),
			ExistingCategories.Contains(CategoryName));
	}

	for (const KawaiiPhysicsEditorCategoryNames::FCategoryFilterGroup& FilterGroup :
	     KawaiiPhysicsEditorCategoryNames::GetFilterGroups())
	{
		for (const FName& CategoryName : FilterGroup.CategoryNames)
		{
			bOk &= TestTrue(
				FString::Printf(TEXT("フィルタグループ %s のカテゴリが実在すること: %s"),
				                *FilterGroup.GroupId.ToString(),
				                *CategoryName.ToString()),
				ExistingCategories.Contains(CategoryName));
		}
	}

	return bOk;
#else
	return true;
#endif
}

#endif
