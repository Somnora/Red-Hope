#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RHArtWireCommandlet.generated.h"

/**
 * RHArtWire - the headless material wiring pass (premium-asset-plan P0/G0.2).
 *
 * Editor closed:
 *   UnrealEditor-Cmd <proj> -run=RHArtWire [-master] [-wire] [-dryrun]
 *                                          [-reparent] [-force]
 *
 * -master   authors /Game/RedHope/Art/M_RH_Master, the one master material every
 *           shipped mesh is meant to wear (see docs/art-bible.md section 3).
 *           Skipped if it already exists, so hand-tuning survives; -force
 *           re-authors it from scratch.
 * -wire     gives every wired building mesh an MI_<name> instance of that
 *           master, carrying its own BaseTex and its function accent, then
 *           points the mesh's material slot at it (only when it does not
 *           already, because re-saving a static mesh re-bakes its render data).
 * -reparent required to move an EXISTING instance off another master. Five of
 *           the wired rows are shipped hand-built instances of M_ModelTex, and
 *           adopting them into the family rewrites clean art in place - so it
 *           has to be asked for by name.
 * -dryrun   logs the plan, naming every asset that would be reparented, and
 *           writes nothing. Safe before the master exists.
 * No switches = -master -wire (the usual full pass).
 *
 * A re-import resets a mesh's material slot, so re-run this after any reimport.
 *
 * Sibling of URHArtCommandlet (-run=RHArt), which authors the terrain and
 * vertex-colour materials; this one owns the model-material family instead.
 * Editor-only by construction: the whole body is WITH_EDITOR.
 */
UCLASS()
class URHArtWireCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
