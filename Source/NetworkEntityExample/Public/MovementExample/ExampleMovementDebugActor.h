#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExampleMovementDebugActor.generated.h"

class UFlecsGameInstanceSubsystem;
class UCanvas;
class APlayerController;
struct FExampleMovementDebugBuffer;

UCLASS()
class NETWORKENTITYEXAMPLE_API AExampleMovementDebugActor : public AActor
{
	GENERATED_BODY()

public:
	AExampleMovementDebugActor();

	virtual void Tick(float DeltaSeconds) override;
	
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;


public:
	UPROPERTY(EditAnywhere, Category="Flecs Example", meta=(ClampMin="0.0", ClampMax="2.0"))
	float InterpolationDelaySeconds = 0.15f;
	UPROPERTY(EditAnywhere, Category="Flecs Example")
	FVector BoxExtent = FVector(35.0, 35.0, 50.0);
	UPROPERTY(EditAnywhere, Category="Flecs Example")
	bool bShowStatsOverlay = true;

private:
	void DrawStatsOverlay(UCanvas* Canvas, APlayerController* Controller);
	FDelegateHandle StatsDrawHandle;
	struct FSample
	{
		double Time = 0;
		FVector Position = FVector::ZeroVector;
		float Yaw = 0;
		float Health = 0;
	};

	struct FHistory
	{
		TArray<FSample> Samples;
	};

	TMap<uint64, FHistory> Histories;
	TWeakObjectPtr<UFlecsGameInstanceSubsystem> Flecs;
	FName PublisherName;
	uint64 Epoch = 0;
	uint64 LastTick = 0;
	bool bRegistered = false;
	bool bSystemRegistered = false;
	TSharedPtr<FExampleMovementDebugBuffer, ESPMode::ThreadSafe> Buffer;
};
