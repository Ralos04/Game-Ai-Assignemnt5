#include "GAPerceptionComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GAPerceptionSystem.h"

UGAPerceptionComponent::UGAPerceptionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;

	// Default vision parameters
	VisionParameters.VisionAngle = 90.0f;
	VisionParameters.VisionDistance = 1000.0;
}


void UGAPerceptionComponent::OnRegister()
{
	Super::OnRegister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->RegisterPerceptionComponent(this);
	}
}

void UGAPerceptionComponent::OnUnregister()
{
	Super::OnUnregister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->UnregisterPerceptionComponent(this);
	}
}


APawn* UGAPerceptionComponent::GetOwnerPawn() const
{
	AActor* Owner = GetOwner();
	if (Owner)
	{
		APawn* Pawn = Cast<APawn>(Owner);
		if (Pawn)
		{
			return Pawn;
		}
		else
		{
			AController* Controller = Cast<AController>(Owner);
			if (Controller)
			{
				return Controller->GetPawn();
			}
		}
	}

	return NULL;
}



// Returns the Target this AI is attending to right now.

UGATargetComponent* UGAPerceptionComponent::GetCurrentTarget() const
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);

	if (PerceptionSystem && PerceptionSystem->TargetComponents.Num() > 0)
	{
		UGATargetComponent* TargetComponent = PerceptionSystem->TargetComponents[0];
		if (TargetComponent->IsKnown())
		{
			return PerceptionSystem->TargetComponents[0];
		}
	}

	return NULL;
}

bool UGAPerceptionComponent::HasTarget() const
{
	return GetCurrentTarget() != NULL;
}


bool UGAPerceptionComponent::GetCurrentTargetState(FTargetCache& TargetStateOut, FTargetData& TargetDataOut) const
{
	UGATargetComponent* Target = GetCurrentTarget();
	if (Target)
	{
		const FTargetData* TargetData = TargetMap.Find(Target->TargetGuid);
		if (TargetData)
		{
			TargetStateOut = Target->LastKnownState;
			TargetDataOut = *TargetData;
			return true;
		}

	}
	return false;
}


void UGAPerceptionComponent::GetAllTargetStates(bool OnlyKnown, TArray<FTargetCache>& TargetCachesOut, TArray<FTargetData>& TargetDatasOut) const
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		TArray<TObjectPtr<UGATargetComponent>>& TargetComponents = PerceptionSystem->GetAllTargetComponents();
		for (UGATargetComponent* TargetComponent : TargetComponents)
		{
			const FTargetData* TargetData = TargetMap.Find(TargetComponent->TargetGuid);
			if (TargetData)
			{
				if (!OnlyKnown || TargetComponent->IsKnown())
				{
					TargetCachesOut.Add(TargetComponent->LastKnownState);
					TargetDatasOut.Add(*TargetData);
				}
			}
		}
	}
}


void UGAPerceptionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateAllTargetData();
}


void UGAPerceptionComponent::UpdateAllTargetData()
{
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		TArray<TObjectPtr<UGATargetComponent>>& TargetComponents = PerceptionSystem->GetAllTargetComponents();
		for (UGATargetComponent* TargetComponent : TargetComponents)
		{
			UpdateTargetData(TargetComponent);
		}
	}
}

void UGAPerceptionComponent::UpdateTargetData(UGATargetComponent* TargetComponent)
{
	// REMEMBER: the UGAPerceptionComponent is going to be attached to the controller, not the pawn. So we call this special accessor to 
	// get the pawn that our controller is controlling
	APawn* OwnerPawn = GetOwnerPawn();

	FTargetData *TargetData = TargetMap.Find(TargetComponent->TargetGuid);
	if (TargetData == NULL)		// If we don't already have a target data for the given target component, add it
	{
		FTargetData NewTargetData;
		FGuid TargetGuid = TargetComponent->TargetGuid;
		TargetData = &TargetMap.Add(TargetGuid, NewTargetData);
	}

	if (TargetData)
	{
		// TODO PART 3
		// 
		// - Update TargetData->bClearLOS
		//		Use this.VisionParameters to determine whether the target is within the vision cone or not 
		//		(and ideally do so before you case a ray towards it)
		// - Update TargetData->Awareness
		//		On ticks when the AI has a clear LOS, the Awareness should grow
		//		On ticks when the AI does not have a clear LOS, the Awareness should decay
		//
		// Awareness should be clamped to the range [0, 1]
		// You can add parameters to the UGAPerceptionComponent to control the speed at which awareness rises and falls

		// YOUR CODE HERE
		
		
		TargetData->bClearLos = IsPerceived(TargetComponent->GetOwner()->GetActorLocation());

		if (TargetData->bClearLos)
		{
			// Increase awareness if LOS is clear
			//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Perceived"));
			TargetData->Awareness = FMath::Min(1.0f, TargetData->Awareness + (GetWorld()->GetDeltaSeconds() * 2)); 
		}
		else
		{
			// Decrease awareness if LOS is not clear
			//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Not Perceived"));
			TargetData->Awareness = FMath::Max(0.0f, TargetData->Awareness - (GetWorld()->GetDeltaSeconds() * 2)); 
		}
	}
}


bool UGAPerceptionComponent::IsPerceived(const FVector& position) const{
	APawn* OwnerPawn = GetOwnerPawn();
	if (!OwnerPawn) {
		return false;
	}

	FVector OwnerLocation = OwnerPawn->GetActorLocation();
	FVector ToTarget = position - OwnerLocation;
	ToTarget.Z = 0.0f;
	ToTarget.Normalize();

	FVector OwnerForward = OwnerPawn ? OwnerPawn->GetActorForwardVector() : FVector::ZeroVector;

	float DotProduct = FVector::DotProduct(OwnerForward, ToTarget);
	float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(DotProduct));
	bool bClearLOS = AngleDegrees < VisionParameters.VisionAngle / 2;

	float DistanceToTarget = FVector::Distance(ToTarget, OwnerLocation);


	if (bClearLOS && DistanceToTarget <= VisionParameters.VisionDistance) {
		FHitResult HitResult;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(OwnerPawn);
		bool bHitSomething = GetWorld()->LineTraceSingleByChannel(HitResult, OwnerLocation, position, ECollisionChannel::ECC_Visibility, Params);

		
		return !bHitSomething;
	}
	return false;
}

const FTargetData* UGAPerceptionComponent::GetTargetData(FGuid TargetGuid) const
{
	return TargetMap.Find(TargetGuid);
}
