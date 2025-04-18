#include "GATargetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameAI/Grid/GAGridActor.h"
#include "GAPerceptionSystem.h"
#include "ProceduralMeshComponent.h"
#include "WorldCollision.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"


UGATargetComponent::UGATargetComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;

	SetTickGroup(ETickingGroup::TG_PostUpdateWork);

	// Generate a new guid
	TargetGuid = FGuid::NewGuid();
}


AGAGridActor* UGATargetComponent::GetGridActor() const
{
	AGAGridActor* Result = GridActor.Get();
	if (Result)
	{
		return Result;
	}
	else
	{
		AActor* GenericResult = UGameplayStatics::GetActorOfClass(this, AGAGridActor::StaticClass());
		if (GenericResult)
		{
			Result = Cast<AGAGridActor>(GenericResult);
			if (Result)
			{
				// Cache the result
				// Note, GridActor is marked as mutable in the header, which is why this is allowed in a const method
				GridActor = Result;
			}
		}

		return Result;
	}
}


void UGATargetComponent::OnRegister()
{
	Super::OnRegister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->RegisterTargetComponent(this);
	}

	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		OccupancyMap = FGAGridMap(Grid, 0.0f);
	}
}

void UGATargetComponent::OnUnregister()
{
	Super::OnUnregister();

	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		PerceptionSystem->UnregisterTargetComponent(this);
	}
}



void UGATargetComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UE_LOG(LogTemp, Warning, TEXT("im ticking"));
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool isImmediate = false;

	// update my perception state FSM
	UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
	if (PerceptionSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("Got the perception system!"));
		TArray<TObjectPtr<UGAPerceptionComponent>>& PerceptionComponents = PerceptionSystem->GetAllPerceptionComponents();
		for (UGAPerceptionComponent* PerceptionComponent : PerceptionComponents)
		{
			UE_LOG(LogTemp, Warning, TEXT("going through the perception components..."));
			const FTargetData* TargetData = PerceptionComponent->GetTargetData(TargetGuid);
			if (TargetData && (TargetData->Awareness >= 1.0f))
			{
				UE_LOG(LogTemp, Warning, TEXT("setting isImmediate to True"));
				isImmediate = true;
				break;
			}
		}
	}

	if (isImmediate)
	{
		UE_LOG(LogTemp, Warning, TEXT("Going through Immediate"));
		AActor* Owner = GetOwner();
		LastKnownState.State = GATS_Immediate;

		// REFRESH MY STATE
		LastKnownState.Set(Owner->GetActorLocation(), Owner->GetVelocity());

		// Tell the omap to clear out and put all the probability in the observed location
		OccupancyMapSetPosition(LastKnownState.Position);
	}
	else if (IsKnown())
	{
		UE_LOG(LogTemp, Warning, TEXT("IsKnown!"));
		LastKnownState.State = GATS_Hidden;
	}

	if (LastKnownState.State == GATS_Hidden)
	{
		UE_LOG(LogTemp, Warning, TEXT("IsHidden!"));
		OccupancyMapUpdate();
	}

	// As long as I'm known, whether I'm immediate or not, diffuse the probability in the omap

	if (IsKnown())
	{
		OccupancyMapDiffuse();
	}

	if (bDebugOccupancyMap)
	{
		AGAGridActor* Grid = GetGridActor();
		Grid->DebugGridMap = OccupancyMap;
		GridActor->RefreshDebugTexture();
		GridActor->DebugMeshComponent->SetVisibility(true);
	}
}


void UGATargetComponent::OccupancyMapSetPosition(const FVector& Position)
{
	// TODO PART 4

	// We've been observed to be in a given position
	// Clear out all probability in the omap, and set the appropriate cell to P = 1.0

	const AGAGridActor* Grid = GetGridActor();
	bDebugOccupancyMap = true;

	if (!OccupancyMap.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Occupancy map is not valid."));
		return;
	}

	// Reset the occupancy map to 0.0
	OccupancyMap.ResetData(0.0f);

	// Set the probability of the cell corresponding to the given position to 1.0
	const FCellRef OccupiedTile = Grid->GetCellRef(Position);
	OccupancyMap.SetValue(OccupiedTile, 1.0f);
}

// Helper function to check if a location is blocked
bool UGATargetComponent::IsLocationBlocked(const FVector& Location)
{
	// Create parameters for the trace
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

	// Simple sphere trace to check if location is blocked
	return GetWorld()->SweepTestByChannel(
		Location,              // Start
		Location,              // End (same as start for simple overlap)
		FQuat::Identity,       // Rotation
		ECC_WorldStatic,       // Collision channel
		FCollisionShape::MakeSphere(50.0f), // Shape
		QueryParams            // Parameters
	);
}

void UGATargetComponent::OccupancyMapUpdate()
{
	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FGAGridMap VisibilityMap(Grid, 0.0f);

		// TODO PART 4

		// STEP 1: Build a visibility map, based on the perception components of the AIs in the world
		// The visibility map is a simple map where each cell is either 0 (not currently visible to ANY perceiver) or 1 (currently visible to one or more perceivers).
		// 

		float minVal = FLT_MAX;
		float maxVal = -FLT_MAX;

		TSet<FCellRef> allCells;
		TSet<FCellRef> visibleCells;
		TSet<FCellRef> hiddenCells;

		UGAPerceptionSystem* PerceptionSystem = UGAPerceptionSystem::GetPerceptionSystem(this);
		if (PerceptionSystem)
		{
			TArray<TObjectPtr<UGAPerceptionComponent>>& PerceptionComponents = PerceptionSystem->GetAllPerceptionComponents();
			for (int x = 0; x < Grid->XCount; ++x) {
				for (int y = 0; y < Grid->YCount; ++y) {
					FCellRef cellReference = FCellRef(x, y);

					for (UGAPerceptionComponent* PerceptionComponent : PerceptionComponents) {
						// Find visible cells for this perceiver.
						// Reminder: Use the PerceptionComponent.VisionParameters when determining whether a cell is visible or not (in addition to a line trace).
						// Suggestion: you might find it useful to add a UGAPerceptionComponent::TestVisibility method to the perception component.
						if (PerceptionComponent->IsPerceived(Grid->GetCellPosition(cellReference) + FVector::UpVector * 50.0f)) {
							//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, TEXT("Perceived"));
							visibleCells.Add(cellReference);
							allCells.Add(cellReference);
						}
						else {
							hiddenCells.Add(cellReference);
							allCells.Add(cellReference);
							float valueAt;
							OccupancyMap.GetValue(cellReference, valueAt);

							minVal = FMath::Min(minVal, valueAt);
							maxVal = FMath::Max(maxVal, valueAt);
						}
					}
				}
			}
		}



		// STEP 2: Clear out the probability in the visible cells
		for (FCellRef ref : visibleCells) {
			OccupancyMap.SetValue(ref, 0.0f);
			minVal = 0.0f;
		}

		// STEP 3: Renormalize the OMap, so that it's still a valid probability distribution
		FCellRef HighestLikelihoodCell;
		float MaxLikelihood = -1.0f;
		FVector CellPosition;

		float NormalizationFactor = maxVal - minVal;
		if (NormalizationFactor != 0.0f)
		{
			for (FCellRef cellReference : hiddenCells)
			{
				float valueAt;
				OccupancyMap.GetValue(cellReference, valueAt);
				float value = FMath::Clamp((valueAt - minVal) / NormalizationFactor, 0.0f, 1.0f);
				OccupancyMap.SetValue(cellReference, value);
				if (value > MaxLikelihood)
				{
					FCellRef tempCell = HighestLikelihoodCell;
					float tempLikelihood = MaxLikelihood;
					HighestLikelihoodCell = cellReference;
					MaxLikelihood = value;

					if (Grid->IsValidCell(HighestLikelihoodCell)) {
						// Get the position of the highest likelihood cell
						CellPosition = Grid->GetCellPosition(HighestLikelihoodCell) + FVector(0.0f, 0.0f, 100.0f);

						// Clamp the X and Y coordinates within the valid range
						HighestLikelihoodCell.X = FMath::Clamp(HighestLikelihoodCell.X, 0, 99);
						HighestLikelihoodCell.Y = FMath::Clamp(HighestLikelihoodCell.Y, 0, 99);

						// Recalculate the position based on the clamped coordinates
						CellPosition = Grid->GetCellPosition(HighestLikelihoodCell) + FVector(0.0f, 0.0f, 100.0f);

						// Create a sphere collider
						FCollisionShape SphereShape = FCollisionShape::MakeSphere(50.0f);
						FCollisionQueryParams Params;
						Params.AddIgnoredActor(GetOwner());

						// Check if location is blocked using our helper function
						if (IsLocationBlocked(CellPosition))
						{
							HighestLikelihoodCell = tempCell;
							MaxLikelihood = tempLikelihood;

							FVector debugCellPos = Grid->GetCellPosition(HighestLikelihoodCell);
							//UE_LOG(LogTemp, Warning, TEXT("CellPosition: X=%f, Y=%f, Z=%f"), CellPosition.X, CellPosition.Y, CellPosition.Z);

							//UE_LOG(LogTemp, Warning, TEXT("HighestLikelihoodCell: X=%d, Y=%d"), HighestLikelihoodCell.X, HighestLikelihoodCell.Y);

							//UE_LOG(LogTemp, Warning, TEXT("Debug Cell Pos: X=%d, Y=%d, Z=%d"), debugCellPos.X, debugCellPos.Y, debugCellPos.Z);

						}
					}
				}
			}
		}
		// STEP 4: Extract the highest-likelihood cell on the omap and refresh the LastKnownState.

		// I don't know why this doesn't actually work at all
		//LastKnownState.Set(Grid->GetCellPosition(HighestLikelihoodCell), LastKnownState.Velocity);

		LastKnownState.Set(CellPosition, LastKnownState.Velocity);
		//UE_LOG(LogTemp, Warning, TEXT("LastKnownPos: X=%f, Y=%f, Z=%f"), LastKnownState.Position.X, LastKnownState.Position.Y, LastKnownState.Position.Z);
		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Green, FString::SanitizeFloat(maxVal));
		DrawDebugSphere(GetWorld(), Grid->GetCellPosition(HighestLikelihoodCell), 50, 1, FColor::Green, true, 10, 1, .5f);
	}

}


void UGATargetComponent::OccupancyMapDiffuse()
{
	// TODO PART 4
	// Diffuse the probability in the OMAP
	AGAGridActor* Grid = GetGridActor();
	if (!OccupancyMap.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Occupancy map is not valid."));
		return;
	}

	const int XCount = Grid->XCount;
	const int YCount = Grid->YCount;

	FGAGridMap diffusionMap(Grid, 0.0f);
	diffusionMap = OccupancyMap;

	// Diffusion parameters
	const float DiffusionFactor = 0.4f; // Adjust based on desired diffusion rate
	const float InverseDiffusionFactor = 1.0f - DiffusionFactor;

	// Diffuse the probability from each cell to its neighboring cells
	for (int i = 0; i < 20; ++i) {
		for (int X = 0; X < XCount; ++X)
		{
			for (int Y = 0; Y < YCount; ++Y)
			{
				int adjacentNeighbors = 0;
				float totalValues = 0.0f;

				// Iterate over neighboring cells (including diagonals)
				for (int dX = -1; dX <= 1; ++dX)
				{
					for (int dY = -1; dY <= 1; ++dY)
					{
						int NeighborX = X + dX;
						int NeighborY = Y + dY;
						FCellRef neighbor;
						neighbor.X = NeighborX;
						neighbor.Y = NeighborY;

						// Ensure the neighbor is within bounds
						if (Grid->IsValidCell(neighbor))
						{

							float NeighborValue;
							diffusionMap.GetValue(neighbor, NeighborValue);
							totalValues += NeighborValue;
							++adjacentNeighbors;

						}
					}
				}
				FCellRef currCell;
				currCell.X = X;
				currCell.Y = Y;

				float currVal;
				diffusionMap.GetValue(currCell, currVal);

				float diffusion = (InverseDiffusionFactor)*currVal + (DiffusionFactor / adjacentNeighbors) * totalValues;

				diffusionMap.SetValue(currCell, diffusion);

			}
		}
	}


	// Update the occupancy map with the diffused values
	OccupancyMap = diffusionMap;
}