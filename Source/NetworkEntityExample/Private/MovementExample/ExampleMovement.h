#pragma once

#include "CoreMinimal.h"
#include "Networking/FlecsNetworkSessionTypes.h"
#include "FlecsSimulationTypes.h"

struct FExampleLocation
{
	FVector3f Value = FVector3f::ZeroVector;
};

struct FExampleRotation
{
	float Yaw = 0.0f;
};

struct FExampleHealth
{
	float Value = 100.0f;
};

struct FExampleStamina
{
	float Value = 100.0f;
	bool bResting = false;
};

struct FExampleSpeed
{
	float Value = 0.0f;
	FVector2f Target = FVector2f::ZeroVector;
};

struct FExampleMovementTag
{
};

struct FExampleMovementSample
{
	uint64 Entity = 0;
	FVector3f Position = FVector3f::ZeroVector;
	float Yaw = 0.0f;
	float Health = 0.0f;
};

namespace ExampleMovement
{
	inline constexpr uint32 PublicationType = 0x4d56454d;
	inline constexpr const TCHAR* Definition = TEXT("example:movement.entity");
	inline constexpr const TCHAR* Location = TEXT("example:movement.location");
	inline constexpr const TCHAR* Rotation = TEXT("example:movement.rotation");
	inline constexpr const TCHAR* Health = TEXT("example:movement.health");
	void UpdateMovement(float DeltaSeconds, FExampleLocation& LocationValue, FExampleRotation& RotationValue,
	                    FExampleHealth& HealthValue, FExampleStamina& Stamina, FExampleSpeed& Speed, FRandomStream& Random);
	/** Register the example's default worker-only server systems. */
	void RegisterDefaultServerSystems(flecs::world& Worker);
	/** Optional replacement executes on the simulation worker and must not access UObjects. */
	bool Configure(UWorld& World, FFlecsNetworkSessionRegistrar& Registrar, FString& Error,
	               FFlecsWorkerCommand RegisterServerSystemsOverride = {});
	FFlecsWorkerCommand TakeServerSystemRegistrar(UWorld& World);
	void ForgetServerSystemRegistrar(UWorld& World);
	FFlecsPeerSpawnRequest MakeSpawn(int32 Index);
}
