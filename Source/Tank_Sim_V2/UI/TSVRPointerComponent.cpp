#include "UI/TSVRPointerComponent.h"

#include "Components/WidgetComponent.h"
#include "CollisionQueryParams.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "Tank_Sim_V2.h"
#include "WorldCollision.h"

UTSVRPointerComponent::UTSVRPointerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Custom, for the reason in the header: the automatic source would ignore everything under the
	// attachment root, which under the crew model is the whole tank the panel is mounted on.
	InteractionSource = EWidgetInteractionSource::Custom;

	// Cockpit range. A crew panel is within arm's reach; a long ray just means the beam catches
	// panels across the hull that the player is not looking at.
	InteractionDistance = 300.f;

	TraceChannel = ECC_Visibility;

	// Show the beam by default so a player can see where they are pointing. Designers can turn it
	// off per hand.
	bShowDebug = false;
}

void UTSVRPointerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Slate tracks focus and capture PER VIRTUAL USER. Two pointers sharing an index means the hands
	// fight over focus, and the symptom is a button that highlights under one hand and activates
	// under the other. Epic's docs call this out; it is invisible until both hands are used at once,
	// so check it loudly at startup rather than leaving it to be discovered in a headset.
	if (const AActor* Owner = GetOwner())
	{
		// Scans the BASE class, not just ours. ATSVRPawn already creates a stock
		// UWidgetInteractionComponent natively, which defaults to VirtualUserIndex 0 - so a check that
		// only looked at UTSVRPointerComponent would have missed the one collision that actually
		// exists in this project.
		TArray<UWidgetInteractionComponent*> Pointers;
		Owner->GetComponents<UWidgetInteractionComponent>(Pointers);
		for (const UWidgetInteractionComponent* Other : Pointers)
		{
			if (Other && Other != this && Other->VirtualUserIndex == VirtualUserIndex)
			{
				UE_LOG(LogTankSim, Error,
					TEXT("[VRPointer] %s and %s on %s share VirtualUserIndex %d. Slate tracks focus PER ")
					TEXT("virtual user, so they will fight: a button highlights under one and activates ")
					TEXT("under the other. Give each its own index."),
					*GetName(), *Other->GetName(), *GetNameSafe(Owner), VirtualUserIndex);
			}
		}
	}

	// Attach to the named hand, if one was given. See AttachToComponentName for why this is code
	// rather than a Blueprint parenting.
	if (!AttachToComponentName.IsNone())
	{
		if (const AActor* Owner = GetOwner())
		{
			TArray<USceneComponent*> SceneComponents;
			Owner->GetComponents<USceneComponent>(SceneComponents);

			USceneComponent** Found = SceneComponents.FindByPredicate(
				[this](const USceneComponent* Component)
				{ return Component && Component != this && Component->GetFName() == AttachToComponentName; });

			if (Found && *Found)
			{
				AttachToComponent(*Found, FAttachmentTransformRules::KeepRelativeTransform);
			}
			else
			{
				UE_LOG(LogTankSim, Warning,
					TEXT("[VRPointer] %s: no component named '%s' on %s - the laser will fire from the ")
					TEXT("pawn origin instead of the hand."),
					*GetName(), *AttachToComponentName.ToString(), *GetNameSafe(Owner));
			}
		}
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// The InputComponent does not exist before possession, so bind on restart as well as now.
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UTSVRPointerComponent::HandlePawnRestarted);
	}

	BindClickAction();
}

void UTSVRPointerComponent::HandlePawnRestarted(APawn* Pawn)
{
	BindClickAction();
}

void UTSVRPointerComponent::BindClickAction()
{
	if (bClickBound || !ClickAction)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	UEnhancedInputComponent* EIC = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!EIC)
	{
		return;
	}

	EIC->BindAction(ClickAction, ETriggerEvent::Started, this, &UTSVRPointerComponent::Input_Click_Pressed);
	EIC->BindAction(ClickAction, ETriggerEvent::Completed, this, &UTSVRPointerComponent::Input_Click_Released);
	bClickBound = true;
}

void UTSVRPointerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// MUST run before Super: the base tick consumes CustomHitResult to decide what is hovered, so
	// updating it afterwards would leave the pointer acting on last frame's target.
	UpdateCustomHit();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UTSVRPointerComponent::UpdateCustomHit()
{
	CustomHitResult = FHitResult();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = GetComponentLocation();
	const FVector End = Start + GetForwardVector() * InteractionDistance;

	// No ignore list at all. That is the whole point of this class - the stock trace's ignore list
	// would contain the panel we are trying to hit.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSVRPointerTrace), /*bTraceComplex=*/false);

	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, TraceChannel, Params);

	for (const FHitResult& Hit : Hits)
	{
		if (!bOnlyHitWidgetComponents)
		{
			CustomHitResult = Hit;
			return;
		}

		if (Cast<UWidgetComponent>(Hit.GetComponent()))
		{
			CustomHitResult = Hit;
			return;
		}
	}
}

bool UTSVRPointerComponent::IsPointingAtWidget() const
{
	return CustomHitResult.bBlockingHit && Cast<UWidgetComponent>(CustomHitResult.GetComponent()) != nullptr;
}

void UTSVRPointerComponent::PressPointer()
{
	PressPointerKey(EKeys::LeftMouseButton);
}

void UTSVRPointerComponent::ReleasePointer()
{
	ReleasePointerKey(EKeys::LeftMouseButton);
}

void UTSVRPointerComponent::Input_Click_Pressed()
{
	PressPointer();
}

void UTSVRPointerComponent::Input_Click_Released()
{
	ReleasePointer();
}
