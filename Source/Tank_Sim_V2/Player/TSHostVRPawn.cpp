#include "Player/TSHostVRPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSVRModeLibrary.h"
#include "Tank_Sim_V2.h"
#include "UI/TSVRPointerComponent.h"
#include "XRDeviceVisualizationComponent.h"

namespace
{
	// Same asset paths as the crew VR input (Config/DefaultInput.ini registers IMC_HostVR with OpenXR).
	// Soft references only - nothing is loaded until the host actually possesses this pawn (RULE 2).
	const TCHAR* HostVRContextPath = TEXT("/Game/TankSimulation/Input/Contexts/IMC_HostVR.IMC_HostVR");
	const TCHAR* VRWidgetContextPath = TEXT("/Game/TankSimulation/Input/Contexts/IMC_VR_Widget.IMC_VR_Widget");
	const TCHAR* ActionsFolder = TEXT("/Game/TankSimulation/Input/Actions/");

	FSoftObjectPath ActionPath(const TCHAR* Name)
	{
		return FSoftObjectPath(FString::Printf(TEXT("%s%s.%s"), ActionsFolder, Name, Name));
	}

	// Scale from widget pixels to centimetres. 1000 px -> 1 m.
	constexpr float PanelWorldScale = 0.1f;
}

ATSHostVRPawn::ATSHostVRPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Snap turns and mouse yaw rotate the actor, which carries the tracked space round with it. Pitch
	// and roll stay with the head: turning the world under a headset is what makes people sick.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(RootComponent);

	// The base camera follows the control rotation for mouse look. In a headset the HEAD is the view,
	// so it moves under the tracked origin and composes the HMD pose instead.
	Camera->SetupAttachment(VROrigin);
	Camera->bUsePawnControlRotation = false;
	Camera->bLockToHmd = true;

	LeftHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(VROrigin);
	LeftHand->MotionSource = FName(TEXT("Left"));

	RightHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(VROrigin);
	RightHand->MotionSource = FName(TEXT("Right"));

	LeftHandModel = CreateDefaultSubobject<UXRDeviceVisualizationComponent>(TEXT("LeftHandModel"));
	LeftHandModel->SetupAttachment(LeftHand);
	LeftHandModel->DisplayModelSource = FName(TEXT("OpenXR"));
	LeftHandModel->bIsVisualizationActive = true;
	LeftHandModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RightHandModel = CreateDefaultSubobject<UXRDeviceVisualizationComponent>(TEXT("RightHandModel"));
	RightHandModel->SetupAttachment(RightHand);
	RightHandModel->DisplayModelSource = FName(TEXT("OpenXR"));
	RightHandModel->bIsVisualizationActive = true;
	RightHandModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LaserPointer = CreateDefaultSubobject<UTSVRPointerComponent>(TEXT("LaserPointer"));
	LaserPointer->SetupAttachment(RightHand);
	// Long enough to reach a panel summoned at PanelDistance with room to spare.
	LaserPointer->InteractionDistance = 600.f;
	// 0 is reserved for stock interaction components; the crew pawns use 1, 2 and 4. The host never
	// shares a pawn with them, but a distinct index costs nothing.
	LaserPointer->VirtualUserIndex = 3;
	LaserPointer->PointerIndex = 3;
	LaserPointer->bAutoActivate = false;
	LaserPointer->PrimaryComponentTick.bStartWithTickEnabled = false;

	LaserBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaserBeam"));
	LaserBeam->SetupAttachment(LaserPointer);
	LaserBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaserBeam->SetCastShadow(false);
	LaserBeam->SetHiddenInGame(true);
	// The cylinder's length runs along its local Z; pitch it onto the pointer's forward (X).
	LaserBeam->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));

	auto SetupPanel = [this](UWidgetComponent* Panel)
	{
		Panel->SetupAttachment(VROrigin);
		// World, not Screen: a screen-space widget draws to the flat viewport, which a headset cannot see.
		Panel->SetWidgetSpace(EWidgetSpace::World);
		Panel->SetTwoSided(true);
		Panel->SetRelativeScale3D(FVector(PanelWorldScale));
		// The laser traces Visibility; a panel that does not block it is visible but dead.
		Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
		Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Panel->SetGenerateOverlapEvents(false);
		Panel->SetHiddenInGame(true);
		Panel->SetVisibility(false);
		Panel->SetCastShadow(false);
		// Hangs from its top edge, so a console that grows as players join grows downwards.
		Panel->SetPivot(FVector2D(0.5f, 0.f));
	};

	ConsolePanel = CreateDefaultSubobject<UWidgetComponent>(TEXT("ConsolePanel"));
	SetupPanel(ConsolePanel);
	// The lobby console sizes itself (rows appear as players join), so render at whatever it asks for.
	ConsolePanel->SetDrawAtDesiredSize(true);
	ConsolePanel->SetDrawSize(FVector2D(960.f, 900.f));

	VoicePanel = CreateDefaultSubobject<UWidgetComponent>(TEXT("VoicePanel"));
	SetupPanel(VoicePanel);
	// The voice panel is a canvas anchored to its top-RIGHT corner (UTSHostVoicePanelWidget), which
	// has no meaningful desired size - so give it a fixed "screen" to be anchored in.
	VoicePanel->SetDrawAtDesiredSize(false);
	VoicePanel->SetDrawSize(FVector2D(400.f, 560.f));

	HostVRMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(HostVRContextPath));
	VRWidgetMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(VRWidgetContextPath));
	FlyAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_HostFly")));
	TurnLiftAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_HostTurnLift")));
	FlyFastAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_HostFlyFast")));
	TalkAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_HostTalk")));
	ClickAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_Primary")));
	MenuAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_Menu")));
	RecenterAction = TSoftObjectPtr<UInputAction>(ActionPath(TEXT("IA_Recenter")));

	LaserBeamMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	LaserBeamMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow")));
}

void ATSHostVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Keeps ADefaultPawn's keyboard/mouse flight, so the host can still be flown from the desk.
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSHostVRPawn: the input component is not an Enhanced Input component - VR controls are unbound."));
		return;
	}

	// Loaded here, at possession, never in the constructor (RULE 2). Each binding is guarded: a
	// missing asset drops that one control with a warning instead of taking the rest down with it.
	auto Bind = [this, EIC](const TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, void (ATSHostVRPawn::*Handler)(const FInputActionValue&))
	{
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			EIC->BindAction(Action, Event, this, Handler);
		}
		else
		{
			UE_LOG(LogTankSim, Warning, TEXT("ATSHostVRPawn: input action '%s' did not load - that VR control is unbound."),
				*Soft.ToString());
		}
	};

	Bind(FlyAction, ETriggerEvent::Triggered, &ATSHostVRPawn::Input_Fly);
	Bind(TurnLiftAction, ETriggerEvent::Triggered, &ATSHostVRPawn::Input_TurnLift);
	// Completed as well as Triggered: Triggered sends nothing on release, and the snap turn only
	// re-arms once the stick is seen back near the centre.
	Bind(TurnLiftAction, ETriggerEvent::Completed, &ATSHostVRPawn::Input_TurnLiftReleased);
	Bind(FlyFastAction, ETriggerEvent::Started, &ATSHostVRPawn::Input_FlyFastPressed);
	Bind(FlyFastAction, ETriggerEvent::Completed, &ATSHostVRPawn::Input_FlyFastReleased);
	Bind(FlyFastAction, ETriggerEvent::Canceled, &ATSHostVRPawn::Input_FlyFastReleased);
	// Both edges, or a microphone opened by the trigger never closes (the PC's V key has the same rule).
	Bind(TalkAction, ETriggerEvent::Started, &ATSHostVRPawn::Input_TalkPressed);
	Bind(TalkAction, ETriggerEvent::Completed, &ATSHostVRPawn::Input_TalkReleased);
	Bind(TalkAction, ETriggerEvent::Canceled, &ATSHostVRPawn::Input_TalkReleased);
	Bind(ClickAction, ETriggerEvent::Started, &ATSHostVRPawn::Input_ClickPressed);
	Bind(ClickAction, ETriggerEvent::Completed, &ATSHostVRPawn::Input_ClickReleased);
	Bind(ClickAction, ETriggerEvent::Canceled, &ATSHostVRPawn::Input_ClickReleased);
	Bind(MenuAction, ETriggerEvent::Started, &ATSHostVRPawn::Input_Menu);
	Bind(RecenterAction, ETriggerEvent::Started, &ATSHostVRPawn::Input_Recenter);

	const APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	for (const TSoftObjectPtr<UInputMappingContext>& Soft : { HostVRMappingContext, VRWidgetMappingContext })
	{
		if (UInputMappingContext* Context = Soft.LoadSynchronous())
		{
			// Priority 1, above the optional HostMappingContext the base adds at 0.
			Subsystem->AddMappingContext(Context, 1);
		}
		else
		{
			UE_LOG(LogTankSim, Warning, TEXT("ATSHostVRPawn: mapping context '%s' did not load - VR controls will not respond."),
				*Soft.ToString());
		}
	}
}

void ATSHostVRPawn::ApplyHostDisplayModeDeferred()
{
	ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// The server only hands this pawn to a host whose client reported a headset, but a headset can be
	// unplugged between that report and now. SetVRModeEnabled refuses in that case, and the host is
	// then treated as flat until the server moves them back to the desktop pawn.
	bVRActive = UTSVRModeLibrary::SetVRModeEnabled(true, VRTrackingOrigin);
	if (!bVRActive)
	{
		UE_LOG(LogTankSim, Warning, TEXT("ATSHostVRPawn: stereo did not start - the host stays on the flat screen."));
		EnablePointer(false);
		SetPanelsShown(false);
		Super::ApplyHostDisplayModeDeferred();
		return;
	}

	Camera->bLockToHmd = true;
	if (UTSVRModeLibrary::IsHeadTrackingActive())
	{
		UTSVRModeLibrary::RecenterHMD();
	}

	if (USpectatorPawnMovement* Movement = Cast<USpectatorPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = VRFlySpeed;
	}

	EnablePointer(true);

	// Hand the controller's lobby console and voice panel over to the world-space panels. Stereo is
	// already on, so the controller also drops the mouse cursor / UI input mode, which in a headset
	// would only swallow input.
	PC->RefreshHostPanelPlacement();

	SetPanelsShown(bShowPanelsOnEnterVR);

	UE_LOG(LogTankSim, Log, TEXT("ATSHostVRPawn: host is in VR - free camera on the motion controllers, panels %s."),
		bPanelsShown ? TEXT("shown") : TEXT("hidden"));
}

void ATSHostVRPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The widgets belong to the controller and outlive this pawn. Let go of them explicitly so the
	// desktop pawn can put them back in the viewport - a widget cannot sit in two places at once.
	if (ConsolePanel) { ConsolePanel->SetWidget(nullptr); }
	if (VoicePanel) { VoicePanel->SetWidget(nullptr); }

	// A microphone keyed by the trigger must not stay open because the pawn went away mid-press. The
	// controller is remembered from the press: by EndPlay after a mode swap it has already
	// possessed the other pawn, so GetController() is null here.
	if (ATSTankPlayerController* PC = TalkController.Get())
	{
		PC->StopVoiceTransmit();
	}
	TalkController = nullptr;

	Super::EndPlay(EndPlayReason);
}

void ATSHostVRPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bVRActive && IsLocallyControlled())
	{
		UpdateLaserBeam();
	}
}

// --- Panels ---------------------------------------------------------------------------------------

UWidgetComponent* ATSHostVRPawn::GetPanelComponent(ETSHostVRPanel Panel) const
{
	return Panel == ETSHostVRPanel::Voice ? VoicePanel.Get() : ConsolePanel.Get();
}

void ATSHostVRPawn::MountPanelWidget(ETSHostVRPanel Panel, UUserWidget* Widget)
{
	UWidgetComponent* Component = GetPanelComponent(Panel);
	if (!Component || !Widget)
	{
		return;
	}

	if (Component->GetWidget() == Widget)
	{
		return;
	}

	// Off the other panel first, in case it was ever mounted there.
	UnmountPanelWidget(Widget);

	Component->SetWidget(Widget);
	UpdatePanelVisibility();
}

void ATSHostVRPawn::UnmountPanelWidget(UUserWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	for (UWidgetComponent* Component : { ConsolePanel.Get(), VoicePanel.Get() })
	{
		if (Component && Component->GetWidget() == Widget)
		{
			Component->SetWidget(nullptr);
		}
	}
	UpdatePanelVisibility();
}

bool ATSHostVRPawn::IsWidgetMounted(const UUserWidget* Widget) const
{
	return Widget
		&& ((ConsolePanel && ConsolePanel->GetWidget() == Widget) || (VoicePanel && VoicePanel->GetWidget() == Widget));
}

void ATSHostVRPawn::SetPanelsShown(bool bShow)
{
	bPanelsShown = bShow;
	if (bShow)
	{
		PlacePanelsInFrontOfHead();
	}
	UpdatePanelVisibility();
}

void ATSHostVRPawn::UpdatePanelVisibility()
{
	for (UWidgetComponent* Component : { ConsolePanel.Get(), VoicePanel.Get() })
	{
		if (!Component)
		{
			continue;
		}

		const bool bShow = bPanelsShown && bVRActive && Component->GetWidget() != nullptr;
		Component->SetVisibility(bShow);
		Component->SetHiddenInGame(!bShow);
		// A hidden panel must not keep eating laser traces.
		Component->SetCollisionEnabled(bShow ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

FRotator ATSHostVRPawn::GetHeadYawRotation() const
{
	const FRotator Head = Camera ? Camera->GetComponentRotation() : GetActorRotation();
	return FRotator(0.f, Head.Yaw, 0.f);
}

void ATSHostVRPawn::PlacePanelsInFrontOfHead()
{
	if (!Camera || !VROrigin)
	{
		return;
	}

	// Worked out in world space from the head, then stored relative to VROrigin - so the panels stay
	// where they were summoned relative to the host while flying, and turn with a snap turn.
	const FVector HeadLocation = Camera->GetComponentLocation();
	const FRotator HeadYaw = GetHeadYawRotation();

	auto Place = [&](UWidgetComponent* Component, float YawOffset)
	{
		if (!Component)
		{
			return;
		}

		const FRotator Facing(0.f, HeadYaw.Yaw + YawOffset, 0.f);
		const FVector Location = HeadLocation + Facing.Vector() * PanelDistance + FVector(0.f, 0.f, PanelHeightOffset);

		// A widget component shows its front along its +X axis, so turn it back to face the head.
		Component->SetWorldLocationAndRotation(Location, FRotator(0.f, Facing.Yaw + 180.f, 0.f));
	};

	Place(ConsolePanel, 0.f);
	Place(VoicePanel, VoicePanelYawOffset);
}

// --- Laser ------------------------------------------------------------------------------------------

void ATSHostVRPawn::EnablePointer(bool bEnable)
{
	if (LaserPointer)
	{
		LaserPointer->SetActive(bEnable);
		LaserPointer->SetComponentTickEnabled(bEnable);
	}

	if (!bEnable && LaserBeam)
	{
		LaserBeam->SetHiddenInGame(true);
	}

	if (bEnable && LaserBeam && !LaserBeam->GetStaticMesh())
	{
		if (UStaticMesh* Mesh = LaserBeamMesh.LoadSynchronous())
		{
			LaserBeam->SetStaticMesh(Mesh);
		}
		if (UMaterialInterface* Material = LaserBeamMaterial.LoadSynchronous())
		{
			if (UMaterialInstanceDynamic* MID = LaserBeam->CreateDynamicMaterialInstance(0, Material))
			{
				MID->SetVectorParameterValue(LaserBeamColorParameter, LaserBeamColor);
			}
		}
	}
}

void ATSHostVRPawn::UpdateLaserBeam()
{
	if (!LaserPointer || !LaserBeam)
	{
		return;
	}

	// Only while it would do something: a beam permanently out of the hand clutters the view while
	// flying, and pointing at a panel is when the host needs to see where it lands.
	const bool bOverPanel = LaserPointer->IsActive() && bPanelsShown && LaserPointer->IsPointingAtWidget();
	LaserBeam->SetHiddenInGame(!bOverPanel);
	if (!bOverPanel)
	{
		return;
	}

	const float Length = FMath::Max(1.f, LaserPointer->GetLastHitResult().Distance);

	// The engine cylinder is 100 units tall and 100 across, centred on its pivot.
	LaserBeam->SetRelativeLocation(FVector(Length * 0.5f, 0.f, 0.f));
	LaserBeam->SetRelativeScale3D(FVector(LaserBeamThickness / 100.f, LaserBeamThickness / 100.f, Length / 100.f));
}

// --- Input ------------------------------------------------------------------------------------------

float ATSHostVRPawn::ApplyDeadZone(float Axis) const
{
	const float Magnitude = FMath::Abs(Axis);
	if (Magnitude <= StickDeadZone)
	{
		return 0.f;
	}
	return FMath::Sign(Axis) * FMath::Clamp((Magnitude - StickDeadZone) / (1.f - StickDeadZone), 0.f, 1.f);
}

void ATSHostVRPawn::Input_Fly(const FInputActionValue& Value)
{
	const FVector2D Stick = Value.Get<FVector2D>();
	const float Forward = ApplyDeadZone(Stick.Y);
	const float Right = ApplyDeadZone(Stick.X);

	// Along where the head is facing, flattened: looking down at the battlefield should not dive the
	// host into it. Height is the right stick's job.
	const FRotator HeadYaw = GetHeadYawRotation();
	if (Forward != 0.f)
	{
		AddMovementInput(FRotationMatrix(HeadYaw).GetUnitAxis(EAxis::X), Forward);
	}
	if (Right != 0.f)
	{
		AddMovementInput(FRotationMatrix(HeadYaw).GetUnitAxis(EAxis::Y), Right);
	}
}

void ATSHostVRPawn::Input_TurnLift(const FInputActionValue& Value)
{
	const FVector2D Stick = Value.Get<FVector2D>();

	const float Lift = ApplyDeadZone(Stick.Y);
	if (Lift != 0.f)
	{
		AddMovementInput(FVector::UpVector, Lift);
	}

	// Snap turn: one step per deflection, re-armed once the stick comes back towards the centre.
	const float Turn = Stick.X;
	if (bSnapTurnArmed && FMath::Abs(Turn) >= SnapTurnThreshold)
	{
		bSnapTurnArmed = false;
		if (AController* OwningController = GetController())
		{
			FRotator Control = OwningController->GetControlRotation();
			Control.Yaw += FMath::Sign(Turn) * SnapTurnDegrees;
			OwningController->SetControlRotation(Control);
		}
	}
	else if (!bSnapTurnArmed && FMath::Abs(Turn) <= SnapTurnRearmThreshold)
	{
		bSnapTurnArmed = true;
	}
}

void ATSHostVRPawn::Input_TurnLiftReleased(const FInputActionValue& Value)
{
	bSnapTurnArmed = true;
}

void ATSHostVRPawn::SetFlyFast(bool bFast)
{
	if (USpectatorPawnMovement* Movement = Cast<USpectatorPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = bFast ? VRFastFlySpeed : VRFlySpeed;
	}
}

void ATSHostVRPawn::Input_FlyFastPressed(const FInputActionValue& Value)
{
	SetFlyFast(true);
}

void ATSHostVRPawn::Input_FlyFastReleased(const FInputActionValue& Value)
{
	SetFlyFast(false);
}

void ATSHostVRPawn::Input_TalkPressed(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(GetController()))
	{
		TalkController = PC;
		PC->StartVoiceTransmit();
	}
}

void ATSHostVRPawn::Input_TalkReleased(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = TalkController.Get())
	{
		PC->StopVoiceTransmit();
	}
	TalkController = nullptr;
}

void ATSHostVRPawn::Input_ClickPressed(const FInputActionValue& Value)
{
	if (LaserPointer && LaserPointer->IsActive())
	{
		LaserPointer->PressPointer();
	}
}

void ATSHostVRPawn::Input_ClickReleased(const FInputActionValue& Value)
{
	if (LaserPointer && LaserPointer->IsActive())
	{
		LaserPointer->ReleasePointer();
	}
}

void ATSHostVRPawn::Input_Menu(const FInputActionValue& Value)
{
	if (bVRActive)
	{
		SetPanelsShown(!bPanelsShown);
	}
}

void ATSHostVRPawn::Input_Recenter(const FInputActionValue& Value)
{
	UTSVRModeLibrary::RecenterHMD();
}
