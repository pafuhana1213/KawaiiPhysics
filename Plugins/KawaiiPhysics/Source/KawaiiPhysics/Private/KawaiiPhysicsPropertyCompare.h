// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace KawaiiPhysicsPropertyComparePrivate
{

// カーブの KeyHandle の表（FIndexedCurve::KeyHandlesToIndices の FKeyHandleMap）か。
// KeyHandle はプロセスで一意の ID で、値のコピーでは写らないので、同じキーのカーブでも一致しない。
// ほかの Transient は比べる（UE 5.3/5.4 の FCollisionResponse::ResponseToChannels のように、実際に使う設定もある）。
// FKeyHandleMap は Engine の外へ公開されていない（StaticStruct をリンクできない）ので、構造体の名前で見分ける。
static inline bool IsCurveKeyHandleMapProperty(const FProperty& Property)
{
	static const FName KeyHandleMapName(TEXT("KeyHandleMap"));
	const FStructProperty* StructProperty = CastField<FStructProperty>(&Property);
	return StructProperty && StructProperty->Struct && StructProperty->Struct->GetFName() == KeyHandleMapName;
}

// 設定の同値判定。カーブの KeyHandle の表だけを比べず、ほかは Property->Identical と同じ規則で比べる。
static inline bool ArePropertyValuesIdentical(const FProperty* Property, const void* A, const void* B,
	uint32 PortFlags)
{
	// nullptr との比較はプロパティ固有の既定値判定を維持する。
	if (!B)
	{
		return Property->Identical(A, B, PortFlags);
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayA(ArrayProperty, A);
		FScriptArrayHelper ArrayB(ArrayProperty, B);
		if (ArrayA.Num() != ArrayB.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < ArrayA.Num(); ++Index)
		{
			if (!ArePropertyValuesIdentical(ArrayProperty->Inner, ArrayA.GetRawPtr(Index), ArrayB.GetRawPtr(Index), PortFlags))
			{
				return false;
			}
		}
		return true;
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		// ネイティブの同値判定は、非 UPROPERTY の値や型固有の比較規則も扱うため維持する。
		if (!(StructProperty->Struct->StructFlags & STRUCT_IdenticalNative))
		{
			for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if (IsCurveKeyHandleMapProperty(**It))
				{
					continue;
				}
				for (int32 ArrayIndex = 0; ArrayIndex < It->ArrayDim; ++ArrayIndex)
				{
					if (!ArePropertyValuesIdentical(*It, It->ContainerPtrToValuePtr<void>(A, ArrayIndex),
						It->ContainerPtrToValuePtr<void>(B, ArrayIndex), PortFlags))
					{
						return false;
					}
				}
			}
			return true;
		}
	}
	return Property->Identical(A, B, PortFlags);
}

} // namespace KawaiiPhysicsPropertyComparePrivate
