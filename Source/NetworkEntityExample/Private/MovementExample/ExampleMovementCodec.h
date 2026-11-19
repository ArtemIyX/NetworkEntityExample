#pragma once

#include "MovementExample/ExampleMovement.h"

/** Example wire format: XYZ signed centimeters (12 bytes) and a circular uint16 yaw (2 bytes). */
namespace ExampleMovementCodec
{
	inline constexpr uint32 LocationBytes = 12;
	inline constexpr uint32 RotationBytes = 2;
	bool EncodeLocation(const FExampleLocation& Value, TArray<uint8>& OutBytes);
	bool DecodeLocation(TConstArrayView<uint8> Bytes, FExampleLocation& OutValue);
	bool EncodeRotation(const FExampleRotation& Value, TArray<uint8>& OutBytes);
	bool DecodeRotation(TConstArrayView<uint8> Bytes, FExampleRotation& OutValue);
}
