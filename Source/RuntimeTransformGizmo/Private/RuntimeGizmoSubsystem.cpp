#include "RuntimeGizmoSubsystem.h"

bool URuntimeGizmoSubsystem::GetDragging() const
{
	return bDragging.load();
}

void URuntimeGizmoSubsystem::SetDragging(bool bNewValue)
{
	bDragging.store(bNewValue);
}