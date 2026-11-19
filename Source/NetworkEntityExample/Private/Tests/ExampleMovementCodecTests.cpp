#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MovementExample/ExampleMovementCodec.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleMovementCodecTest, "Flecs.Example.QuantizedMovementCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExampleMovementCodecTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FExampleLocation Location;
	Location.Value = FVector3f(12.25f, -2.5f, 100.0f);
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Encode location"), ExampleMovementCodec::EncodeLocation(Location, Bytes))) return false;
	const TArray<uint8> ExpectedLocation = {0, 0, 0, 12, 0xff, 0xff, 0xff, 0xfd, 0, 0, 0, 100};
	TestTrue(TEXT("Location bytes use rounded signed centimeters and network order"), Bytes == ExpectedLocation);
	FExampleLocation DecodedLocation;
	TestTrue(TEXT("Decode location"), ExampleMovementCodec::DecodeLocation(Bytes, DecodedLocation));
	TestTrue(TEXT("Location reconstruction"), DecodedLocation.Value == FVector3f(12.0f, -3.0f, 100.0f));
	TArray<uint8> Reencoded;
	TestTrue(TEXT("Location re-encodes"), ExampleMovementCodec::EncodeLocation(DecodedLocation, Reencoded));
	TestTrue(TEXT("Location bytes round-trip exactly"), Reencoded == Bytes);
	const TArray<uint8> ShortLocation = {0, 1};
	TestFalse(TEXT("Truncated location rejected"), ExampleMovementCodec::DecodeLocation(ShortLocation, DecodedLocation));
	TestTrue(TEXT("Rejected location preserves output"), DecodedLocation.Value == FVector3f(12.0f, -3.0f, 100.0f));
	const TArray<uint8> NonCanonical = {0x7f, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0};
	TestFalse(TEXT("Unrepresentable float location rejected"), ExampleMovementCodec::DecodeLocation(NonCanonical, DecodedLocation));
	Location.Value.X = std::numeric_limits<float>::infinity();
	Reencoded = {42};
	TestFalse(TEXT("Non-finite location rejected"), ExampleMovementCodec::EncodeLocation(Location, Reencoded));
	TestTrue(TEXT("Rejected location encoding preserves output"), Reencoded == TArray<uint8>({42}));

	FExampleRotation Rotation;
	Rotation.Yaw = -90.0f;
	if (!TestTrue(TEXT("Encode yaw"), ExampleMovementCodec::EncodeRotation(Rotation, Bytes))) return false;
	TestTrue(TEXT("Yaw uses a two-byte circular code"), Bytes == TArray<uint8>({0xc0, 0x00}));
	FExampleRotation DecodedRotation;
	TestTrue(TEXT("Decode yaw"), ExampleMovementCodec::DecodeRotation(Bytes, DecodedRotation));
	TestTrue(TEXT("Yaw reconstructs canonical equivalent"), DecodedRotation.Yaw == 270.0f);
	TestTrue(TEXT("Yaw re-encodes"), ExampleMovementCodec::EncodeRotation(DecodedRotation, Reencoded));
	TestTrue(TEXT("Yaw bytes round-trip exactly"), Reencoded == Bytes);
	TestFalse(TEXT("Truncated yaw rejected"), ExampleMovementCodec::DecodeRotation(TArray<uint8>({0xc0}), DecodedRotation));
	TestTrue(TEXT("Rejected yaw preserves output"), DecodedRotation.Yaw == 270.0f);
	Rotation.Yaw = std::numeric_limits<float>::quiet_NaN();
	Reencoded = {42};
	TestFalse(TEXT("Non-finite yaw rejected"), ExampleMovementCodec::EncodeRotation(Rotation, Reencoded));
	TestTrue(TEXT("Rejected yaw encoding preserves output"), Reencoded == TArray<uint8>({42}));
	return true;
}
#endif
