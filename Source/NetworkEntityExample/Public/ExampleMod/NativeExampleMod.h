#pragma once

#include "CoreMinimal.h"
#include "ModRegistry/ModSchemaRegistry.h"
#include "Networking/FlecsNetworkSessionTypes.h"

/** Example replicated state encoded field-by-field in a versioned wire format. */
struct FNativeExampleVitals
{
	int32 XCentimeters = 0;
	int32 YCentimeters = 0;
	int32 ZCentimeters = 0;
	uint16 Energy = 100;
};

/**
 * Worker-confined backing store used by the native example callbacks. Retain one
 * instance for the session and access it only from the simulation worker after
 * registration. Thread-safe reference counting does not make this state thread-safe.
 */
class NETWORKENTITYEXAMPLE_API FNativeExampleModWorkerState
{
public:
	static TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> Create();

private:
	friend class FNativeExampleMod;
	TMap<uint64, FNativeExampleVitals> VitalsByEntityToken;
};

/** Minimal native mod example: one component, one entity definition, and one command. */
class NETWORKENTITYEXAMPLE_API FNativeExampleMod
{
public:
	static const TCHAR* VitalsComponentId();
	static const TCHAR* BeaconDefinitionId();
	static const TCHAR* RechargeCommandId();
	static const TCHAR* RechargePermissionId();

	/** Register before the session starts; callbacks are invoked only by the worker model. */
	static bool Register(FlecsModRegistry::FRegistry& Registry,
	                     TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
	                     FString& OutError);
	/** Register example schemas and trusted session hooks before the registry freezes. */
	static bool RegisterSession(FFlecsNetworkSessionRegistrar& Registrar,
	                            TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
	                            FFlecsPeerSpawnFactory SpawnFactory, FString& OutError);

	/** Produce the component's explicit versioned payload for validated entity creation. */
	static bool EncodeVitals(const FNativeExampleVitals& Values, TArray<uint8>& OutBytes);
	static bool MakePeerSpawnRequest(const FVector& Position, bool bLiveProbe,
	                                 FFlecsPeerSpawnRequest& OutRequest, FString& OutError);
	/** Worker-only spatial accessor for the entity model; coordinates are Unreal centimeters. */
	static bool ReadPosition(TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
	                         uint64 EntityToken, FVector3d& OutPosition);
	/** Produce a versioned command payload; accepted amounts are 1 through 25. */
	static bool EncodeRechargeCommand(uint16 Amount, TArray<uint8>& OutBytes);
};
