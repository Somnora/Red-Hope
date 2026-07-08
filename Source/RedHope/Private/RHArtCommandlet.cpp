#include "RHArtCommandlet.h"
#include "RedHope.h"

#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	bool SaveAsset(UObject* Asset)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *FileName, SaveArgs);
	}

	// ImportAssets brings normal maps in as plain color textures; wrong
	// compression + sRGB makes them shade visibly wrong. Idempotent.
	void FixNormalTexture(const TCHAR* Path)
	{
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Path);
		if (!Tex)
		{
			UE_LOG(LogRedHope, Warning, TEXT("RHArt: normal texture missing: %s"), Path);
			return;
		}
		if (Tex->CompressionSettings == TC_Normalmap && !Tex->SRGB)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArt: %s already normal-configured"), *Tex->GetName());
			return;
		}
		Tex->PreEditChange(nullptr);
		Tex->CompressionSettings = TC_Normalmap;
		Tex->SRGB = false;
		Tex->LODGroup = TEXTUREGROUP_WorldNormalMap;
		Tex->PostEditChange();
		UE_LOG(LogRedHope, Display, TEXT("RHArt: %s -> TC_Normalmap/sRGB-off, resave %s"),
			*Tex->GetName(), SaveAsset(Tex) ? TEXT("OK") : TEXT("FAILED"));
	}

	UMaterial* NewMaterialPackage(const FString& PackagePath, const TCHAR* AssetName)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		return NewObject<UMaterial>(Package, AssetName, RF_Public | RF_Standalone);
	}

	template <typename T>
	T* Expr(UMaterial* Mat)
	{
		T* E = NewObject<T>(Mat);
		Mat->GetExpressionCollection().AddExpression(E);
		E->Material = Mat;
		return E;
	}

	// The triplanar surface material: world-aligned texture (no UVs needed -
	// procedural terrain, boulders, and stretched ISM cubes all tile cleanly)
	// with matching triplanar normal mapping (world-space perturbation - the
	// same UV-less meshes have no usable tangent basis either).
	bool AuthorMarsSurface()
	{
		UMaterial* Mat = NewMaterialPackage(TEXT("/Game/RedHope/Art/M_MarsSurface"), TEXT("M_MarsSurface"));

		// --- Parameters.
		auto* Tex = Expr<UMaterialExpressionTextureObjectParameter>(Mat);
		Tex->ParameterName = FName("SurfTex");
		Tex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
		Tex->SamplerType = SAMPLERTYPE_Color;
		auto* NTex = Expr<UMaterialExpressionTextureObjectParameter>(Mat);
		NTex->ParameterName = FName("NormTex");
		NTex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture_Normal.Mars_Regolith_Texture_Normal"));
		NTex->SamplerType = SAMPLERTYPE_Normal;
		auto* Tile = Expr<UMaterialExpressionScalarParameter>(Mat);
		Tile->ParameterName = FName("TileCm");
		Tile->DefaultValue = 900.f;
		auto* Tint = Expr<UMaterialExpressionVectorParameter>(Mat);
		Tint->ParameterName = FName("Tint");
		Tint->DefaultValue = FLinearColor::White;
		auto* Rough = Expr<UMaterialExpressionScalarParameter>(Mat);
		Rough->ParameterName = FName("Rough");
		Rough->DefaultValue = 0.95f;
		auto* NStrength = Expr<UMaterialExpressionScalarParameter>(Mat);
		NStrength->ParameterName = FName("NormalStrength");
		NStrength->DefaultValue = 1.f;

		// --- Shared plane coordinates: world position / TileCm per plane.
		auto* WPos = Expr<UMaterialExpressionWorldPosition>(Mat);
		const auto PlaneUV = [&](bool R, bool G, bool B) -> UMaterialExpressionDivide*
		{
			auto* Mask = Expr<UMaterialExpressionComponentMask>(Mat);
			Mask->Input.Connect(0, WPos);
			Mask->R = R; Mask->G = G; Mask->B = B; Mask->A = 0;
			auto* Div = Expr<UMaterialExpressionDivide>(Mat);
			Div->A.Connect(0, Mask);
			Div->B.Connect(0, Tile);
			return Div;
		};
		UMaterialExpressionDivide* UVxy = PlaneUV(true, true, false);
		UMaterialExpressionDivide* UVxz = PlaneUV(true, false, true);
		UMaterialExpressionDivide* UVyz = PlaneUV(false, true, true);
		const auto Sample = [&](UMaterialExpressionTextureObjectParameter* From,
			UMaterialExpressionDivide* UV, EMaterialSamplerType Sampler) -> UMaterialExpressionTextureSample*
		{
			auto* S = Expr<UMaterialExpressionTextureSample>(Mat);
			S->TextureObject.Connect(0, From);
			S->Coordinates.Connect(0, UV);
			S->SamplerType = Sampler;
			return S;
		};

		// --- Axis weights: |vertex normal| shares, normalized to sum 1.
		auto* Normal = Expr<UMaterialExpressionVertexNormalWS>(Mat);
		auto* AbsN = Expr<UMaterialExpressionAbs>(Mat);
		AbsN->Input.Connect(0, Normal);
		const auto AxisMask = [&](bool R, bool G, bool B) -> UMaterialExpressionComponentMask*
		{
			auto* Mask = Expr<UMaterialExpressionComponentMask>(Mat);
			Mask->Input.Connect(0, AbsN);
			Mask->R = R; Mask->G = G; Mask->B = B; Mask->A = 0;
			return Mask;
		};
		auto* NX = AxisMask(true, false, false);
		auto* NY = AxisMask(false, true, false);
		auto* NZ = AxisMask(false, false, true);
		auto* SumA = Expr<UMaterialExpressionAdd>(Mat);
		SumA->A.Connect(0, NX);
		SumA->B.Connect(0, NY);
		auto* SumN = Expr<UMaterialExpressionAdd>(Mat);
		SumN->A.Connect(0, SumA);
		SumN->B.Connect(0, NZ);
		const auto Weight = [&](UMaterialExpressionComponentMask* AxisN) -> UMaterialExpressionDivide*
		{
			auto* W = Expr<UMaterialExpressionDivide>(Mat);
			W->A.Connect(0, AxisN);
			W->B.Connect(0, SumN);
			return W;
		};
		UMaterialExpressionDivide* Wx = Weight(NX);
		UMaterialExpressionDivide* Wy = Weight(NY);
		UMaterialExpressionDivide* Wz = Weight(NZ);

		// --- Base color: three weighted projections, tinted.
		const auto WeightedColor = [&](UMaterialExpressionDivide* UV, UMaterialExpressionDivide* W)
			-> UMaterialExpressionMultiply*
		{
			auto* Mul = Expr<UMaterialExpressionMultiply>(Mat);
			Mul->A.Connect(0, Sample(Tex, UV, SAMPLERTYPE_Color));
			Mul->B.Connect(0, W);
			return Mul;
		};
		auto* ColA = Expr<UMaterialExpressionAdd>(Mat);
		ColA->A.Connect(0, WeightedColor(UVxy, Wz)); // XY projection shows on up-facing surface
		ColA->B.Connect(0, WeightedColor(UVxz, Wy));
		auto* ColSum = Expr<UMaterialExpressionAdd>(Mat);
		ColSum->A.Connect(0, ColA);
		ColSum->B.Connect(0, WeightedColor(UVyz, Wx));
		auto* Final = Expr<UMaterialExpressionMultiply>(Mat);
		Final->A.Connect(0, ColSum);
		Final->B.Connect(0, Tint);

		// --- Normal: each projection's tangent-space sample perturbs the two
		// world axes of its plane (the map is noise-like detail; exact
		// handedness doesn't matter), weighted by axis share x NormalStrength,
		// summed onto the vertex normal and renormalized. World-space output.
		const auto NSampleRG = [&](UMaterialExpressionDivide* UV) -> UMaterialExpressionComponentMask*
		{
			auto* Mask = Expr<UMaterialExpressionComponentMask>(Mat);
			Mask->Input.Connect(0, Sample(NTex, UV, SAMPLERTYPE_Normal));
			Mask->R = 1; Mask->G = 1; Mask->B = 0; Mask->A = 0;
			return Mask;
		};
		const auto MaskOf = [&](UMaterialExpression* From, bool R, bool G) -> UMaterialExpressionComponentMask*
		{
			auto* Mask = Expr<UMaterialExpressionComponentMask>(Mat);
			Mask->Input.Connect(0, From);
			Mask->R = R; Mask->G = G; Mask->B = 0; Mask->A = 0;
			return Mask;
		};
		const auto Append = [&](UMaterialExpression* A, UMaterialExpression* B) -> UMaterialExpressionAppendVector*
		{
			auto* Ap = Expr<UMaterialExpressionAppendVector>(Mat);
			Ap->A.Connect(0, A);
			Ap->B.Connect(0, B);
			return Ap;
		};
		auto* Zero = Expr<UMaterialExpressionConstant>(Mat);
		Zero->R = 0.f;
		// Plane RG -> world 3-vector: XY plane perturbs (x,y,0); XZ plane
		// (x,0,z); YZ plane (0,y,z).
		UMaterialExpressionComponentMask* RGxy = NSampleRG(UVxy);
		UMaterialExpressionComponentMask* RGxz = NSampleRG(UVxz);
		UMaterialExpressionComponentMask* RGyz = NSampleRG(UVyz);
		UMaterialExpression* Pxy = Append(RGxy, Zero);
		UMaterialExpression* Pxz = Append(Append(MaskOf(RGxz, true, false), Zero), MaskOf(RGxz, false, true));
		UMaterialExpression* Pyz = Append(Append(Zero, MaskOf(RGyz, true, false)), MaskOf(RGyz, false, true));
		const auto WeightedPerturb = [&](UMaterialExpression* P, UMaterialExpressionDivide* W) -> UMaterialExpressionMultiply*
		{
			auto* WS = Expr<UMaterialExpressionMultiply>(Mat);
			WS->A.Connect(0, W);
			WS->B.Connect(0, NStrength);
			auto* Mul = Expr<UMaterialExpressionMultiply>(Mat);
			Mul->A.Connect(0, P);
			Mul->B.Connect(0, WS);
			return Mul;
		};
		auto* PerA = Expr<UMaterialExpressionAdd>(Mat);
		PerA->A.Connect(0, WeightedPerturb(Pxy, Wz));
		PerA->B.Connect(0, WeightedPerturb(Pxz, Wy));
		auto* PerSum = Expr<UMaterialExpressionAdd>(Mat);
		PerSum->A.Connect(0, PerA);
		PerSum->B.Connect(0, WeightedPerturb(Pyz, Wx));
		auto* NormSum = Expr<UMaterialExpressionAdd>(Mat);
		NormSum->A.Connect(0, Normal);
		NormSum->B.Connect(0, PerSum);
		auto* NormOut = Expr<UMaterialExpressionNormalize>(Mat);
		NormOut->VectorInput.Connect(0, NormSum);

		UMaterialEditorOnlyData* Data = Mat->GetEditorOnlyData();
		Data->BaseColor.Connect(0, Final);
		Data->Roughness.Connect(0, Rough);
		Data->Normal.Connect(0, NormOut);
		Mat->bTangentSpaceNormal = false; // world-space normal output
		Mat->bUsedWithInstancedStaticMeshes = true;
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		const bool bSaved = SaveAsset(Mat);
		UE_LOG(LogRedHope, Display, TEXT("RHArt: authored + saved M_MarsSurface (%d expressions, triplanar color+normal) %s"),
			Mat->GetExpressionCollection().Expressions.Num(), bSaved ? TEXT("OK") : TEXT("FAILED"));
		return bSaved;
	}

	// The GLB model material: image-to-3D exports (TripoSR/InstantMesh) bake
	// their color into COLOR_0 vertex colors with no textures; the importer's
	// DefaultMaterial ignores them and renders gray. This shows them.
	bool AuthorVertexColor()
	{
		UMaterial* Mat = NewMaterialPackage(TEXT("/Game/RedHope/Art/M_VertexColor"), TEXT("M_VertexColor"));
		auto* VC = Expr<UMaterialExpressionVertexColor>(Mat);
		auto* Tint = Expr<UMaterialExpressionVectorParameter>(Mat);
		Tint->ParameterName = FName("Tint");
		Tint->DefaultValue = FLinearColor::White;
		auto* Rough = Expr<UMaterialExpressionScalarParameter>(Mat);
		Rough->ParameterName = FName("Rough");
		Rough->DefaultValue = 0.6f;
		auto* Mul = Expr<UMaterialExpressionMultiply>(Mat);
		Mul->A.Connect(0, VC);
		Mul->B.Connect(0, Tint);
		UMaterialEditorOnlyData* Data = Mat->GetEditorOnlyData();
		Data->BaseColor.Connect(0, Mul);
		Data->Roughness.Connect(0, Rough);
		Mat->bUsedWithInstancedStaticMeshes = true;
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		const bool bSaved = SaveAsset(Mat);
		UE_LOG(LogRedHope, Display, TEXT("RHArt: authored + saved M_VertexColor %s"), bSaved ? TEXT("OK") : TEXT("FAILED"));
		return bSaved;
	}

	// Receipts for imported GLB models: without painted vertex colors the
	// vertex-color material renders black-on-white, so log the evidence.
	void ReportModel(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
		{
			UE_LOG(LogRedHope, Warning, TEXT("RHArt: model missing or no render data: %s"), Path);
			return;
		}
		const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
		const FBoxSphereBounds B = Mesh->GetBounds();
		UE_LOG(LogRedHope, Display, TEXT("RHArt: model %s: %d tris, %d verts, %u color verts, bounds origin (%.0f %.0f %.0f) extent (%.0f %.0f %.0f)"),
			*Mesh->GetName(), LOD.GetNumTriangles(), LOD.GetNumVertices(),
			LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices(),
			B.Origin.X, B.Origin.Y, B.Origin.Z, B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z);
	}
}
#endif // WITH_EDITOR

int32 URHArtCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	// Order matters: normal maps must be TC_Normalmap BEFORE the material
	// referencing them compiles, or the sampler type mismatches.
	FixNormalTexture(TEXT("/Game/RedHope/Art/Mars_Regolith_Texture_Normal.Mars_Regolith_Texture_Normal"));
	FixNormalTexture(TEXT("/Game/RedHope/Art/Cavern_Basalt_Texture_Normal.Cavern_Basalt_Texture_Normal"));
	FixNormalTexture(TEXT("/Game/RedHope/Art/Steel_Grid_Texture_Normal.Steel_Grid_Texture_Normal"));

	bool bOk = AuthorMarsSurface();
	bOk &= AuthorVertexColor();

	// M_Graybox (robots, buildings, scatter fallback) shipped without the ISM
	// usage flag - -game runs swapped in the DEFAULT material. Keep enforced.
	if (UMaterial* Graybox = LoadObject<UMaterial>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
	{
		if (!Graybox->bUsedWithInstancedStaticMeshes)
		{
			Graybox->bUsedWithInstancedStaticMeshes = true;
			Graybox->PreEditChange(nullptr);
			Graybox->PostEditChange();
			UE_LOG(LogRedHope, Display, TEXT("RHArt: M_Graybox ISM usage flag set, resave %s"),
				SaveAsset(Graybox) ? TEXT("OK") : TEXT("FAILED"));
		}
		else
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArt: M_Graybox ISM usage flag already set"));
		}
	}

	ReportModel(TEXT("/Game/RedHope/Art/Models/forge/StaticMeshes/forge.forge"));
	return bOk ? 0 : 1;
#else
	UE_LOG(LogRedHope, Error, TEXT("RHArt: editor-only commandlet"));
	return 1;
#endif
}
