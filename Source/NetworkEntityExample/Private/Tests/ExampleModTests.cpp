#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "ExampleMod/NativeExampleMod.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeExampleModContractTest, "Flecs.Mod.NativeExampleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNativeExampleModContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace FlecsModRegistry;
	FString Error;
	FRegistry Registry;
	auto State = FNativeExampleModWorkerState::Create();
	if (!TestTrue(TEXT("Register native example"), FNativeExampleMod::Register(Registry, State, Error)) ||
		!TestTrue(TEXT("Freeze native example"), Registry.Freeze(1, Error))) return false;
	const FComponentSchema* Component = Registry.FindComponent(FNativeExampleMod::VitalsComponentId());
	const FEntityDefinition* Definition = Registry.FindEntityDefinition(FNativeExampleMod::BeaconDefinitionId());
	const FCommandSchema* Command = Registry.FindCommand(FNativeExampleMod::RechargeCommandId());
	if (!TestNotNull(TEXT("Vitals schema exists"), Component) || !TestNotNull(TEXT("Beacon definition exists"), Definition) ||
		!TestNotNull(TEXT("Recharge command exists"), Command)) return false;
	TestEqual(TEXT("Component wire limit"), Component->MaxEncodedBytes, uint32(15));
	TestEqual(TEXT("Command wire limit"), Command->MaxPayloadBytes, uint32(3));
	TestEqual(TEXT("Command declares permission"), Command->RequiredPermission, FString(FNativeExampleMod::RechargePermissionId()));
	TestTrue(TEXT("Definition includes component"), Definition->ComponentIds.Contains(FNativeExampleMod::VitalsComponentId()));
	const FCompatibilityResult Compatible = Registry.CompareRequired(Registry);
	TestTrue(TEXT("Frozen example manifest is compatible"), Compatible.bCompatible);
	uint16 WireId = 0;
	TestTrue(TEXT("Example component receives session ID"), Compatible.SessionMapping.FindWireId(EEntryKind::Component, FNativeExampleMod::VitalsComponentId(), WireId));
	TestTrue(TEXT("Example definition receives session ID"), Compatible.SessionMapping.FindWireId(EEntryKind::EntityDefinition, FNativeExampleMod::BeaconDefinitionId(), WireId));
	TestTrue(TEXT("Example command receives session ID"), Compatible.SessionMapping.FindWireId(EEntryKind::Command, FNativeExampleMod::RechargeCommandId(), WireId));

	FNativeExampleVitals Values;
	Values.XCentimeters = -1;
	Values.YCentimeters = 0;
	Values.ZCentimeters = 1;
	Values.Energy = 80;
	TArray<uint8> Encoded;
	if (!TestTrue(TEXT("Encode bounded vitals"), FNativeExampleMod::EncodeVitals(Values, Encoded))) return false;
	const TArray<uint8> Expected = { 1, 0x7f, 0xff, 0xff, 0xff, 0x80, 0, 0, 0, 0x80, 0, 0, 1, 0, 80 };
	TestTrue(TEXT("Vitals use versioned big-endian field encoding"), Encoded == Expected);
	Values.Energy = 101;
	TArray<uint8> Unchanged = { 9 };
	TestFalse(TEXT("Out-of-range energy cannot be encoded"), FNativeExampleMod::EncodeVitals(Values, Unchanged));
	TestTrue(TEXT("Failed encoding leaves output unchanged"), Unchanged == TArray<uint8>({ 9 }));
	TArray<uint8> Recharge;
	TestFalse(TEXT("Zero recharge rejected"), FNativeExampleMod::EncodeRechargeCommand(0, Recharge));
	TestFalse(TEXT("Recharge above limit rejected"), FNativeExampleMod::EncodeRechargeCommand(26, Recharge));
	if (!TestTrue(TEXT("Maximum recharge encoded"), FNativeExampleMod::EncodeRechargeCommand(25, Recharge))) return false;

	struct FWorkerResults
	{
		uint32 Thread = 0;
		bool bCreate = false;
		bool bDuplicateCreateRejected = false;
		bool bApply = false;
		bool bExtractMatches = false;
		bool bShortRejected = false;
		bool bVersionRejected = false;
		bool bInvalidEnergyRejected = false;
		bool bMalformedLeavesState = false;
		bool bBadCommandRejected = false;
		bool bMissingTargetRejected = false;
		bool bRechargeAccepted = false;
		bool bEnergyClamped = false;
		bool bRemove = false;
	};
	const uint32 CallerThread = FPlatformTLS::GetCurrentThreadId();
	TFuture<FWorkerResults> Future = Async(EAsyncExecution::Thread, [Component, Definition, Command, Encoded, Recharge]()
	{
		FWorkerResults Result;
		Result.Thread = FPlatformTLS::GetCurrentThreadId();
		constexpr uint64 Token = 99;
		Result.bCreate = Definition->Create(Token);
		Result.bDuplicateCreateRejected = !Definition->Create(Token);
		Result.bApply = Component->Apply(Token, Encoded);
		TArray<uint8> Extracted;
		Result.bExtractMatches = Component->Extract(Token, Extracted) && Extracted == Encoded;
		TArray<uint8> Malformed = Encoded;
		Malformed.Pop();
		Result.bShortRejected = !Component->Apply(Token, Malformed);
		Malformed = Encoded;
		Malformed[0] = 2;
		Result.bVersionRejected = !Component->Apply(Token, Malformed);
		Malformed = Encoded;
		Malformed[13] = 0;
		Malformed[14] = 101;
		Result.bInvalidEnergyRejected = !Component->Apply(Token, Malformed);
		Extracted.Reset();
		Result.bMalformedLeavesState = Component->Extract(Token, Extracted) && Extracted == Encoded;
		Malformed = Recharge;
		Malformed[0] = 2;
		Result.bBadCommandRejected = !Command->Execute(1, Token, Malformed);
		Result.bMissingTargetRejected = !Command->Execute(1, Token + 1, Recharge);
		Result.bRechargeAccepted = Command->Execute(1, Token, Recharge);
		Extracted.Reset();
		Result.bEnergyClamped = Component->Extract(Token, Extracted) && Extracted.Num() == 15 && Extracted[13] == 0 && Extracted[14] == 100;
		Component->Remove(Token);
		Result.bRemove = !Component->Extract(Token, Extracted);
		return Result;
	});
	const FWorkerResults Worker = Future.Get();
	TestTrue(TEXT("Callbacks execute on worker thread"), Worker.Thread != 0 && Worker.Thread != CallerThread);
	TestTrue(TEXT("Definition creates entity"), Worker.bCreate);
	TestTrue(TEXT("Duplicate create rejected"), Worker.bDuplicateCreateRejected);
	TestTrue(TEXT("Valid component applies and extracts"), Worker.bApply && Worker.bExtractMatches);
	TestTrue(TEXT("Malformed component input rejected without changing state"), Worker.bShortRejected && Worker.bVersionRejected && Worker.bInvalidEnergyRejected && Worker.bMalformedLeavesState);
	TestTrue(TEXT("Invalid command or target rejected"), Worker.bBadCommandRejected && Worker.bMissingTargetRejected);
	TestTrue(TEXT("Valid recharge clamps energy"), Worker.bRechargeAccepted && Worker.bEnergyClamped);
	TestTrue(TEXT("Component removal clears worker state"), Worker.bRemove);
	return true;
}

#endif
