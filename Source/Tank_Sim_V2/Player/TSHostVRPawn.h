// The host's body when their assigned ETSPlayMode is VR: the same free-roam match-admin camera as
// ATSHostCameraPawn, flown from the motion controllers, with the host's screen-space panels (the
// lobby console and the command-net voice panel) moved onto world-space panels in front of them.
//
// WHAT THE HOST CAN DO, BOTH WAYS
//   Desktop (ATSHostCameraPawn)        VR (this pawn)
//   WASD / Q E fly                      left stick flies along the head's heading
//   mouse look                          head look; right stick X snap-turns
//   Q / E up and down                   right stick Y climbs / descends
//   (fixed speed)                       right grip held = fly faster
//   lobby console, mouse cursor         the same widget on a floating panel, right-hand laser +
//                                       right trigger to click
//   voice panel (top right)             the same widget on a second panel beside it
//   V push to talk                      left trigger push to talk (V still works)
//   F1 console focus                    left Y / menu button shows or hides the panels
//   -                                   left menu button recentres the headset
// The keyboard bindings ADefaultPawn registers still work in VR, so somebody at the desk can fly
// the host while the headset is on.
//
// THE PANELS ARE THE CONTROLLER'S WIDGETS, NOT COPIES. ATSTankPlayerController owns the lobby console
// and the voice panel for every host, and hands those same instances to this pawn's widget
// components (RefreshHostPanelPlacement). One widget, one set of button handlers, one state - so the
// VR host cannot drift out of step with the desktop host, and switching modes mid-match keeps every
// selection the host had made.
//
// Placement is Blueprint data (RULE 8): PanelDistance / PanelHeightOffset / VoicePanelYawOffset and
// the widget components themselves can be tuned in a Blueprint subclass. The panels are pawn-locked,
// not head-locked: they ride along while flying and snap in front of the head when summoned, which
// is the comfortable option for a floating menu.
#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayTypes.h"
#include "Player/TSHostCameraPawn.h"
#include "TSHostVRPawn.generated.h"

class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UMotionControllerComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTSVRPointerComponent;
class UUserWidget;
class UWidgetComponent;
class UXRDeviceVisualizationComponent;
struct FInputActionValue;

// Which world-space panel a host widget belongs on.
UENUM(BlueprintType)
enum class ETSHostVRPanel : uint8
{
	Console,
	Voice
};

UCLASS()
class ATSHostVRPawn : public ATSHostCameraPawn
{
	GENERATED_BODY()

public:
	ATSHostVRPawn(const FObjectInitializer& ObjectInitializer);

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Puts a host widget on one of this pawn's world-space panels (removing it from wherever it was).
	// Called by ATSTankPlayerController, which owns the widget.
	void MountPanelWidget(ETSHostVRPanel Panel, UUserWidget* Widget);

	// Takes the widget off whichever panel holds it. Safe for a widget that is not mounted.
	void UnmountPanelWidget(UUserWidget* Widget);

	bool IsWidgetMounted(const UUserWidget* Widget) const;

	// Shows or hides both panels. Showing them re-places them in front of where the head is facing.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Host|VR")
	void SetPanelsShown(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host|VR")
	bool ArePanelsShown() const { return bPanelsShown; }

protected:
	virtual void ApplyHostDisplayModeDeferred() override;

	// --- Components -------------------------------------------------------------------------------

	// Tracking space. The camera and both hands are children, so snap turns and flight move the whole
	// tracked volume together.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UMotionControllerComponent> LeftHand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UMotionControllerComponent> RightHand;

	// The runtime's own controller models (OpenXR render models), so the hands are visible with no
	// mesh asset to author - the same source the VR template uses.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UXRDeviceVisualizationComponent> LeftHandModel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UXRDeviceVisualizationComponent> RightHandModel;

	// Right-hand laser for the panels. Custom hit source, so the host's own (collision-less) pawn and
	// the world never block it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UTSVRPointerComponent> LaserPointer;

	// Visible beam for the laser. Shown only while it is over a panel, so it does not clutter the
	// view while flying.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UStaticMeshComponent> LaserBeam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UWidgetComponent> ConsolePanel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host|VR")
	TObjectPtr<UWidgetComponent> VoicePanel;

	// --- Input (soft, loaded at possession - RULE 2) ---------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputMappingContext> HostVRMappingContext;

	// Right trigger -> IA_Primary (laser click) and left Y -> IA_Menu (panels). Shared with the crew
	// pawns' widget pointer.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputMappingContext> VRWidgetMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> FlyAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> TurnLiftAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> FlyFastAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> TalkAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> ClickAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> MenuAction;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Input")
	TSoftObjectPtr<UInputAction> RecenterAction;

	// --- Tuning -----------------------------------------------------------------------------------

	// Local: tracking is centred on the headset's start pose, so the host's head is where the pawn is
	// - the same eye point the flat free-cam flies.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR")
	TEnumAsByte<EHMDTrackingOrigin::Type> VRTrackingOrigin = EHMDTrackingOrigin::Local;

	// Flight speed in VR, cm/s. Lower than the desktop 4000 on purpose: fast smooth locomotion is the
	// main cause of VR sickness.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "100.0"))
	float VRFlySpeed = 1500.f;

	// Speed while the right grip is held.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "100.0"))
	float VRFastFlySpeed = 4000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float StickDeadZone = 0.2f;

	// Snap turn step. Snap rather than smooth for the same comfort reason as VRFlySpeed.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float SnapTurnDegrees = 30.f;

	// Stick deflection that fires a snap turn, and the one it has to drop back under to re-arm.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SnapTurnThreshold = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SnapTurnRearmThreshold = 0.3f;

	// Where the console panel appears when summoned, relative to the head: this far ahead...
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels", meta = (ClampMin = "30.0"))
	float PanelDistance = 150.f;

	// ...and this far below eye level.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	float PanelHeightOffset = -20.f;

	// The voice panel sits to the right of the console, turned this far (degrees, around the head).
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	float VoicePanelYawOffset = 38.f;

	// Panels are visible as soon as the host enters VR, like the desktop console on arrival.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	bool bShowPanelsOnEnterVR = true;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	TSoftObjectPtr<UStaticMesh> LaserBeamMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	TSoftObjectPtr<UMaterialInterface> LaserBeamMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	FName LaserBeamColorParameter = TEXT("Color");

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels")
	FLinearColor LaserBeamColor = FLinearColor(0.2f, 0.8f, 1.f);

	// Beam thickness in cm.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host|VR Panels", meta = (ClampMin = "0.05"))
	float LaserBeamThickness = 0.4f;

private:
	void Input_Fly(const FInputActionValue& Value);
	void Input_TurnLift(const FInputActionValue& Value);
	void Input_TurnLiftReleased(const FInputActionValue& Value);
	void Input_FlyFastPressed(const FInputActionValue& Value);
	void Input_FlyFastReleased(const FInputActionValue& Value);
	void Input_TalkPressed(const FInputActionValue& Value);
	void Input_TalkReleased(const FInputActionValue& Value);
	void Input_ClickPressed(const FInputActionValue& Value);
	void Input_ClickReleased(const FInputActionValue& Value);
	void Input_Menu(const FInputActionValue& Value);
	void Input_Recenter(const FInputActionValue& Value);

	// Applies a dead zone and rescales, so a stick that rests at 0.1 reads zero.
	float ApplyDeadZone(float Axis) const;

	// Heading of the head in world space, yaw only.
	FRotator GetHeadYawRotation() const;

	// Moves both panels in front of the head's current heading.
	void PlacePanelsInFrontOfHead();

	void UpdatePanelVisibility();
	void UpdateLaserBeam();
	void SetFlyFast(bool bFast);
	void EnablePointer(bool bEnable);

	UWidgetComponent* GetPanelComponent(ETSHostVRPanel Panel) const;

	bool bPanelsShown = false;
	bool bSnapTurnArmed = true;
	// Set while the talk trigger is held.
	TWeakObjectPtr<class ATSTankPlayerController> TalkController;
	bool bVRActive = false;
};
