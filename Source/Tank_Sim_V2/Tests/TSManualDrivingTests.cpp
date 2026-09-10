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
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "MotionControllerComponent.h"
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
		ATSCrewPawn* MakeCrew(ATSTankPlayerController*& OutPC) const
		{
			UClass* PawnClass = LoadClass<ATSCrewPawn>(nullptr, DesktopPawnClassPath);
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

		void TearDown()
		{
			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance = nullptr;
			}
			World = nullptr;
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
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Access::TickInterior(Tank, 1.f / 30.f);
	}
	TestTrue(FString::Printf(TEXT("interior RIGHT lever alpha follows (got %.3f)"), Tank->GetInteriorRightLeverAlpha()),
		FMath::IsNearlyEqual(Tank->GetInteriorRightLeverAlpha(), 0.5f, 0.02f));
	TestTrue(TEXT("interior LEFT lever stays at rest while steering right"), Tank->GetInteriorLeftLeverAlpha() < 0.02f);
	const float RestRoll = Tank->RightLeverRestRotation.Roll;
	const float PulledRoll = Tank->RightLeverPulledRotation.Roll;
	const float GotRoll = Tank->GetInteriorLeverRotation(false).Roll;
	TestTrue(FString::Printf(TEXT("the right lever BONE sits halfway between its measured poses (%.2f, expect %.2f)"),
		GotRoll, (RestRoll + PulledRoll) * 0.5f), FMath::IsNearlyEqual(GotRoll, (RestRoll + PulledRoll) * 0.5f, 0.2f));

	// --- Releasing and leaving -------------------------------------------------------------------
	Access::Grip(Driver, false, false);
	Access::Update(Driver);
	TestTrue(FString::Printf(TEXT("releasing the right lever springs it back (got %.2f)"), Input().Y), Near(Input(), FVector2D(0.f, -0.5f)));

	DriverPS->SetDriveControlMode(ETSDriveControlMode::Analog);
	TestFalse(TEXT("leaving Manual releases a lever still held"), Access::Held(Driver, true));
	TestTrue(FString::Printf(TEXT("leaving Manual mid-pull sends the terminal STOP (got %.2f, %.2f)"), Input().X, Input().Y),
		Near(Input(), FVector2D::ZeroVector));

	Fx.TearDown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
