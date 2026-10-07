#include "UI/TSVRHandComponent.h"

#include "Animation/AnimInstance.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayTypes.h"
#include "InputCoreTypes.h"
#include "Engine/OverlapResult.h"
#include "WorldCollision.h"
#include "Tank_Sim_V2.h"

UTSVRHandComponent::UTSVRHandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = false;

	// The laser path of the base class is never used here - the hit point comes from the fingertip.
	InteractionDistance = 20.f;
	bShowDebug = false;
}

float UTSVRHandComponent::ComputePokeDepth(const FVector& Tip, const FVector& PlanePoint, const FVector& PlaneNormal, float ApproachSide)
{
	// Distance in front of the plane on the approach side is negative; past it, positive.
	return -ApproachSide * FVector::DotProduct(Tip - PlanePoint, PlaneNormal);
}

float UTSVRHandComponent::ComputeFingerCurl(const FVector& Base, const FVector& Mid, const FVector& Tip)
{
	const FVector A = (Mid - Base).GetSafeNormal();
	const FVector B = (Tip - Mid).GetSafeNormal();
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		return 0.f;
	}
	// Straight finger: the two segments are parallel (0 deg). A closed fist folds the tip back on the
	// knuckle, ~150 deg between knuckle segment and tip direction.
	const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.f, 1.f)));
	return FMath::Clamp(Angle / 150.f, 0.f, 1.f);
}

void UTSVRHandComponent::SetControllerInput(float InGrip, float InTrigger, bool bInTriggerTouched, bool bInThumbTouched)
{
	Grip = FMath::Clamp(InGrip, 0.f, 1.f);
	Trigger = FMath::Clamp(InTrigger, 0.f, 1.f);
	bTriggerTouched = bInTriggerTouched;
	bThumbTouched = bInThumbTouched;
}

void UTSVRHandComponent::SetInputActions(const UInputAction* InGrip, const UInputAction* InTrigger,
	const UInputAction* InTriggerTouch, const UInputAction* InThumbTouch)
{
	GripAction = InGrip;
	TriggerAction = InTrigger;
	TriggerTouchAction = InTriggerTouch;
	ThumbTouchAction = InThumbTouch;
}

void UTSVRHandComponent::ReadInputActions()
{
	// Read the action VALUES rather than binding callbacks: the hand needs a level every frame, and a
	// Triggered binding says nothing on release (see the IA_Drive note in CLAUDE.md).
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UEnhancedPlayerInput* PlayerInputPtr = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
	if (!PlayerInputPtr)
	{
		return;
	}

	auto Axis = [PlayerInputPtr](const TWeakObjectPtr<const UInputAction>& Action)
	{
		return Action.IsValid() ? PlayerInputPtr->GetActionValue(Action.Get()).Get<float>() : 0.f;
	};
	auto Held = [PlayerInputPtr](const TWeakObjectPtr<const UInputAction>& Action, bool bDefault)
	{
		return Action.IsValid() ? PlayerInputPtr->GetActionValue(Action.Get()).Get<bool>() : bDefault;
	};

	// With no touch action assigned, assume the finger rests on the trigger and the thumb on the
	// stick: a relaxed hand, rather than a permanent point and thumbs-up.
	SetControllerInput(Axis(GripAction), Axis(TriggerAction), Held(TriggerTouchAction, true), Held(ThumbTouchAction, true));
}

USkeletalMeshComponent* UTSVRHandComponent::FindHandMesh() const
{
	if (USkeletalMeshComponent* Override = HandMeshOverride.Get())
	{
		return Override;
	}

	// The hand mesh is a sibling under the same motion controller (BP_MannequinsXR on BP_TSVRPawn).
	const USceneComponent* Parent = GetAttachParent();
	if (!Parent)
	{
		return nullptr;
	}
	for (USceneComponent* Child : Parent->GetAttachChildren())
	{
		if (USkeletalMeshComponent* HandMesh = Cast<USkeletalMeshComponent>(Child))
		{
			return HandMesh;
		}
	}
	return nullptr;
}

bool UTSVRHandComponent::ReadHandTracking(FHandPose& OutPose)
{
	FXRHandTrackingState State;
	UHeadMountedDisplayFunctionLibrary::GetHandTrackingState(this, EXRSpaceType::UnrealWorldSpace,
		bIsLeftHand ? EControllerHand::Left : EControllerHand::Right, State);

	if (!State.bValid || State.TrackingStatus == ETrackingStatus::NotTracked || State.HandKeyLocations.Num() < EHandKeypointCount)
	{
		bTrackedPinch = false;
		return false;
	}

	const TArray<FVector>& K = State.HandKeyLocations;
	auto At = [&K](EHandKeypoint Key) { return K[static_cast<int32>(Key)]; };
	auto Curl = [&](EHandKeypoint Meta, EHandKeypoint Prox, EHandKeypoint Tip)
	{
		return ComputeFingerCurl(At(Meta), At(Prox), At(Tip));
	};

	const float IndexCurl = Curl(EHandKeypoint::IndexMetacarpal, EHandKeypoint::IndexProximal, EHandKeypoint::IndexTip);
	const float MiddleCurl = Curl(EHandKeypoint::MiddleMetacarpal, EHandKeypoint::MiddleProximal, EHandKeypoint::MiddleTip);
	const float RingCurl = Curl(EHandKeypoint::RingMetacarpal, EHandKeypoint::RingProximal, EHandKeypoint::RingTip);
	const float LittleCurl = Curl(EHandKeypoint::LittleMetacarpal, EHandKeypoint::LittleProximal, EHandKeypoint::LittleTip);
	const float ThumbCurl = Curl(EHandKeypoint::ThumbMetacarpal, EHandKeypoint::ThumbProximal, EHandKeypoint::ThumbTip);

	OutPose.Grasp = (MiddleCurl + RingCurl + LittleCurl) / 3.f;
	OutPose.IndexCurl = IndexCurl;
	OutPose.Point = 1.f - IndexCurl;
	OutPose.ThumbUp = 1.f - ThumbCurl;

	TrackedTip = At(EHandKeypoint::IndexTip);
	TrackedTipDirection = (TrackedTip - At(EHandKeypoint::IndexDistal)).GetSafeNormal();

	// Thumb-to-index pinch with hysteresis, so a pinch held at the threshold does not chatter.
	const float PinchDistance = FVector::Dist(At(EHandKeypoint::ThumbTip), TrackedTip);
	bTrackedPinch = bTrackedPinch ? PinchDistance < PinchEndDistance : PinchDistance < PinchStartDistance;
	return true;
}

UTSVRHandComponent::FHandPose UTSVRHandComponent::PoseFromController() const
{
	// The Touch controller's own convention: grip curls the lower three fingers, the trigger curls
	// the index, and lifting a finger off a capacitive sensor extends it.
	FHandPose Pose;
	Pose.Grasp = Grip;
	Pose.IndexCurl = Trigger;
	Pose.Point = bTriggerTouched ? 0.f : 1.f;
	Pose.ThumbUp = bThumbTouched ? 0.f : 1.f;
	return Pose;
}

void UTSVRHandComponent::SetAnimFloat(UAnimInstance* Anim, FName Name, float Value)
{
	// By name: ABP_MannequinsXR is a Blueprint type C++ cannot name. "real" is a double since UE 5.0.
	FProperty* Prop = Anim->GetClass()->FindPropertyByName(Name);
	if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue_InContainer(Anim, Value);
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue_InContainer(Anim, Value);
	}
}

void UTSVRHandComponent::ApplyPose(float DeltaTime, const FHandPose& Target)
{
	auto Step = [&](float Current, float Goal)
	{
		return PoseInterpSpeed > 0.f ? FMath::FInterpTo(Current, Goal, DeltaTime, PoseInterpSpeed) : Goal;
	};
	DrawnPose.Grasp = Step(DrawnPose.Grasp, Target.Grasp);
	DrawnPose.IndexCurl = Step(DrawnPose.IndexCurl, Target.IndexCurl);
	DrawnPose.Point = Step(DrawnPose.Point, Target.Point);
	DrawnPose.ThumbUp = Step(DrawnPose.ThumbUp, Target.ThumbUp);

	const USkeletalMeshComponent* HandMesh = FindHandMesh();
	UAnimInstance* Anim = HandMesh ? HandMesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}
	SetAnimFloat(Anim, TEXT("PoseAlphaGrasp"), DrawnPose.Grasp);
	SetAnimFloat(Anim, TEXT("PoseAlphaIndexCurl"), DrawnPose.IndexCurl);
	SetAnimFloat(Anim, TEXT("PoseAlphaPoint"), DrawnPose.Point);
	SetAnimFloat(Anim, TEXT("PoseAlphaThumbUp"), DrawnPose.ThumbUp);
}

void UTSVRHandComponent::UpdateFingertip(USkeletalMeshComponent* HandMesh)
{
	if (bHandTracked)
	{
		FingertipLocation = TrackedTip;
		FingertipDirection = TrackedTipDirection;
		return;
	}

	const TCHAR* Suffix = bIsLeftHand ? TEXT("_l") : TEXT("_r");
	const FName Mid(*(IndexMidBone.ToString() + Suffix));
	const FName Tip(*(IndexTipBone.ToString() + Suffix));
	if (HandMesh && HandMesh->GetBoneIndex(Mid) != INDEX_NONE && HandMesh->GetBoneIndex(Tip) != INDEX_NONE)
	{
		const FVector MidLoc = HandMesh->GetBoneLocation(Mid);
		const FVector TipLoc = HandMesh->GetBoneLocation(Tip);
		FingertipDirection = (TipLoc - MidLoc).GetSafeNormal();
		FingertipLocation = TipLoc + FingertipDirection * FingertipExtension;
		return;
	}

	// No hand mesh: a point a little ahead of the controller.
	const USceneComponent* Parent = GetAttachParent();
	const FTransform T = Parent ? Parent->GetComponentTransform() : GetComponentTransform();
	FingertipDirection = T.GetUnitAxis(EAxis::X);
	FingertipLocation = T.GetLocation() + FingertipDirection * 8.f;
}

void UTSVRHandComponent::UpdatePoke(float DeltaTime)
{
	CustomHitResult = FHitResult();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Any hand pokes except one holding a lever. It used to require an extended index (finger OFF the
	// trigger), and a player holding a controller naturally keeps it on the trigger - the fingertip
	// reached the panel centre and nothing happened. Now the hand points BY ITSELF near a panel
	// (bNearPanel -> auto-point pose in TickComponent), the way Meta's interaction SDK does it.
	const bool bCanPoke = GraspOverride < 0.99f;
	bDiagCanPoke = bCanPoke;
	bNearPanel = false;

	// The side the player is looking from. A panel is only pressed from that face, so a hand resting
	// BEHIND a screen (common: the driver panel sits nearer the face than the resting hands) cannot
	// press it by being pulled back.
	FVector ViewLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				ViewLocation = PC->PlayerCameraManager->GetCameraLocation();
			}
		}
	}
	DiagPanel.Reset();
	DiagDistance = -1.f;
	DiagDepth = 0.f;

	struct FCandidate
	{
		UWidgetComponent* Widget = nullptr;
		FVector Projected = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
		float Distance = 0.f;   // signed, along Normal
	};

	auto Evaluate = [this](UWidgetComponent* Widget, FCandidate& Out, float EdgeMargin)
	{
		const FVector Normal = Widget->GetForwardVector();
		const FVector Point = Widget->GetComponentLocation();
		const float Distance = FVector::DotProduct(FingertipLocation - Point, Normal);
		const FVector Projected = FingertipLocation - Normal * Distance;

		FVector2D Local;
		Widget->GetLocalHitLocation(Projected, Local);
		const FVector2D Size = Widget->GetCurrentDrawSize();
		if (Local.X < -EdgeMargin || Local.Y < -EdgeMargin || Local.X > Size.X + EdgeMargin || Local.Y > Size.Y + EdgeMargin)
		{
			return false;
		}
		Out.Widget = Widget;
		Out.Projected = Projected;
		Out.Normal = Normal;
		Out.Distance = Distance;
		return true;
	};

	FCandidate Best;
	bool bHave = false;

	if (bPokePressed)
	{
		// Stay on the panel being pressed; a little edge slack so a press at a border is not dropped.
		UWidgetComponent* Target = PokeTarget.Get();
		bHave = Target && Target->IsVisible() && Evaluate(Target, Best, 20.f);
	}
	else if (bCanPoke)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TSVRHandPoke), false);
		World->OverlapMultiByChannel(Overlaps, FingertipLocation, FQuat::Identity, TraceChannel,
			FCollisionShape::MakeSphere(FMath::Max3(PokeHoverDistance, PokeMaxDepth, AutoPointDistance)), Params);

		float BestAbs = TNumericLimits<float>::Max();
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UWidgetComponent* Widget = Cast<UWidgetComponent>(Overlap.GetComponent());
			FCandidate C;
			if (Widget && Widget->IsVisible() && Evaluate(Widget, C, 40.f) && FMath::Abs(C.Distance) < BestAbs)
			{
				BestAbs = FMath::Abs(C.Distance);
				Best = C;
				bHave = true;
			}
		}
	}

	if (!bHave)
	{
		bPokeArmed = false;
		bHavePrevTipLocal = false;
		HideCursor();
		if (bPokePressed)
		{
			EndPoke();
		}
		PokeTarget.Reset();
		return;
	}

	// The pressable face is the one facing the viewer.
	if (PokeTarget.Get() != Best.Widget)
	{
		PokeTarget = Best.Widget;
		bPokeArmed = false;
		bHavePrevTipLocal = false;
	}
	PokeApproachSide = FVector::DotProduct(ViewLocation - Best.Projected, Best.Normal) >= 0.f ? 1.f : -1.f;
	const float Depth = ComputePokeDepth(FingertipLocation, Best.Projected, Best.Normal, PokeApproachSide);

	// Point the hand while it is in front of a panel and close to it (or pressing it).
	bNearPanel = bCanPoke && Depth > -AutoPointDistance && Depth <= PokeMaxDepth;

	// Armed only by being IN FRONT first, so a finger that arrives from behind or from the side
	// already through the plane never presses.
	if (!bPokePressed && Depth < -PokePressDepth && Depth > -FMath::Max(PokeHoverDistance, AutoPointDistance))
	{
		bPokeArmed = true;
	}

	DiagPanel = Best.Widget->GetName();
	DiagDistance = Best.Distance;
	DiagDepth = Depth;

	// Everything below is in the PANEL's local space (plane X = 0), so a moving tank carries the press
	// point and the fingertip history with it.
	const FTransform PanelTransform = Best.Widget->GetComponentTransform();
	const FVector TipLocal = PanelTransform.InverseTransformPositionNoScale(FingertipLocation);

	const double Now = World->GetRealTimeSeconds();
	if (!bPokePressed)
	{
		if (bCanPoke && bPokeArmed && Depth >= PokePressDepth && Depth <= PokeMaxDepth
			&& Now - LastPokeReleaseTime >= PokeCooldownSeconds)
		{
			bPokePressed = true;
			bPokeArmed = false;

			// Press where the finger CROSSED the screen, not where it ended up after pushing through:
			// interpolate last frame's tip and this one to the plane.
			FVector Crossing(0.f, TipLocal.Y, TipLocal.Z);
			if (bHavePrevTipLocal && FMath::Sign(PrevTipLocal.X) != FMath::Sign(TipLocal.X) && !FMath::IsNearlyEqual(PrevTipLocal.X, TipLocal.X))
			{
				const float Alpha = PrevTipLocal.X / (PrevTipLocal.X - TipLocal.X);
				Crossing = FMath::Lerp(PrevTipLocal, TipLocal, Alpha);
				Crossing.X = 0.f;
			}
			PressPointLocal = Crossing;
		}
	}
	else if (Depth < -PokeReleaseDistance || Depth > PokeMaxDepth || !bCanPoke)
	{
		EndPoke();
	}
	PrevTipLocal = TipLocal;
	bHavePrevTipLocal = true;

	// Hover follows the fingertip. A PRESS is pinned to the point it went in at, like a touchscreen:
	// a finger pushed 3-7cm through a screen drifts sideways, and a button only clicks if the release
	// lands on it - the drift was why most presses did nothing.
	const FVector HitPoint = bPokePressed ? PanelTransform.TransformPositionNoScale(PressPointLocal) : Best.Projected;
	CustomHitResult.bBlockingHit = true;
	CustomHitResult.Component = Best.Widget;
	CustomHitResult.ImpactPoint = HitPoint;
	CustomHitResult.Location = HitPoint;
	CustomHitResult.ImpactNormal = Best.Normal * PokeApproachSide;
	CustomHitResult.Normal = CustomHitResult.ImpactNormal;
	CustomHitResult.TraceStart = HitPoint + CustomHitResult.ImpactNormal * 5.f;
	CustomHitResult.TraceEnd = HitPoint - CustomHitResult.ImpactNormal * 5.f;
	CustomHitResult.Distance = 5.f;

	UpdateCursor(Best.Widget, HitPoint, Best.Normal * PokeApproachSide, Depth);
}

void UTSVRHandComponent::SetCursorAssets(UStaticMesh* Mesh, UMaterialInterface* Material, FName ColorParameter)
{
	CursorMeshAsset = Mesh;
	CursorMaterialAsset = Material;
	CursorColorParameter = ColorParameter;
}

void UTSVRHandComponent::HideCursor()
{
	if (Cursor)
	{
		Cursor->SetVisibility(false);
	}
}

void UTSVRHandComponent::UpdateCursor(UWidgetComponent* Panel, const FVector& HitPoint, const FVector& TowardViewer, float Depth)
{
	// Only while in front of the panel and close, or pressing. Further away it is just noise.
	const bool bShow = bShowFingertipCursor && CursorMeshAsset && (bPokePressed || (Depth < 0.f && Depth > -AutoPointDistance));
	if (!bShow)
	{
		HideCursor();
		return;
	}

	if (!Cursor)
	{
		Cursor = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None);
		Cursor->SetStaticMesh(CursorMeshAsset);
		Cursor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cursor->SetGenerateOverlapEvents(false);
		Cursor->SetCastShadow(false);
		Cursor->SetUsingAbsoluteScale(true);
		Cursor->RegisterComponent();
		if (CursorMaterialAsset)
		{
			CursorMID = UMaterialInstanceDynamic::Create(CursorMaterialAsset, this);
			Cursor->SetMaterial(0, CursorMID);
		}
	}

	// Attached to the panel (not placed in world space each tick) so it cannot lag a moving tank -
	// the same trap that made the lever markers flicker.
	if (Cursor->GetAttachParent() != Panel)
	{
		Cursor->AttachToComponent(Panel, FAttachmentTransformRules::KeepWorldTransform);
	}
	Cursor->SetWorldLocation(HitPoint + TowardViewer * 0.15f);

	// Shrinks as the finger closes in, so the dot itself says how far there is to go.
	const float Closeness = bPokePressed ? 1.f : 1.f - FMath::Clamp(-Depth / AutoPointDistance, 0.f, 1.f);
	const float SizeCm = FMath::Lerp(CursorFarSize, CursorNearSize, Closeness);
	const float MeshSize = FMath::Max(static_cast<float>(CursorMeshAsset->GetBounds().BoxExtent.GetMax()) * 2.f, 0.01f);
	Cursor->SetWorldScale3D(FVector(SizeCm / MeshSize));
	Cursor->SetVisibility(true);

	if (CursorMID)
	{
		CursorMID->SetVectorParameterValue(CursorColorParameter, bPokePressed ? CursorPressedColor : CursorHoverColor);
	}
}

void UTSVRHandComponent::EndPoke()
{
	if (!bPokePressed)
	{
		return;
	}
	bPokePressed = false;
	LastPokeReleaseTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	ReleasePointer();
	OnPoke.Broadcast(false);
}

void UTSVRHandComponent::ResetHand()
{
	EndPoke();
	PokeTarget.Reset();
	bPokeArmed = false;
	bNearPanel = false;
	bHavePrevTipLocal = false;
	HideCursor();
	CustomHitResult = FHitResult();
	GraspOverride = -1.f;
	Grip = Trigger = 0.f;
	bTriggerTouched = bThumbTouched = false;
	if (bPinching)
	{
		bPinching = false;
		OnPinch.Broadcast(false);
	}
	DrawnPose = FHandPose();
	ApplyPose(0.f, FHandPose());
}

void UTSVRHandComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// The controller's sensors always come first. Meta's runtime also reports hand JOINTS while the
	// controllers are held (XR_EXT_hand_tracking_data_source: a hand synthesised around the controller),
	// and that skeleton is a permanent fist - trusting it made both hands read grasp=1/point=0 for the
	// whole session, so the index never extended and nothing could be poked. Optical hand tracking is
	// used only once the controller has been idle long enough to mean it was put down.
	ReadInputActions();
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (Grip > 0.02f || Trigger > 0.02f || bTriggerTouched || bThumbTouched)
	{
		LastControllerInputTime = Now;
	}
	const bool bControllerIdle = Now - LastControllerInputTime >= ControllerIdleSecondsForHandTracking;

	FHandPose Tracked;
	const bool bTrackingValid = ReadHandTracking(Tracked);
	bHandTracked = bTrackingValid && bControllerIdle;

	FHandPose Target = bHandTracked ? Tracked : PoseFromController();

	// Pinch only means anything for real hands; a synthesised controller hand can read as pinching.
	const bool bNowPinching = bHandTracked && bTrackedPinch;
	if (bNowPinching != bPinching)
	{
		bPinching = bNowPinching;
		OnPinch.Broadcast(bPinching);
	}
	// Near a panel the hand points, whatever the finger is doing on the trigger. Last frame's
	// proximity, since the fingertip itself depends on this pose.
	if (bNearPanel)
	{
		Target.Point = 1.f;
		Target.IndexCurl = 0.f;
		Target.Grasp = FMath::Max(Target.Grasp, 0.8f);
	}
	if (GraspOverride >= 0.f)
	{
		Target.Grasp = FMath::Max(Target.Grasp, GraspOverride);
		if (GraspOverride >= 0.99f)
		{
			// A held lever wraps the whole hand into a fist: index curled and thumb folded over,
			// whatever the capacitive sensors say about where the real fingers are resting.
			Target.Point = 0.f;
			Target.IndexCurl = 1.f;
			Target.ThumbUp = 0.f;
		}
	}
	ApplyPose(DeltaTime, Target);

	UpdateFingertip(FindHandMesh());

	const bool bWasPressed = bPokePressed;
	UpdatePoke(DeltaTime);

	// Skip UTSVRPointerComponent's laser trace: the hit result was just built from the fingertip.
	// The base tick turns it into hover; the press goes AFTER it, so Slate already has the pointer
	// over the button it is pressing.
	UWidgetInteractionComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bPokePressed && !bWasPressed)
	{
		PressPointer();
		OnPoke.Broadcast(true);
		UE_LOG(LogTankSim, Log, TEXT("[VRHands] %s PRESS on %s"), bIsLeftHand ? TEXT("L") : TEXT("R"), *DiagPanel);
	}

	if (bLogDiagnostics && Now >= NextDiagLogTime)
	{
		NextDiagLogTime = Now + 1.0;
		UE_LOG(LogTankSim, Log,
			TEXT("[VRHands] %s src=%s trackValid=%d | grip=%.2f trig=%.2f trigTouch=%d thumbTouch=%d | pose grasp=%.2f point=%.2f | canPoke=%d panel=%s dist=%.1f depth=%.1f pressed=%d | tip=%s"),
			bIsLeftHand ? TEXT("L") : TEXT("R"), bHandTracked ? TEXT("HANDS") : TEXT("CONTROLLER"), bTrackingValid ? 1 : 0,
			Grip, Trigger, bTriggerTouched ? 1 : 0, bThumbTouched ? 1 : 0, DrawnPose.Grasp, DrawnPose.Point,
			bDiagCanPoke ? 1 : 0, DiagPanel.IsEmpty() ? TEXT("-") : *DiagPanel, DiagDistance, DiagDepth, bPokePressed ? 1 : 0,
			*FingertipLocation.ToCompactString());
	}
}
