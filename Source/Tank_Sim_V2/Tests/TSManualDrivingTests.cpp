// VR hand ("Manual") driving - automated, no headset, no PIE.
//
// The Meta XR Simulator can drive a fake OpenXR runtime end to end, but it replaces the runtime a
// real Quest Link session uses, so it is not the default. These tests instead cover every layer that
// does NOT need an XR runtime, and name the layers they cannot reach:
//
//   covered here                                   NOT covered here (and what covers it)
//   ------------------------------------------     -------------------------------------------------
//   key -> action TYPE match (was T1)              OpenXR accepting the bindings: a `-game -vr` run
//   descriptions OpenXR requires (was T0)             and `grep XR_ERROR` (see CLAUDE.md)
//   the new actions exist, bound, assigned         the action firing from a physical trigger/grip
//   drive mapping + lever pull maths                  (key layer is the T1 check; firing is human)
//   hand -> grab -> pull -> RPC -> tank input      whether 60cm reach / the pull axis FEEL right
//   interior animation follows the hands              (a human in the headset, last)
//   gating: seat, mode, reach, leaving Manual
//
// Hands are simulated by placing the MotionController components directly. That is legitimate, not
// a trick: UMotionControllerComponent::TickComponent only writes the transform when the controller
// reports as TRACKED, so with no XR runtime (and no world tick) a placed hand stays placed.
//
// Input handlers are called directly rather than through InjectInputForAction: injection needs a
// ULocalPlayer's Enhanced Input subsystem, which a transient test world has none of. The key->action
// layer that this skips is exactly what the InputBindings test covers statically.

#include "Misc/AutomationTest.h"

#include "Core/TSGameMode.h"
#include "Core/TSTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "WheeledVehiclePawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "MotionControllerComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Player/TSCrewPawn.h"
#include "Player/TSDesktopPawn.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankControllerBase.h"

#if WITH_DEV_AUTOMATION_TESTS

// Friend of ATSCrewPawn and ATSTankControllerBase. Reaches the private input handlers and state so the
// tests exercise the REAL code paths rather than a re-implementation of them. Global scope, because
// the friend declarations name it at global scope.
struct FTSManualDrivingTestAccess
{
	static bool IsLocalManualDriver(const ATSCrewPawn* Pawn) { return Pawn->IsLocalManualDriver(); }
	static void Gas(ATSCrewPawn* Pawn, float Value) { Pawn->Input_PedalGas(FInputActionValue(Value)); }
	static void GasReleased(ATSCrewPawn* Pawn) { Pawn->Input_PedalGasReleased(FInputActionValue(0.f)); }
	static void Brake(ATSCrewPawn* Pawn, float Value) { Pawn->Input_PedalBrake(FInputActionValue(Value)); }
	static void BrakeReleased(ATSCrewPawn* Pawn) { Pawn->Input_PedalBrakeReleased(FInputActionValue(0.f)); }

	static void Grip(ATSCrewPawn* Pawn, bool bLeft, bool bPressed)
	{
		const FInputActionValue Value(bPressed);
		if (bLeft)
		{
			bPressed ? Pawn->Input_LeverGripLeftPressed(Value) : Pawn->Input_LeverGripLeftReleased(Value);
		}
		else
		{
			bPressed ? Pawn->Input_LeverGripRightPressed(Value) : Pawn->Input_LeverGripRightReleased(Value);
		}
	}

	static void Update(ATSCrewPawn* Pawn) { Pawn->UpdateManualDriving(); }
	static bool Held(const ATSCrewPawn* Pawn, bool bLeft) { return bLeft ? Pawn->bLeftLeverHeld : Pawn->bRightLeverHeld; }
	static UMotionControllerComponent* Hand(ATSCrewPawn* Pawn, bool bLeft) { return bLeft ? Pawn->LeftHand.Get() : Pawn->RightHand.Get(); }

	static UInputAction* PawnAction(const ATSCrewPawn* Pawn, int32 Which)
	{
		switch (Which)
		{
		case 0: return Pawn->IA_DrivePedalGas;
		case 1: return Pawn->IA_DrivePedalBrake;
		case 2: return Pawn->IA_LeverGripLeft;
		default: return Pawn->IA_LeverGripRight;
		}
	}

	static void TickInterior(ATSTankControllerBase* Tank, float DeltaSeconds) { Tank->UpdateInteriorControlState(DeltaSeconds); }
	static bool LeverOverride(const ATSTankControllerBase* Tank) { return Tank->bLocalLeverOverride; }

	// --- Feedback ---
	static void Feedback(ATSCrewPawn* Pawn) { Pawn->UpdateLeverFeedback(); }
	static USkeletalMeshComponent* HandMesh(const ATSCrewPawn* Pawn, bool bLeft) { return Pawn->FindHandMesh(bLeft); }
	static UStaticMeshComponent* Indicator(const ATSCrewPawn* Pawn, bool bLeft) { return bLeft ? Pawn->LeftLeverIndicator.Get() : Pawn->RightLeverIndicator.Get(); }
	static bool InReach(const ATSCrewPawn* Pawn, bool bLeft) { return bLeft ? Pawn->bLeftHandInReach : Pawn->bRightHandInReach; }
	static float MissGrasp(const ATSCrewPawn* Pawn) { return Pawn->LeverMissGraspAlpha; }
	static const UObject* HapticEffect(const ATSCrewPawn* Pawn) { return Pawn->LeverHapticEffect; }
	static const UObject* IndicatorMesh(const ATSCrewPawn* Pawn) { return Pawn->LeverGrabIndicatorMesh; }

	// The hand AnimBP's grasp float, read the same way the pawn writes it. -1 = no anim instance,
	// -2 = no such variable.
	static float Grasp(const ATSCrewPawn* Pawn, bool bLeft)
	{
		const USkeletalMeshComponent* Mesh = Pawn->FindHandMesh(bLeft);
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (!Anim)
		{
			return -1.f;
		}
		const FProperty* Prop = Anim->GetClass()->FindPropertyByName(Pawn->HandGraspPoseVariable);
		if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			return static_cast<float>(DoubleProp->GetPropertyValue_InContainer(Anim));
		}
		if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			return FloatProp->GetPropertyValue_InContainer(Anim);
		}
		return -2.f;
	}
};

namespace
{
	const TCHAR* VKTankClassPath =
		TEXT("/Game/YI_TankCollection/Blueprint/WW2_VK1602Leopard/Controller/BP_VK1602Leopard_Controller_Chaos.BP_VK1602Leopard_Controller_Chaos_C");
	const TCHAR* VRPawnClassPath = TEXT("/Game/TankSimulation/Player/BP_TSVRPawn.BP_TSVRPawn_C");
	const TCHAR* DesktopPawnClassPath = TEXT("/Game/TankSimulation/Player/BP_TSDesktopPawn.BP_TSDesktopPawn_C");

	const TCHAR* ContextNames[] = { TEXT("IMC_Shared"), TEXT("IMC_Driver"), TEXT("IMC_Gunner"), TEXT("IMC_Commander"), TEXT("IMC_VR_Widget") };

	UInputMappingContext* LoadContext(const TCHAR* Name)
	{
		return LoadObject<UInputMappingContext>(nullptr,
			*FString::Printf(TEXT("/Game/TankSimulation/Input/Contexts/%s.%s"), Name, Name));
	}

	bool Near(const FVector2D& A, const FVector2D& B, float Tolerance = 0.01f)
	{
		return FMath::IsNearlyEqual(A.X, B.X, Tolerance) && FMath::IsNearlyEqual(A.Y, B.Y, Tolerance);
	}

	// Same construction as FTSFlowFixture (TSMatchFlowTests.cpp) - see that file for why the world has
	// to come from a GameInstance - but spawning the REAL VK1602, because the lever grab points and the
	// interior poses only exist on the real rig. The flow test's stand-in tank has neither.
	struct FTSManualFixture
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		ATSGameMode* GameMode = nullptr;

		bool Setup(TSubclassOf<APawn> TankClass)
		{
			GameInstance = NewObject<UGameInstance>(GEngine);
			if (!GameInstance)
			{
				return false;
			}
			GameInstance->InitializeStandalone();
			World = GameInstance->GetWorld();
			if (!World)
			{
				return false;
			}
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->DefaultGameMode = ATSGameMode::StaticClass();
			}
			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());

			GameMode = World->GetAuthGameMode<ATSGameMode>();
			if (GameMode)
			{
				GameMode->SetDefaultTankClassForTesting(TankClass);
			}
			return GameMode != nullptr;
		}

		// A crew member as the manual-driving code sees one: a LOCAL controller (IsLocalManualDriver
		// requires it), a PlayerState, and a possessed crew pawn carrying the two motion controllers.
		//
		// The desktop pawn BLUEPRINT, deliberately on both counts:
		//  - desktop, because it has the same hands (they live on ATSCrewPawn) but can never try to
		//    switch stereo on, so the test cannot touch the editor's viewport;
		//  - the Blueprint rather than the native class, because the native class carries no input
		//    assets and the pawn rightly logs an ERROR about that - which fails any automation test.
		ATSCrewPawn* MakeCrew(ATSTankPlayerController*& OutPC, const TCHAR* PawnClassPath = DesktopPawnClassPath) const
		{
			UClass* PawnClass = LoadClass<ATSCrewPawn>(nullptr, PawnClassPath);
			if (!PawnClass)
			{
				return nullptr;
			}
			ATSTankPlayerController* PC = World->SpawnActor<ATSTankPlayerController>();
			ATSTankPlayerState* PS = World->SpawnActor<ATSTankPlayerState>();
			ATSCrewPawn* Pawn = World->SpawnActor<ATSCrewPawn>(PawnClass);
			if (!PC || !PS || !Pawn)
			{
				return nullptr;
			}
			PC->PlayerState = PS;
			PS->SetOwner(PC);
			// The engine's own switch (public "for GameModeBase to use"): a transient world has no
			// viewport to give the controller a ULocalPlayer, and every manual-driving path is gated
			// on IsLocalController().
			PC->SetAsLocalPlayerController();
			PC->Possess(Pawn);
			OutPC = PC;
			return Pawn;
		}

		// This teardown CRASHED THE EDITOR in its first version, ~25 minutes after a green run:
		//   Assertion failed: Vehicle != 0   ChaosVehicleManager.cpp:137   (called from GC)
		// Two engine facts combine into that:
		//  - UGameInstance::Shutdown does NOT destroy the world - it only clears WorldContext - so the
		//    test world lived on until some later garbage collection picked it up;
		//  - FChaosVehicleManager holds WEAK pointers to its vehicles and check()s each one as its
		//    destructor removes them. Collected out of order, a vehicle is already gone when the
		//    manager dies with it still listed.
		// So: destroy every Chaos vehicle first, while its physics scene is alive - OnDestroyPhysicsState
		// then removes it from the manager with a valid pointer - and destroy the world HERE, not in a
		// GC nobody is watching. The flow tests never hit this because their stand-in tank is not a
		// Chaos vehicle; anything spawning a real tank must tear down like this.
		void TearDown()
		{
			if (World)
			{
				TArray<AActor*> Vehicles;
				for (TActorIterator<AWheeledVehiclePawn> It(World); It; ++It)
				{
					Vehicles.Add(*It);
				}
				for (AActor* Vehicle : Vehicles)
				{
					Vehicle->Destroy();
				}
			}

			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance = nullptr;
			}

			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};
}

// ------------------------------------------------------------------------------------------------
// 1. The drive mapping - pure.
// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSManualDriveMappingTest,
	"TankSim.VR.ManualDriving.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSManualDriveMappingTest::RunTest(const FString& Parameters)
{
	struct FCase { float Gas, Brake, Left, Right; FVector2D Expect; const TCHAR* What; };
	const FCase Cases[] = {
		{ 1.f, 0.f, 0.f, 0.f,   FVector2D( 1.f,  0.f),   TEXT("gas alone drives forward") },
		{ 0.f, 1.f, 0.f, 0.f,   FVector2D(-1.f,  0.f),   TEXT("brake alone reverses") },
		{ 0.f, 0.f, 1.f, 0.f,   FVector2D( 0.f, -1.f),   TEXT("LEFT lever steers LEFT (negative)") },
		{ 0.f, 0.f, 0.f, 1.f,   FVector2D( 0.f,  1.f),   TEXT("RIGHT lever steers RIGHT (positive)") },
		{ .5f, 0.f, .25f, 0.f,  FVector2D( .5f, -.25f),  TEXT("mixed input is proportional") },
		{ 1.f, 1.f, 1.f, 1.f,   FVector2D( 0.f,  0.f),   TEXT("opposing pedals and levers cancel") },
		{ 2.f, -1.f, 3.f, -2.f, FVector2D( 1.f, -1.f),   TEXT("out-of-range input is clamped") },
	};
	for (const FCase& C : Cases)
	{
		const FVector2D Got = ATSCrewPawn::ComputeManualDriveInput(C.Gas, C.Brake, C.Left, C.Right);
		TestTrue(FString::Printf(TEXT("%s: got (%.3f, %.3f)"), C.What, Got.X, Got.Y), Near(Got, C.Expect, 1e-5f));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// 2. Lever pull from hand travel - pure.
// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSLeverPullTest,
	"TankSim.VR.ManualDriving.LeverPull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSLeverPullTest::RunTest(const FString& Parameters)
{
	const FVector Start(100.f, 20.f, 50.f);
	const FVector Back(-1.f, 0.f, 0.f);   // the default pull axis
	auto Pull = [&](const FVector& Delta, const FVector& Axis = FVector(-1.f, 0.f, 0.f), float Full = 20.f)
	{
		return ATSCrewPawn::ComputeLeverPull(Start, Start + Delta, Axis, Full);
	};

	TestEqual(TEXT("no movement = no pull"), Pull(FVector::ZeroVector), 0.f);
	TestEqual(TEXT("half the pull distance along the axis = 0.5"), Pull(FVector(-10.f, 0.f, 0.f)), 0.5f);
	TestEqual(TEXT("the full pull distance = 1"), Pull(FVector(-20.f, 0.f, 0.f)), 1.f);
	TestEqual(TEXT("beyond the full distance clamps at 1"), Pull(FVector(-60.f, 0.f, 0.f)), 1.f);
	TestEqual(TEXT("pushing AWAY (against the axis) is not a pull"), Pull(FVector(10.f, 0.f, 0.f)), 0.f);
	TestEqual(TEXT("moving sideways is not a pull"), Pull(FVector(0.f, 15.f, 0.f)), 0.f);
	TestEqual(TEXT("only the along-axis component counts"), Pull(FVector(-10.f, 30.f, -30.f)), 0.5f);
	TestEqual(TEXT("an unnormalised axis gives the same answer"), Pull(FVector(-10.f, 0.f, 0.f), Back * 7.f), 0.5f);
	TestEqual(TEXT("a zero axis can never pull"), Pull(FVector(-10.f, 0.f, 0.f), FVector::ZeroVector), 0.f);
	return true;
}

// ------------------------------------------------------------------------------------------------
// 3. Input bindings - the T0/T1 checks from CLAUDE.md, plus the new manual-driving wiring.
// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSVRInputBindingsTest,
	"TankSim.VR.InputBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSVRInputBindingsTest::RunTest(const FString& Parameters)
{
	TSet<FString> SeenDescriptions;
	TSet<const UInputAction*> SeenActions;
	int32 XRKeysChecked = 0;

	// OpenXR builds an action set per context and an action per Input Action, and REJECTS an empty or
	// >= 128 char localized name (XR_ERROR_LOCALIZED_NAME_INVALID), which takes every VR input down.
	auto CheckDescription = [&](const FString& Owner, const FString& Description)
	{
		const FString Trimmed = Description.TrimStartAndEnd();
		TestFalse(FString::Printf(TEXT("%s description is not empty"), *Owner), Trimmed.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s description is under 128 chars (%d)"), *Owner, Trimmed.Len()), Trimmed.Len() < 128);
		TestFalse(FString::Printf(TEXT("%s description '%s' is unique"), *Owner, *Trimmed), SeenDescriptions.Contains(Trimmed));
		SeenDescriptions.Add(Trimmed);
	};

	for (const TCHAR* Name : ContextNames)
	{
		const UInputMappingContext* Context = LoadContext(Name);
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), Name), Context))
		{
			continue;
		}
		CheckDescription(Name, Context->ContextDescription.ToString());

		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!Action)
			{
				continue;
			}
			if (!SeenActions.Contains(Action))
			{
				SeenActions.Add(Action);
				CheckDescription(Action->GetName(), Action->ActionDescription.ToString());
			}

			// OpenXR only parses 4-token keys; anything else is not an XR key (a key it skips cannot be
			// bound in a headset, which is why the count is reported below).
			TArray<FString> Tokens;
			const FString KeyName = Mapping.Key.GetFName().ToString();
			KeyName.ParseIntoArray(Tokens, TEXT("_"));
			if (Tokens.Num() != 4 || !(Tokens[0] == TEXT("OculusTouch") || Tokens[0] == TEXT("ValveIndex") || Tokens[0] == TEXT("Vive")))
			{
				continue;
			}
			++XRKeysChecked;

			// An Axis2D action must bind a *_2D key, or OpenXR silently leaves it unbound.
			const bool bKeyIs2D = Tokens[3] == TEXT("2D");
			const bool bActionIs2D = Action->ValueType == EInputActionValueType::Axis2D;
			TestEqual(FString::Printf(TEXT("%s: %s component type matches %s"), Name, *KeyName, *Action->GetName()), bKeyIs2D, bActionIs2D);
		}
	}
	TestTrue(FString::Printf(TEXT("XR keys were actually checked (%d)"), XRKeysChecked), XRKeysChecked > 40);

	// The manual-driving actions: right type, bound to the right keys, and the grips must not consume
	// (IMC_Shared already maps the same grips to IA_Grab / IA_Interact).
	struct FExpected { const TCHAR* Action; EInputActionValueType Type; bool bConsume; const TCHAR* OculusKey; };
	const FExpected Expected[] = {
		{ TEXT("IA_DrivePedalGas"),   EInputActionValueType::Axis1D,  true,  TEXT("OculusTouch_Right_Trigger_Axis") },
		{ TEXT("IA_DrivePedalBrake"), EInputActionValueType::Axis1D,  true,  TEXT("OculusTouch_Left_Trigger_Axis") },
		{ TEXT("IA_LeverGripLeft"),   EInputActionValueType::Boolean, false, TEXT("OculusTouch_Left_Grip_Click") },
		{ TEXT("IA_LeverGripRight"),  EInputActionValueType::Boolean, false, TEXT("OculusTouch_Right_Grip_Click") },
	};
	const UInputMappingContext* Driver = LoadContext(TEXT("IMC_Driver"));
	for (const FExpected& E : Expected)
	{
		const UInputAction* Action = LoadObject<UInputAction>(nullptr,
			*FString::Printf(TEXT("/Game/TankSimulation/Input/Actions/%s.%s"), E.Action, E.Action));
		if (!TestNotNull(FString::Printf(TEXT("%s exists"), E.Action), Action))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s value type"), E.Action), static_cast<int32>(Action->ValueType), static_cast<int32>(E.Type));
		TestEqual(FString::Printf(TEXT("%s bConsumeInput"), E.Action), Action->bConsumeInput, E.bConsume);

		int32 Bindings = 0;
		bool bHasOculus = false;
		if (Driver)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Driver->GetMappings())
			{
				if (Mapping.Action == Action)
				{
					++Bindings;
					bHasOculus |= Mapping.Key.GetFName() == FName(E.OculusKey);
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s is bound in IMC_Driver for all three profiles"), E.Action), Bindings, 3);
		TestTrue(FString::Printf(TEXT("%s is bound to %s"), E.Action, E.OculusKey), bHasOculus);
	}

	// ...and the VR crew pawn actually references them - a null here silently skips the BindAction.
	if (UClass* VRPawnClass = LoadClass<ATSCrewPawn>(nullptr, VRPawnClassPath))
	{
		const ATSCrewPawn* CDO = VRPawnClass->GetDefaultObject<ATSCrewPawn>();
		for (int32 Which = 0; Which < 4; ++Which)
		{
			TestNotNull(FString::Printf(TEXT("BP_TSVRPawn assigns manual-driving action #%d"), Which),
				FTSManualDrivingTestAccess::PawnAction(CDO, Which));
		}
	}
	else
	{
		AddError(TEXT("BP_TSVRPawn class did not load"));
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// 4. The whole pipeline on the real VK1602: hands -> grab -> pull -> RPC -> tank -> interior pose.
// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSManualDrivingPipelineTest,
	"TankSim.VR.ManualDriving.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSManualDrivingPipelineTest::RunTest(const FString& Parameters)
{
	using Access = FTSManualDrivingTestAccess;

	UClass* VKClass = LoadClass<APawn>(nullptr, VKTankClassPath);
	if (!TestNotNull(TEXT("the VK1602 tank Blueprint loads"), VKClass))
	{
		return false;
	}

	FTSManualFixture Fx;
	if (!TestTrue(TEXT("world with a live ATSGameMode"), Fx.Setup(VKClass)))
	{
		Fx.TearDown();
		return false;
	}

	ATSTankPlayerController* DriverPC = nullptr;
	ATSTankPlayerController* GunnerPC = nullptr;
	ATSCrewPawn* Driver = Fx.MakeCrew(DriverPC);
	ATSCrewPawn* Gunner = Fx.MakeCrew(GunnerPC);
	if (!TestTrue(TEXT("two local crew members with possessed pawns"), Driver && Gunner && DriverPC && GunnerPC))
	{
		Fx.TearDown();
		return false;
	}
	ATSTankPlayerState* DriverPS = DriverPC->GetPlayerState<ATSTankPlayerState>();
	ATSTankPlayerState* GunnerPS = GunnerPC->GetPlayerState<ATSTankPlayerState>();

	TestTrue(TEXT("Driver takes Team A"), Fx.GameMode->TryAssignTeam(DriverPC, ETSTeamId::TeamA));
	TestTrue(TEXT("Driver takes the Driver seat"), Fx.GameMode->TryAssignRole(DriverPC, ETSCrewRole::Driver));
	TestTrue(TEXT("Gunner takes Team A"), Fx.GameMode->TryAssignTeam(GunnerPC, ETSTeamId::TeamA));
	TestTrue(TEXT("Gunner takes the Gunner seat"), Fx.GameMode->TryAssignRole(GunnerPC, ETSCrewRole::Gunner));

	ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(DriverPS->GetAssignedTank());
	UTSTankControlComponent* Control = Tank ? Tank->FindComponentByClass<UTSTankControlComponent>() : nullptr;
	if (!TestTrue(TEXT("a real VK1602 was spawned and assigned, with a control component"), Tank && Control))
	{
		Fx.TearDown();
		return false;
	}
	auto Input = [&]() { return Control->GetCurrentDriveInput(); };

	// --- Gating: Analog mode, and the GameMode's own rule ------------------------------------------
	TestFalse(TEXT("an Analog-mode Driver is NOT a manual driver"), Access::IsLocalManualDriver(Driver));
	Access::Gas(Driver, 1.f);
	Access::Update(Driver);
	TestTrue(TEXT("Analog mode: the gas trigger drives nothing"), Near(Input(), FVector2D::ZeroVector));

	TestFalse(TEXT("the GameMode REFUSES Manual for a Desktop-mode player (it would strand them with no stick)"),
		Fx.GameMode->TrySetDriveControlMode(DriverPC, ETSDriveControlMode::Manual));

	// The refusal above is the GameMode's VR rule, tested. From here the mode is set on the PlayerState
	// directly, because granting VR needs a real headset report - the pipeline under test is the same.
	DriverPS->SetDriveControlMode(ETSDriveControlMode::Manual);
	GunnerPS->SetDriveControlMode(ETSDriveControlMode::Manual);
	TestTrue(TEXT("a Manual-mode Driver IS a manual driver"), Access::IsLocalManualDriver(Driver));
	TestFalse(TEXT("a Manual-mode GUNNER is not - the levers belong to the Driver's seat"), Access::IsLocalManualDriver(Gunner));

	// --- Pedals ----------------------------------------------------------------------------------
	Access::Gas(Driver, 1.f);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("gas trigger -> tank throttle 1 (got %.2f, %.2f)"), Input().X, Input().Y), Near(Input(), FVector2D(1.f, 0.f)));

	Access::GasReleased(Driver);
	Access::Update(Driver);
	TestTrue(TEXT("releasing the trigger sends the terminal STOP"), Near(Input(), FVector2D::ZeroVector));

	Access::Brake(Driver, 0.6f);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("brake trigger 0.6 -> throttle -0.6 (got %.2f)"), Input().X), Near(Input(), FVector2D(-0.6f, 0.f)));
	Access::BrakeReleased(Driver);
	Access::Update(Driver);

	Access::Gas(Gunner, 1.f);
	Access::Update(Gunner);
	TestTrue(TEXT("the Gunner's trigger drives nothing"), Near(Input(), FVector2D::ZeroVector));

	// --- Lever grab points on the real rig -------------------------------------------------------
	FVector GrabL, GrabR;
	const bool bHasL = Tank->GetLeverGrabLocation(true, GrabL);
	const bool bHasR = Tank->GetLeverGrabLocation(false, GrabR);
	if (!TestTrue(TEXT("both lever grab points resolve on the interior mesh"), bHasL && bHasR))
	{
		Fx.TearDown();
		return false;
	}
	TestTrue(FString::Printf(TEXT("the grab points are real bone locations, not the tank origin (%.1f cm away)"),
		FVector::Dist(GrabL, Tank->GetActorLocation())), FVector::Dist(GrabL, Tank->GetActorLocation()) > 10.f);
	AddInfo(FString::Printf(TEXT("left/right grab points are %.1f cm apart"), FVector::Dist(GrabL, GrabR)));

	const FTransform TankXf = Tank->GetActorTransform();
	const FVector PullWorld = TankXf.TransformVectorNoScale(Tank->LeverPullAxisLocal.GetSafeNormal());
	UMotionControllerComponent* LeftHand = Access::Hand(Driver, true);
	UMotionControllerComponent* RightHand = Access::Hand(Driver, false);
	if (!TestTrue(TEXT("the pawn has both motion controllers"), LeftHand && RightHand))
	{
		Fx.TearDown();
		return false;
	}

	// --- Reach -----------------------------------------------------------------------------------
	LeftHand->SetWorldLocation(GrabL + FVector(0.f, 0.f, Tank->LeverGrabRadius + 50.f));
	Access::Grip(Driver, true, true);
	TestFalse(TEXT("a grip OUT of reach does not take the lever"), Access::Held(Driver, true));
	Access::Grip(Driver, true, false);

	LeftHand->SetWorldLocation(GrabL);
	Access::Grip(Driver, true, true);
	TestTrue(TEXT("a grip ON the grab point takes the left lever"), Access::Held(Driver, true));

	// --- Pull ------------------------------------------------------------------------------------
	LeftHand->SetWorldLocation(GrabL + PullWorld * Tank->LeverPullDistance * 0.5f);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("pulling the LEFT lever halfway steers left -0.5 (got %.2f, %.2f)"), Input().X, Input().Y),
		Near(Input(), FVector2D(0.f, -0.5f)));

	RightHand->SetWorldLocation(GrabR);
	Access::Grip(Driver, false, true);
	TestTrue(TEXT("the right hand takes the right lever"), Access::Held(Driver, false));
	RightHand->SetWorldLocation(GrabR + PullWorld * Tank->LeverPullDistance);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("right fully + left half -> steering +0.5 (got %.2f)"), Input().Y), Near(Input(), FVector2D(0.f, 0.5f)));

	Access::Gas(Driver, 1.f);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("pedal and levers together (got %.2f, %.2f)"), Input().X, Input().Y), Near(Input(), FVector2D(1.f, 0.5f)));
	Access::GasReleased(Driver);

	// --- The interior animation follows the hands ------------------------------------------------
	// On the Driver's own machine each lever shows ITS OWN pull (local override), not the net steering:
	// right fully + left half is steering +0.5, but the levers must sit at 1.0 and 0.5 - each under
	// the hand holding it. The net-steering view would show the left lever at rest in the left hand.
	TestTrue(TEXT("the manual Driver's hands own this machine's interior levers"), Access::LeverOverride(Tank));
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Access::TickInterior(Tank, 1.f / 30.f);
	}
	TestTrue(FString::Printf(TEXT("interior RIGHT lever sits where the right hand holds it (got %.3f, expect 1.0)"), Tank->GetInteriorRightLeverAlpha()),
		FMath::IsNearlyEqual(Tank->GetInteriorRightLeverAlpha(), 1.f, 0.02f));
	TestTrue(FString::Printf(TEXT("interior LEFT lever shows its own half pull, not the net steering (got %.3f, expect 0.5)"), Tank->GetInteriorLeftLeverAlpha()),
		FMath::IsNearlyEqual(Tank->GetInteriorLeftLeverAlpha(), 0.5f, 0.02f));

	// Compared as quaternion angles, not Euler roll: both lever poses sit at pitch ~-89, right at the
	// gimbal, where roll and yaw trade off and a halfway Rotator need not show a halfway roll.
	auto PoseFraction = [](const FRotator& Got, const FRotator& Rest, const FRotator& Pulled)
	{
		const double Full = Rest.Quaternion().AngularDistance(Pulled.Quaternion());
		return Full > UE_KINDA_SMALL_NUMBER ? Rest.Quaternion().AngularDistance(Got.Quaternion()) / Full : 0.0;
	};
	const double RightFrac = PoseFraction(Tank->GetInteriorLeverRotation(false), Tank->RightLeverRestRotation, Tank->RightLeverPulledRotation);
	const double LeftFrac = PoseFraction(Tank->GetInteriorLeverRotation(true), Tank->LeftLeverRestRotation, Tank->LeftLeverPulledRotation);
	TestTrue(FString::Printf(TEXT("the right lever BONE is at its measured pulled pose (%.3f of the way)"), RightFrac), FMath::IsNearlyEqual(RightFrac, 1.0, 0.02));
	TestTrue(FString::Printf(TEXT("the left lever BONE is halfway between its measured poses (%.3f of the way)"), LeftFrac), FMath::IsNearlyEqual(LeftFrac, 0.5, 0.02));

	// --- Releasing and leaving -------------------------------------------------------------------
	Access::Grip(Driver, false, false);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("releasing the right lever springs it back (got %.2f)"), Input().Y), Near(Input(), FVector2D(0.f, -0.5f)));

	DriverPS->SetDriveControlMode(ETSDriveControlMode::Analog);
	TestFalse(TEXT("leaving Manual releases a lever still held"), Access::Held(Driver, true));
	TestTrue(FString::Printf(TEXT("leaving Manual mid-pull sends the terminal STOP (got %.2f, %.2f)"), Input().X, Input().Y),
		Near(Input(), FVector2D::ZeroVector));

	TestFalse(TEXT("leaving Manual drops the local lever override"), Access::LeverOverride(Tank));
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Access::TickInterior(Tank, 1.f / 30.f);
	}
	TestTrue(FString::Printf(TEXT("the interior levers go back to following the steering input (L %.3f, R %.3f)"),
		Tank->GetInteriorLeftLeverAlpha(), Tank->GetInteriorRightLeverAlpha()),
		Tank->GetInteriorLeftLeverAlpha() < 0.02f && Tank->GetInteriorRightLeverAlpha() < 0.02f);

	Fx.TearDown();
	return true;
}

// ------------------------------------------------------------------------------------------------
// 5. Feedback on the real VR pawn: the handle marker, the reach cue, the hand closing and snapping
//    onto the handle, and all of it undone on release and on leaving Manual.
//    Haptics are fired but not asserted: a transient world has no controller to play them on.
// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSManualDrivingFeedbackTest,
	"TankSim.VR.ManualDriving.Feedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSManualDrivingFeedbackTest::RunTest(const FString& Parameters)
{
	using Access = FTSManualDrivingTestAccess;

	UClass* VKClass = LoadClass<APawn>(nullptr, VKTankClassPath);
	if (!TestNotNull(TEXT("the VK1602 tank Blueprint loads"), VKClass))
	{
		return false;
	}

	FTSManualFixture Fx;
	if (!TestTrue(TEXT("world with a live ATSGameMode"), Fx.Setup(VKClass)))
	{
		Fx.TearDown();
		return false;
	}

	// The VR pawn this time: the visible hands and the feedback assets are BP_TSVRPawn data.
	ATSTankPlayerController* PC = nullptr;
	ATSCrewPawn* Driver = Fx.MakeCrew(PC, VRPawnClassPath);
	if (!TestTrue(TEXT("a local VR crew pawn"), Driver && PC))
	{
		Fx.TearDown();
		return false;
	}
	ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	TestTrue(TEXT("Driver takes Team A"), Fx.GameMode->TryAssignTeam(PC, ETSTeamId::TeamA));
	TestTrue(TEXT("Driver takes the Driver seat"), Fx.GameMode->TryAssignRole(PC, ETSCrewRole::Driver));
	PS->SetDriveControlMode(ETSDriveControlMode::Manual);
	ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(PS->GetAssignedTank());
	if (!TestTrue(TEXT("the manual Driver of a real VK1602"), Tank && Access::IsLocalManualDriver(Driver)))
	{
		Fx.TearDown();
		return false;
	}

	TestNotNull(TEXT("BP_TSVRPawn assigns the lever haptic effect"), Access::HapticEffect(Driver));
	TestNotNull(TEXT("BP_TSVRPawn assigns the handle marker mesh"), Access::IndicatorMesh(Driver));
	USkeletalMeshComponent* HandMesh = Access::HandMesh(Driver, true);
	UMotionControllerComponent* Hand = Access::Hand(Driver, true);
	FVector Grab;
	if (!TestNotNull(TEXT("a visible hand mesh rides the left controller"), HandMesh)
		|| !TestTrue(TEXT("the left handle socket resolves"), Hand && Tank->GetLeverGrabLocation(true, Grab)))
	{
		Fx.TearDown();
		return false;
	}
	TestTrue(FString::Printf(TEXT("the hand's AnimBP exposes the grasp pose (read %.2f)"), Access::Grasp(Driver, true)),
		Access::Grasp(Driver, true) >= 0.f);

	// --- Out of reach: the marker shows where to hold; a grip there closes on nothing -------------
	Hand->SetWorldLocation(Grab + FVector(0.f, 0.f, Tank->LeverGrabRadius + 20.f));
	Access::Feedback(Driver);
	UStaticMeshComponent* Marker = Access::Indicator(Driver, true);
	if (TestNotNull(TEXT("a marker is created on the left handle"), Marker))
	{
		TestTrue(TEXT("the marker is visible on a free lever"), Marker->IsVisible());
		TestTrue(FString::Printf(TEXT("the marker sits on the handle socket (%.2f cm off)"), FVector::Dist(Marker->GetComponentLocation(), Grab)),
			FVector::Dist(Marker->GetComponentLocation(), Grab) < 0.1f);
	}
	TestFalse(TEXT("a hand out of reach is not flagged in reach"), Access::InReach(Driver, true));
	Access::Grip(Driver, true, true);
	TestFalse(TEXT("an out-of-reach grip takes nothing"), Access::Held(Driver, true));
	TestTrue(FString::Printf(TEXT("a missed grip half-closes the hand (grasp %.2f)"), Access::Grasp(Driver, true)),
		FMath::IsNearlyEqual(Access::Grasp(Driver, true), Access::MissGrasp(Driver), 0.01f));
	Access::Grip(Driver, true, false);
	TestTrue(TEXT("letting go opens the hand"), FMath::IsNearlyZero(Access::Grasp(Driver, true), 0.01f));

	// --- In reach: flagged, grabbed, fist, hand drawn on the handle, marker hidden -----------------
	const FVector Gap(0.f, 0.f, 6.f);
	Hand->SetWorldLocation(Grab + Gap);
	Access::Feedback(Driver);
	TestTrue(TEXT("a hand within reach is flagged in reach"), Access::InReach(Driver, true));
	const FVector RestRelative = HandMesh->GetRelativeLocation();
	const FVector HandMeshBefore = HandMesh->GetComponentLocation();

	Access::Grip(Driver, true, true);
	TestTrue(TEXT("a grip in reach takes the lever"), Access::Held(Driver, true));
	TestTrue(FString::Printf(TEXT("holding a lever closes the hand (grasp %.2f)"), Access::Grasp(Driver, true)),
		FMath::IsNearlyEqual(Access::Grasp(Driver, true), 1.f, 0.01f));
	Access::Update(Driver);
	Access::Feedback(Driver);
	const FVector Moved = HandMesh->GetComponentLocation() - HandMeshBefore;
	TestTrue(FString::Printf(TEXT("the drawn hand snaps onto the handle - moved by the controller-to-handle gap (%.2f cm off)"),
		FVector::Dist(Moved, -Gap)), FVector::Dist(Moved, -Gap) < 0.1f);
	if (Marker)
	{
		TestFalse(TEXT("the marker hides while the lever is held"), Marker->IsVisible());
	}
	TestTrue(TEXT("this machine's interior lever follows the hand"), Access::LeverOverride(Tank));

	// --- Release: hand back on the controller, marker back ----------------------------------------
	Access::Grip(Driver, true, false);
	Access::Update(Driver);
	Access::Feedback(Driver);
	TestTrue(FString::Printf(TEXT("released, the hand goes back on its controller (%.3f cm off)"),
		FVector::Dist(HandMesh->GetRelativeLocation(), RestRelative)), FVector::Dist(HandMesh->GetRelativeLocation(), RestRelative) < 0.01f);
	if (Marker)
	{
		TestTrue(TEXT("the marker comes back on release"), Marker->IsVisible());
	}

	// --- Leaving Manual clears every cue ----------------------------------------------------------
	PS->SetDriveControlMode(ETSDriveControlMode::Analog);
	if (Marker)
	{
		TestFalse(TEXT("leaving Manual hides the marker"), Marker->IsVisible());
	}
	TestFalse(TEXT("leaving Manual drops the lever override"), Access::LeverOverride(Tank));

	Fx.TearDown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
