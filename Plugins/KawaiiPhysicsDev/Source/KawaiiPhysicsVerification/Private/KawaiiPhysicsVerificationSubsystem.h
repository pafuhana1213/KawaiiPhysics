// Copyright 2019-2026 pafuhana1213. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
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
	};

	struct FCell
	{
		FString Id;
		FCellMotion Motion;
		TArray<FCellEvent> Events;
		TWeakObjectPtr<USkeletalMeshComponent> Component;
		TArray<double> Samples;
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
	bool ApplyDueEvents(const FCell& Cell) const;
	void WriteRecordingAndExit(bool bSucceeded);

	TArray<FCell> Cells;
	TArray<TSharedPtr<class FJsonValue>> PendingCellValues;
	TWeakObjectPtr<class ACameraActor> CameraActor;
	FString OutputPath;
	FString CaptureDirectory;
	FDelegateHandle PreActorTickHandle;
	int32 NumFrames = 0;
	int32 FrameIndex = 0;
	double SimulationTime = 0.0;
	double LastTickSeconds = 0.0;
	TArray<double> FrameMilliseconds;
	TArray<double> CalibrationStartMilliseconds;
	TArray<double> CalibrationEndMilliseconds;
	int32 ScanFramesRemaining = 0;
	int32 StartDelayFramesRemaining = 0;
	bool bRecordTransforms = true;
	bool bRunning = false;
};
