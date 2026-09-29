#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "RuntimeGizmoSubsystem.generated.h"

UCLASS()
class RUNTIMETRANSFORMGIZMO_API URuntimeGizmoSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

private:

	std::atomic<bool> bDragging;
	
public:

	virtual void SetDragging(bool bNewValue);

	UFUNCTION(BlueprintPure, Category = "Frozen Forest | Runtime Gizmo")
	virtual bool GetDragging() const;

};
