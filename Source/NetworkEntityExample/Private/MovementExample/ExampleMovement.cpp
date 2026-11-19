#include "MovementExample/ExampleMovement.h"
#include "MovementExample/ExampleMovementCodec.h"
#include "MovementExample/ExampleMovementStats.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	TMap<TWeakObjectPtr<UWorld>, FFlecsWorkerCommand> WorldSystemRegistrars;

	struct FMovementState
	{
		flecs::world* World = nullptr;
		bool bServer = false;
	};

	template <typename T>
	bool Encode(const T& Value, TArray<uint8>& Bytes)
	{
		Bytes.Reset();
		FMemoryWriter Writer(Bytes);
		T Copy = Value;
		Writer.Serialize(&Copy, sizeof(T));
		return !Writer.IsError();
	}

	template <typename T>
	bool Decode(TConstArrayView<uint8> Bytes, T& Value)
	{
		if (Bytes.Num() != sizeof(T)) return false;
		FMemory::Memcpy(&Value, Bytes.GetData(), sizeof(T));
		return true;
	}

	bool Encode(const FExampleLocation& Value, TArray<uint8>& Bytes) { return ExampleMovementCodec::EncodeLocation(Value, Bytes); }
	bool Decode(TConstArrayView<uint8> Bytes, FExampleLocation& Value) { return ExampleMovementCodec::DecodeLocation(Bytes, Value); }
	bool Encode(const FExampleRotation& Value, TArray<uint8>& Bytes) { return ExampleMovementCodec::EncodeRotation(Value, Bytes); }
	bool Decode(TConstArrayView<uint8> Bytes, FExampleRotation& Value) { return ExampleMovementCodec::DecodeRotation(Bytes, Value); }
	template <typename T> uint32 EncodedSize() { return sizeof(T); }
	template <> uint32 EncodedSize<FExampleLocation>() { return ExampleMovementCodec::LocationBytes; }
	template <> uint32 EncodedSize<FExampleRotation>() { return ExampleMovementCodec::RotationBytes; }
	template <typename T> FlecsModRegistry::FVersion CodecVersion() { return {1, 0, 0}; }
	template <> FlecsModRegistry::FVersion CodecVersion<FExampleLocation>() { return {2, 0, 0}; }
	template <> FlecsModRegistry::FVersion CodecVersion<FExampleRotation>() { return {2, 0, 0}; }


	template <typename T>
	bool RegisterComponent(FlecsModRegistry::FRegistry& Registry, const TCHAR* Id,
	                       const TSharedRef<FMovementState, ESPMode::ThreadSafe>& State, FString& Error)
	{
		FlecsModRegistry::FComponentSchema Schema;
		Schema.StableId = Id;
		Schema.Version = CodecVersion<T>();
		Schema.MaxEncodedBytes = EncodedSize<T>();
		Schema.AllowedEntityDefinitions.Add(ExampleMovement::Definition);
		Schema.Extract = [State](uint64 Token, TArray<uint8>& Bytes)
		{
			if (!State->World) return false;
			flecs::entity Entity(State->World->c_ptr(), Token);
			const T* Value = Entity.try_get<T>();
			return Value && Encode(*Value, Bytes);
		};
		Schema.Apply = [State](uint64 Token, TConstArrayView<uint8> Bytes)
		{
			T Value;
			if (!State->World || !Decode(Bytes, Value)) return false;
			flecs::entity Entity(State->World->c_ptr(), Token);
			if (!Entity.is_alive()) return false;
			Entity.set<T>(Value);
			return true;
		};
		Schema.Remove = [State](uint64 Token)
		{
			if (State->World)
			{
				flecs::entity Entity(State->World->c_ptr(), Token);
				if (Entity.is_alive()) Entity.remove<T>();
			}
		};
		return Registry.RegisterComponent(MoveTemp(Schema), Error);
	}
}

void ExampleMovement::UpdateMovement(float Dt, FExampleLocation& LocationValue,
	FExampleRotation& RotationValue, FExampleHealth& HealthValue,
	FExampleStamina& Stamina, FExampleSpeed& Speed, FRandomStream& Random)
{
	SCOPE_CYCLE_COUNTER(STAT_FlecsExampleMovement);
	INC_DWORD_STAT(STAT_FlecsExampleEntities);
	if (Stamina.Value <= 10.0f) Stamina.bResting = true;
	if (Stamina.Value >= 80.0f) Stamina.bResting = false;
	Stamina.Value = FMath::Clamp(Stamina.Value + (Stamina.bResting ? 20.0f : -8.0f) * Dt, 0.0f, 100.0f);
	Speed.Value = Stamina.bResting ? 0.0f : 80.0f + Stamina.Value * 1.2f;
	FVector2f Position(LocationValue.Value.X, LocationValue.Value.Y);
	FVector2f ToTarget = Speed.Target - Position;
	if (ToTarget.SizeSquared() < 100.0f)
	{
		Speed.Target = FVector2f(Random.FRandRange(-1000.0f, 1000.0f), Random.FRandRange(-1000.0f, 1000.0f));
		ToTarget = Speed.Target - Position;
	}
	Position += ToTarget.GetSafeNormal() * FMath::Min(Speed.Value * Dt, ToTarget.Size());
	LocationValue.Value.X = Position.X;
	LocationValue.Value.Y = Position.Y;
	RotationValue.Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Position.Y, -Position.X));
	HealthValue.Value = FMath::Max(0.0f, HealthValue.Value - 0.5f * Dt);
	if (HealthValue.Value <= 0.0f) HealthValue.Value = 100.0f;
}

void ExampleMovement::RegisterDefaultServerSystems(flecs::world& Worker)
{
	Worker.system<FExampleLocation, FExampleRotation, FExampleHealth, FExampleStamina, FExampleSpeed>("Example.Movement.Server")
		.kind(flecs::OnUpdate).each([Random = MakeShared<FRandomStream>(1947)](flecs::iter& It, size_t,
			FExampleLocation& LocationValue, FExampleRotation& RotationValue, FExampleHealth& HealthValue,
			FExampleStamina& Stamina, FExampleSpeed& Speed)
		{
			UpdateMovement(static_cast<float>(It.delta_time()), LocationValue, RotationValue, HealthValue, Stamina, Speed, *Random);
		});
}

bool ExampleMovement::Configure(UWorld& World, FFlecsNetworkSessionRegistrar& Registrar, FString& Error,
                                FFlecsWorkerCommand RegisterServerSystemsOverride)
{
	UGameInstance* Game = World.GetGameInstance();
	UFlecsGameInstanceSubsystem* Flecs = Game ? Game->GetSubsystem<UFlecsGameInstanceSubsystem>() : nullptr;
	if (!Flecs)
	{
		Error = TEXT("Flecs game instance subsystem unavailable");
		return false;
	}
	const auto State = MakeShared<FMovementState, ESPMode::ThreadSafe>();
	State->bServer = World.GetNetMode() != NM_Client;
	if (!RegisterComponent<FExampleLocation>(Registrar.Registry, Location, State, Error) ||
		!RegisterComponent<FExampleRotation>(Registrar.Registry, Rotation, State, Error) ||
		!RegisterComponent<FExampleHealth>(Registrar.Registry, Health, State, Error))
		return false;
	FlecsModRegistry::FEntityDefinition Def;
	Def.StableId = Definition;
	Def.Version = {1, 0, 0};
	Def.ComponentIds = {Location, Rotation, Health};
	Def.MaxInitialStateBytes = ExampleMovementCodec::LocationBytes + ExampleMovementCodec::RotationBytes + sizeof(FExampleHealth);
	Def.Create = [State](uint64 Token)
	{
		if (!State->World) return false;
		flecs::entity Entity(State->World->c_ptr(), Token);
		if (!Entity.is_alive()) return false;
		Entity.add<FExampleMovementTag>();
		if (State->bServer)
		{
			Entity.set<FExampleStamina>({100.0f, false});
			Entity.set<FExampleSpeed>({});
		}
		return true;
	};
	if (!Registrar.Registry.RegisterEntityDefinition(MoveTemp(Def), Error)) return false;
	if (!Registrar.RegisterSpatial(Definition, EFlecsTrustedSpatialPolicy::PositionRequired,
	                               [State](uint64 Token, FVector3d& Position)
	                               {
		                               if (!State->World) return false;
		                               flecs::entity Entity(State->World->c_ptr(), Token);
		                               const FExampleLocation* LocationValue = Entity.try_get<FExampleLocation>();
		                               if (!LocationValue) return false;
		                               Position = FVector3d(LocationValue->Value);
		                               return true;
	                               }, Error))
		return false;
	const uint64 Epoch = Flecs->GetActiveEpoch();
	if (!RegisterServerSystemsOverride)
	{
		RegisterServerSystemsOverride = ExampleMovement::RegisterDefaultServerSystems;
	}
	if (State->bServer)
	{
		for (auto It = WorldSystemRegistrars.CreateIterator(); It; ++It)
			if (!It.Key().IsValid()) It.RemoveCurrent();
		WorldSystemRegistrars.Add(&World, MoveTemp(RegisterServerSystemsOverride));
	}
	const auto Result = Flecs->RegisterPersistentWorkerLifecycle(Epoch, TEXT("Example.Movement"),
		[State](flecs::world& Worker, uint64)
		{
			State->World = &Worker;
		},
		[State](flecs::world&, uint64)
		{
			State->World = nullptr;
		});
	if (Result != EFlecsWorkerCommandResult::Accepted)
	{
		Error = TEXT("Could not register example worker lifecycle");
		return false;
	}
	return true;
}

FFlecsWorkerCommand ExampleMovement::TakeServerSystemRegistrar(UWorld& World)
{
	check(IsInGameThread());
	FFlecsWorkerCommand Registrar;
	WorldSystemRegistrars.RemoveAndCopyValue(&World, Registrar);
	return Registrar;
}

void ExampleMovement::ForgetServerSystemRegistrar(UWorld& World)
{
	check(IsInGameThread());
	WorldSystemRegistrars.Remove(&World);
}

FFlecsPeerSpawnRequest ExampleMovement::MakeSpawn(int32 Index)
{
	FFlecsPeerSpawnRequest Request;
	Request.DefinitionStableId = Definition;
	FExampleLocation LocationValue;
	LocationValue.Value = FVector3f((Index % 5) * 200.0f, (Index / 5) * 200.0f, 50.0f);
	FExampleRotation RotationValue;
	FExampleHealth HealthValue;
	auto Add = [&Request](const TCHAR* Id, const auto& Value)
	{
		FlecsSimulation::FInitialComponent Component;
		Component.StableComponentId = Id;
		Encode(Value, Component.Payload);
		Request.InitialComponents.Add(MoveTemp(Component));
	};
	Add(Location, LocationValue);
	Add(Rotation, RotationValue);
	Add(Health, HealthValue);
	return Request;
}
