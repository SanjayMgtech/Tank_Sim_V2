#include "UI/TSDriverHUDWidget.h"

void UTSDriverHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const APawn* Tank = GetAssignedTank();
	const float NewSpeed = Tank ? Tank->GetVelocity().Size() : 0.f;

	if (!FMath::IsNearlyEqual(NewSpeed, CurrentSpeed, 0.5f))
	{
		CurrentSpeed = NewSpeed;
		OnSpeedUpdated(CurrentSpeed);
	}
}
