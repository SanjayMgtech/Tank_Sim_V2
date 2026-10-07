// One VR hand: fingertip POKE on world-space UI, and the hand's finger pose.
//
// POKE. The index fingertip presses a UWidgetComponent by passing through its plane, like a real
// touch screen - no laser, no trigger. It reuses UTSVRPointerComponent's Custom hit source (the stock
// World source ignores everything under the pawn's attachment root, which is the tank the panels are
// mounted on) and supplies the fingertip projected onto the panel as the hit point.
//
// POSE. Drives ABP_MannequinsXR's PoseAlpha* variables (Grasp / IndexCurl / Point / ThumbUp) from one
// of two sources, chosen per frame:
//   * optical hand tracking (Quest 3 with the controllers put down) - read from the XR_EXT_hand_tracking
//     joints, which also give the real fingertip and a thumb-index PINCH;
//   * the controller - grip and trigger axes plus the capacitive touch sensors, fed by the pawn
//     (SetControllerInput), exactly how the Meta hands animate a Touch controller.
//
// Everything here is local and cosmetic, so nothing replicates.
#pragma once

#include "CoreMinimal.h"
#include "UI/TSVRPointerComponent.h"
#include "TSVRHandComponent.generated.h"

class UWidgetComponent;
class USkeletalMeshComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FTSVRHandPokeEvent, bool /*bPressed*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FTSVRHandPinchEvent, bool /*bPinching*/);

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class TANK_SIM_V2_API UTSVRHandComponent : public UTSVRPointerComponent
{
	GENERATED_BODY()

public:
	UTSVRHandComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Which hand this is. Selects the bone suffix and the hand-tracking source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	bool bIsLeftHand = false;

	// Controller sensors. Either set directly, or give the component the input actions and it reads
	// their current values off the owning player's Enhanced Input every tick.
	void SetControllerInput(float InGrip, float InTrigger, bool bInTriggerTouched, bool bInThumbTouched);
	void SetInputActions(const UInputAction* InGrip, const UInputAction* InTrigger,
		const UInputAction* InTriggerTouch, const UInputAction* InThumbTouch);

	// Forces the grasp alpha (a held lever closes the fist). Negative = no override.
	void SetGraspOverride(float Alpha) { GraspOverride = Alpha; }

	// The hand mesh to pose while it is NOT under this component's motion controller - the crew pawn
	// re-attaches it to a held lever's socket. nullptr returns to the usual sibling lookup.
	void SetHandMeshOverride(USkeletalMeshComponent* Mesh) { HandMeshOverride = Mesh; }

	// Stops any press in progress and returns the hand to an open pose. Used when the hand is parked.
	void ResetHand();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR Hand")
	bool IsHandTracked() const { return bHandTracked; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR Hand")
	bool IsPinching() const { return bPinching; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR Hand")
	bool IsPoking() const { return bPokePressed; }

	// Fingertip in world space this frame (valid after the first tick).
	FVector GetFingertipLocation() const { return FingertipLocation; }

	FTSVRHandPokeEvent OnPoke;
	FTSVRHandPinchEvent OnPinch;

	// Pure poke maths, static so it can be unit tested without a world: signed penetration of the
	// fingertip through a panel, positive once the tip is past the plane on the side opposite to
	// ApproachSide. ApproachSide is +1/-1 along PlaneNormal.
	static float ComputePokeDepth(const FVector& Tip, const FVector& PlanePoint, const FVector& PlaneNormal, float ApproachSide);

	// Pure curl from three joints (metacarpal, proximal, tip): 0 = straight finger, 1 = tip folded back
	// ~150 degrees from the knuckle segment, i.e. a closed fist.
	static float ComputeFingerCurl(const FVector& Base, const FVector& Mid, const FVector& Tip);

	// --- Tuning ---------------------------------------------------------------------------------

	// Fingertip must come within this distance of a panel before it is hovered.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "1"))
	float PokeHoverDistance = 6.f;

	// How far past the panel the tip goes before it counts as a press (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float PokePressDepth = 0.4f;

	// How far back out it must come before the press releases (cm, in front of the panel). The gap
	// between the two is the hysteresis that stops a resting finger clicking repeatedly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float PokeReleaseDistance = 1.2f;

	// Past this depth the finger has gone through the panel, not pressed it - release.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "1"))
	float PokeMaxDepth = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float PokeCooldownSeconds = 0.15f;

	// The fingertip extends this far past the last index bone (the mesh has no tip bone).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float FingertipExtension = 2.2f;

	// Thumb tip to index tip distance (cm) that starts / ends a hand-tracked pinch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0.5"))
	float PinchStartDistance = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0.5"))
	float PinchEndDistance = 3.5f;

	// A dot drawn on the panel under the fingertip: where the press will land.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	bool bShowFingertipCursor = true;

	// Dot diameter (cm) at AutoPointDistance and at the surface.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0.1"))
	float CursorFarSize = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0.1"))
	float CursorNearSize = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	FLinearColor CursorHoverColor = FLinearColor(0.6f, 0.8f, 2.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	FLinearColor CursorPressedColor = FLinearColor(0.2f, 3.f, 0.4f);

	// Mesh/material for the dot (Blueprint data, handed over by the pawn). No mesh = no dot.
	void SetCursorAssets(class UStaticMesh* Mesh, class UMaterialInterface* Material, FName ColorParameter);

	// Within this distance in front of a panel the hand curls into a pointing pose on its own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "1"))
	float AutoPointDistance = 10.f;

	// Optical hand tracking takes over only after the controller's sensors have been idle this long.
	// While a controller is held, Meta's runtime reports a synthesised fist; the sensors are the truth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float ControllerIdleSecondsForHandTracking = 2.f;

	// Once-a-second [VRHands] log: pose source, sensors, nearest panel and poke depth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	bool bLogDiagnostics = true;

	// How quickly the drawn pose follows its target. 0 = instant.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand", meta = (ClampMin = "0"))
	float PoseInterpSpeed = 18.f;

	// Index bones on the hand mesh, without the _l/_r suffix.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	FName IndexMidBone = TEXT("index_02");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR Hand")
	FName IndexTipBone = TEXT("index_03");

private:
	struct FHandPose
	{
		float Grasp = 0.f;
		float IndexCurl = 0.f;
		float Point = 0.f;
		float ThumbUp = 0.f;
	};

	USkeletalMeshComponent* FindHandMesh() const;
	bool ReadHandTracking(FHandPose& OutPose);
	FHandPose PoseFromController() const;
	void ApplyPose(float DeltaTime, const FHandPose& Target);
	void UpdateFingertip(USkeletalMeshComponent* HandMesh);
	void UpdatePoke(float DeltaTime);
	void EndPoke();
	void SetAnimFloat(class UAnimInstance* Anim, FName Name, float Value);

	void ReadInputActions();
	TWeakObjectPtr<const UInputAction> GripAction;
	TWeakObjectPtr<const UInputAction> TriggerAction;
	TWeakObjectPtr<const UInputAction> TriggerTouchAction;
	TWeakObjectPtr<const UInputAction> ThumbTouchAction;

	// Controller input
	float Grip = 0.f;
	float Trigger = 0.f;
	bool bTriggerTouched = false;
	bool bThumbTouched = false;
	float GraspOverride = -1.f;

	TWeakObjectPtr<USkeletalMeshComponent> HandMeshOverride;

	FHandPose DrawnPose;

	// Hand tracking
	double LastControllerInputTime = 0.0;
	bool bTrackedPinch = false;
	bool bHandTracked = false;

	// Diagnostics
	double NextDiagLogTime = 0.0;
	FString DiagPanel;
	float DiagDistance = -1.f;
	float DiagDepth = 0.f;
	bool bDiagCanPoke = false;
	bool bPinching = false;
	FVector TrackedTip = FVector::ZeroVector;
	FVector TrackedTipDirection = FVector::ForwardVector;

	// Poke
	FVector FingertipLocation = FVector::ZeroVector;
	FVector FingertipDirection = FVector::ForwardVector;
	TWeakObjectPtr<UWidgetComponent> PokeTarget;
	float PokeApproachSide = 1.f;
	bool bPokePressed = false;
	bool bPokeArmed = false;
	bool bNearPanel = false;
	FVector PressPointLocal = FVector::ZeroVector;   // panel space
	FVector PrevTipLocal = FVector::ZeroVector;      // panel space
	bool bHavePrevTipLocal = false;

	// Fingertip cursor
	void UpdateCursor(UWidgetComponent* Panel, const FVector& HitPoint, const FVector& TowardViewer, float Depth);
	void HideCursor();
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> Cursor;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> CursorMID;
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMesh> CursorMeshAsset;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInterface> CursorMaterialAsset;
	FName CursorColorParameter = TEXT("Color");
	double LastPokeReleaseTime = -1.0;
};
