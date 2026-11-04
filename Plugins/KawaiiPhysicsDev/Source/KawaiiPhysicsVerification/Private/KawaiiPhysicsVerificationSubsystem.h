// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Async/Future.h"
#include "Subsystems/WorldSubsystem.h"
#include "KawaiiPhysicsVerificationSubsystem.generated.h"

class USkeletalMeshComponent;

/**
 * Spawns the cells of a JSON manifest, moves them on a frame-driven path and records the final
 * component-space bone locations, then quits. Enabled only by -KPVerify=<manifest>; the recording
 * goes to -KPVerifyOut=<jsonl>.
 */
UCLASS()
class UKawaiiPhysicsVerificationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FCellMotion
	{
		FVector BaseLocation = FVector::ZeroVector;
		double BaseYawDegrees = 0.0;
		FVector LinearAmplitude = FVector::ZeroVector;
		double LinearPeriod = 1.0;
		double YawAmplitudeDegrees = 0.0;
		double YawPeriod = 1.0;
	};

	/** A change applied just before the given recorded frame is ticked. */
	struct FCellEvent
	{
		int32 Frame = 0;
		TArray<TPair<FName, FString>> Overrides;
		bool bResetDynamics = false;
		FVector LocationOffset = FVector::ZeroVector;
		double YawOffsetDegrees = 0.0;
	};

	struct FCell
	{
		FString Id;
		FCellMotion Motion;
		TArray<FCellEvent> Events;
		TWeakObjectPtr<USkeletalMeshComponent> Component;
		TArray<double> Samples;
		struct FNodeRecord
		{
			FString Root;
			FString BonesJson;
			int32 FirstFrame = 0;
			int32 BoneCount = INDEX_NONE;
			TArray<double> BoneFrames;
			TArray<TArray<double>> LimitFrames;
		};
		TArray<FNodeRecord> NodeRecords;
		bool bDriven = true;
	};

	bool LoadManifest(const FString& ManifestPath);
	bool SpawnCells(const TArray<TSharedPtr<class FJsonValue>>& CellValues);
	void ReadCamera(const TSharedPtr<class FJsonObject>& CameraObject);
	void HoldCameraView() const;
	void RequestFrameCapture() const;
	bool SpawnCell(UWorld& InWorld, const TSharedPtr<class FJsonObject>& CellObject);
	void PrepareWorldComponents() const;
	void DiscoverWorldCells();
	void HandlePreActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
	void ApplyMotion(const FCell& Cell, double Time) const;
	bool ReadCellEvents(const TSharedPtr<class FJsonObject>& CellObject, FCell& Cell) const;
	bool ApplyDueEvents(FCell& Cell) const;
	bool RecordCellNodes(FCell& Cell) const;
	void WriteRecordingAndExit(bool bSucceeded);
	bool AdvanceCsvCapture();

	enum class ECsvCaptureState : uint8 { Disabled, Waiting, Starting, Capturing, Writing, Complete };
	ECsvCaptureState CsvState = ECsvCaptureState::Disabled;
	FString CsvDirectory;
	FString CsvFilename;
	FString CsvWrittenPath;
	TSharedFuture<FString> CsvWriteFuture;
	int32 CsvStartFrame = 0;
	int32 CsvFrames = 0;
	int32 CsvStartRunnerFrame = INDEX_NONE;
	int32 CsvStartCsvFrame = INDEX_NONE;
	int32 CsvWaitTicks = 0;
	double CsvWriteStartSeconds = 0.0;

	TArray<FCell> Cells;
	TArray<TSharedPtr<class FJsonValue>> PendingCellValues;
	TWeakObjectPtr<class ACameraActor> CameraActor;
	FString OutputPath;
	FString EffectiveCVarsJson = TEXT("{}");
	FString CaptureDirectory;
	FDelegateHandle PreActorTickHandle;
	int32 NumFrames = 0;
	int32 FrameIndex = 0;
	double SimulationTime = 0.0;
	double LastTickSeconds = 0.0;
	TArray<double> FrameMilliseconds;
	TArray<double> FrameDeltaSeconds;
	TArray<double> CalibrationStartMilliseconds;
	TArray<double> CalibrationEndMilliseconds;
	int32 ScanFramesRemaining = 0;
	int32 StartDelayFramesRemaining = 0;
	bool bRecordTransforms = true;
	bool bRecordNodes = false;
	bool bDisableKawaiiPhysics = false;
	bool bRunning = false;
};
