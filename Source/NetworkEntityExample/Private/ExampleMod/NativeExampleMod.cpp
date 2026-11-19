#include "ExampleMod/NativeExampleMod.h"

namespace
{
	constexpr uint8 VitalsPayloadVersion = 1;
	constexpr int32 VitalsPayloadSize = 15;
	constexpr uint32 CoordinateBias = 0x80000000u;
	constexpr uint16 MaxRechargeAmount = 25;
	constexpr uint16 MaxEnergy = 100;

	void AppendU16BE(TArray<uint8>& OutBytes, uint16 Value)
	{
		OutBytes.Add(static_cast<uint8>((Value >> 8) & 0xff));
		OutBytes.Add(static_cast<uint8>(Value & 0xff));
	}

	void AppendU32BE(TArray<uint8>& OutBytes, uint32 Value)
	{
		OutBytes.Add(static_cast<uint8>((Value >> 24) & 0xff));
		OutBytes.Add(static_cast<uint8>((Value >> 16) & 0xff));
		OutBytes.Add(static_cast<uint8>((Value >> 8) & 0xff));
		OutBytes.Add(static_cast<uint8>(Value & 0xff));
	}

	uint16 ReadU16BE(TConstArrayView<uint8> Bytes, int32 Offset)
	{
		return static_cast<uint16>((static_cast<uint16>(Bytes[Offset]) << 8) |
			static_cast<uint16>(Bytes[Offset + 1]));
	}

	uint32 ReadU32BE(TConstArrayView<uint8> Bytes, int32 Offset)
	{
		return (static_cast<uint32>(Bytes[Offset]) << 24) |
			(static_cast<uint32>(Bytes[Offset + 1]) << 16) |
			(static_cast<uint32>(Bytes[Offset + 2]) << 8) |
			static_cast<uint32>(Bytes[Offset + 3]);
	}

	uint32 EncodeCoordinate(int32 Coordinate)
	{
		return static_cast<uint32>(static_cast<int64>(Coordinate) + static_cast<int64>(CoordinateBias));
	}

	int32 DecodeCoordinate(uint32 Encoded)
	{
		return static_cast<int32>(static_cast<int64>(Encoded) - static_cast<int64>(CoordinateBias));
	}

	bool DecodeVitals(TConstArrayView<uint8> Bytes, FNativeExampleVitals& OutValues)
	{
		if (Bytes.Num() != VitalsPayloadSize || Bytes[0] != VitalsPayloadVersion)
		{
			return false;
		}

		FNativeExampleVitals Candidate;
		Candidate.XCentimeters = DecodeCoordinate(ReadU32BE(Bytes, 1));
		Candidate.YCentimeters = DecodeCoordinate(ReadU32BE(Bytes, 5));
		Candidate.ZCentimeters = DecodeCoordinate(ReadU32BE(Bytes, 9));
		Candidate.Energy = ReadU16BE(Bytes, 13);
		if (Candidate.Energy > MaxEnergy)
		{
			return false;
		}

		OutValues = Candidate;
		return true;
	}
}

TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> FNativeExampleModWorkerState::Create()
{
	return MakeShared<FNativeExampleModWorkerState, ESPMode::ThreadSafe>();
}

const TCHAR* FNativeExampleMod::VitalsComponentId()
{
	return TEXT("example:vitals");
}

const TCHAR* FNativeExampleMod::BeaconDefinitionId()
{
	return TEXT("example:beacon");
}

const TCHAR* FNativeExampleMod::RechargeCommandId()
{
	return TEXT("example:recharge");
}

const TCHAR* FNativeExampleMod::RechargePermissionId()
{
	return TEXT("example:beacon.recharge");
}

bool FNativeExampleMod::EncodeVitals(const FNativeExampleVitals& Values, TArray<uint8>& OutBytes)
{
	if (Values.Energy > MaxEnergy)
	{
		return false;
	}

	TArray<uint8> Candidate;
	Candidate.Reserve(VitalsPayloadSize);
	Candidate.Add(VitalsPayloadVersion);
	AppendU32BE(Candidate, EncodeCoordinate(Values.XCentimeters));
	AppendU32BE(Candidate, EncodeCoordinate(Values.YCentimeters));
	AppendU32BE(Candidate, EncodeCoordinate(Values.ZCentimeters));
	AppendU16BE(Candidate, Values.Energy);
	OutBytes = MoveTemp(Candidate);
	return true;
}

bool FNativeExampleMod::ReadPosition(
	TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
	uint64 EntityToken, FVector3d& OutPosition)
{
	if (EntityToken == 0)
	{
		return false;
	}
	const FNativeExampleVitals* Values = WorkerState->VitalsByEntityToken.Find(EntityToken);
	if (!Values)
	{
		return false;
	}
	OutPosition = FVector3d(
		static_cast<double>(Values->XCentimeters),
		static_cast<double>(Values->YCentimeters),
		static_cast<double>(Values->ZCentimeters));
	return true;
}

bool FNativeExampleMod::MakePeerSpawnRequest(const FVector& Position, bool bLiveProbe,
                                             FFlecsPeerSpawnRequest& OutRequest, FString& OutError)
{
	OutRequest = {};
	if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z) ||
		Position.X < MIN_int32 || Position.X > MAX_int32 || Position.Y < MIN_int32 || Position.Y > MAX_int32 ||
		Position.Z < MIN_int32 || Position.Z > MAX_int32)
	{
		OutError = TEXT("The player's focal position cannot be represented by the example component.");
		return false;
	}
	FNativeExampleVitals Vitals;
	Vitals.XCentimeters = FMath::RoundToInt(Position.X);
	Vitals.YCentimeters = FMath::RoundToInt(Position.Y);
	Vitals.ZCentimeters = FMath::RoundToInt(Position.Z);
	if (bLiveProbe) Vitals.Energy = 1;
	FlecsSimulation::FInitialComponent Component;
	Component.StableComponentId = VitalsComponentId();
	if (!EncodeVitals(Vitals, Component.Payload))
	{
		OutError = TEXT("Could not encode the example entity's initial component.");
		return false;
	}
	OutRequest.DefinitionStableId = BeaconDefinitionId();
	OutRequest.InitialComponents.Add(MoveTemp(Component));
	OutError.Reset();
	return true;
}

bool FNativeExampleMod::EncodeRechargeCommand(uint16 Amount, TArray<uint8>& OutBytes)
{
	if (Amount == 0 || Amount > MaxRechargeAmount)
	{
		return false;
	}

	TArray<uint8> Candidate;
	Candidate.Reserve(3);
	Candidate.Add(1);
	AppendU16BE(Candidate, Amount);
	OutBytes = MoveTemp(Candidate);
	return true;
}

bool FNativeExampleMod::RegisterSession(FFlecsNetworkSessionRegistrar& Registrar,
                                        TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
                                        FFlecsPeerSpawnFactory SpawnFactory, FString& OutError)
{
	if (!Register(Registrar.Registry, WorkerState, OutError)) return false;
	if (!Registrar.RegisterPermission(RechargePermissionId(),
	                                  [](uint64 ConnectionToken, uint64 OwnerConnectionToken, uint32, uint16)
	                                  {
		                                  return ConnectionToken != 0 && ConnectionToken == OwnerConnectionToken;
	                                  }, OutError))
		return false;
	if (!Registrar.RegisterSpatial(BeaconDefinitionId(), EFlecsTrustedSpatialPolicy::PositionRequired,
	                               [WorkerState](uint64 EntityToken, FVector3d& OutPosition)
	                               {
		                               return FNativeExampleMod::ReadPosition(WorkerState, EntityToken, OutPosition);
	                               }, OutError))
		return false;
	return Registrar.SetPeerSpawnFactory(MoveTemp(SpawnFactory), OutError);
}

bool FNativeExampleMod::Register(FlecsModRegistry::FRegistry& Registry,
                                 TSharedRef<FNativeExampleModWorkerState, ESPMode::ThreadSafe> WorkerState,
                                 FString& OutError)
{
	using namespace FlecsModRegistry;

	FComponentSchema Vitals;
	Vitals.StableId = VitalsComponentId();
	Vitals.Version = {1, 0, 0};
	Vitals.MaxEncodedBytes = VitalsPayloadSize;
	Vitals.Visibility = EVisibilityPolicy::Relevant;
	Vitals.UpdateRateHz = 15.0f;
	Vitals.Priority = 10;
	Vitals.AllowedEntityDefinitions.Add(BeaconDefinitionId());
	Vitals.Extract = [WorkerState](uint64 EntityToken, TArray<uint8>& OutBytes)
	{
		const FNativeExampleVitals* Values = WorkerState->VitalsByEntityToken.Find(EntityToken);
		return Values && FNativeExampleMod::EncodeVitals(*Values, OutBytes);
	};
	Vitals.Apply = [WorkerState](uint64 EntityToken, TConstArrayView<uint8> Bytes)
	{
		FNativeExampleVitals Candidate;
		if (EntityToken == 0 || !DecodeVitals(Bytes, Candidate))
		{
			return false;
		}
		WorkerState->VitalsByEntityToken.Add(EntityToken, Candidate);
		return true;
	};
	Vitals.Remove = [WorkerState](uint64 EntityToken)
	{
		WorkerState->VitalsByEntityToken.Remove(EntityToken);
	};
	if (!Registry.RegisterComponent(MoveTemp(Vitals), OutError))
	{
		return false;
	}

	FEntityDefinition Beacon;
	Beacon.StableId = BeaconDefinitionId();
	Beacon.Version = {1, 0, 0};
	Beacon.ComponentIds.Add(VitalsComponentId());
	Beacon.MaxInitialStateBytes = VitalsPayloadSize;
	Beacon.Create = [WorkerState](uint64 EntityToken)
	{
		if (EntityToken == 0 || WorkerState->VitalsByEntityToken.Contains(EntityToken))
		{
			return false;
		}
		WorkerState->VitalsByEntityToken.Add(EntityToken, FNativeExampleVitals{});
		return true;
	};
	if (!Registry.RegisterEntityDefinition(MoveTemp(Beacon), OutError))
	{
		return false;
	}

	FCommandSchema Recharge;
	Recharge.StableId = RechargeCommandId();
	Recharge.Version = {1, 0, 0};
	Recharge.MaxPayloadBytes = 3;
	Recharge.RequiredPermission = RechargePermissionId();
	Recharge.Validate = [WorkerState](uint64, uint64 TargetToken, TConstArrayView<uint8> Payload)
	{
		if (Payload.Num() != 3 || Payload[0] != 1 || TargetToken == 0 ||
			!WorkerState->VitalsByEntityToken.Contains(TargetToken))
		{
			return false;
		}
		const uint16 Amount = ReadU16BE(Payload, 1);
		return Amount != 0 && Amount <= MaxRechargeAmount;
	};
	Recharge.Execute = [WorkerState](uint64 ConnectionToken, uint64 TargetToken, TConstArrayView<uint8> Payload)
	{
		(void)ConnectionToken;
		if (Payload.Num() != 3 || Payload[0] != 1 || TargetToken == 0)
		{
			return false;
		}
		const uint16 Amount = ReadU16BE(Payload, 1);
		FNativeExampleVitals* Values = WorkerState->VitalsByEntityToken.Find(TargetToken);
		if (!Values || Amount == 0 || Amount > MaxRechargeAmount)
		{
			return false;
		}
		Values->Energy = static_cast<uint16>(FMath::Min<uint32>(MaxEnergy,
		                                                        static_cast<uint32>(Values->Energy) + Amount));
		return true;
	};
	return Registry.RegisterCommand(MoveTemp(Recharge), OutError);
}
