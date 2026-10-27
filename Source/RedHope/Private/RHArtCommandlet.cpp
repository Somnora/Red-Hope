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
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialParameterCollection.h"
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

	// M_RH_BuildReveal (visual pass item 2, 2026-10-26): a construction site's own model rising inside its scaffold
	// (docs/qa/2026-10-25/visual-pass-design.md section 2, DIRECTOR ANSWER Q-A (a)). It is M_RH_Master's texture inputs
	// (BaseTex, MRTex, NormTex with UseMRTex / UseNormTex, Metallic, Rough, the accent lerp and the EmissiveFloor lift;
	// no machine glow - a site is not running) plus a world-Z clip: the surface draws only below BuildCutZ (world cm),
	// so the colony visualizer raises the cut with the build fraction and the model grows bottom-up. Just under the cut
	// a thin teal line glows (CutLineCm deep, CutColor x CutGlow x MPC_Atmosphere.GlowScale - the rh.Glow dial - and
	// only on faces that stand across the cut, not on a member lying flat in it), and the model's inside draws in
	// InteriorColor rather than the hull's paint: the faces seen from behind, and the faces closed inside the solid,
	// black in the model's HiddenMask (scripts/blender/rh_bake_hidden.py: never seen by the strategy camera on the
	// finished model; the 2026-10-26 review saw a magenta slab inside the Electrolyzer and lit beige floors inside the
	// Forge and the AirFilter at the cut). HiddenMask defaults to white (nothing hidden), so a model without one keeps
	// only the back-face rule. Masked and two-sided; the ISM and
	// static-lighting usage flags are set so no mesh path swaps in the default material. The visualizer puts a MID of it
	// on each slot of a site's model, its values copied from that slot's MI_<name> by parameter name.
	bool AuthorBuildReveal()
	{
		UMaterial* Mat = NewMaterialPackage(TEXT("/Game/RedHope/Art/M_RH_BuildReveal"), TEXT("M_RH_BuildReveal"));

		// --- M_RH_Master's inputs, same names and defaults (rh_author_master.py).
		auto* BaseTex = Expr<UMaterialExpressionTextureSampleParameter2D>(Mat);
		BaseTex->ParameterName = FName("BaseTex");
		BaseTex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
		BaseTex->SamplerType = SAMPLERTYPE_Color;
		auto* MRTex = Expr<UMaterialExpressionTextureSampleParameter2D>(Mat);
		MRTex->ParameterName = FName("MRTex");
		MRTex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Masks/T_RH_MaskWhite.T_RH_MaskWhite"));
		MRTex->SamplerType = SAMPLERTYPE_Masks; // T_RH_MaskWhite and every MR map are TC_MASKS (the 08-16 sweep)
		auto* NormTex = Expr<UMaterialExpressionTextureSampleParameter2D>(Mat);
		NormTex->ParameterName = FName("NormTex");
		NormTex->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
		NormTex->SamplerType = SAMPLERTYPE_Normal;
		const auto Scalar = [Mat](const TCHAR* Name, float Default) -> UMaterialExpressionScalarParameter*
		{
			auto* P = Expr<UMaterialExpressionScalarParameter>(Mat);
			P->ParameterName = FName(Name);
			P->DefaultValue = Default;
			return P;
		};
		const auto Vector = [Mat](const TCHAR* Name, const FLinearColor& Default) -> UMaterialExpressionVectorParameter*
		{
			auto* P = Expr<UMaterialExpressionVectorParameter>(Mat);
			P->ParameterName = FName(Name);
			P->DefaultValue = Default;
			return P;
		};
		const auto Lerp = [Mat](UMaterialExpression* A, int32 AOut, UMaterialExpression* B, int32 BOut, UMaterialExpression* Alpha)
		{
			auto* L = Expr<UMaterialExpressionLinearInterpolate>(Mat);
			L->A.Connect(AOut, A);
			L->B.Connect(BOut, B);
			L->Alpha.Connect(0, Alpha);
			return L;
		};
		auto* UseMR = Scalar(TEXT("UseMRTex"), 0.f);
		auto* UseNorm = Scalar(TEXT("UseNormTex"), 0.f);
		auto* Metallic = Scalar(TEXT("Metallic"), 0.f);
		auto* Rough = Scalar(TEXT("Rough"), 0.75f);
		auto* AccentColor = Vector(TEXT("AccentColor"), FLinearColor(0.5f, 0.5f, 0.5f));
		auto* AccentAmount = Scalar(TEXT("AccentAmount"), 0.f);
		auto* Floor = Scalar(TEXT("EmissiveFloor"), 0.08f);
		// --- The reveal. BuildCutZ defaults far above any model, so the bare material draws whole.
		auto* CutZ = Scalar(TEXT("BuildCutZ"), 1.0e7f);
		auto* LineCm = Scalar(TEXT("CutLineCm"), 8.f);
		auto* CutColor = Vector(TEXT("CutColor"), FLinearColor(0.05f, 2.2f, 1.56f)); // the teal of the kit's lit cells
		auto* CutGlow = Scalar(TEXT("CutGlow"), 1.f);
		auto* Interior = Vector(TEXT("InteriorColor"), FLinearColor(0.035f, 0.036f, 0.04f));
		// The hidden-face mask: white = seen from outside, black = closed inside (the rh_hs masks' LinearGrayscale
		// sampler, as M_RH_Master's EmissiveMask; T_RH_MaskWhiteG is the grayscale white default).
		auto* HiddenMask = Expr<UMaterialExpressionTextureSampleParameter2D>(Mat);
		HiddenMask->ParameterName = FName("HiddenMask");
		HiddenMask->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Masks/T_RH_MaskWhiteG.T_RH_MaskWhiteG"));
		HiddenMask->SamplerType = SAMPLERTYPE_LinearGrayscale;
		if (!HiddenMask->Texture)
		{
			UE_LOG(LogRedHope, Error, TEXT("RHArt: T_RH_MaskWhiteG missing - M_RH_BuildReveal not authored"));
			return false;
		}

		// Albedo = lerp(BaseTex, AccentColor, AccentAmount); the inside wears InteriorColor: back faces, and the faces
		// the model's HiddenMask marks closed inside.
		auto* Albedo = Lerp(BaseTex, 0, AccentColor, 0, AccentAmount);
		auto* Facing = Expr<UMaterialExpressionTwoSidedSign>(Mat);   // +1 front, -1 back
		auto* Front = Expr<UMaterialExpressionSaturate>(Mat);
		Front->Input.Connect(0, Facing);
		auto* Outside = Expr<UMaterialExpressionMultiply>(Mat);
		Outside->A.Connect(0, Front);
		Outside->B.Connect(1, HiddenMask);                           // R (outputs: 0 RGB, 1 R, 2 G, 3 B)
		auto* Surface = Lerp(Interior, 0, Albedo, 0, Outside);
		// glTF MR packing: G = roughness, B = metallic (texture sample outputs: 0 RGB, 1 R, 2 G, 3 B).
		auto* MetalOut = Lerp(Metallic, 0, MRTex, 3, UseMR);
		auto* RoughOut = Lerp(Rough, 0, MRTex, 2, UseMR);
		auto* Flat = Expr<UMaterialExpressionConstant3Vector>(Mat);
		Flat->Constant = FLinearColor(0.f, 0.f, 1.f);
		auto* NormOut = Lerp(Flat, 0, NormTex, 0, UseNorm);

		// Depth under the cut, in cm: the opacity mask (clipped at 0.5 cm) and the line's ramp.
		auto* WPos = Expr<UMaterialExpressionWorldPosition>(Mat);
		auto* WZ = Expr<UMaterialExpressionComponentMask>(Mat);
		WZ->Input.Connect(0, WPos);
		WZ->R = 0; WZ->G = 0; WZ->B = 1; WZ->A = 0;
		auto* Depth = Expr<UMaterialExpressionSubtract>(Mat);
		Depth->A.Connect(0, CutZ);
		Depth->B.Connect(0, WZ);
		auto* Ramp = Expr<UMaterialExpressionDivide>(Mat);
		Ramp->A.Connect(0, Depth);
		Ramp->B.Connect(0, LineCm);
		auto* Near = Expr<UMaterialExpressionOneMinus>(Mat);
		Near->Input.Connect(0, Ramp);
		auto* Line = Expr<UMaterialExpressionSaturate>(Mat);
		Line->Input.Connect(0, Near);
		// Emissive = CutColor x CutGlow x line + surface x EmissiveFloor (the master's Mars-ambient lift).
		auto* Glow = Expr<UMaterialExpressionMultiply>(Mat);
		Glow->A.Connect(0, CutColor);
		Glow->B.Connect(0, CutGlow);
		// Only on faces standing across the cut: x saturate(2 (1 - |N.z|)), so a member lying flat at the cut height does
		// not glow its whole length (the 2026-10-26 review's "floating teal bar" on the half-built Solar Array).
		auto* VN = Expr<UMaterialExpressionVertexNormalWS>(Mat);
		auto* VNZ = Expr<UMaterialExpressionComponentMask>(Mat);
		VNZ->Input.Connect(0, VN);
		VNZ->R = 0; VNZ->G = 0; VNZ->B = 1; VNZ->A = 0;
		auto* VNAbs = Expr<UMaterialExpressionAbs>(Mat);
		VNAbs->Input.Connect(0, VNZ);
		auto* Upright = Expr<UMaterialExpressionOneMinus>(Mat);
		Upright->Input.Connect(0, VNAbs);
		auto* Upright2 = Expr<UMaterialExpressionMultiply>(Mat);
		Upright2->A.Connect(0, Upright);
		Upright2->ConstB = 2.f;
		auto* Across = Expr<UMaterialExpressionSaturate>(Mat);
		Across->Input.Connect(0, Upright2);
		auto* LineAcross = Expr<UMaterialExpressionMultiply>(Mat);
		LineAcross->A.Connect(0, Line);
		LineAcross->B.Connect(0, Across);
		// The rh.Glow dial (MPC_Atmosphere.GlowScale), as M_RH_Master's machine glow.
		auto* GlowScale = Expr<UMaterialExpressionCollectionParameter>(Mat);
		UMaterialParameterCollection* Atmos = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/RedHope/Sky/MPC_Atmosphere.MPC_Atmosphere"));
		GlowScale->Collection = Atmos;
		GlowScale->ParameterName = FName("GlowScale");
		GlowScale->ParameterId = Atmos ? Atmos->GetParameterId(FName("GlowScale")) : FGuid();
		if (!Atmos || !GlowScale->ParameterId.IsValid())
		{
			UE_LOG(LogRedHope, Error, TEXT("RHArt: MPC_Atmosphere.GlowScale missing - M_RH_BuildReveal not authored"));
			return false;
		}
		auto* GlowDial = Expr<UMaterialExpressionMultiply>(Mat);
		GlowDial->A.Connect(0, Glow);
		GlowDial->B.Connect(0, GlowScale);
		auto* LineGlow = Expr<UMaterialExpressionMultiply>(Mat);
		LineGlow->A.Connect(0, GlowDial);
		LineGlow->B.Connect(0, LineAcross);
		auto* Lift = Expr<UMaterialExpressionMultiply>(Mat);
		Lift->A.Connect(0, Surface);
		Lift->B.Connect(0, Floor);
		auto* Emissive = Expr<UMaterialExpressionAdd>(Mat);
		Emissive->A.Connect(0, LineGlow);
		Emissive->B.Connect(0, Lift);

		UMaterialEditorOnlyData* Data = Mat->GetEditorOnlyData();
		Data->BaseColor.Connect(0, Surface);
		Data->Metallic.Connect(0, MetalOut);
		Data->Roughness.Connect(0, RoughOut);
		Data->Normal.Connect(0, NormOut);
		Data->EmissiveColor.Connect(0, Emissive);
		Data->OpacityMask.Connect(0, Depth);
		Mat->BlendMode = BLEND_Masked;
		Mat->OpacityMaskClipValue = 0.5f;
		Mat->TwoSided = true;
		// (The setter, not the bUsedWith* fields: 5.8 deprecates writing them directly.)
		Mat->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		Mat->SetUsageByFlag(MATUSAGE_StaticLighting, true);
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		const bool bSaved = SaveAsset(Mat);
		UE_LOG(LogRedHope, Display, TEXT("RHArt: authored + saved M_RH_BuildReveal (%d expressions, masked two-sided, ISM + static lighting usage) %s"),
			Mat->GetExpressionCollection().Expressions.Num(), bSaved ? TEXT("OK") : TEXT("FAILED"));
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
	// -BuildReveal authors M_RH_BuildReveal alone: the full pass below rebuilds M_MarsSurface and M_VertexColor from
	// scratch, which a site-kit change has no business re-saving.
	if (FParse::Param(*Params, TEXT("BuildReveal")))
	{
		return AuthorBuildReveal() ? 0 : 1;
	}
	// Order matters: normal maps must be TC_Normalmap BEFORE the material
	// referencing them compiles, or the sampler type mismatches.
	FixNormalTexture(TEXT("/Game/RedHope/Art/Mars_Regolith_Texture_Normal.Mars_Regolith_Texture_Normal"));
	FixNormalTexture(TEXT("/Game/RedHope/Art/Cavern_Basalt_Texture_Normal.Cavern_Basalt_Texture_Normal"));
	FixNormalTexture(TEXT("/Game/RedHope/Art/Steel_Grid_Texture_Normal.Steel_Grid_Texture_Normal"));

	bool bOk = AuthorMarsSurface();
	bOk &= AuthorVertexColor();
	bOk &= AuthorBuildReveal();

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
