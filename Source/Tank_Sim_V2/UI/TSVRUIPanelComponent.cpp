#include "UI/TSVRUIPanelComponent.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Tank_Sim_V2.h"
#include "UI/TSUISubsystem.h"

UTSVRUIPanelComponent::UTSVRUIPanelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// World, not Screen: Screen space here would put us back to the flat viewport a headset cannot see.
	SetWidgetSpace(EWidgetSpace::World);

	// DrawSize is the widget's RESOLUTION in pixels; the component's world size comes from that
	// multiplied by the pixel-to-world scale below. Keeping the resolution generous and the world
	// size modest is what stops text turning to mush in a headset.
	SetDrawSize(FVector2D(1000.f, 700.f));

	// 1000x700 px at 0.06 -> roughly 60cm x 42cm. About the size of a real instrument panel, which is
	// a comfortable read at arm's length. Designers retune this per panel in the Blueprint.
	SetPivot(FVector2D(0.5f, 0.5f));

	// The pointer trace MUST be able to hit this, or the ray passes straight through and the panel is
	// visible but dead. UWidgetInteractionComponent traces on the Visibility channel by default, so
	// query-only collision that BLOCKS Visibility is the minimum that makes a panel interactive.
	// This is the single most common reason a VR panel "does not respond".
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	SetGenerateOverlapEvents(false);

	// Two-sided so a panel is not invisible from a seat that happens to sit behind it.
	SetTwoSided(true);

	// Hidden until the router says otherwise, so a flat player never gets a stray quad in the world.
	SetVisibility(false);
	SetHiddenInGame(true);
}

UTSUISubsystem* UTSVRUIPanelComponent::GetUISubsystem() const
{
	const AActor* Owner = GetOwner();
	const UGameInstance* GameInstance = Owner ? Owner->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UTSUISubsystem>() : nullptr;
}

void UTSVRUIPanelComponent::BeginPlay()
{
	Super::BeginPlay();

	if (PanelWidgetClass)
	{
		SetWidgetClass(PanelWidgetClass);
	}

	RefreshPanelVisibility();
}

void UTSVRUIPanelComponent::SetPanelWidgetClass(TSubclassOf<UUserWidget> NewWidgetClass)
{
	PanelWidgetClass = NewWidgetClass;
	SetWidgetClass(NewWidgetClass);
	RefreshPanelVisibility();
}

bool UTSVRUIPanelComponent::ShouldPanelBeVisible() const
{
	if (!PanelWidgetClass)
	{
		return false;
	}

	if (!bHideWhenPresentationIsFlat)
	{
		return true;
	}

	// One owner of the flat-vs-world decision. See UTSUISubsystem::GetPresentationMode - it is
	// derived from the display device, never from whether this player is the host.
	const UTSUISubsystem* UI = GetUISubsystem();
	return UI && UI->ShouldUseWorldSpaceUI();
}

void UTSVRUIPanelComponent::RefreshPanelVisibility()
{
	// NOT named bVisible: USceneComponent declares a member of that name, and UHT builds with
	// -WarningsAsErrors so C4458 shadowing is fatal. Same family as 'Role' and 'Mesh' (CLAUDE.md).
	const bool bShouldShow = ShouldPanelBeVisible();

	SetVisibility(bShouldShow);
	SetHiddenInGame(!bShouldShow);

	// Collision follows visibility: a hidden panel must not keep eating pointer traces, or a flat
	// player's interaction ray would silently collide with an invisible quad.
	SetCollisionEnabled(bShouldShow ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);

	UE_LOG(LogTankSim, Log, TEXT("[VRUIPanel] %s on %s: visible=%s widget=%s"),
		*GetName(), *GetNameSafe(GetOwner()),
		bShouldShow ? TEXT("YES") : TEXT("no"),
		PanelWidgetClass ? *PanelWidgetClass->GetName() : TEXT("<none>"));
}
