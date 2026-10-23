#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RHPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class ARHStrategyPawn;
class SRHCommandDeck;
struct FInputActionValue;
struct FRHBuildingRow;

// Mission control's hands (M1-a Gate C). Enhanced Input actions and the
// mapping context are constructed at runtime in C++ - no input assets, the
// whole surface is compile-gated like everything else. Orders still travel
// through the sim's uplink queue with full signal lag: the UI can aim, only
// the seam can act.
UCLASS()
class REDHOPE_API ARHPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARHPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// --- Command deck API ---
	void BeginPlacement(FName BuildingDef);
	void BeginDigDesignation();
	void BeginSurveyDesignation();
	// Road mode (C4): BUILD > Logistics > Road. Press anchors the first end (it snaps
	// to an existing junction when one is near), drag aims the second with a ghost
	// strip and a live length / bill / preflight readout, release transmits a
	// BuildRoad order. The mode stays armed for the next stretch; cancel exits.
	void BeginRoad();
	bool IsRoadMode() const { return bRoadMode; }
	// Capture/QA (RH.RoadGhost): hold the road ghost mid-drag from one point to
	// another, as if the mouse were down there; it never transmits.
	void Debug_RoadGhost(const FVector& FromCm, const FVector& ToCm);
	// Demolish Road tool (C6b, Q3a): BUILD > Logistics > Demolish Road. Hover lights the road edge under
	// the cursor (white outline, with the sim's own CanDemolishRoad preflight and the 50% salvage on the
	// hint), a click transmits the DemolishRoad order and the confirm line states the salvage. The mode
	// stays armed for the next edge; cancel exits.
	void BeginRoadDemolish();
	bool IsRoadDemolishMode() const { return bRoadDemolishMode; }
	// Capture/QA (RH.RoadDemolishHover): hold the tool's hover on a ground point, as if the cursor were
	// there; it never transmits.
	void Debug_RoadDemolishHover(const FVector& AtCm);
	// Tunnel brush (D3, Q2a): the deck's Tunnel button on a floor view. Press anchors the first end on the lattice, drag aims the
	// second with the sim's own preview (an L: along X, then along Y; the live cell count, Borer-hours, spoil and the preflight's
	// reason), release transmits a DigTunnel order. The mode stays armed for the next tunnel; cancel exits.
	void BeginTunnel();
	// Cavern brush (D6a): the deck's Cavern button on a floor view, a box variant of the tunnel brush (it shares the armed flag, so every
	// place that disarms the tunnel brush disarms this). Press anchors one corner, drag aims the opposite corner with the sim's own
	// preview (the cells to carve, Borer-hours, spoil, O2 and the preflight's reason), release transmits a DigCavern order: Level = the
	// viewed floor (the top of the box), Value = Floors. Floors is 1..RH::CavernMaxFloors: the Cavern button steps it while armed.
	void BeginCavern(int32 Floors);
	bool IsCavernMode() const { return bTunnelMode && bCavernBrush; }
	int32 GetCavernFloors() const { return CavernFloors; }
	// Capture/QA (RH.CavernGhost): hold the cavern brush's ghost mid-drag on a floor between two lattice corners, as if the mouse were
	// down there; it never transmits.
	void Debug_CavernGhost(int32 Top, int32 Floors, const FIntPoint& FromCell, const FIntPoint& ToCell);
	bool IsTunnelMode() const { return bTunnelMode && !bCavernBrush; }
	// Capture/QA (RH.TunnelGhost): hold the brush's ghost mid-drag on a floor between two lattice points, as if the mouse
	// were down there; it never transmits.
	void Debug_TunnelGhost(int32 Level, const FIntPoint& FromCell, const FIntPoint& ToCell);
	// The traffic overlay (C6b): heat on the road strips, a legend on the deck; lives beside the survey map.
	void ToggleTrafficOverlay();
	bool IsTrafficOverlayOn() const;
	// District paint mode (C5a): pick a Kind on the deck's DISTRICT tab (or the City panel). On the
	// surface press anchors a corner of a lattice rectangle (10 m cells, at most 32 x 32), drag sizes
	// it under a ghost that shows the sim's own preflight - the cells it adds, the district it joins,
	// what is out of grid coverage - and release transmits a PaintDistrict order; underground one
	// click makes the floor you are on a district. The mode stays armed for the next district;
	// cancel exits.
	void BeginDistrictPaint(FName Kind);
	bool IsDistrictMode() const { return bDistrictMode; }
	FName GetDistrictKind() const { return DistrictKind; }
	// Capture/QA (RH.District ghost): hold the paint ghost mid-drag between two lattice cells, as if
	// the mouse were down there; it never transmits.
	void Debug_DistrictGhost(FName Kind, const FIntPoint& FromCell, const FIntPoint& ToCell);
	// D6b: what an UNDERGROUND press does. 0 (the default) makes the whole floor you are on a district, one click, as always; 1..3 paints
	// a SHAPE that many floors tall, from the floor you are on and down: press a corner on the rock or the dug floor, drag to the opposite
	// one (the lattice of the shaft head, 10 m cells, at most 32 x 32), release to transmit. The deck's DISTRICT tab steps it
	// (whole floor, 1, 2, 3 floors). Rock may be painted, so a cavern can be zoned before it is dug.
	int32 GetDistrictShapeFloors() const { return DistrictShapeFloors; }
	void SetDistrictShapeFloors(int32 Floors);
	// Capture/QA (RH.District shapeghost): hold the shape ghost mid-drag on a floor between two lattice points of the shaft head's frame.
	void Debug_DistrictShapeGhost(FName Kind, int32 Level, int32 Floors, const FIntPoint& FromCell, const FIntPoint& ToCell);
	// Paint-to-size excavation (M1-d Gate A2): click anchors a corner on the
	// active subsurface floor, drag sizes the rectangle, release transmits an
	// Excavate order counted in 10x10 m cells.
	void BeginExcavateDesignation();
	// Room zoning (M2 Gate B): pick a function on the deck, click carved cells
	// on the active floor to designate them (uplink Designate orders). The mode
	// stays armed for painting several cells; cancel exits.
	void BeginZoneDesignation(FName Room);
	// Furnish mode (BM-5): pick a catalogue piece on the deck, hover a zoned
	// cell or habitat bay - the ghost snaps to the 2.5 m sub-grid - T turns
	// it, click transmits a Furnish order; the mode stays armed. Sell mode:
	// click a piece to sell it back (FurnitureRefundFraction).
	void BeginFurnish(FName Row);
	void BeginSellFurniture();
	// Fit mode (BM-6): pick a window, door or airlock on the deck's WALLS tab,
	// hover a surface habitat bay - the nearest wall of that bay lights - and
	// click to transmit a Fit order. Sell mode sells fittings too.
	void BeginFit(FName Row);
	bool IsFitMode() const { return bFitMode; }
	void Debug_ShowWallsTab();
	// Capture/QA (RH.DeckDistrict): show the deck's DISTRICT tab; RH.DeckCity folds the City panel.
	void Debug_ShowDistrictTab();
	void Debug_SetCityExpanded(bool bOn);
	bool IsFurnishMode() const { return bFurnishMode; }
	FName GetFurnishRow() const { return FurnishRow; }
	bool IsSellMode() const { return bSellMode; }
	// Capture/QA (RH.DeckFurnish): show the deck's FURNISH tab.
	void Debug_ShowFurnishTab();
	void CancelModes();
	void SetSimSpeed(float Tier);
	void TogglePause();
	void QuickSave();
	void QuickLoad();
	FName GetPendingBuildDef() const { return PendingBuildDef; }
	bool IsDigMode() const { return bDigMode; }
	bool IsSurveyMode() const { return bSurveyMode; }
	bool IsExcavateMode() const { return bExcavateMode; }
	bool IsRoadDragging() const { return bRoadDragging; }
	bool IsZoneMode() const { return bZoneMode; }
	FName GetZoneRoom() const { return ZoneRoom; }
	// The elevator (M1-d): which floor the player is looking at and ordering
	// on. 0 = surface; -N needs no shaft to LOOK, only to order work there.
	void SetActiveLevel(int32 Level);
	int32 GetActiveLevel() const { return ActiveLevel; }
	// D2: move the camera to the next shaft head in Id order (wraps). A no-op with one shaft.
	void FocusNextShaft();
	// D4: the shaft whose head is nearest the camera focus (0 with one shaft) - the shaft card's shaft.
	int32 GetNearestShaftId() const;

	// D8 (the deep view, docs/qa/2026-10-23/d8-deep-view-design.md R1): the section, a cut along a vertical plane through every floor
	// under a shaft. This controller owns all of its state and pushes a mirror to the colony visualizer and the section visualizer.
	// ActiveLevel stays frozen at the entry floor while it is on; the slider, the strip rows and PageUp/PageDown move SectionFocus only.
	bool CanEnterSection() const;               // some shaft is bored, no menu or title is open
	void EnterSection();
	void ExitSection(int32 ToLevel);            // back to the floor view, on ToLevel (through SetActiveLevel's clamp)
	void ToggleSection();
	bool IsSectionOn() const { return bSectionOn; }
	int32 GetSectionFocus() const { return SectionFocus; }
	int32 GetSectionAxis() const { return SectionAxis; }       // 0..3: the camera looks along +X, +Y, -X, -Y
	int32 GetSectionPlaneRow() const { return SectionPlaneRow; }
	int32 GetSectionShaft() const { return SectionShaft; }
	// The floor the deck shows as "yours": the focus in the section, else the active floor.
	int32 GetShownLevel() const { return bSectionOn ? SectionFocus : ActiveLevel; }
	void SetSectionFocus(int32 Level);          // clamped to SURF..the deepest bored floor
	void StepSectionFocus(int32 Delta);
	void TurnSection(int32 Sign);               // a quarter turn; the cut keeps its centre
	void StepSectionPlane(int32 Rows);          // + is away from the camera
	void SetSectionPlaneRow(int32 Row);
	void SetSectionAxis(int32 Axis);
	// Cut here (a cavern's card): enter if needed, put the plane through the box's middle row and the focus on its bottom floor.
	bool CutSectionAtCavern(int32 CavernId);
	// The deck's visible feedback channels (M1-b Gate C: GEngine debug text
	// provably hides under the Slate deck, so the deck renders these instead).
	// Confirm = one-shot notice with expiry (order sent, save/load result);
	// Hint = the live mode prompt, valid only while a mode is active.
	FText GetConfirmText() const;
	FLinearColor GetConfirmColor() const { return ConfirmColor; }
	FText GetHintText() const;
	FLinearColor GetHintColor() const { return HintColor; }
	// Deck-originated orders (manifest, launch, politics buttons) report
	// through the same confirm line as the controller's own.
	void PostConfirm(const FString& Text, const FLinearColor& Color) { SetConfirm(Text, Color); }
	// --- Menus (RHMenus: title, pause, settings, credits) ---
	// The sim holds still and the deck steps aside while a menu is up.
	void OpenPauseMenu();
	void CloseMenus();
	void StartNewColony();   // title: close it, run the sim, replay the opening shot
	void ContinueColony();   // title: load the quicksave, then as above
	void QuitGame();
	bool IsMenuOpen() const;
	bool AreTipsOn() const;
	void SetTipsOn(bool bOn);
	// Capture/QA (RH.Menu): open a menu screen by name (title, pause,
	// settings, credits, none).
	void Debug_OpenMenu(const FString& Name);
	// Click test (RHClickTest.cpp): how many clicks reached OnClick, i.e. the WORLD. A click on a button or a panel must
	// leave it unchanged; a button that leaks its click into the world moves it (2026-10-21: every deck click did).
	int32 GetWorldClickCount() const { return WorldClickCount; }
	// The deck is up (not stepped aside for a menu) and its build drawer is open.
	bool IsDeckShown() const;
	bool IsBuildDrawerOpen() const;
	// Capture/QA (RH.DeckScroll): scroll the deck's right column to a pixel offset.
	void Debug_ScrollDeck(float OffsetPx);
	// Capture/QA (RH.DeckExpand): unfold every deck panel.
	void Debug_ExpandDeck(bool bRoster);
	// Capture/QA (RH.DeckBuild): open the deck's build drawer on a category.
	void Debug_OpenBuildDrawer(FName Category);
	// Click-to-inspect (M1-b Gate C): the selected building's sim id, 0 = none.
	int32 GetSelectedBuildingId() const { return SelectedBuildingId; }
	// Station cell inspection (tiered production T4): a click on a zoned seat
	// cell (Lab / Workstation / Garden) with no mode armed selects the cell;
	// the action card reads its seats and offers ASSIGN STAFF.
	bool HasSelectedCell() const { return SelectedCellLevel < 0 && SelectedCellIndex != INDEX_NONE; }
	// BM-2: a work-station bay of a surface habitat, inspected like a cell.
	bool HasSelectedBay() const { return SelectedBayHab != 0 && SelectedBay != INDEX_NONE; }
	int32 GetSelectedBayHab() const { return SelectedBayHab; }
	int32 GetSelectedBay() const { return SelectedBay; }
	void Debug_SelectBay(int32 Hab, int32 Bay) { SelectedBuildingId = 0; SelectedCellLevel = 0; SelectedCellIndex = INDEX_NONE; SelectedBayHab = Hab; SelectedBay = Bay; }
	int32 GetSelectedCellLevel() const { return SelectedCellLevel; }
	int32 GetSelectedCellIndex() const { return SelectedCellIndex; }
	void Debug_SelectCell(int32 Level, int32 Cell) { SelectedBuildingId = 0; SelectedCellLevel = Level; SelectedCellIndex = Cell; }
	// Debug/help: select a building by id (RH.Select) - raises its in-world
	// action card without hunting for the gray-box footprint with the cursor.
	void Debug_SelectBuilding(int32 Id) { SelectedBuildingId = Id; }
	// Surveyed-land map (director request): toggle the coverage overlay.
	void ToggleSurveyMap() { bShowSurveyMap = !bShowSurveyMap; }
	bool IsSurveyMapOn() const { return bShowSurveyMap; }

private:
	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnOrbit(const FInputActionValue& Value);
	void OnTilt(const FInputActionValue& Value);
	void OnClick();
	void OnCancel();
	void OnRotate();
	// D5 depth: PageUp / PageDown step the view one floor toward the surface / deeper (SetActiveLevel keeps its reached-depth clamp).
	void OnFloorUp();
	void OnFloorDown();
	// D8: the section's own inputs (V, Home, End; Q/E edge-triggered; W/S released) and its per-frame work.
	void OnSectionToggle();
	void OnSectionHome();
	void OnSectionEnd();
	void OnSectionTurn(const FInputActionValue& Value);
	void OnPanCompleted();
	void TickSection(float DeltaTime);
	void HandleSectionClick();
	void PushSection();                          // the mirrors: colony, section visualizer, pawn
	void FrameSection(bool bRefit);              // the camera's focus on the cut (and, with bRefit, the zoom that fits the dig)
	bool SectionPickAtCursor(int32& OutLevel, FIntPoint& OutCell) const;
	// The furnishable cell under a ground point and the slot that centres the
	// armed piece there; false off any room.
	bool ComputeFurnishSpot(const FVector& GroundCm, FIntVector& OutKey, int32& OutSlot) const;
	const struct FRHFurnitureInstance* FindFurnitureAt(const FVector& GroundCm) const;
	// The wall of the surface bay under a point nearest to it (and its side).
	bool ComputeFitEdge(const FVector& GroundCm, FIntVector& OutEdge, int32& OutDir) const;
	// Cursor ray intersected with the ground plane (Z=0), snapped to the 2 m grid.
	bool CursorToGround(FVector& OutCm) const;
	ARHStrategyPawn* StrategyPawn() const;
	void SetConfirm(const FString& Text, const FLinearColor& Color);
	// Nearest building whose footprint (plus a half-cell of grace) contains
	// the point; 0 if open ground.
	int32 FindBuildingAt(const FVector& GroundCm) const;
	// First Scout row's WorkRate = the survey reveal radius (preview honesty:
	// same number CompleteSurvey uses).
	float SurveyRadiusM() const;

	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> IA_Pan;
	UPROPERTY() TObjectPtr<UInputAction> IA_Zoom;
	UPROPERTY() TObjectPtr<UInputAction> IA_Orbit;
	UPROPERTY() TObjectPtr<UInputAction> IA_Tilt;
	UPROPERTY() TObjectPtr<UInputAction> IA_Click;
	UPROPERTY() TObjectPtr<UInputAction> IA_Cancel;
	UPROPERTY() TObjectPtr<UInputAction> IA_Rotate;
	UPROPERTY() TObjectPtr<UInputAction> IA_Pause;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed3;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed8;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed60;
	UPROPERTY() TObjectPtr<UInputAction> IA_FloorUp;
	UPROPERTY() TObjectPtr<UInputAction> IA_FloorDown;
	UPROPERTY() TObjectPtr<UInputAction> IA_Section;
	UPROPERTY() TObjectPtr<UInputAction> IA_SectionHome;
	UPROPERTY() TObjectPtr<UInputAction> IA_SectionEnd;

	TSharedPtr<SRHCommandDeck> Deck;
	TSharedPtr<class SRHMenus> Menus;
	float SpeedBeforeMenu = 1.f;
	int32 WorldClickCount = 0; // OnClick calls: the click test's leak counter
	// The title shows at boot except for automated runs (captures and
	// playtests pass -unattended; -RHNoTitle skips it by hand).
	bool ShouldShowTitle() const;
	// Any order being aimed or a building card open - what Esc cancels first.
	bool HasActiveMode() const;
	void OpenMenuScreen(uint8 Screen);
	FName PendingBuildDef;
	bool bDigMode = false;
	bool bSurveyMode = false;
	bool bShowSurveyMap = false;
	// Excavation designation state (M1-d): anchor set on click, rect follows
	// the cursor, release transmits. Cells are 10x10 m.
	bool bExcavateMode = false;
	bool bExcavateDragging = false;
	FVector ExcavateAnchorCm = FVector::ZeroVector;
	// Drag-sized placement (Agri Gate C): a DragSized def (the greenhouse dome)
	// anchors its first bed on click and grows in whole beds toward the cursor
	// while the button is held; release transmits.
	bool bBuildDragging = false;
	FVector BuildAnchorCm = FVector::ZeroVector;
	// The drag's footprint centre and beds per side for the cursor at GroundCm.
	void ComputeDragFootprint(const FVector& GroundCm, const FRHBuildingRow* Def, int32 MaxBeds, FVector& OutCenter, FIntPoint& OutSize) const;
	// Road mode state (C4).
	bool bRoadMode = false;
	bool bRoadDragging = false;
	FVector RoadAnchorCm = FVector::ZeroVector;
	bool bRoadForced = false;
	FVector RoadForcedToCm = FVector::ZeroVector;
	// A ground point snapped to the road grid, and to an existing junction within
	// 4 m of it (so a stretch can start exactly where another ends).
	FVector SnapRoadPoint(const FVector& GroundCm) const;
	// Tunnel brush state (D3).
	bool bTunnelMode = false;
	bool bTunnelDragging = false;
	bool bTunnelForced = false;
	FVector TunnelAnchorCm = FVector::ZeroVector;
	FVector TunnelForcedToCm = FVector::ZeroVector;
	// Cavern brush state (D6a): bCavernBrush is only read while bTunnelMode is set.
	bool bCavernBrush = false;
	int32 CavernFloors = 1;
	// Demolish Road state (C6b).
	bool bRoadDemolishMode = false;
	bool bRoadDemolishForced = false;
	FVector RoadDemolishForcedCm = FVector::ZeroVector;
	// District paint state (C5a).
	bool bDistrictMode = false;
	bool bDistrictDragging = false;
	bool bDistrictForced = false;
	FName DistrictKind;
	FIntPoint DistrictAnchor = FIntPoint::ZeroValue;
	FIntPoint DistrictForcedTo = FIntPoint::ZeroValue;
	int32 DistrictShapeFloors = 0;           // D6b: 0 = an underground click is the whole floor; 1..3 = a shape that many floors tall
	// Zone designation state (M2 Gate B).
	bool bZoneMode = false;
	FName ZoneRoom;
	// Furnish / Sell state (BM-5).
	bool bFurnishMode = false;
	FName FurnishRow;
	int32 FurnishRot = 0;
	bool bSellMode = false;
	bool bFitMode = false;
	FName FitRow;
	// The carved cell (spiral index) under a world point on the given floor,
	// or INDEX_NONE. The zoning click's inverse of the sim's SpiralCell.
	int32 FindCellAt(const FVector& GroundCm, int32 Level) const;
	int32 ActiveLevel = 0;
	// D8: the section's state (R1) and the floor-view pose it put aside.
	bool bSectionOn = false;
	bool bSectionExiting = false;               // SetActiveLevel's own call from ExitSection (any other call leaves the section first)
	int32 SectionFocus = 0;
	int32 SectionAxis = 0;
	int32 SectionPlaneRow = 0;
	int32 SectionShaft = 0;
	FVector2D SavedFocusXY = FVector2D::ZeroVector;
	float SavedZoomT = 0.45f, SavedPitchOffset = 0.f, SavedYaw = 0.f;
	bool bSectionStepHeld = false;              // W/S: one row on the press, then a repeat after 0.4 s every 0.25 s
	float SectionStepClock = 0.f;
	int32 SelectedBuildingId = 0;
	int32 SelectedCellLevel = 0;
	int32 SelectedCellIndex = INDEX_NONE;
	int32 SelectedBayHab = 0;
	int32 SelectedBay = INDEX_NONE;
	// Feedback channels (deck-rendered; see GetConfirmText/GetHintText).
	FString ConfirmText;
	FLinearColor ConfirmColor = FLinearColor::White;
	double ConfirmRealSeconds = -1.0e9;
	FString HintText;
	FLinearColor HintColor = FLinearColor::White;
	float LastNonZeroSpeed = 1.f;
	float RmbDragPx = 0.f; // accumulated right-drag distance: click-vs-drag disambiguation
};
