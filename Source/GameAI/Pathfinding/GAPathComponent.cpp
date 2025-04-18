#include "GAPathComponent.h"
#include "GameFramework/NavMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UE_DISABLE_OPTIMIZATION

UGAPathComponent::UGAPathComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	State = GAPS_None;
	bDestinationValid = false;
	ArrivalDistance = 100.0f;

	// A bit of Unreal magic to make TickComponent below get called
	PrimaryComponentTick.bCanEverTick = true;
}


const AGAGridActor* UGAPathComponent::GetGridActor() const
{
	if (GridActor.Get())
	{
		return GridActor.Get();
	}
	else
	{
		AGAGridActor* Result = NULL;
		AActor *GenericResult = UGameplayStatics::GetActorOfClass(this, AGAGridActor::StaticClass());
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

APawn* UGAPathComponent::GetOwnerPawn()
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


void UGAPathComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	bool Valid = false;
	if (bDestinationValid)
	{
		RefreshPath();
		Valid = true;
	}
	else if (bDistanceMapPathValid)
	{
		Valid = true;
	}
	if (Valid)
	{
		if (State == GAPS_Active)
		{
			FollowPath();
		}
	}

	// Super important! Otherwise, unbelievably, the Tick event in Blueprint won't get called

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

EGAPathState UGAPathComponent::RefreshPath()
{
	AActor* Owner = GetOwnerPawn();
	FVector StartPoint = Owner->GetActorLocation();

	check(bDestinationValid);

	float DistanceToDestination = FVector::Dist(StartPoint, Destination);

	if (DistanceToDestination <= ArrivalDistance)
	{
		// Yay! We got there!
		State = GAPS_Finished;
	}
	else
	{
		TArray<FPathStep> UnsmoothedSteps;

		// Replan the path!
		State = AStar(StartPoint, UnsmoothedSteps);
		// Debugging A*
		//Steps = UnsmoothedSteps;

		if (State == EGAPathState::GAPS_Active)
		{
			Steps.Empty();
			State = SmoothPath(StartPoint, UnsmoothedSteps, Steps);
		}
	}

	return State;
}


struct FCellRecord
{
	FCellRecord(const FCellRef& CellIn, const FCellRef &PrevCellIn, float CumulativeDistanceIn, float TotalScoreIn) : 
		Cell(CellIn), 
		PreviousCell(PrevCellIn),
		CumulativeDistance(CumulativeDistanceIn),
		TotalScore(TotalScoreIn) {}

	FCellRecord() : Cell(), PreviousCell(), CumulativeDistance(0.0f), TotalScore(0.0f) {}

	FCellRef Cell;
	FCellRef PreviousCell;
	float CumulativeDistance;
	float TotalScore;

	bool operator<(const FCellRecord& OtherRecord) const
	{
		return TotalScore < OtherRecord.TotalScore;
	}
};

EGAPathState UGAPathComponent::AStar(const FVector &StartPoint, TArray<FPathStep> &StepsOut)
{
	const AGAGridActor* Grid = GetGridActor();
	if (!Grid)
	{
		return GAPS_Invalid;
	}

	FCellRef StartCellRef = Grid->GetCellRef(StartPoint);
	if (StartCellRef.IsValid())
	{
		TArray<FCellRecord> Heap;
		TMap<FCellRef, FCellRecord> Closed;

		float StartDistance = StartCellRef.Distance(DestinationCell);

		FCellRecord StartRecord(StartCellRef, FCellRef::Invalid, 0.0f, StartDistance);
		Closed.Add(StartCellRef, StartRecord);

		Heap.HeapPush(StartRecord);

		while (Heap.Num() > 0)
		{
			FCellRecord CurrentRecord;
			Heap.HeapPop(CurrentRecord);

			// Close me!
			Closed.Add(CurrentRecord.Cell, CurrentRecord);

			if (CurrentRecord.Cell == DestinationCell)
			{
				// We found our way! Hurray!
				TArray<FCellRef> ReversePath;
				FCellRecord *CellRecord = &CurrentRecord;

				while (CellRecord)
				{
					ReversePath.Add(CellRecord->Cell);
					CellRecord = Closed.Find(CellRecord->PreviousCell);
				}

				// Now reverse it. Note, we're going to leave off the first cell!
				for (int32 StepIndex = ReversePath.Num() - 2; StepIndex >= 0; StepIndex--)
				{
					FPathStep Step;
					Step.CellRef = ReversePath[StepIndex];
					Step.Point = FVector2D(GridActor->GetCellPosition(Step.CellRef));
					StepsOut.Add(Step);
				}

				// minor tweak -- set the last cell position to the destination point, rather than the cell point
				if (StepsOut.Num() > 0)
				{
					StepsOut.Last().Point = FVector2D(Destination);
				}

				return GAPS_Active;
			}
			else
			{
				TArray<FCellRef> Neighbors;

				Grid->GetNeighbors(CurrentRecord.Cell, true, Neighbors);

				for (FCellRef& NCell : Neighbors)
				{
					if (!Closed.Contains(NCell))
					{
						int32 DX = FMath::Abs(CurrentRecord.Cell.X - NCell.X);
						int32 DY = FMath::Abs(CurrentRecord.Cell.Y - NCell.Y);

						float ParentD = ((DX > 0) && (DY > 0)) ? UE_SQRT_2 : 1.0f;
						float H = NCell.Distance(DestinationCell);
						float TotalScore = CurrentRecord.CumulativeDistance + ParentD + H;

						// See if it's already on the heap
						int32 ExistingIndex = Heap.IndexOfByPredicate([NCell](const FCellRecord& Record) {
							return Record.Cell == NCell;
						});

						bool bAdd = true;

						if (ExistingIndex != INDEX_NONE)
						{
							FCellRecord& ExistingRecord = Heap[ExistingIndex];
							if (TotalScore < ExistingRecord.TotalScore)
							{
								// I get to replace you!
								Heap.HeapRemoveAt(ExistingIndex);
							}
							else
							{
								bAdd = false;
							}
						}

						if (bAdd)
						{
							FCellRecord NewRecord(NCell, CurrentRecord.Cell, CurrentRecord.CumulativeDistance + ParentD, TotalScore);
							Heap.HeapPush(NewRecord);
						}
					}
				}
			}
		}
	}

	// Yikes, didn't find the destination
	return GAPS_Invalid;
}


bool UGAPathComponent::Dijkstra(const FVector& StartPoint, FGAGridMap& DistanceMapOut) const
{
	bool Result = false;

	const AGAGridActor* Grid = GetGridActor();
	if (!Grid)
	{
		return false;
	}

	FCellRef StartCellRef = Grid->GetCellRef(StartPoint);
	if (StartCellRef.IsValid())
	{
		FCellRecord StartRecord(StartCellRef, FCellRef::Invalid, 0.0f, 0.0f);
		TArray<FCellRecord> Heap;
		float DiagonalDistance = UE_SQRT_2 * Grid->CellScale;

		Result = true;

		Heap.HeapPush(StartRecord);

		while (Heap.Num() > 0)
		{
			FCellRecord CurrentRecord;
			Heap.HeapPop(CurrentRecord);

			DistanceMapOut.SetValue(CurrentRecord.Cell, CurrentRecord.CumulativeDistance);

			{
				TArray<FCellRef> Neighbors;

				Grid->GetNeighbors(CurrentRecord.Cell, true, Neighbors);

				for (FCellRef& NCell : Neighbors)
				{
					float CurrentDistanceInMap;

					if (DistanceMapOut.GetValue(NCell, CurrentDistanceInMap))
					{
						if (CurrentDistanceInMap == FLT_MAX)
						{
							int32 DX = FMath::Abs(CurrentRecord.Cell.X - NCell.X);
							int32 DY = FMath::Abs(CurrentRecord.Cell.Y - NCell.Y);

							float ParentD = ((DX > 0) && (DY > 0)) ? DiagonalDistance : Grid->CellScale;
							float CumulativeDistance = CurrentRecord.CumulativeDistance + ParentD;
							float TotalScore = CumulativeDistance;			// could also add penalties here

							// See if it's already on the heap
							int32 ExistingIndex = Heap.IndexOfByPredicate([NCell](const FCellRecord& Record) {
								return Record.Cell == NCell;
								});

							bool bAdd = true;

							if (ExistingIndex != INDEX_NONE)
							{
								FCellRecord& ExistingRecord = Heap[ExistingIndex];
								if (TotalScore < ExistingRecord.TotalScore)
								{
									// I get to replace you!
									Heap.HeapRemoveAt(ExistingIndex);
								}
								else
								{
									bAdd = false;
								}
							}

							if (bAdd)
							{
								FCellRecord NewRecord(NCell, CurrentRecord.Cell, CumulativeDistance, TotalScore);
								Heap.HeapPush(NewRecord);
							}
						}
					}
				}
			}
		}
	}

	return Result;
}

bool UGAPathComponent::BuildPathFromDistanceMap(const FVector& StartPoint, const FCellRef& CellRef, const FGAGridMap& DistanceMap)
{
	bool Result = false;
	TArray<FCellRef> Cells;
	FCellRef CurrentCell = CellRef;
	const AGAGridActor* Grid = GetGridActor();

	bDistanceMapPathValid = false;

	if (Grid == NULL)
	{
		return false;
	}

	FCellRef StartCell = Grid->GetCellRef(StartPoint);

	while (true)
	{

		if (StartCell == CurrentCell)
		{
			// Found the start!
			break;
		}
		else
		{
			float D;

			TArray<FCellRef> Neighbors;
			FVector CurrentPosition = Grid->GetCellPosition(CurrentCell);

			Cells.Add(CurrentCell);
			Grid->GetNeighbors(CurrentCell, true, Neighbors);
			DistanceMap.GetValue(CurrentCell, D);

			float BestNeighborDistance = FLT_MAX;
			FCellRef BestNeighbor;

			for (FCellRef &Neighbor : Neighbors)
			{
				FVector NeighborPosition = Grid->GetCellPosition(Neighbor);
				
				float ND;
				DistanceMap.GetValue(Neighbor, ND);

				if (ND < D)
				{
					float TotalND = FVector::Dist(CurrentPosition, NeighborPosition) + ND;
					if (TotalND < BestNeighborDistance)
					{
						BestNeighborDistance = TotalND;
						BestNeighbor = Neighbor;
					}
				}
			}

			if (BestNeighbor.IsValid())
			{
				CurrentCell = BestNeighbor;
			}
			else
			{
				// Shouldn't happen, but whatever
				break;
			}
		}
	}

	if (Cells.Num() > 0)
	{
		Result = true;

		TArray<FPathStep> UnsmoothedSteps;

		for (int32 Index = Cells.Num() - 1; Index >= 0; Index--)
		{
			FPathStep Step;

			Step.CellRef = Cells[Index];
			Step.Point = FVector2D(Grid->GetCellPosition(Cells[Index]));
			UnsmoothedSteps.Add(Step);
		}

		Steps.Empty();

		State = SmoothPath(StartPoint, UnsmoothedSteps, Steps);
		if (State == GAPS_Active)
		{
			bDistanceMapPathValid = true;
		}
	}

	return Result;
}


EGAPathState UGAPathComponent::SmoothPath(const FVector& StartPoint, const TArray<FPathStep>& UnsmoothedSteps, TArray<FPathStep>& SmoothedStepsOut)
{
	const AGAGridActor* Grid = GetGridActor();

	if (UnsmoothedSteps.Num() <= 1)
	{
		// Only 1 step -- consider it smoothed
		SmoothedStepsOut = UnsmoothedSteps;
		return GAPS_Active;
	}
	else if (Grid)
	{
		int32 StepCount = UnsmoothedSteps.Num();
		FVector LastPoint = StartPoint;
		FCellRef LastCell = Grid->GetCellRef(StartPoint);

		// Find the first step that fails. Note, we leave the last step for after the loop
		for (int32 StepIndex = 1; StepIndex < StepCount-1; StepIndex++)
		{
			FVector CellPoint = Grid->GetCellPosition(UnsmoothedSteps[StepIndex].CellRef);
			FVector HitLocation;

			if (Grid->TraceLine(LastPoint, CellPoint, HitLocation))
			{
				// we hit something
				const FPathStep& StepToAdd = UnsmoothedSteps[StepIndex - 1];
				SmoothedStepsOut.Add(StepToAdd);
				LastPoint = FVector(StepToAdd.Point, 0.0f);
				LastCell = StepToAdd.CellRef;
			}
		}

		// We got to the end!
		SmoothedStepsOut.Add(UnsmoothedSteps[StepCount - 1]);
		return GAPS_Active;
	}
	else
	{
		return GAPS_Invalid;
	}
}


void UGAPathComponent::FollowPath()
{
	AActor* Owner = GetOwnerPawn();
	

	if (Owner != NULL)
	{
		FVector StartPoint = Owner->GetActorLocation();

		check(State == GAPS_Active);
		check(Steps.Num() > 0);

		// Always follow the first step, assuming that we are refreshing the whole path every tick
		FVector V = FVector(Steps[0].Point, 0.0f) - StartPoint;
		V.Z = 0.0f;
		V.Normalize();

		UNavMovementComponent* MovementComponent = Owner->FindComponentByClass<UNavMovementComponent>();
		if (MovementComponent)
		{
			MovementComponent->RequestPathMove(V);
		}
	}
	
}

void UGAPathComponent::ClearPath()
{
	bDestinationValid = false;
	bDistanceMapPathValid = false;
	Steps.Empty();
	State = GAPS_None;
}


EGAPathState UGAPathComponent::SetDestination(const FVector &DestinationPoint)
{
	Destination = DestinationPoint;

	State = GAPS_Invalid;
	bDestinationValid = true;

	const AGAGridActor* Grid = GetGridActor();
	if (Grid)
	{
		FCellRef CellRef = Grid->GetCellRef(Destination);
		if (CellRef.IsValid())
		{
			DestinationCell = CellRef;
			bDestinationValid = true;

			RefreshPath();
		}
	}

	return State;
}


UE_ENABLE_OPTIMIZATION
