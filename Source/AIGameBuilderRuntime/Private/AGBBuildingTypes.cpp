#include "AGBBuildingTypes.h"

FBox UAGBBuildPieceDefinition::GetLocalBounds() const
{
	const double S = GridSize * 0.5;
	const double T = Thickness;
	const double H = Height;
	switch (PieceType)
	{
	case EAGBPieceType::Foundation:
		return FBox(FVector(-S, -S, -GetFoundationDepth()), FVector(S, S, 0.0)); // Legs reach into the ground.
	case EAGBPieceType::Floor:
		return FBox(FVector(-S, -S, -T), FVector(S, S, 0.0));
	case EAGBPieceType::Wall:
	case EAGBPieceType::Doorway:
	case EAGBPieceType::Window:
		return FBox(FVector(-T * 0.5, -S, 0.0), FVector(T * 0.5, S, H));
	case EAGBPieceType::Door:
		return FBox(FVector(-T * 0.25, -S * 0.35, 0.0), FVector(T * 0.25, S * 0.35, H * 0.75));
	case EAGBPieceType::Roof:
		return FBox(FVector(-S, -S, 0.0), FVector(S, S, H * 0.5));
	case EAGBPieceType::Stairs:
		return FBox(FVector(-S, -S * 0.5, 0.0), FVector(S, S * 0.5, H));
	case EAGBPieceType::Storage:
		return FBox(FVector(-50.0, -30.0, 0.0), FVector(50.0, 30.0, 60.0));
	case EAGBPieceType::Bed:
		return FBox(FVector(-100.0, -50.0, 0.0), FVector(100.0, 50.0, 50.0));
	default:
		return FBox(FVector(-50.0, -50.0, 0.0), FVector(50.0, 50.0, 100.0));
	}
}
