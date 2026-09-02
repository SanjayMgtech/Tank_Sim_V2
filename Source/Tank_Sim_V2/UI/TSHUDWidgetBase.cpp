#include "UI/TSHUDWidgetBase.h"

#include "Player/TSTankPlayerState.h"

void UTSHUDWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (ATSTankPlayerState* PS = GetTankPlayerState())
	{
		PS->OnAssignmentChanged.AddDynamic(this, &UTSHUDWidgetBase::HandleAssignmentChanged);
	}

	HandleAssignmentChanged();
}

APawn* UTSHUDWidgetBase::GetAssignedTank() const
{
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS ? PS->GetAssignedTank() : nullptr;
}

ATSTankPlayerState* UTSHUDWidgetBase::GetTankPlayerState() const
{
	return GetOwningPlayerState<ATSTankPlayerState>();
}

void UTSHUDWidgetBase::NativeOnAssignmentRefreshed()
{
}

void UTSHUDWidgetBase::HandleAssignmentChanged()
{
	NativeOnAssignmentRefreshed();
	OnAssignmentRefreshed();
}
