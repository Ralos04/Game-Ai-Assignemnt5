#include "GASpatialComponent.h"
#include "GameAI/Pathfinding/GAPathComponent.h"
#include "GameAI/Grid/GAGridMap.h"
#include "Kismet/GameplayStatics.h"
#include "Math/MathFwd.h"
#include "GASpatialFunction.h"
#include "ProceduralMeshComponent.h"
#include <GameAI/Perception/GAPerceptionSystem.h>

UE_DISABLE_OPTIMIZATION

UGASpatialComponent::UGASpatialComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Initialize the sampling range for evaluating the spatial function
    SampleDimensions = 8000.0f;
}

// Retrieves and caches the grid actor instance in the world
const AGAGridActor* UGASpatialComponent::GetGridActor() const
{
    AGAGridActor* Result = GridActor.Get();
    if (Result)
    {
        return Result;
    }
    // Locate and cache the grid actor
    AActor* GenericResult = UGameplayStatics::GetActorOfClass(this, AGAGridActor::StaticClass());
    Result = Cast<AGAGridActor>(GenericResult);
    if (Result)
    {
        GridActor = Result;
    }
    return Result;
}

// Retrieves and caches the path component attached to the same owner
UGAPathComponent* UGASpatialComponent::GetPathComponent() const
{
    UGAPathComponent* Result = PathComponent.Get();
    if (Result)
    {
        return Result;
    }
    AActor* Owner = GetOwner();
    if (Owner)
    {
        Result = Owner->GetComponentByClass<UGAPathComponent>();
        if (Result)
        {
            PathComponent = Result;
        }
    }
    return Result;
}

// Returns the pawn controlled by this component's owner (controller or pawn)
APawn* UGASpatialComponent::GetOwnerPawn() const
{
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;
    if (APawn* Pawn = Cast<APawn>(Owner)) return Pawn;
    if (AController* Controller = Cast<AController>(Owner)) return Controller->GetPawn();
    return nullptr;
}

/**
 * Chooses a target position based on the spatial function.
 * @param PathfindToPosition  If true, will build a path to the chosen position.
 * @param Debug              (unused) originally used for debug rendering.
 * @returns true if a valid position was found.
 */
bool UGASpatialComponent::ChoosePosition(bool PathfindToPosition, bool Debug)
{
    bool Result = false;
    const APawn* OwnerPawn = GetOwnerPawn();
    const AGAGridActor* Grid = GetGridActor();
    UGAPathComponent* PathComp = GetPathComponent();

    // Validate prerequisites
    if (!OwnerPawn || !Grid || !PathComp || !SpatialFunctionReference.Get())
    {
        if (!SpatialFunctionReference.Get())
            UE_LOG(LogTemp, Warning, TEXT("UGASpatialComponent has no SpatialFunctionReference assigned."));
        return false;
    }

    // Default object for spatial function
    const UGASpatialFunction* SpatialFunc = SpatialFunctionReference->GetDefaultObject<UGASpatialFunction>();

    // Determine sampling bounds around pawn
    FVector PawnLoc3D = OwnerPawn->GetActorLocation();
    FVector2D PawnLoc2D(PawnLoc3D);
    FBox2D Box(EForceInit::ForceInit);
    Box += PawnLoc2D;
    Box = Box.ExpandBy(SampleDimensions * 0.5f);

    FIntRect CellRect;
    if (!Grid->GridSpaceBoundsToRect2D(Box, CellRect))
        return false;

    FGridBox GridBox(CellRect);
    FGAGridMap GridMap(Grid, GridBox, 0.0f);
    FGAGridMap DistanceMap(Grid, GridBox, FLT_MAX);

    // Step 1: gather reachable cells via Dijkstra
    PathComp->Dijkstra(PawnLoc3D, DistanceMap);
    GridMap.SetValue(BestCell, SpatialFunc->LastCellBonus);

    // Step 2: evaluate each spatial function layer
    for (const FFunctionLayer& Layer : SpatialFunc->Layers)
    {
        EvaluateLayer(Layer, DistanceMap, GridMap);
    }

    // Step 3: select best-scoring cell
    float BestScore = -FLT_MAX;
    FCellRef Chosen = FCellRef::Invalid;
    for (int32 Y = GridBox.MinY; Y <= GridBox.MaxY; ++Y)
    {
        for (int32 X = GridBox.MinX; X <= GridBox.MaxX; ++X)
        {
            FCellRef Cell(X, Y);
            float Dist;
            if (DistanceMap.GetValue(Cell, Dist) && Dist < FLT_MAX)
            {
                float Score;
                GridMap.GetValue(Cell, Score);
                if (Score > BestScore)
                {
                    BestScore = Score;
                    Chosen = Cell;
                }
            }
        }
    }
    BestCell = Chosen;
    Result = BestCell.IsValid();

    // Step 4: optionally pathfind to chosen cell
    if (PathfindToPosition)
    {
        if (BestCell.IsValid())
            PathComp->BuildPathFromDistanceMap(PawnLoc3D, BestCell, DistanceMap);
        else
            PathComp->ClearPath();
    }
    return Result;
}

/**
 * Evaluates a single layer of the spatial function for each traversable cell.
 */
void UGASpatialComponent::EvaluateLayer(
    const FFunctionLayer& Layer,
    const FGAGridMap& DistanceMap,
    FGAGridMap& GridMap
) const
{
    // Get world, grid, and target info
    UWorld* World = GetWorld();
    const AGAGridActor* Grid = GetGridActor();
    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    FTargetCache TC;
    FTargetData TD;
    if (!GetOwner()->GetComponentByClass<UGAPerceptionComponent>()
        ->GetCurrentTargetState(TC, TD))
        return;
    FVector TargetPos = TC.Position;
    FVector Offset(0, 0, 60);

    // Loop through each cell in the sampling box
    for (int32 Y = GridMap.GridBounds.MinY; Y <= GridMap.GridBounds.MaxY; ++Y)
        for (int32 X = GridMap.GridBounds.MinX; X <= GridMap.GridBounds.MaxX; ++X)
        {
            FCellRef C(X, Y);
            ECellData CD = Grid->GetCellData(C);
            if (!EnumHasAllFlags(CD, ECellData::CellDataTraversable)) continue;

            float Dist;
            if (!DistanceMap.GetValue(C, Dist) || Dist >= FLT_MAX) continue;

            // Compute raw layer input
            float Raw = 0;
            FVector CellWorld = Grid->GetCellPosition(C);
            switch (Layer.Input)
            {
            case SI_None: break;
            case SI_TargetRange: Raw = FVector::Dist(CellWorld, TargetPos); break;
            case SI_PathDistance: Raw = Dist; break;
            case SI_LOS:
            {
                FVector Start = CellWorld + Offset;
                FHitResult Hit;
                FCollisionQueryParams P;
                P.AddIgnoredActor(PlayerPawn);
                P.AddIgnoredActor(GetOwnerPawn());
                bool bHit = World->LineTraceSingleByChannel(Hit, Start, TargetPos, ECollisionChannel::ECC_Visibility, P);
                Raw = bHit ? 0.f : 1.f;
                break;
            }
            }

            // Apply response curve and combine
            float EvalVal = Layer.ResponseCurve.GetRichCurveConst()->Eval(Raw, Raw);
            float Curr = 0, Out = 0;
            GridMap.GetValue(C, Curr);
            switch (Layer.Op)
            {
            case SO_None:     Out = Curr;                break;
            case SO_Add:      Out = Curr + EvalVal;        break;
            case SO_Multiply: Out = Curr * EvalVal;        break;
            }
            GridMap.SetValue(C, Out);
        }
}

UE_ENABLE_OPTIMIZATION
