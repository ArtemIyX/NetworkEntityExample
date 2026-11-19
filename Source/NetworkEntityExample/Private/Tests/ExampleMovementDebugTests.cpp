#if WITH_DEV_AUTOMATION_TESTS
#include "MovementExample/ExampleMovementDebug.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleDebugMissingPlayerTest, "Flecs.Example.DebugMissingPlayer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FExampleDebugMissingPlayerTest::RunTest(const FString& Parameters)
{
 (void)Parameters;
 FVector Position(1.0, 2.0, 3.0);
 TestFalse(TEXT("Missing world has no local focal position"), ExampleMovementDebug::TryGetFocalPosition(nullptr, Position));
 TestTrue(TEXT("Unavailable focal position leaves output unchanged"), Position == FVector(1.0, 2.0, 3.0));
 return true;
}
#endif
