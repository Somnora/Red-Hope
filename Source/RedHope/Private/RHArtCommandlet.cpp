#include "RHArtCommandlet.h"
#include "RedHope.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

int32 URHArtCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const FString PackagePath = TEXT("/Game/RedHope/Art/M_MarsSurface");
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	UMaterial* Mat = NewObject<UMaterial>(Package, TEXT("M_MarsSurface"), RF_Public | RF_Standalone);
	const auto Expr = [&]<typename T>(T*) -> T*
	{
		T* E = NewObject<T>(Mat);
		Mat->GetExpressionCollection().AddExpression(E);
		E->Material = Mat;
		return E;
	};

	// --- Parameters.
	auto* Tex = Expr((UMaterialExpressionTextureObjectParameter*)nullptr);
	Tex->ParameterName = FName("SurfTex");
	Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
	Tex->SamplerType = SAMPLERTYPE_Color;
	auto* Tile = Expr((UMaterialExpressionScalarParameter*)nullptr);
	Tile->ParameterName = FName("TileCm");
	Tile->DefaultValue = 900.f;
	auto* Tint = Expr((UMaterialExpressionVectorParameter*)nullptr);
	Tint->ParameterName = FName("Tint");
	Tint->DefaultValue = FLinearColor::White;
	auto* Rough = Expr((UMaterialExpressionScalarParameter*)nullptr);
	Rough->ParameterName = FName("Rough");
	Rough->DefaultValue = 0.95f;

	// --- Triplanar: three world-plane projections of SurfTex, blended by the
	// axis-aligned share of the surface normal. Terrain/boulders have no UVs;
	// this needs none.
	auto* WPos = Expr((UMaterialExpressionWorldPosition*)nullptr);
	const auto Plane = [&](bool R, bool G, bool B) -> UMaterialExpressionTextureSample*
	{
		auto* Mask = Expr((UMaterialExpressionComponentMask*)nullptr);
		Mask->Input.Connect(0, WPos);
		Mask->R = R; Mask->G = G; Mask->B = B; Mask->A = 0;
		auto* Div = Expr((UMaterialExpressionDivide*)nullptr);
		Div->A.Connect(0, Mask);
		Div->B.Connect(0, Tile);
		auto* Sample = Expr((UMaterialExpressionTextureSample*)nullptr);
		Sample->TextureObject.Connect(0, Tex);
		Sample->Coordinates.Connect(0, Div);
		Sample->SamplerType = SAMPLERTYPE_Color;
		return Sample;
	};
	UMaterialExpressionTextureSample* SampXY = Plane(true, true, false);  // floor-ish faces
	UMaterialExpressionTextureSample* SampXZ = Plane(true, false, true);  // walls facing Y
	UMaterialExpressionTextureSample* SampYZ = Plane(false, true, true);  // walls facing X

	auto* Normal = Expr((UMaterialExpressionVertexNormalWS*)nullptr);
	auto* AbsN = Expr((UMaterialExpressionAbs*)nullptr);
	AbsN->Input.Connect(0, Normal);
	const auto Axis = [&](bool R, bool G, bool B) -> UMaterialExpressionComponentMask*
	{
		auto* Mask = Expr((UMaterialExpressionComponentMask*)nullptr);
		Mask->Input.Connect(0, AbsN);
		Mask->R = R; Mask->G = G; Mask->B = B; Mask->A = 0;
		return Mask;
	};
	auto* NX = Axis(true, false, false);
	auto* NY = Axis(false, true, false);
	auto* NZ = Axis(false, false, true);
	auto* SumA = Expr((UMaterialExpressionAdd*)nullptr);
	SumA->A.Connect(0, NX);
	SumA->B.Connect(0, NY);
	auto* SumN = Expr((UMaterialExpressionAdd*)nullptr);
	SumN->A.Connect(0, SumA);
	SumN->B.Connect(0, NZ);

	const auto Weighted = [&](UMaterialExpressionTextureSample* Sample, UMaterialExpressionComponentMask* AxisN)
		-> UMaterialExpressionMultiply*
	{
		auto* W = Expr((UMaterialExpressionDivide*)nullptr);
		W->A.Connect(0, AxisN);
		W->B.Connect(0, SumN);
		auto* Mul = Expr((UMaterialExpressionMultiply*)nullptr);
		Mul->A.Connect(0, Sample);
		Mul->B.Connect(0, W);
		return Mul;
	};
	auto* BlendA = Expr((UMaterialExpressionAdd*)nullptr);
	BlendA->A.Connect(0, Weighted(SampXY, NZ)); // XY projection shows on up-facing surface
	BlendA->B.Connect(0, Weighted(SampXZ, NY));
	auto* Blend = Expr((UMaterialExpressionAdd*)nullptr);
	Blend->A.Connect(0, BlendA);
	Blend->B.Connect(0, Weighted(SampYZ, NX));

	auto* Final = Expr((UMaterialExpressionMultiply*)nullptr);
	Final->A.Connect(0, Blend);
	Final->B.Connect(0, Tint);

	UMaterialEditorOnlyData* Data = Mat->GetEditorOnlyData();
	Data->BaseColor.Connect(0, Final);
	Data->Roughness.Connect(0, Rough);
	// Baked-in usage flags: everything here renders on ISMs and procedural
	// meshes. Without these, -game runs silently swap in the DEFAULT material
	// (editor runs auto-add them, which masks the bug).
	Mat->bUsedWithInstancedStaticMeshes = true;
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();

	const FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Mat, *FileName, SaveArgs))
	{
		UE_LOG(LogRedHope, Error, TEXT("RHArt: SavePackage failed for %s"), *PackagePath);
		return 1;
	}
	UE_LOG(LogRedHope, Display, TEXT("RHArt: authored + saved %s (%d expressions, default tex %s)"),
		*PackagePath, Mat->GetExpressionCollection().Expressions.Num(),
		Tex->Texture ? *Tex->Texture->GetName() : TEXT("NONE"));

	// M_Graybox (robots, buildings, terrain scatter fallback) has shipped
	// without the ISM usage flag since day one - the smoke logs warn it falls
	// back to the default material in -game. Flip + resave in place; the
	// MCP-authored graph is untouched.
	if (UMaterial* Graybox = LoadObject<UMaterial>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
	{
		if (!Graybox->bUsedWithInstancedStaticMeshes)
		{
			Graybox->bUsedWithInstancedStaticMeshes = true;
			Graybox->PreEditChange(nullptr);
			Graybox->PostEditChange();
			const FString GbFile = FPackageName::LongPackageNameToFilename(
				TEXT("/Game/RedHope/Art/M_Graybox"), FPackageName::GetAssetPackageExtension());
			const bool bSaved = UPackage::SavePackage(Graybox->GetOutermost(), Graybox, *GbFile, SaveArgs);
			UE_LOG(LogRedHope, Display, TEXT("RHArt: M_Graybox ISM usage flag set, resave %s"),
				bSaved ? TEXT("OK") : TEXT("FAILED"));
		}
		else
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArt: M_Graybox ISM usage flag already set"));
		}
	}
	return 0;
#else
	UE_LOG(LogRedHope, Error, TEXT("RHArt: editor-only commandlet"));
	return 1;
#endif
}
