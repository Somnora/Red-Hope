#include "RHArtWireCommandlet.h"
#include "RedHope.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "StaticMeshResources.h"

namespace
{
	const TCHAR* GMasterPath = TEXT("/Game/RedHope/Art/M_RH_Master");
	const TCHAR* GMasterObj = TEXT("/Game/RedHope/Art/M_RH_Master.M_RH_Master");

	// A texture that certainly exists and is certainly a colour texture, used as
	// the master's BaseTex default. Every instance overrides it; the default only
	// has to be present and type-correct or the master will not compile.
	const TCHAR* GDefaultBaseTex = TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture");

	bool SaveAsset(UObject* Asset)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *FileName, SaveArgs);
	}

	// X/Y place the node in the material graph. Authored materials otherwise
	// open as one overlapping pile, which makes the master unusable for the
	// hand-tuning P1 expects to do on it.
	template <typename T>
	T* Expr(UMaterial* Mat, int32 X = 0, int32 Y = 0)
	{
		T* E = NewObject<T>(Mat);
		Mat->GetExpressionCollection().AddExpression(E);
		E->Material = Mat;
		E->MaterialExpressionEditorX = X;
		E->MaterialExpressionEditorY = Y;
		return E;
	}

	/**
	 * M_RH_Master - one master for every model in the game.
	 *
	 * Deliberately texture-light for this gate: BaseTex plus scalars. Normal and
	 * MRA sampling waits for the bake stage (P1/C4) that actually produces
	 * T_<name>_N and T_<name>_MRA, because a texture parameter whose default is
	 * the wrong sampler type fails to compile - and today's assets have no
	 * normal maps to point at.
	 *
	 * Params:
	 *   BaseTex        the asset's albedo
	 *   AccentColor    function identity hue (art bible section 2.3)
	 *   AccentAmount   how much of the surface the accent tints (0 = none)
	 *   Metallic, Rough
	 *   EmissiveColor  the machine's glow hue
	 *   EmissiveAmount lit area strength
	 *   PoweredState   1 running, 0 dark - driven per building at runtime
	 *   EmissiveFloor  0.08 x base, the documented Mars-ambient lift
	 */
	bool AuthorMaster()
	{
		UPackage* Package = CreatePackage(GMasterPath);
		Package->FullyLoad();
		UMaterial* Mat = NewObject<UMaterial>(Package, TEXT("M_RH_Master"), RF_Public | RF_Standalone);

		auto* BaseTex = Expr<UMaterialExpressionTextureSampleParameter2D>(Mat, -900, -300);
		BaseTex->ParameterName = FName("BaseTex");
		BaseTex->Texture = LoadObject<UTexture2D>(nullptr, GDefaultBaseTex);
		BaseTex->SamplerType = SAMPLERTYPE_Color;

		auto* AccentColor = Expr<UMaterialExpressionVectorParameter>(Mat, -900, 0);
		AccentColor->ParameterName = FName("AccentColor");
		AccentColor->DefaultValue = FLinearColor(0.5f, 0.5f, 0.5f);

		auto* AccentAmount = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 130);
		AccentAmount->ParameterName = FName("AccentAmount");
		AccentAmount->DefaultValue = 0.f;

		auto* Metallic = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 230);
		Metallic->ParameterName = FName("Metallic");
		Metallic->DefaultValue = 0.f;

		auto* Rough = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 330);
		Rough->ParameterName = FName("Rough");
		Rough->DefaultValue = 0.75f;

		auto* EmissiveColor = Expr<UMaterialExpressionVectorParameter>(Mat, -900, 430);
		EmissiveColor->ParameterName = FName("EmissiveColor");
		EmissiveColor->DefaultValue = FLinearColor::Black;

		auto* EmissiveAmount = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 560);
		EmissiveAmount->ParameterName = FName("EmissiveAmount");
		EmissiveAmount->DefaultValue = 0.f;

		auto* Powered = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 660);
		Powered->ParameterName = FName("PoweredState");
		Powered->DefaultValue = 1.f;

		auto* Floor = Expr<UMaterialExpressionScalarParameter>(Mat, -900, 760);
		Floor->ParameterName = FName("EmissiveFloor");
		Floor->DefaultValue = 0.08f;

		// BaseColor = lerp(albedo, accent, accentAmount)
		auto* Albedo = Expr<UMaterialExpressionLinearInterpolate>(Mat, -500, -200);
		Albedo->A.Connect(0, BaseTex);
		Albedo->B.Connect(0, AccentColor);
		Albedo->Alpha.Connect(0, AccentAmount);

		// Emissive = accentGlow * amount * powered + base * floor
		auto* GlowA = Expr<UMaterialExpressionMultiply>(Mat, -500, 430);
		GlowA->A.Connect(0, EmissiveColor);
		GlowA->B.Connect(0, EmissiveAmount);
		auto* GlowB = Expr<UMaterialExpressionMultiply>(Mat, -340, 500);
		GlowB->A.Connect(0, GlowA);
		GlowB->B.Connect(0, Powered);
		auto* FloorMul = Expr<UMaterialExpressionMultiply>(Mat, -340, 700);
		FloorMul->A.Connect(0, Albedo);
		FloorMul->B.Connect(0, Floor);
		auto* Emissive = Expr<UMaterialExpressionAdd>(Mat, -180, 600);
		Emissive->A.Connect(0, GlowB);
		Emissive->B.Connect(0, FloorMul);

		UMaterialEditorOnlyData* Data = Mat->GetEditorOnlyData();
		Data->BaseColor.Connect(0, Albedo);
		Data->Metallic.Connect(0, Metallic);
		Data->Roughness.Connect(0, Rough);
		Data->EmissiveColor.Connect(0, Emissive);

		Mat->bUsedWithInstancedStaticMeshes = true;
		Mat->bUsedWithSkeletalMesh = true;
		Mat->PreEditChange(nullptr);
		Mat->PostEditChange();
		FAssetRegistryModule::AssetCreated(Mat);
		const bool bSaved = SaveAsset(Mat);
		UE_LOG(LogRedHope, Display, TEXT("RHArtWire: authored M_RH_Master (%d expressions) %s"),
			Mat->GetExpressionCollection().Expressions.Num(), bSaved ? TEXT("OK") : TEXT("FAILED"));
		return bSaved;
	}

	// One row per mesh we wire. Accent hues mirror TintFor in the colony
	// visualizer, which is the art bible's function-accent table.
	struct FWireRow
	{
		const TCHAR* MeshObj;   // /Game/... object path of the static mesh
		const TCHAR* BaseTex;   // its albedo texture object path
		FLinearColor Accent;
		float AccentAmount;
		float Metallic;
		float Rough;
		FLinearColor Glow;
		float GlowAmount;
	};

	FString PackageDirOf(const UObject* Asset)
	{
		return FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	}

	bool WireOne(const FWireRow& Row, bool bDryRun, bool bAllowReparent)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Row.MeshObj);
		if (!Mesh)
		{
			UE_LOG(LogRedHope, Warning, TEXT("RHArtWire: mesh missing, skipped: %s"), Row.MeshObj);
			return false;
		}

		const FString MIName = FString::Printf(TEXT("MI_%s"), *Mesh->GetName());
		const FString MIPackagePath = PackageDirOf(Mesh) / MIName;
		const FString MIObjectPath = MIPackagePath + TEXT(".") + MIName;

		// Probe for an existing instance quietly - on a first run most of these
		// legitimately do not exist and a load warning per row is just noise.
		UMaterialInstanceConstant* MI = LoadObject<UMaterialInstanceConstant>(
			nullptr, *MIObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UMaterial* Master = LoadObject<UMaterial>(nullptr, GMasterObj, nullptr, LOAD_NoWarn | LOAD_Quiet);

		// Five of the wired rows are shipped, hand-built instances of M_ModelTex.
		// Moving them onto the new master is intended (the mixed set unifies by
		// material) but it is an in-place rewrite of clean art, so it is called
		// out by name and gated behind an explicit switch.
		const bool bWouldReparent = (MI != nullptr) && (MI->Parent != nullptr) && (MI->Parent != Master);

		if (bDryRun)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArtWire: [dryrun] %s -> %s (%s)"),
				*Mesh->GetName(), *MIName,
				MI ? TEXT("existing instance") : TEXT("new instance"));
			if (bWouldReparent)
			{
				UE_LOG(LogRedHope, Warning, TEXT("RHArtWire: [dryrun] %s is parented to %s and WOULD BE REPARENTED onto M_RH_Master (needs -reparent)"),
					*MIName, *GetNameSafe(MI->Parent));
			}
			return true;
		}

		if (!Master)
		{
			UE_LOG(LogRedHope, Error, TEXT("RHArtWire: M_RH_Master not found - run with -master first"));
			return false;
		}
		// A missing texture used to warn and wire anyway. On a reparent row that
		// silently strips a working building's albedo and saves it, so it is now
		// a hard failure.
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Row.BaseTex);
		if (!Tex)
		{
			UE_LOG(LogRedHope, Error, TEXT("RHArtWire: texture missing for %s (%s) - row skipped"),
				*Mesh->GetName(), Row.BaseTex);
			return false;
		}

		if (bWouldReparent)
		{
			UE_LOG(LogRedHope, Warning, TEXT("RHArtWire: %s is parented to %s - reparenting onto M_RH_Master"),
				*MIName, *GetNameSafe(MI->Parent));
			if (!bAllowReparent)
			{
				UE_LOG(LogRedHope, Warning, TEXT("RHArtWire: %s skipped - pass -reparent to allow it"), *MIName);
				return false;
			}
		}

		if (!MI)
		{
			UPackage* MIPackage = CreatePackage(*MIPackagePath);
			MIPackage->FullyLoad();
			MI = NewObject<UMaterialInstanceConstant>(MIPackage, *MIName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(MI);
		}

		MI->PreEditChange(nullptr);
		MI->SetParentEditorOnly(Master);
		MI->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(FName("BaseTex")), Tex);
		MI->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName("AccentColor")), Row.Accent);
		MI->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName("EmissiveColor")), Row.Glow);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName("AccentAmount")), Row.AccentAmount);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName("Metallic")), Row.Metallic);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName("Rough")), Row.Rough);
		MI->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName("EmissiveAmount")), Row.GlowAmount);
		MI->PostEditChange();

		// Point the mesh's first slot at the instance - but ONLY if it is not
		// already pointing there. Five of these meshes already reference their
		// MI_<name>, and a blanket PostEditChange() on a static mesh runs a full
		// Build(), which re-bakes render data and mutates serialized lightmap
		// properties. Re-baking meshes that were just carefully re-cut, to
		// re-assign a material they already have, is pure damage.
		TArray<FStaticMaterial> Mats = Mesh->GetStaticMaterials();
		const bool bSlotNeedsUpdate = (Mats.Num() == 0) || (Mats[0].MaterialInterface != MI);
		bool bMeshSaved = true;
		if (bSlotNeedsUpdate)
		{
			if (Mats.Num() == 0)
			{
				Mats.Add(FStaticMaterial(MI));
			}
			else
			{
				Mats[0].MaterialInterface = MI;
			}
			Mesh->Modify();
			Mesh->SetStaticMaterials(Mats);
			// Targeted equivalent of the rebuild for a material swap.
			Mesh->UpdateUVChannelData(false);
			Mesh->MarkPackageDirty();
			bMeshSaved = SaveAsset(Mesh);
		}

		const bool bMISaved = SaveAsset(MI);

		// Geometry receipt. The weld pass claims ~83% of vertices were merged;
		// this is the number UE actually built, which is the only one that
		// counts. A mesh still sitting near 3 verts per triangle did not get
		// the welded export (or the build re-split it) and should be re-cut.
		if (const FStaticMeshRenderData* RD = Mesh->GetRenderData())
		{
			if (RD->LODResources.Num() > 0)
			{
				const FStaticMeshLODResources& LOD = RD->LODResources[0];
				const int32 Tris = LOD.GetNumTriangles();
				const int32 Verts = LOD.GetNumVertices();
				UE_LOG(LogRedHope, Display, TEXT("RHArtWire: %s geometry: %d tris, %d verts (%.2f verts/tri)"),
					*Mesh->GetName(), Tris, Verts,
					Tris > 0 ? static_cast<float>(Verts) / static_cast<float>(Tris) : 0.f);
			}
		}

		UE_LOG(LogRedHope, Display, TEXT("RHArtWire: %s -> %s (MI %s, mesh slot %s)"),
			*Mesh->GetName(), *MIName,
			bMISaved ? TEXT("saved") : TEXT("FAILED"),
			bSlotNeedsUpdate ? (bMeshSaved ? TEXT("updated") : TEXT("FAILED")) : TEXT("already correct"));
		return bMISaved && bMeshSaved;
	}

	// The wired set = EVERY real model the game actually renders under the mixed
	// set (premium-asset-plan section 6). That deliberately spans both lineages:
	// the four painted 2026-07-17 meshes that won their slot, AND the seven kept
	// originals, which are re-parented from M_ModelTex onto the same master so
	// coherence comes from the surface rather than from the mesh. Meshes nobody
	// renders (BatteryStation, IceProcessor, CargoLander, OreExtractor and the
	// three unattached models) are left alone - no point churning assets that
	// never reach the screen. The old vertex-coloured forge is also left alone:
	// its colour lives in vertex data, not a texture, and HeavyForge replaced it.
	//
	// Accents are the TintFor table (= art bible section 2.3); glow hues are the
	// RHCanon glow set. Emissive amounts stay conservative because the 0.08
	// emissive floor already lifts everything out of Mars ambient.
	bool WireAll(bool bDryRun, bool bAllowReparent)
	{
		const FLinearColor FurnaceGlow(6.0f, 1.6f, 0.15f);
		const FLinearColor TealGlow(0.1f, 4.5f, 3.2f);
		const FLinearColor IceGlow(0.8f, 2.8f, 5.0f);
		const FLinearColor AmberGlow(4.5f, 2.2f, 0.2f);

		const FWireRow Rows[] =
		{
			// --- the painted meshes that won their slot (Models2 lineage) ---
			// Forge
			{ TEXT("/Game/RedHope/Art/Models2/HeavyForge/HeavyForge/StaticMeshes/HeavyForge.HeavyForge"),
			  TEXT("/Game/RedHope/Art/Models2/HeavyForge/HeavyForge/Textures/HeavyForge_textured.HeavyForge_textured"),
			  FLinearColor(0.95f, 0.35f, 0.08f), 0.10f, 0.35f, 0.62f, FurnaceGlow, 0.22f },
			// SolarArray
			{ TEXT("/Game/RedHope/Art/Models2/SolarPanel/SolarPanel/StaticMeshes/SolarPanel.SolarPanel"),
			  TEXT("/Game/RedHope/Art/Models2/SolarPanel/SolarPanel/Textures/SolarPanel_textured.SolarPanel_textured"),
			  FLinearColor(0.25f, 0.45f, 0.95f), 0.10f, 0.20f, 0.45f, IceGlow, 0.10f },
			// Habitat
			{ TEXT("/Game/RedHope/Art/Models2/HabitatDome/HabitatDome/StaticMeshes/HabitatDome.HabitatDome"),
			  TEXT("/Game/RedHope/Art/Models2/HabitatDome/HabitatDome/Textures/HabitatDome_textured.HabitatDome_textured"),
			  FLinearColor(0.95f, 0.95f, 0.95f), 0.06f, 0.10f, 0.72f, AmberGlow, 0.16f },
			// ComputeModule
			{ TEXT("/Game/RedHope/Art/Models2/CommandModule/CommandModule/StaticMeshes/CommandModule.CommandModule"),
			  TEXT("/Game/RedHope/Art/Models2/CommandModule/CommandModule/Textures/CommandModule_textured.CommandModule_textured"),
			  FLinearColor(0.90f, 0.25f, 0.55f), 0.10f, 0.25f, 0.60f, TealGlow, 0.22f },

			// --- the kept originals, brought into the same family ---
			// Their MI_<name> assets already exist (parented to M_ModelTex); the
			// wire pass re-parents those same assets onto M_RH_Master in place.
			// BatteryBank - the display panels are why this mesh was kept
			{ TEXT("/Game/RedHope/Art/Models/battery/battery.battery"),
			  TEXT("/Game/RedHope/Art/Models/battery/T_battery_BC.T_battery_BC"),
			  FLinearColor(0.10f, 0.85f, 0.65f), 0.10f, 0.25f, 0.70f, TealGlow, 0.20f },
			// WaterPlant
			{ TEXT("/Game/RedHope/Art/Models/ice/ice.ice"),
			  TEXT("/Game/RedHope/Art/Models/ice/T_ice_BC.T_ice_BC"),
			  FLinearColor(0.25f, 0.55f, 0.95f), 0.10f, 0.30f, 0.68f, IceGlow, 0.18f },
			// Lander
			{ TEXT("/Game/RedHope/Art/Models/lander2/lander2.lander2"),
			  TEXT("/Game/RedHope/Art/Models/lander2/T_lander2_BC.T_lander2_BC"),
			  FLinearColor(0.85f, 0.85f, 0.90f), 0.08f, 0.30f, 0.65f, AmberGlow, 0.12f },
			// Borer - the digging arm is why this mesh was kept
			{ TEXT("/Game/RedHope/Art/Models/extractor2/extractor2.extractor2"),
			  TEXT("/Game/RedHope/Art/Models/extractor2/T_extractor2_BC.T_extractor2_BC"),
			  FLinearColor(0.90f, 0.55f, 0.10f), 0.10f, 0.35f, 0.66f, AmberGlow, 0.18f },
			// Stockpile
			{ TEXT("/Game/RedHope/Art/Models/stockpile/stockpile.stockpile"),
			  TEXT("/Game/RedHope/Art/Models/stockpile/T_stockpile_BC.T_stockpile_BC"),
			  FLinearColor(0.85f, 0.55f, 0.15f), 0.08f, 0.15f, 0.75f, AmberGlow, 0.08f },
			// AirFilter (Interchange lineage - texture lives under Textures/)
			{ TEXT("/Game/RedHope/Art/Machines/RH_AirFilter2/StaticMeshes/RH_AirFilter2.RH_AirFilter2"),
			  TEXT("/Game/RedHope/Art/Machines/RH_AirFilter2/Textures/airfilter2_textured.airfilter2_textured"),
			  FLinearColor(0.30f, 0.85f, 0.80f), 0.10f, 0.25f, 0.62f, TealGlow, 0.20f },
			// HumidityRegulator - no TintFor entry yet, so it takes the
			// life-support teal of its sibling machine rather than gray.
			{ TEXT("/Game/RedHope/Art/Agri/humidity/humidity/StaticMeshes/humidity.humidity"),
			  TEXT("/Game/RedHope/Art/Agri/humidity/humidity/Textures/humidity_textured.humidity_textured"),
			  FLinearColor(0.30f, 0.85f, 0.80f), 0.10f, 0.25f, 0.65f, TealGlow, 0.16f },
		};

		int32 Ok = 0;
		const int32 Total = static_cast<int32>(UE_ARRAY_COUNT(Rows));
		for (const FWireRow& Row : Rows)
		{
			Ok += WireOne(Row, bDryRun, bAllowReparent) ? 1 : 0;
		}
		// Anything short of every row is a failure: a partially wired mixed set
		// leaves half the colony on the old lineage and reads as a bug in-game.
		const bool bAll = (Ok == Total);
		if (bAll)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArtWire: wired %d/%d meshes%s"),
				Ok, Total, bDryRun ? TEXT(" (dryrun)") : TEXT(""));
		}
		else
		{
			UE_LOG(LogRedHope, Error, TEXT("RHArtWire: wired only %d/%d meshes%s"),
				Ok, Total, bDryRun ? TEXT(" (dryrun)") : TEXT(""));
		}
		return bAll;
	}
}
#endif // WITH_EDITOR

int32 URHArtWireCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	const bool bDryRun = FParse::Param(*Params, TEXT("dryrun"));
	const bool bAllowReparent = FParse::Param(*Params, TEXT("reparent"));
	const bool bForce = FParse::Param(*Params, TEXT("force"));
	bool bWantMaster = FParse::Param(*Params, TEXT("master"));
	bool bWantWire = FParse::Param(*Params, TEXT("wire"));
	if (!bWantMaster && !bWantWire)
	{
		bWantMaster = true;
		bWantWire = true;
	}

	bool bOk = true;
	if (bWantMaster)
	{
		// AuthorMaster builds the graph from scratch every time, so an
		// unguarded default run would silently destroy any hand-tuning done in
		// the Material Editor - including the normal/MRA work P1 adds to it.
		// Re-authoring is therefore explicit.
		UMaterial* Existing = LoadObject<UMaterial>(nullptr, GMasterObj, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (bDryRun)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArtWire: [dryrun] would %s M_RH_Master"),
				Existing ? TEXT("SKIP (already exists; -force re-authors)") : TEXT("author"));
		}
		else if (Existing && !bForce)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHArtWire: M_RH_Master already exists - left untouched (pass -force to re-author)"));
		}
		else
		{
			bOk &= AuthorMaster();
		}
	}

	if (bWantWire)
	{
		bOk &= WireAll(bDryRun, bAllowReparent);
	}
	UE_LOG(LogRedHope, Display, TEXT("RHArtWire: done (%s)"), bOk ? TEXT("OK") : TEXT("FAILED"));
	return bOk ? 0 : 1;
#else
	UE_LOG(LogRedHope, Error, TEXT("RHArtWire: editor-only commandlet"));
	return 1;
#endif
}
