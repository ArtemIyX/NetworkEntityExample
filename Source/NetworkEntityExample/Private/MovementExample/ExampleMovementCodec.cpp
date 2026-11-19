#include "MovementExample/ExampleMovementCodec.h"
#include "Quantization/FlecsQuantization.h"

bool ExampleMovementCodec::EncodeLocation(const FExampleLocation& Value, TArray<uint8>& OutBytes)
{
	int32 X, Y, Z;
	if (!FlecsQuantization::QuantizeSigned(Value.Value.X, 1.0, X) ||
		!FlecsQuantization::QuantizeSigned(Value.Value.Y, 1.0, Y) ||
		!FlecsQuantization::QuantizeSigned(Value.Value.Z, 1.0, Z)) return false;
	TArray<uint8> Bytes;
	Bytes.Reserve(LocationBytes);
	FlecsQuantization::WriteInt32(Bytes, X); FlecsQuantization::WriteInt32(Bytes, Y); FlecsQuantization::WriteInt32(Bytes, Z);
	OutBytes = MoveTemp(Bytes);
	return true;
}

bool ExampleMovementCodec::DecodeLocation(TConstArrayView<uint8> Bytes, FExampleLocation& OutValue)
{
	if (Bytes.Num() != static_cast<int32>(LocationBytes)) return false;
	int32 Offset = 0, X, Y, Z;
	if (!FlecsQuantization::ReadInt32(Bytes, Offset, X) || !FlecsQuantization::ReadInt32(Bytes, Offset, Y) ||
		!FlecsQuantization::ReadInt32(Bytes, Offset, Z)) return false;
	FExampleLocation Decoded;
	Decoded.Value = FVector3f(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	// FVector3f cannot represent every int32 centimeter; reject non-canonical wire values.
	int32 CheckX, CheckY, CheckZ;
	if (!FlecsQuantization::QuantizeSigned(Decoded.Value.X, 1.0, CheckX) || CheckX != X ||
		!FlecsQuantization::QuantizeSigned(Decoded.Value.Y, 1.0, CheckY) || CheckY != Y ||
		!FlecsQuantization::QuantizeSigned(Decoded.Value.Z, 1.0, CheckZ) || CheckZ != Z) return false;
	OutValue = Decoded;
	return true;
}

bool ExampleMovementCodec::EncodeRotation(const FExampleRotation& Value, TArray<uint8>& OutBytes)
{
	uint16 Yaw;
	if (!FlecsQuantization::QuantizeAngleDegrees(Value.Yaw, Yaw)) return false;
	TArray<uint8> Bytes;
	Bytes.Reserve(RotationBytes);
	FlecsQuantization::WriteUInt16(Bytes, Yaw);
	OutBytes = MoveTemp(Bytes);
	return true;
}

bool ExampleMovementCodec::DecodeRotation(TConstArrayView<uint8> Bytes, FExampleRotation& OutValue)
{
	if (Bytes.Num() != static_cast<int32>(RotationBytes)) return false;
	int32 Offset = 0;
	uint16 Yaw;
	if (!FlecsQuantization::ReadUInt16(Bytes, Offset, Yaw)) return false;
	FExampleRotation Decoded;
	Decoded.Yaw = static_cast<float>(FlecsQuantization::DequantizeAngleDegrees(Yaw));
	OutValue = Decoded;
	return true;
}
