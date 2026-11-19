#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "MovementExample/ExampleMovementDebugActor.h"
#include "MovementExample/ExampleMovement.h"
#include "MovementExample/ExampleMovementServerSubsystem.h"
#include "MovementExample/ExampleMovementSpawner.h"
#include "Networking/FlecsGenericNetworkSessionSubsystem.h"
#include "Subsystems/FlecsGameInstanceSubsystem.h"
#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleMovementActorStartupTest, "Flecs.Example.ActorStartsPopulation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExampleMovementActorStartupTest::RunTest(const FString& Parameters)
{
 (void)Parameters;
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 if (!TestNotNull(TEXT("Create standalone world"), World)) return false;
 UExampleMovementServerSubsystem* Example = World->GetSubsystem<UExampleMovementServerSubsystem>();
 if (!TestNotNull(TEXT("Example subsystem exists"), Example)) { World->DestroyWorld(false); return false; }
 TestEqual(TEXT("Fixture is standalone"), World->GetNetMode(), NM_Standalone);
 if (UFlecsNetworkSessionSubsystem* Session = World->GetSubsystem<UFlecsNetworkSessionSubsystem>())
 {
  Session->Tick(0.0f);
  TestFalse(TEXT("Session waits for its game instance before freezing"), Session->IsModRegistryFrozen());
 }
 TestFalse(TEXT("Population not requested without actor"), Example->HasStarted());
 TestFalse(TEXT("Subsystem does not tick before activation"), Example->IsTickable());
 Example->Tick(0.1f);
 TestFalse(TEXT("Tick alone does not activate population"), Example->HasStarted());
 AExampleMovementSpawner* Spawner = World->SpawnActor<AExampleMovementSpawner>();
 if (TestNotNull(TEXT("Spawn starter actor"), Spawner))
 {
  Spawner->DispatchBeginPlay();
  TestTrue(TEXT("Placed starter requests population"), Example->HasStarted());
  TestTrue(TEXT("Repeat activation is idempotent"), Example->StartExample());
 }
 AExampleMovementDebugActor* Debug = World->SpawnActor<AExampleMovementDebugActor>();
 if (TestNotNull(TEXT("Spawn local debug actor"), Debug))
 {
  Debug->DispatchBeginPlay();
  TestTrue(TEXT("Debug actor runs in standalone"), Debug->IsActorTickEnabled());
 }
 World->DestroyWorld(false);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleMovementStandaloneIntegrationTest, "Flecs.Example.StandaloneSharedSpawn",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExampleMovementStandaloneIntegrationTest::RunTest(const FString& Parameters)
{
 (void)Parameters;
 if (!TestNotNull(TEXT("Engine is available"), GEngine)) return false;
 UGameInstance* Game = NewObject<UGameInstance>(GEngine);
 if (!TestNotNull(TEXT("Create game instance"), Game)) return false;
 Game->InitializeStandalone();
 UWorld* World = Game->GetWorld();
 if (!TestNotNull(TEXT("Standalone world exists"), World)) { Game->Shutdown(); return false; }
 auto Cleanup = [Game, World]()
 {
  World->DestroyWorld(false);
  Game->Shutdown();
  GEngine->DestroyWorldContext(World);
 };
 UFlecsNetworkSessionSubsystem* Session = World->GetSubsystem<UFlecsNetworkSessionSubsystem>();
 UExampleMovementServerSubsystem* Example = World->GetSubsystem<UExampleMovementServerSubsystem>();
 if (!TestNotNull(TEXT("Network session exists"), Session) ||
     !TestNotNull(TEXT("Example subsystem exists"), Example)) { Cleanup(); return false; }
 TestEqual(TEXT("World is standalone"), World->GetNetMode(), NM_Standalone);
 Example->EntityCount = 2;
 int32 Spawned = 0, Failed = 0;
 const FDelegateHandle Handle = Session->OnSessionEvent.AddLambda([&](const FFlecsSessionEvent& Event)
 {
  if (Event.Kind == EFlecsSessionEventKind::SharedSpawnFinished)
  {
   if (Event.SpawnResult == FlecsSimulation::EModelResult::Accepted && Event.SpawnedIdentity.IsValid()) ++Spawned;
   else ++Failed;
  }
 });
 Session->Tick(0.0f);
 Example->Tick(0.0f);
 TestFalse(TEXT("Example waits for placed actor"), Example->HasStarted());
 TestEqual(TEXT("No entity spawned before actor startup"), Spawned, 0);
 AExampleMovementSpawner* Spawner = World->SpawnActor<AExampleMovementSpawner>();
 if (TestNotNull(TEXT("Create example starter"), Spawner))
 {
  Spawner->DispatchBeginPlay();
  const double Deadline = FPlatformTime::Seconds() + 5.0;
  while (Spawned + Failed < 2 && FPlatformTime::Seconds() < Deadline)
  {
   Session->Tick(0.0f);
   Example->Tick(0.0f);
   FPlatformProcess::SleepNoStats(0.005f);
  }
  TestEqual(TEXT("Both shared entities completed spawn"), Spawned, 2);
  TestEqual(TEXT("No shared spawn failed"), Failed, 0);
  UFlecsGameInstanceSubsystem* Flecs = Game->GetSubsystem<UFlecsGameInstanceSubsystem>();
  if (Spawned == 2 && TestNotNull(TEXT("Simulation subsystem exists"), Flecs))
  {
   struct FMovementInspection { std::atomic<bool> bDone{false}; std::atomic<bool> bMoved{false}; };
   const auto Inspection = MakeShared<FMovementInspection, ESPMode::ThreadSafe>();
   const auto Result = Flecs->SubmitWorkerCommand(Flecs->GetActiveEpoch(), [Inspection](flecs::world& Worker)
   {
    Worker.each<const FExampleLocation>([Inspection](const FExampleLocation& Location)
    {
     const FVector3f P = Location.Value;
     if (P != FVector3f(0.0f, 0.0f, 50.0f) && P != FVector3f(200.0f, 0.0f, 50.0f))
      Inspection->bMoved.store(true, std::memory_order_release);
    });
    Inspection->bDone.store(true, std::memory_order_release);
   });
   if (TestTrue(TEXT("Queue movement inspection"), Result == EFlecsWorkerCommandResult::Accepted))
   {
    const double InspectionDeadline = FPlatformTime::Seconds() + 2.0;
    while (!Inspection->bDone.load(std::memory_order_acquire) && FPlatformTime::Seconds() < InspectionDeadline)
     FPlatformProcess::SleepNoStats(0.005f);
    TestTrue(TEXT("Movement inspection completed"), Inspection->bDone.load(std::memory_order_acquire));
    TestTrue(TEXT("Spawned example entities move on the worker"), Inspection->bMoved.load(std::memory_order_acquire));
   }
  }
 }
 Session->OnSessionEvent.Remove(Handle);
 Cleanup();
 return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleMovementDedicatedModeTest, "Flecs.Example.DedicatedModeSharedSpawn",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExampleMovementDedicatedModeTest::RunTest(const FString& Parameters)
{
 (void)Parameters;
 if (!TestNotNull(TEXT("Engine is available"), GEngine)) return false;
 UGameInstance* Game = NewObject<UGameInstance>(GEngine);
 if (!TestNotNull(TEXT("Create game instance"), Game)) return false;
 Game->InitializeStandalone();
 UWorld* World = Game->GetWorld();
 if (!TestNotNull(TEXT("World exists"), World)) { Game->Shutdown(); return false; }
 // Use Unreal's editor net-mode fallback; this does not create a network driver.
 World->WorldType = EWorldType::PIE;
 World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
 auto Cleanup = [Game, World]()
 {
  World->DestroyWorld(false);
  Game->Shutdown();
  GEngine->DestroyWorldContext(World);
 };
 UFlecsNetworkSessionSubsystem* Session = World->GetSubsystem<UFlecsNetworkSessionSubsystem>();
 UExampleMovementServerSubsystem* Example = World->GetSubsystem<UExampleMovementServerSubsystem>();
 if (!TestNotNull(TEXT("Network session exists"), Session) ||
     !TestNotNull(TEXT("Example subsystem exists"), Example)) { Cleanup(); return false; }
 TestEqual(TEXT("World reports dedicated server mode"), World->GetNetMode(), NM_DedicatedServer);
 Example->EntityCount = 1;
 int32 Spawned = 0, Failed = 0;
 const FDelegateHandle Handle = Session->OnSessionEvent.AddLambda([&](const FFlecsSessionEvent& Event)
 {
  if (Event.Kind == EFlecsSessionEventKind::SharedSpawnFinished)
  {
   if (Event.SpawnResult == FlecsSimulation::EModelResult::Accepted && Event.SpawnedIdentity.IsValid()) ++Spawned;
   else ++Failed;
  }
 });
 Session->Tick(0.0f);
 Example->Tick(0.0f);
 TestFalse(TEXT("No actor means no start request"), Example->HasStarted());
 TestEqual(TEXT("No shared spawn before activation"), Spawned + Failed, 0);
 AExampleMovementSpawner* Spawner = World->SpawnActor<AExampleMovementSpawner>();
 if (TestNotNull(TEXT("Create example starter"), Spawner))
 {
  Spawner->DispatchBeginPlay();
  const double Deadline = FPlatformTime::Seconds() + 5.0;
  while (Spawned + Failed < 1 && FPlatformTime::Seconds() < Deadline)
  {
   Session->Tick(0.0f);
   Example->Tick(0.0f);
   FPlatformProcess::SleepNoStats(0.005f);
  }
  TestEqual(TEXT("Shared entity spawned in dedicated mode"), Spawned, 1);
  TestEqual(TEXT("Dedicated-mode spawn did not fail"), Failed, 0);
 }
 Session->OnSessionEvent.Remove(Handle);
 Cleanup();
 return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExampleMovementBehaviorTest, "Flecs.Example.MovementBehavior",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExampleMovementBehaviorTest::RunTest(const FString& Parameters)
{
 (void)Parameters;
 const FFlecsPeerSpawnRequest Spawn = ExampleMovement::MakeSpawn(0);
 TestEqual(TEXT("Movement spawn contains three network components"), Spawn.InitialComponents.Num(), 3);
 TSet<FString> ComponentIds;
 for (const auto& Component : Spawn.InitialComponents) ComponentIds.Add(Component.StableComponentId);
 TestTrue(TEXT("Location is replicated"), ComponentIds.Contains(ExampleMovement::Location));
 TestTrue(TEXT("Rotation is replicated"), ComponentIds.Contains(ExampleMovement::Rotation));
 TestTrue(TEXT("Health is replicated"), ComponentIds.Contains(ExampleMovement::Health));
 TestFalse(TEXT("Stamina is not replicated"), ComponentIds.Contains(TEXT("example:movement.stamina")));
 TestFalse(TEXT("Speed is not replicated"), ComponentIds.Contains(TEXT("example:movement.speed")));
 FExampleLocation Location;
 FExampleRotation Rotation;
 FExampleHealth Health;
 FExampleStamina Stamina;
 FExampleSpeed Speed;
 Speed.Target = FVector2f(1000.0f, 0.0f);
 FRandomStream Random(1947);
 ExampleMovement::UpdateMovement(1.0f, Location, Rotation, Health, Stamina, Speed, Random);
 TestTrue(TEXT("Moving entity advances toward destination"), Location.Value.X > 0.0f);
 TestTrue(TEXT("Movement drains stamina"), Stamina.Value < 100.0f);
 TestTrue(TEXT("Health changes"), Health.Value < 100.0f);
 const FVector3f BeforeRest = Location.Value;
 Stamina.Value = 5.0f;
 ExampleMovement::UpdateMovement(1.0f, Location, Rotation, Health, Stamina, Speed, Random);
 TestTrue(TEXT("Low stamina starts recovery"), Stamina.bResting && Stamina.Value > 5.0f);
 TestTrue(TEXT("Entity is stationary while resting"), Location.Value == BeforeRest && Speed.Value == 0.0f);
 Stamina.Value = 80.0f;
 ExampleMovement::UpdateMovement(1.0f, Location, Rotation, Health, Stamina, Speed, Random);
 TestFalse(TEXT("Recovered entity resumes moving"), Stamina.bResting);
 TestTrue(TEXT("Recovered entity has speed"), Speed.Value > 0.0f);
 return true;
}

#endif
