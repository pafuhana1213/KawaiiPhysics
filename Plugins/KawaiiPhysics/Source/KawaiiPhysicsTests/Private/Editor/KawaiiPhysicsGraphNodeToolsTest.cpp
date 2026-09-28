// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "KawaiiPhysicsEditorLibrary.h"

#include "AnimGraphNode_KawaiiPhysics.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "ExternalForces/KawaiiPhysicsExternalForce.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Basic.h"
#include "ExternalForces/KawaiiPhysicsExternalForce_Wind.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"

namespace
{
	// 同一モジュールの他テストとユニティビルドで衝突しないよう、ヘルパー名に GraphNodeTools を付ける
	USkeleton* CreateGraphNodeToolsTestSkeleton(UObject* Outer)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Outer ? Outer : GetTransientPackage());
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Pelvis"), TEXT("Pelvis"), 0),
		             FTransform(FVector(0.0, 0.0, 100.0)));
		Modifier.Add(FMeshBoneInfo(TEXT("Spine_01"), TEXT("Spine_01"), 1),
		             FTransform(FRotator(-90.0, 0.0, 0.0), FVector(0.0, 0.0, 10.0)));
		Modifier.Add(FMeshBoneInfo(TEXT("Neck"), TEXT("Neck"), 2), FTransform(FVector(20.0, 0.0, 0.0)));
		Modifier.Add(FMeshBoneInfo(TEXT("HeadTop"), TEXT("HeadTop"), 3), FTransform(FVector(10.0, 0.0, 0.0)));
		Modifier.Add(FMeshBoneInfo(TEXT("Thigh_L"), TEXT("Thigh_L"), 1), FTransform(FVector(0.0, 10.0, 0.0)));
		Modifier.Add(FMeshBoneInfo(TEXT("Thigh_R"), TEXT("Thigh_R"), 1), FTransform(FVector(0.0, -10.0, 0.0)));
		return Skeleton;
	}

	UAnimBlueprint* CreateGraphNodeToolsTestAnimBlueprint(FAutomationTestBase& Test)
	{
		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString PackageName = FString::Printf(TEXT("/Temp/KawaiiPhysicsGraphNodeTools_%s"), *UniqueSuffix);
		UPackage* Package = CreatePackage(*PackageName);
		Package->SetFlags(RF_Transient);

		const FName BlueprintName(*FString::Printf(TEXT("ABP_KawaiiPhysicsGraphNodeTools_%s"), *UniqueSuffix));
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UAnimInstance::StaticClass(),
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UAnimBlueprint::StaticClass(),
			UAnimBlueprintGeneratedClass::StaticClass()));
		Test.TestNotNull(TEXT("Transient AnimBlueprint is created"), AnimBlueprint);
		if (AnimBlueprint)
		{
			AnimBlueprint->TargetSkeleton = CreateGraphNodeToolsTestSkeleton(Package);
		}
		return AnimBlueprint;
	}

	FKawaiiPhysicsGraphNodeHandle AddGraphNodeToolsTestNode(UAnimBlueprint* AnimBlueprint)
	{
		FKawaiiPhysicsNodePlacementRequest Request;
		Request.RootBoneName = TEXT("Spine_01");
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(Request);

		const TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
			UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(AnimBlueprint, Requests);
		return Handles.IsValidIndex(0) ? Handles[0] : FKawaiiPhysicsGraphNodeHandle();
	}

	FKawaiiPhysicsNodePlacementRequest MakeGraphNodeToolsRequest(FName RootBoneName,
	                                                              const TArray<FName>& ExcludeBoneNames)
	{
		FKawaiiPhysicsNodePlacementRequest Request;
		Request.RootBoneName = RootBoneName;
		Request.ExcludeBoneNames = ExcludeBoneNames;
		return Request;
	}

	bool HasGraphNodeToolsNestedRootWarning(const TArray<FString>& Messages, const TCHAR* RootBoneName)
	{
		return Messages.ContainsByPredicate(
			[RootBoneName](const FString& Message)
			{
				return Message.StartsWith(TEXT("Warning:")) &&
					Message.Contains(TEXT("descendant")) &&
					Message.Contains(FString::Printf(TEXT("'%s'"), RootBoneName));
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsNestedRootExcludeBonesTest,
                                 "KawaiiPhysics.EditorScripting.Placement.Validation.NestedRootExcludeBones",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsNestedRootExcludeBonesTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* AnimBlueprint = CreateGraphNodeToolsTestAnimBlueprint(*this);
	if (!AnimBlueprint)
	{
		return false;
	}

	bool bOk = true;

	// ExcludeBones で胴体側の枝を外した Pelvis ノードに対し、HeadTop は子孫でも別チェーンとして扱う
	{
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("Pelvis"), {TEXT("Spine_01"), TEXT("Thigh_L"), TEXT("Thigh_R")}));
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("HeadTop"), {}));
		const TArray<FString> Messages =
			UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(AnimBlueprint, Requests);
		bOk &= TestFalse(TEXT("Excluded branch does not report a nested root warning"),
		                 HasGraphNodeToolsNestedRootWarning(Messages, TEXT("HeadTop")));
		bOk &= TestEqual(TEXT("Excluded branch reports no validation messages"), Messages.Num(), 0);
	}

	// 除外が別の枝だけなら HeadTop は Pelvis のチェーン内に残るため警告する
	{
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("Pelvis"), {TEXT("Thigh_L")}));
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("HeadTop"), {}));
		const TArray<FString> Messages =
			UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(AnimBlueprint, Requests);
		bOk &= TestTrue(TEXT("Unrelated exclude keeps the nested root warning"),
		                HasGraphNodeToolsNestedRootWarning(Messages, TEXT("HeadTop")));
	}

	// Root 自身の除外もチェーン外として扱う
	{
		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("Pelvis"), {TEXT("HeadTop")}));
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("HeadTop"), {}));
		const TArray<FString> Messages =
			UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(AnimBlueprint, Requests);
		bOk &= TestFalse(TEXT("Excluding the nested root itself suppresses the warning"),
		                 HasGraphNodeToolsNestedRootWarning(Messages, TEXT("HeadTop")));
	}

	// AdditionalRootBone は OverrideExcludeBones 使用時にそちらの除外で判定する
	{
		FKawaiiPhysicsNodePlacementRequest PelvisRequest = MakeGraphNodeToolsRequest(TEXT("Thigh_L"), {});
		FKawaiiPhysicsRootBoneSetting AdditionalRootBone;
		AdditionalRootBone.RootBone = FBoneReference(TEXT("Pelvis"));
		AdditionalRootBone.bUseOverrideExcludeBones = true;
		AdditionalRootBone.OverrideExcludeBones.Add(FBoneReference(TEXT("Spine_01")));
		PelvisRequest.AdditionalRootBones.Add(AdditionalRootBone);

		TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
		Requests.Add(PelvisRequest);
		Requests.Add(MakeGraphNodeToolsRequest(TEXT("HeadTop"), {}));
		const TArray<FString> Messages =
			UKawaiiPhysicsEditorLibrary::ValidatePlacementRequests(AnimBlueprint, Requests);
		bOk &= TestFalse(TEXT("OverrideExcludeBones of an AdditionalRootBone suppresses the warning"),
		                 HasGraphNodeToolsNestedRootWarning(Messages, TEXT("HeadTop")));
		// Thigh_L は AdditionalRootBone の Pelvis チェーン内（除外は Spine_01 のみ）のため警告が残る
		bOk &= TestTrue(TEXT("Root inside the AdditionalRootBone chain still warns"),
		                HasGraphNodeToolsNestedRootWarning(Messages, TEXT("Thigh_L")));
	}

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsExternalForcesJsonTest,
                                 "KawaiiPhysics.EditorScripting.ExternalForces.Json",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsExternalForcesJsonTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* AnimBlueprint = CreateGraphNodeToolsTestAnimBlueprint(*this);
	const FKawaiiPhysicsGraphNodeHandle Handle = AddGraphNodeToolsTestNode(AnimBlueprint);
	UAnimGraphNode_KawaiiPhysics* GraphNode = Handle.Node.Get();
	if (!TestNotNull(TEXT("KawaiiPhysics graph node is created"), GraphNode))
	{
		return false;
	}

	bool bOk = true;
	FString Error;
	const FString ForcesJson = TEXT(
		"[{\"_structType\": \"/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce_Basic\","
		" \"ForceDir\": {\"X\": 0, \"Y\": 1, \"Z\": 0},"
		" \"RandomForceScaleRange\": {\"Min\": 30, \"Max\": 30}},"
		" {\"_structType\": \"KawaiiPhysics_ExternalForce_Wind\"}]");
	const int32 SetCount = UKawaiiPhysicsEditorLibrary::SetGraphNodeExternalForcesFromJson(Handle, ForcesJson, Error);
	bOk &= TestEqual(TEXT("Two external forces are set"), SetCount, 2);
	bOk &= TestTrue(TEXT("Successful set reports no error"), Error.IsEmpty());
	bOk &= TestEqual(TEXT("Node holds two external forces"), GraphNode->Node.ExternalForces.Num(), 2);

	const FKawaiiPhysics_ExternalForce_Basic* BasicForce =
		GraphNode->Node.ExternalForces.IsValidIndex(0)
			? GraphNode->Node.ExternalForces[0].GetPtr<FKawaiiPhysics_ExternalForce_Basic>()
			: nullptr;
	if (TestNotNull(TEXT("First force is Basic"), BasicForce))
	{
		bOk &= TestEqual(TEXT("ForceDir is applied"), BasicForce->ForceDir, FVector(0.0, 1.0, 0.0));
		bOk &= TestEqual(TEXT("RandomForceScaleRange.Min is applied"), BasicForce->RandomForceScaleRange.Min, 30.0f);
		bOk &= TestTrue(TEXT("Omitted fields keep defaults"), BasicForce->bIsEnabled);
	}
	else
	{
		bOk = false;
	}
	bOk &= TestTrue(TEXT("Short struct name resolves to Wind"),
	                GraphNode->Node.ExternalForces.IsValidIndex(1) &&
	                GraphNode->Node.ExternalForces[1].GetScriptStruct() ==
	                FKawaiiPhysics_ExternalForce_Wind::StaticStruct());

	FString ReadJson;
	bOk &= TestTrue(TEXT("External forces are read as JSON"),
	                UKawaiiPhysicsEditorLibrary::GetGraphNodeExternalForcesAsJson(Handle, ReadJson));
	bOk &= TestTrue(TEXT("JSON carries the struct path"),
	                ReadJson.Contains(TEXT("/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce_Basic")));
	bOk &= TestTrue(TEXT("JSON carries editable fields"), ReadJson.Contains(TEXT("\"ForceDir\"")));
	bOk &= TestFalse(TEXT("JSON skips runtime-only fields"), ReadJson.Contains(TEXT("\"RandomizedForceScale\"")));

	// 読み出した JSON をそのまま書き戻せる
	bOk &= TestEqual(TEXT("Read JSON round-trips"),
	                 UKawaiiPhysicsEditorLibrary::SetGraphNodeExternalForcesFromJson(Handle, ReadJson, Error), 2);
	FString RoundTripJson;
	UKawaiiPhysicsEditorLibrary::GetGraphNodeExternalForcesAsJson(Handle, RoundTripJson);
	bOk &= TestEqual(TEXT("Round-trip JSON is stable"), RoundTripJson, ReadJson);

	// 不正な入力はノードを変更せずに -1 と理由を返す
	const TArray<FString> InvalidJsons = {
		TEXT("[{\"_structType\": \"/Script/KawaiiPhysics.KawaiiPhysics_ExternalForce\"}]"),
		TEXT("[{\"_structType\": \"/Script/CoreUObject.Vector\"}]"),
		TEXT("[{\"ForceDir\": {\"X\": 1}}]"),
		TEXT("[{\"_structType\": \"KawaiiPhysics_ExternalForce_Basic\", \"NoSuchField\": 1}]"),
		TEXT("[{\"_structType\": \"KawaiiPhysics_ExternalForce_Basic\", \"RandomizedForceScale\": 1}]"),
		TEXT("{\"_structType\": \"KawaiiPhysics_ExternalForce_Basic\"}"),
		TEXT("not json"),
	};
	for (const FString& InvalidJson : InvalidJsons)
	{
		Error.Reset();
		bOk &= TestEqual(*FString::Printf(TEXT("Invalid JSON is rejected: %s"), *InvalidJson),
		                 UKawaiiPhysicsEditorLibrary::SetGraphNodeExternalForcesFromJson(Handle, InvalidJson, Error), -1);
		bOk &= TestFalse(*FString::Printf(TEXT("Invalid JSON reports a reason: %s"), *InvalidJson), Error.IsEmpty());
		bOk &= TestEqual(*FString::Printf(TEXT("Invalid JSON leaves the node unchanged: %s"), *InvalidJson),
		                 GraphNode->Node.ExternalForces.Num(), 2);
	}

	bOk &= TestEqual(TEXT("Empty array clears the forces"),
	                 UKawaiiPhysicsEditorLibrary::SetGraphNodeExternalForcesFromJson(Handle, TEXT("[]"), Error), 0);
	bOk &= TestEqual(TEXT("Node has no external forces after clearing"), GraphNode->Node.ExternalForces.Num(), 0);

	Error.Reset();
	bOk &= TestEqual(TEXT("Invalid handle is rejected"),
	                 UKawaiiPhysicsEditorLibrary::SetGraphNodeExternalForcesFromJson(
		                 FKawaiiPhysicsGraphNodeHandle(), TEXT("[]"), Error), -1);
	FString InvalidHandleJson;
	bOk &= TestFalse(TEXT("Invalid handle cannot be read"),
	                 UKawaiiPhysicsEditorLibrary::GetGraphNodeExternalForcesAsJson(
		                 FKawaiiPhysicsGraphNodeHandle(), InvalidHandleJson));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsReferenceBoneTransformTest,
                                 "KawaiiPhysics.EditorScripting.ReferenceBoneTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsReferenceBoneTransformTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* AnimBlueprint = CreateGraphNodeToolsTestAnimBlueprint(*this);
	const FKawaiiPhysicsGraphNodeHandle Handle = AddGraphNodeToolsTestNode(AnimBlueprint);
	if (!TestTrue(TEXT("KawaiiPhysics graph node is created"), Handle.IsValid()))
	{
		return false;
	}

	bool bOk = true;
	FTransform BoneTransform;
	bOk &= TestTrue(TEXT("Neck reference transform is found"),
	                UKawaiiPhysicsEditorLibrary::GetGraphNodeReferenceBoneTransform(Handle, TEXT("Neck"), BoneTransform));
	// Pelvis(0,0,100) -> Spine_01(0,0,10, Pitch -90) -> Neck(ローカル X 20) なので Neck は Z 方向に 20 下がる
	bOk &= TestTrue(TEXT("Neck component location follows the reference pose"),
	                BoneTransform.GetLocation().Equals(FVector(0.0, 0.0, 90.0), KINDA_SMALL_NUMBER * 10.0));
	bOk &= TestTrue(TEXT("Neck component rotation inherits Spine_01 pitch"),
	                BoneTransform.GetRotation().Equals(FRotator(-90.0, 0.0, 0.0).Quaternion(), 1.e-3f));

	FTransform MissingTransform;
	bOk &= TestFalse(TEXT("Missing bone is rejected"),
	                 UKawaiiPhysicsEditorLibrary::GetGraphNodeReferenceBoneTransform(
		                 Handle, TEXT("NoSuchBone"), MissingTransform));
	bOk &= TestFalse(TEXT("Invalid handle is rejected"),
	                 UKawaiiPhysicsEditorLibrary::GetGraphNodeReferenceBoneTransform(
		                 FKawaiiPhysicsGraphNodeHandle(), TEXT("Neck"), MissingTransform));
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsEditorWorldTest,
                                 "KawaiiPhysics.EditorScripting.EditorWorldIgnoringPlayMode",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsEditorWorldTest::RunTest(const FString& Parameters)
{
	if (!GEditor)
	{
		AddInfo(TEXT("GEditor is not available; skipping."));
		return true;
	}

	return TestTrue(TEXT("Editor world matches the editor world context"),
	                UKawaiiPhysicsEditorLibrary::GetEditorWorldIgnoringPlayMode() ==
	                GEditor->GetEditorWorldContext(false).World());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsAnimNodeFunctionTest,
                                 "KawaiiPhysics.EditorScripting.GraphNode.AnimNodeFunction",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsAnimNodeFunctionTest::RunTest(const FString& Parameters)
{
	UAnimBlueprint* AnimBlueprint = CreateGraphNodeToolsTestAnimBlueprint(*this);
	FKawaiiPhysicsNodePlacementRequest Request;
	Request.RootBoneName = TEXT("Spine_01");
	Request.bAutoConnect = true;
	TArray<FKawaiiPhysicsNodePlacementRequest> Requests;
	Requests.Add(Request);
	const TArray<FKawaiiPhysicsGraphNodeHandle> Handles =
		UKawaiiPhysicsEditorLibrary::AddKawaiiPhysicsNodes(AnimBlueprint, Requests);
	const FKawaiiPhysicsGraphNodeHandle Handle = Handles.IsValidIndex(0)
		? Handles[0] : FKawaiiPhysicsGraphNodeHandle();
	UAnimGraphNode_KawaiiPhysics* GraphNode = Handle.Node.Get();
	if (!TestNotNull(TEXT("KawaiiPhysics graph node is created"), GraphNode))
	{
		return false;
	}

	bool bOk = true;
	const EKawaiiPhysicsAnimNodeFunctionEvent Events[] = {
		EKawaiiPhysicsAnimNodeFunctionEvent::InitialUpdate,
		EKawaiiPhysicsAnimNodeFunctionEvent::BecomeRelevant,
		EKawaiiPhysicsAnimNodeFunctionEvent::Update,
	};
	const FName FunctionNames[] = {TEXT("KPTestInitialUpdate"), TEXT("KPTestBecomeRelevant"), TEXT("KPTestUpdate")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Events); ++Index)
	{
		FString Error;
		bOk &= TestEqual(TEXT("AnimNode Function is created and bound"),
		                UKawaiiPhysicsEditorLibrary::BindGraphNodeAnimNodeFunction(
			                Handle, Events[Index], FunctionNames[Index], Error), 1);
		bOk &= TestTrue(TEXT("New function reports no error"), Error.IsEmpty());
		UFunction* Function = AnimBlueprint->SkeletonGeneratedClass
			? AnimBlueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionNames[Index]) : nullptr;
		bOk &= TestNotNull(TEXT("Function is in the skeleton generated class"), Function);
		if (Function)
		{
			bOk &= TestTrue(TEXT("Function is Blueprint thread safe"),
			                FBlueprintEditorUtils::HasFunctionBlueprintThreadSafeMetaData(Function));
		}
		const FMemberReference* Reference = Index == 0 ? &GraphNode->InitialUpdateFunction
			: Index == 1 ? &GraphNode->BecomeRelevantFunction : &GraphNode->UpdateFunction;
		bOk &= TestEqual(TEXT("Event references its function"), Reference->GetMemberName(), FunctionNames[Index]);
	}
	TArray<FString> Messages;
	bOk &= TestEqual(TEXT("Bound AnimNode Functions compile without errors"),
	                 UKawaiiPhysicsEditorLibrary::CompileAnimBlueprintWithMessages(AnimBlueprint, Messages), 0);

	FString Error;
	bOk &= TestEqual(TEXT("Same function can be rebound without creating a graph"),
	                UKawaiiPhysicsEditorLibrary::BindGraphNodeAnimNodeFunction(
		                Handle, EKawaiiPhysicsAnimNodeFunctionEvent::Update, FunctionNames[2], Error), 0);
	bOk &= TestTrue(TEXT("Rebinding reports no error"), Error.IsEmpty());
	UEdGraph* UpdateGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FunctionNames[2])
		{
			UpdateGraph = Graph;
			break;
		}
	}
	bOk &= TestNotNull(TEXT("Update function graph exists"), UpdateGraph);
	bOk &= TestEqual(TEXT("None clears the binding without creating a graph"),
	                UKawaiiPhysicsEditorLibrary::BindGraphNodeAnimNodeFunction(
		                Handle, EKawaiiPhysicsAnimNodeFunctionEvent::Update, NAME_None, Error), 0);
	bOk &= TestTrue(TEXT("Clearing reports no error"), Error.IsEmpty());
	bOk &= TestTrue(TEXT("Update binding is cleared"), GraphNode->UpdateFunction.GetMemberName().IsNone());
	bOk &= TestTrue(TEXT("Unbinding keeps the function graph"), AnimBlueprint->FunctionGraphs.Contains(UpdateGraph));

	UEdGraph* InvalidGraph = FBlueprintEditorUtils::CreateNewGraph(
		AnimBlueprint, TEXT("KPTestWrongSignature"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph(AnimBlueprint, InvalidGraph, true, static_cast<UFunction*>(nullptr));
	TArray<UK2Node_FunctionEntry*> InvalidEntryNodes;
	InvalidGraph->GetNodesOfClass(InvalidEntryNodes);
	if (InvalidEntryNodes.IsValidIndex(0))
	{
		InvalidEntryNodes[0]->MetaData.bThreadSafe = true;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	}
	bOk &= TestEqual(TEXT("Function with no parameters is rejected"),
	                 UKawaiiPhysicsEditorLibrary::BindGraphNodeAnimNodeFunction(
		                 Handle, EKawaiiPhysicsAnimNodeFunctionEvent::Update, TEXT("KPTestWrongSignature"), Error), -1);
	bOk &= TestFalse(TEXT("Rejected function reports an error"), Error.IsEmpty());
	bOk &= TestTrue(TEXT("Rejected function reports signature mismatch"), Error.Contains(TEXT("signature")));
	bOk &= TestTrue(TEXT("Rejected function leaves Update unbound"), GraphNode->UpdateFunction.GetMemberName().IsNone());
	bOk &= TestEqual(TEXT("Existing AnimGraph name is rejected"),
	                 UKawaiiPhysicsEditorLibrary::BindGraphNodeAnimNodeFunction(
		                 Handle, EKawaiiPhysicsAnimNodeFunctionEvent::Update, UEdGraphSchema_K2::GN_AnimGraph,
		                 Error), -1);
	bOk &= TestFalse(TEXT("Existing graph name reports an error"), Error.IsEmpty());
	bOk &= TestTrue(TEXT("Existing graph name reports it is already in use"), Error.Contains(TEXT("already in use")));

	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKawaiiPhysicsGraphNodeToolsBackgroundCPUThrottleTest,
                                 "KawaiiPhysics.EditorScripting.BackgroundCPUThrottle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKawaiiPhysicsGraphNodeToolsBackgroundCPUThrottleTest::RunTest(const FString& Parameters)
{
	const bool bOriginal = UKawaiiPhysicsEditorLibrary::IsBackgroundCPUThrottleEnabled();
	const bool bPrevious = UKawaiiPhysicsEditorLibrary::SetBackgroundCPUThrottleEnabled(!bOriginal);
	const bool bCurrent = UKawaiiPhysicsEditorLibrary::IsBackgroundCPUThrottleEnabled();
	const bool bRestoredPrevious = UKawaiiPhysicsEditorLibrary::SetBackgroundCPUThrottleEnabled(bOriginal);
	const bool bRestored = UKawaiiPhysicsEditorLibrary::IsBackgroundCPUThrottleEnabled();
	bool bOk = true;
	bOk &= TestEqual(TEXT("Setter returns the previous value"), bPrevious, bOriginal);
	bOk &= TestEqual(TEXT("Getter reports the changed value"), bCurrent, !bOriginal);
	bOk &= TestEqual(TEXT("Restoring returns the changed value"), bRestoredPrevious, !bOriginal);
	bOk &= TestEqual(TEXT("Original value is restored"), bRestored, bOriginal);
	return bOk;
}

#endif
