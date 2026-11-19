#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Networking/FlecsNetworkSessionTypes.h"
#include "FlecsSimulationTypes.h"
#include "ExampleMovementServerSubsystem.generated.h"

class UFlecsNetworkSessionSubsystem;

UCLASS(Config=Game)
class NETWORKENTITYEXAMPLE_API UExampleMovementServerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

	/** Request a single population in this authoritative world. Safe to call more than once. */
	UFUNCTION(BlueprintCallable, Category="Flecs Example")
	bool StartExample();
	/** Configure on the game thread before startup; the callback runs on the worker. */
	void SetSystemRegistrar(FFlecsWorkerCommand Registrar);
	bool HasStarted() const { return bStartRequested; }

	UPROPERTY(Config, EditAnywhere, Category="Flecs Example", meta=(ClampMin="0", ClampMax="1000"))
	int32 EntityCount = 10;

private:
	void HandleEvent(const FFlecsSessionEvent& Event);
	TWeakObjectPtr<UFlecsNetworkSessionSubsystem> Session;
	TSet<uint64> Pending;
	int32 Submitted = 0;
	bool bStartRequested = false;
	bool bSystemsRegistered = false;
	FFlecsWorkerCommand SystemRegistrar;
};
