// Fill out your copyright notice in the Description page of Project Settings.


#include "KawaiiPhysicsLibrary.h"

#include "AnimNode_KawaiiPhysics.h"
#include "KawaiiPhysicsExternalForce.h"

DEFINE_LOG_CATEGORY_STATIC(LogKawaiiPhysicsLibrary, Verbose, All);

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ConvertToKawaiiPhysics(const FAnimNodeReference& Node,
                                                                      EAnimNodeReferenceConversionResult& Result)
{
	return FAnimNodeReference::ConvertToType<FKawaiiPhysicsReference>(Node, Result);
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                               FName& RootBoneName)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetRootBoneName"),
		[RootBoneName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.RootBone = FBoneReference(RootBoneName);
		});

	return KawaiiPhysics;
}

FName UKawaiiPhysicsLibrary::GetRootBoneName(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	FName RootBoneName;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetRootBoneName"),
		[&RootBoneName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			RootBoneName = InKawaiiPhysics.RootBone.BoneName;
		});

	return RootBoneName;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                                   TArray<FName>& ExcludeBoneNames)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExcludeBoneNames"),
		[&ExcludeBoneNames](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.ExcludeBones.Empty();
			for (auto& ExcludeBoneName : ExcludeBoneNames)
			{
				InKawaiiPhysics.ExcludeBones.Add(FBoneReference(ExcludeBoneName));
			}
		});

	return KawaiiPhysics;
}

TArray<FName> UKawaiiPhysicsLibrary::GetExcludeBoneNames(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	TArray<FName> ExcludeBoneNames;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExcludeBoneNames"),
		[&ExcludeBoneNames](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			for (auto& ExcludeBone : InKawaiiPhysics.ExcludeBones)
			{
				ExcludeBoneNames.Add(ExcludeBone.BoneName);
			}
		});

	return ExcludeBoneNames;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::SetExternalForceBoolParameter(
	const FKawaiiPhysicsReference& KawaiiPhysics, int ExternalForceIndex, FName PropertyName, bool Value)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("SetExternalForceBoolParameter"),
		[&ExternalForceIndex, &PropertyName, &Value](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FBoolProperty* BoolProperty = FindFieldChecked<FBoolProperty>(ScriptStruct, PropertyName))
				{
					if (void* ValuePtr = BoolProperty->ContainerPtrToValuePtr<uint8>(&Force))
					{
						BoolProperty->SetPropertyValue(ValuePtr, Value);
					}
				}
			}
		});

	return KawaiiPhysics;
}

bool UKawaiiPhysicsLibrary::GetExternalForceBoolParameter(const FKawaiiPhysicsReference& KawaiiPhysics,
                                                          int ExternalForceIndex, FName PropertyName)
{
	bool Result = false;

	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("GetExternalForceBoolParameter"),
		[&Result, &ExternalForceIndex, &PropertyName](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			if (InKawaiiPhysics.ExternalForces.IsValidIndex(ExternalForceIndex) &&
				InKawaiiPhysics.ExternalForces[ExternalForceIndex].IsValid())
			{
				const auto* ScriptStruct = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetScriptStruct();
				const auto& Force = InKawaiiPhysics.ExternalForces[ExternalForceIndex].GetMutable<
					FKawaiiPhysics_ExternalForce>();

				if (const FBoolProperty* BoolProperty = FindFieldChecked<FBoolProperty>(ScriptStruct, PropertyName))
				{
					Result = *(BoolProperty->ContainerPtrToValuePtr<bool>(&Force));
				}
			}
		});

	return Result;
}

FKawaiiPhysicsReference UKawaiiPhysicsLibrary::ResetDynamics(const FKawaiiPhysicsReference& KawaiiPhysics)
{
	KawaiiPhysics.CallAnimNodeFunction<FAnimNode_KawaiiPhysics>(
		TEXT("ResetDynamics"),
		[](FAnimNode_KawaiiPhysics& InKawaiiPhysics)
		{
			InKawaiiPhysics.ResetDynamics(ETeleportType::ResetPhysics);
		});

	return KawaiiPhysics;
}
