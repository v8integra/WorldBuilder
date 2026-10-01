#include "AIWorldBuilderTerrainMath.h"

namespace AIWorldBuilder::TerrainMath
{
	namespace
	{
		FVector2D SeedOffset(int32 Seed)
		{
			// Large, seed-dependent offsets move each seed to an unrelated part of the noise field.
			const uint32 H1 = GetTypeHash(Seed * 7919 + 1);
			const uint32 H2 = GetTypeHash(Seed * 104729 + 7);
			return FVector2D(double(H1 % 10007) + 0.37, double(H2 % 10009) + 0.71);
		}

		double ShapeEdgeWeight(double R, double Start, double End)
		{
			return 1.0 - SmoothStep(Start, End, R);
		}

		/** Direction-only noise used to make round outlines irregular. */
		double OutlineNoise(const FVector2D& Dir, int32 Seed)
		{
			return FBM(Dir * 1.7, 3, Seed + 101);
		}
	}

	// ---------------------------------------------------------------- noise

	double Noise(const FVector2D& P, int32 Seed)
	{
		return FMath::Clamp(double(FMath::PerlinNoise2D(P + SeedOffset(Seed))), -1.0, 1.0);
	}

	double FBM(const FVector2D& P, int32 Octaves, int32 Seed)
	{
		double Sum = 0.0, Amp = 1.0, Norm = 0.0, Freq = 1.0;
		for (int32 I = 0; I < FMath::Max(1, Octaves); ++I)
		{
			Sum += Amp * Noise(P * Freq, Seed + I * 31);
			Norm += Amp;
			Amp *= 0.5;
			Freq *= 2.0;
		}
		return Sum / Norm;
	}

	double Ridged(const FVector2D& P, int32 Octaves, int32 Seed)
	{
		double Sum = 0.0, Amp = 1.0, Norm = 0.0, Freq = 1.0;
		for (int32 I = 0; I < FMath::Max(1, Octaves); ++I)
		{
			const double N = 1.0 - FMath::Abs(Noise(P * Freq, Seed + I * 31));
			Sum += Amp * N * N;
			Norm += Amp;
			Amp *= 0.5;
			Freq *= 2.0;
		}
		return Sum / Norm;
	}

	// ---------------------------------------------------------------- falloff and blending

	double SmoothStep(double Edge0, double Edge1, double X)
	{
		if (Edge1 <= Edge0)
		{
			return X < Edge0 ? 0.0 : 1.0;
		}
		const double T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Falloff(EFalloff Type, double T)
	{
		if (T >= 1.0)
		{
			return 0.0;
		}
		T = FMath::Max(T, 0.0);
		const double X = 1.0 - T;
		switch (Type)
		{
		case EFalloff::Linear: return X;
		case EFalloff::Sphere: return FMath::Sqrt(1.0 - T * T);
		case EFalloff::Tip:    return 1.0 - FMath::Sqrt(1.0 - X * X);
		case EFalloff::Smooth:
		default:               return X * X * (3.0 - 2.0 * X);
		}
	}

	double EdgeWeight(EFalloff Type, double T, double EdgeFraction)
	{
		EdgeFraction = FMath::Clamp(EdgeFraction, 0.0, 1.0);
		const double Inner = 1.0 - EdgeFraction;
		if (T <= Inner)
		{
			return 1.0;
		}
		if (EdgeFraction <= 0.0)
		{
			return 0.0;
		}
		return Falloff(Type, (T - Inner) / EdgeFraction);
	}

	double BlendHeight(EHeightBlendMode Mode, double Current, double Reference, double ShapeHeight, double Weight, double Alpha)
	{
		const double Target = Reference + ShapeHeight;
		switch (Mode)
		{
		case EHeightBlendMode::Add:     return Current + Alpha * ShapeHeight;
		case EHeightBlendMode::Max:     return FMath::Lerp(Current, FMath::Max(Current, Target), Alpha * Weight);
		case EHeightBlendMode::Min:     return FMath::Lerp(Current, FMath::Min(Current, Target), Alpha * Weight);
		case EHeightBlendMode::Replace: return FMath::Lerp(Current, Target, Weight);
		case EHeightBlendMode::Blend:
		default:                  return FMath::Lerp(Current, Target, Alpha * Weight);
		}
	}

	// ---------------------------------------------------------------- shapes

	double GetFootprintRadiusCm(ERadialShape Shape, const FRadialShapeParams& Params)
	{
		// Outlines are perturbed by up to 25% * NoiseAmount, and craters spill rim material outwards.
		const double Base = Shape == ERadialShape::Crater ? Params.RadiusCm * 1.6 : Params.RadiusCm;
		return Base * (1.0 + 0.25 * FMath::Clamp(Params.NoiseAmount, 0.0, 1.0));
	}

	FShapeSample EvaluateRadialShape(ERadialShape Shape, const FRadialShapeParams& Params, const FVector2D& OffsetCm)
	{
		FShapeSample Out;
		const double R = FMath::Max(Params.RadiusCm, 1.0);
		const double H = Params.HeightCm;
		const double Noise01 = FMath::Clamp(Params.NoiseAmount, 0.0, 1.0);
		const double Sharp = FMath::Clamp(Params.RimSharpness, 0.0, 1.0);
		const double Dist = OffsetCm.Size();
		const FVector2D Dir = Dist > KINDA_SMALL_NUMBER ? OffsetCm / Dist : FVector2D(1.0, 0.0);
		const FVector2D P = OffsetCm / R;	// noise coordinates scale with the shape

		// Irregular outline: stretch the normalized radius by direction-dependent noise.
		const double Outline = 1.0 + 0.25 * Noise01 * OutlineNoise(Dir, Params.Seed);
		const double Rn = (Dist / R) / Outline;

		switch (Shape)
		{
		case ERadialShape::Hill:
		{
			const double Base = Falloff(EFalloff::Smooth, Rn);
			Out.Height = H * Base * (1.0 + 0.15 * Noise01 * FBM(P * 2.0, 3, Params.Seed));
			Out.Weight = ShapeEdgeWeight(Rn, 0.7, 1.0);
			break;
		}
		case ERadialShape::Mountain:
		{
			const double Base = FMath::Pow(Falloff(EFalloff::Smooth, Rn), 1.5);
			// Ridges cut into the flanks; the summit area stays close to the requested height.
			const double Ridge = Ridged(P * 3.0, 5, Params.Seed);
			const double Erosion = Noise01 * (1.0 - Ridge) * SmoothStep(0.0, 0.4, Rn);
			const double Detail = 0.08 * Noise01 * FBM(P * 8.0, 4, Params.Seed + 7);
			Out.Height = H * Base * FMath::Max(0.0, 1.0 - 0.6 * Erosion + Detail);
			Out.Weight = ShapeEdgeWeight(Rn, 0.7, 1.0);
			break;
		}
		case ERadialShape::Volcano:
		{
			const double Rc = FMath::Clamp((Params.CraterRadiusCm > 0.0 ? Params.CraterRadiusCm : 0.12 * R) / R, 0.01, 0.9);
			const double D = Params.CraterDepthCm > 0.0 ? Params.CraterDepthCm : 0.2 * H;
			const double Rv = Dist / R;	// crater keeps a round rim; flanks use the irregular outline
			double Height;
			if (Rv < Rc)
			{
				// Bowl: steeper walls and a flatter floor as the rim gets sharper.
				const double V = Rv / Rc;
				const double Pow = 2.0 + 6.0 * Sharp;
				Height = H - D * (1.0 - FMath::Pow(V, Pow));
			}
			else
			{
				// Fade the irregular outline in away from the crater so the rim stays continuous.
				const double RFlank = Rv / FMath::Lerp(1.0, Outline, SmoothStep(Rc, Rc + 0.2, Rv));
				const double U = FMath::Clamp((RFlank - Rc) / FMath::Max(1.0 - Rc, 0.01), 0.0, 1.0);
				const double K = 1.2 + 0.6 * Sharp;	// concave stratovolcano flanks
				Height = H * FMath::Pow(1.0 - U, K);
				// Eroded gullies, stronger lower down.
				Height *= 1.0 - 0.3 * Noise01 * (1.0 - Ridged(P * 4.0, 5, Params.Seed)) * U;
			}
			// Raised rim lip, symmetric about the rim so both sides meet without a step.
			Height += H * 0.02 * Sharp * FMath::Exp(-FMath::Square((Rv - Rc) / 0.035));

			// Lava channels: notches in the rim plus grooves running down the flank.
			if (Params.LavaChannels > 0)
			{
				const double Angle = FMath::Atan2(Dir.Y, Dir.X);
				for (int32 C = 0; C < Params.LavaChannels; ++C)
				{
					const double Jitter = 0.35 * Noise(FVector2D(C * 1.37, 0.5), Params.Seed + 211);
					const double ChannelAngle = 2.0 * PI * (C + 0.5 + Jitter) / Params.LavaChannels;
					const double DAngle = FMath::Abs(FMath::FindDeltaAngleRadians(Angle, ChannelAngle));
					const double Across = FMath::Exp(-FMath::Square(DAngle / 0.09));
					const double NearRim = FMath::Exp(-FMath::Square((Rv - Rc) / 0.05));
					const double Down = Rv > Rc ? (1.0 - SmoothStep(Rc, 1.0, Rv)) : 0.0;
					Height -= Across * (0.6 * D * NearRim + 0.05 * H * Down);
				}
			}
			Out.Height = Height;
			Out.Weight = ShapeEdgeWeight(Rn, 0.7, 1.0);
			break;
		}
		case ERadialShape::Crater:
		{
			// H is the depth below the original ground; the rim rises to a fraction of it.
			const double RimH = H * 0.25 * (0.5 + Sharp);
			double Height;
			if (Rn < 1.0)
			{
				Height = -H * (1.0 - Rn * Rn) + RimH * SmoothStep(0.55, 1.0, Rn);
			}
			else
			{
				Height = RimH * FMath::Exp(-FMath::Square((Rn - 1.0) / 0.22) * (1.0 + 2.0 * Sharp));
			}
			Out.Height = Height * (1.0 + 0.1 * Noise01 * FBM(P * 4.0, 3, Params.Seed));
			Out.Weight = ShapeEdgeWeight(Rn, 1.3, 1.6);
			break;
		}
		case ERadialShape::Plateau:
		{
			const double Top = FMath::Clamp(Params.TopFraction, 0.05, 0.95);
			const double EdgeEnd = Top + (1.0 - Top) * (1.0 - 0.8 * Sharp);
			double Height = H * (1.0 - SmoothStep(Top, EdgeEnd, Rn));
			Height += H * 0.02 * Noise01 * FBM(P * 3.0, 3, Params.Seed) * (Rn < Top ? 1.0 : 0.0);
			Out.Height = Height;
			Out.Weight = ShapeEdgeWeight(Rn, 0.85, 1.0);
			break;
		}
		case ERadialShape::Mesa:
		default:
		{
			// Flat top, near-vertical cliff down to ~35% height, then a concave talus slope to the base.
			const double Top = FMath::Clamp(Params.TopFraction, 0.05, 0.9);
			const double CliffWidth = 0.02 + 0.08 * (1.0 - Sharp);
			const double Talus = 0.35 * FMath::Pow(1.0 - SmoothStep(Top, 1.0, Rn), 1.5);
			const double Cliff = 1.0 - SmoothStep(Top, Top + CliffWidth, Rn);
			Out.Height = H * (Talus + (1.0 - Talus) * Cliff);
			Out.Weight = ShapeEdgeWeight(Rn, 0.85, 1.0);
			break;
		}
		}
		return Out;
	}

	FShapeSample EvaluateLineShape(ELineShape Shape, const FLineShapeParams& Params, const FVector2D& PointCm, double& OutAlong)
	{
		FShapeSample Out;
		const FVector2D AB = Params.EndCm - Params.StartCm;
		const double Len = AB.Size();
		const FVector2D Axis = Len > KINDA_SMALL_NUMBER ? AB / Len : FVector2D(1.0, 0.0);
		const FVector2D Perp(-Axis.Y, Axis.X);
		const double HalfW = FMath::Max(Params.HalfWidthCm, 1.0);
		const double Noise01 = FMath::Clamp(Params.NoiseAmount, 0.0, 1.0);

		const FVector2D Rel = PointCm - Params.StartCm;
		const double AlongCm = FVector2D::DotProduct(Rel, Axis);
		OutAlong = Len > KINDA_SMALL_NUMBER ? FMath::Clamp(AlongCm / Len, 0.0, 1.0) : 0.0;

		// Meander: shift the centre line sideways with noise along its length.
		const double Wobble = Noise01 * 0.5 * HalfW * FBM(FVector2D(AlongCm / (4.0 * HalfW), 0.3), 3, Params.Seed);
		const double Across = FVector2D::DotProduct(Rel, Perp) - Wobble;
		const double Beyond = AlongCm < 0.0 ? -AlongCm : (AlongCm > Len ? AlongCm - Len : 0.0);
		const double S = FMath::Sqrt(Across * Across + Beyond * Beyond) / HalfW;

		const double Profile = Falloff(EFalloff::Smooth, S);
		const double Variation = 1.0 + 0.35 * Noise01 * FBM(FVector2D(AlongCm / (2.0 * HalfW), 7.1), 4, Params.Seed + 3);
		Out.Height = (Shape == ELineShape::Ridge ? 1.0 : -1.0) * Params.HeightCm * Profile * Variation;
		Out.Weight = ShapeEdgeWeight(S, 0.7, 1.0);
		return Out;
	}

	// ---------------------------------------------------------------- geometry helpers

	TArray<double> PolylineCumulativeLengths(TConstArrayView<FVector2D> Points)
	{
		TArray<double> Lengths;
		Lengths.Reserve(Points.Num());
		double Total = 0.0;
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			if (I > 0)
			{
				Total += FVector2D::Distance(Points[I - 1], Points[I]);
			}
			Lengths.Add(Total);
		}
		return Lengths;
	}

	double DistanceToPolyline(const FVector2D& P, TConstArrayView<FVector2D> Points, double& OutAlong)
	{
		OutAlong = 0.0;
		if (Points.IsEmpty())
		{
			return TNumericLimits<double>::Max();
		}
		if (Points.Num() == 1)
		{
			return FVector2D::Distance(P, Points[0]);
		}

		double Best = TNumericLimits<double>::Max();
		double Walked = 0.0;
		for (int32 I = 0; I + 1 < Points.Num(); ++I)
		{
			const FVector2D A = Points[I];
			const FVector2D AB = Points[I + 1] - A;
			const double SegLenSq = AB.SizeSquared();
			const double T = SegLenSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / SegLenSq, 0.0, 1.0) : 0.0;
			const double D = FVector2D::Distance(P, A + AB * T);
			const double SegLen = FMath::Sqrt(SegLenSq);
			if (D < Best)
			{
				Best = D;
				OutAlong = Walked + T * SegLen;
			}
			Walked += SegLen;
		}
		return Best;
	}

	namespace
	{
		struct FSlopeStop { double Degrees; FColor Color; const TCHAR* Name; };
		const FSlopeStop SlopeStops[] = {
			{  0.0, FColor( 34, 139,  34), TEXT("green") },
			{ 10.0, FColor(154, 205,  50), TEXT("yellow-green") },
			{ 20.0, FColor(255, 215,   0), TEXT("yellow") },
			{ 30.0, FColor(255, 140,   0), TEXT("orange") },
			{ 40.0, FColor(220,  20,  60), TEXT("red") },
			{ 55.0, FColor(128,   0, 128), TEXT("purple") },
		};
	}

	FColor SlopeToColor(double SlopeDegrees)
	{
		const int32 Count = UE_ARRAY_COUNT(SlopeStops);
		if (SlopeDegrees <= SlopeStops[0].Degrees)
		{
			return SlopeStops[0].Color;
		}
		for (int32 I = 1; I < Count; ++I)
		{
			if (SlopeDegrees <= SlopeStops[I].Degrees)
			{
				const double T = (SlopeDegrees - SlopeStops[I - 1].Degrees) / (SlopeStops[I].Degrees - SlopeStops[I - 1].Degrees);
				const FColor A = SlopeStops[I - 1].Color, B = SlopeStops[I].Color;
				auto Mix = [T](uint8 X, uint8 Y) { return static_cast<uint8>(FMath::RoundToInt32(FMath::Lerp(double(X), double(Y), T))); };
				return FColor(Mix(A.R, B.R), Mix(A.G, B.G), Mix(A.B, B.B), 255);
			}
		}
		return SlopeStops[Count - 1].Color;
	}

	FString SlopeLegend()
	{
		TArray<FString> Parts;
		for (const FSlopeStop& Stop : SlopeStops)
		{
			Parts.Add(FString::Printf(TEXT("%s=%.0f deg"), Stop.Name, Stop.Degrees));
		}
		return FString::Join(Parts, TEXT(", ")) + TEXT(" (and steeper); magenta = no data");
	}

	FColor HeightToGrey(double Height, double Min, double Max)
	{
		const double T = Max > Min ? FMath::Clamp((Height - Min) / (Max - Min), 0.0, 1.0) : 0.5;
		const uint8 G = static_cast<uint8>(FMath::RoundToInt32(T * 255.0));
		return FColor(G, G, G, 255);
	}

	void BoxBlur(TArray<double>& Grid, int32 Width, int32 Height, int32 RadiusSamples, int32 Passes)
	{
		if (RadiusSamples <= 0 || Width <= 0 || Height <= 0 || Grid.Num() != Width * Height)
		{
			return;
		}

		TArray<double> Temp;
		Temp.SetNumUninitialized(Grid.Num());
		TArray<double> Prefix;
		const int32 Window = 2 * RadiusSamples + 1;

		auto BlurLine = [&](const double* In, double* Out, int32 Count, int32 Stride)
		{
			// Prefix sums over the line with edge clamping.
			Prefix.SetNumUninitialized(Count + 1);
			Prefix[0] = 0.0;
			for (int32 I = 0; I < Count; ++I)
			{
				Prefix[I + 1] = Prefix[I] + In[I * Stride];
			}
			for (int32 I = 0; I < Count; ++I)
			{
				const int32 Lo = I - RadiusSamples;
				const int32 Hi = I + RadiusSamples;
				double Sum = Prefix[FMath::Min(Hi, Count - 1) + 1] - Prefix[FMath::Max(Lo, 0)];
				// Clamp: samples beyond each edge repeat the edge value.
				if (Lo < 0) { Sum += -Lo * In[0]; }
				if (Hi > Count - 1) { Sum += (Hi - (Count - 1)) * In[(Count - 1) * Stride]; }
				Out[I * Stride] = Sum / Window;
			}
		};

		for (int32 Pass = 0; Pass < FMath::Max(1, Passes); ++Pass)
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				BlurLine(&Grid[Y * Width], &Temp[Y * Width], Width, 1);
			}
			for (int32 X = 0; X < Width; ++X)
			{
				BlurLine(&Temp[X], &Grid[X], Height, Width);
			}
		}
	}
}
