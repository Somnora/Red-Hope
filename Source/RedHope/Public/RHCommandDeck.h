#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "RHDistricts.h"

class ARHPlayerController;
class ITableRow;
class STableViewBase;
template <typename ItemType> class SListView;

// Command deck v1 (M1-a Gate C): the mission-control surface. Pure C++ Slate
// (UMG authoring is unavailable to this toolchain); functional gray-box now,
// diegetic skin in M1-d. Bottom bar = build palette from DT_Buildings slice
// rows + dig + speed tiers + save/load; top-right = the colony readout. The
// deck only aims - every order still crosses the sim's uplink seam.
class REDHOPE_API SRHCommandDeck : public SCompoundWidget
{
public:
	// Full-viewport root: SelfHitTestInvisible so only its panels and buttons take hits, never the empty space between
	// them (those clicks belong to the world). CloseMenus restores exactly this.
	SLATE_BEGIN_ARGS(SRHCommandDeck) { _Visibility = EVisibility::SelfHitTestInvisible; }
		SLATE_ARGUMENT(TWeakObjectPtr<ARHPlayerController>, OwnerPC)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	// Capture/QA: unfold every collapsible panel (RH.DeckExpand); the roster
	// only with "roster", it is tall enough to push the rest off screen.
	void Debug_ExpandAll(bool bRoster) { bFleetExpanded = bCrewExpanded = bPoliticsExpanded = bPowerExpanded = true; bRosterExpanded = bRoster; }
	// The settings menu's "Guide and tips" switch.
	bool AreTipsOn() const { return !bTipsOff; }
	void SetTipsOn(bool bOn) { SetTipsOff(!bOn); }
	// Capture/QA (RH.DeckScroll): scroll the right-hand column to a pixel offset (a tall city outgrows the screen).
	void Debug_ScrollRight(float OffsetPx);
	// Capture/QA: open the build drawer on a category (RH.DeckBuild).
	void Debug_OpenBuild(FName Category) { SelectedBuildCategory = Category; bBuildDrawerOpen = true; }
	bool IsBuildDrawerOpen() const { return bBuildDrawerOpen; }
	// A click, release or wheel that lands on a deck panel and that no button inside took stops here: it must not also
	// act on the world underneath (clear a selection, drop an armed build). Bubbling reaches this root even though it is
	// SelfHitTestInvisible; the empty space between panels is not a hit, so it still reaches the world.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	// Drives the uplink panel rebuild (dynamic button rows can't be attributes).
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText GetNoticeText() const;
	EVisibility GetNoticeVisibility() const;
	// PC feedback channels (M1-b Gate C): one-shot confirms + live mode hint,
	// replacing the GEngine debug text that hid under the Slate deck.
	FText GetConfirmText() const;
	FSlateColor GetConfirmColor() const;
	EVisibility GetConfirmVisibility() const;
	FText GetHintText() const;
	FSlateColor GetHintColor() const;
	EVisibility GetHintVisibility() const;
	// PAUSED banner: unmissable, unlike the readout's small speed label.
	EVisibility GetPausedVisibility() const;
	// World-pressure banner (M1-c): active storm/flare, top center.
	FText GetEventText() const;
	FSlateColor GetEventColor() const;
	EVisibility GetEventVisibility() const;
	// Transient must-not-miss banner (onset 1x snap, era refusal, ship ETA).
	FText GetAlertText() const;
	EVisibility GetAlertVisibility() const;
	// Uplink queue panel (M1-c): orders in flight with countdowns; the list
	// rebuilds only when the queue changes (ids are the change signal).
	void RefreshUplinkPanel();
	// Power strip-chart: last 3 sols of gen/load/bank as block-char sparklines.
	FText GetPowerChartText() const;
	// Inspection card: click a building, read its live state.
	FText GetInspectText() const;
	EVisibility GetInspectVisibility() const;
	// The in-world action card (M2 Gate D+ UX overhaul, director's #1 ask):
	// clicking a machine floats a card ABOVE it in the world with a plain-
	// language blurb, its live state, and the verbs it affords with real costs.
	// Positioned each Tick by projecting the building's world point to screen.
	// GetInspectVisibility gates it (a building is selected); the offset getter
	// places it. Actions issue normal uplink orders - the card never bypasses
	// the sim's signal lag.
	FText GetActionCardTitle() const;      // display name + director's blurb
	FMargin GetActionCardOffset() const;   // projected screen pos (local units)
	EVisibility GetActionCardVisibility() const; // selected AND on-screen
	// Per-verb action buttons + their cost labels. Visible only for a building
	// that affords the verb (Bore/Excavate = the Borer; Off/On = anything built).
	EVisibility GetBoreActionVisibility() const;
	FText GetExcavateActionLabel() const;
	EVisibility GetExcavateActionVisibility() const;
	EVisibility GetActionPowerVisibility() const;
	// DAY SHIFT (D5a): on a finished heavy machine (Forge, Electrolyzer, Water
	// Plant, Ice Drill, Borer) - works while the sun is up, idles at night.
	EVisibility GetDayShiftVisibility() const;
	// ASSIGN STAFF (tiered production T4): on a selected work station - a zoned
	// seat cell or a built Lab module - puts it first in the colony's staff
	// order (uplink verb "Assign").
	EVisibility GetAssignActionVisibility() const;
	FText GetAssignActionLabel() const;
	FReply HandleAssign();
	// The selected station's key for the sim's seat maps, or false.
	bool GetSelectedStationKey(FIntVector& OutKey) const;
	// DEMOLISH (BM-3): any selected structure but the Lander. The sim's own
	// preflight (CanDemolishBuilding) names the salvage or the refusal; the
	// first click arms, a second within DemolishArmSeconds transmits a normal
	// uplink order (cancellable until it lands).
	EVisibility GetDemolishActionVisibility() const;
	FText GetDemolishActionLabel() const;
	FText GetDemolishNote() const;
	FReply HandleDemolish();
	bool IsDemolishInFlight(int32 BuildingId) const;
	int32 DemolishArmedId = 0;
	double DemolishArmedAt = 0.0;
	static constexpr double DemolishArmSeconds = 4.0;
	// The crew panel shows once anyone has landed (M2 Gate A2).
	EVisibility GetCrewVisibility() const;
	// Room zoning (M2 Gate B): arm the controller's zone-paint mode.
	FReply HandleZone(FName Room);
	// Furnish mode (BM-5): the zone row's FURNISH tab holds the catalogue.
	FReply HandleFurnish(FName Row);
	FReply HandleSellFurniture();
	// BM-6: the WALLS tab - windows, doors, airlocks.
	FReply HandleFit(FName Row);
	// C5a: the DISTRICT tab - a Kind arms the controller's district paint mode (the City panel's
	// kind chips do the same).
	FReply HandleDistrict(FName Kind);
	// D6b: what an underground press paints in district mode - the whole floor, or a shape 1..3 floors tall (steps on each press).
	FText GetDistrictShapeLabel() const;
	FReply HandleDistrictShape();
	int32 RowTab = 0; // the zone row's tab: 0 ZONE, 1 FURNISH, 2 WALLS, 3 DISTRICT
public:
	void Debug_SetFurnishTab(bool bOn) { RowTab = bOn ? 1 : 0; }
	void Debug_SetRowTab(int32 Tab) { RowTab = FMath::Clamp(Tab, 0, 3); }
	// Capture/QA (RH.DeckCity): fold the City panel open or shut.
	// Opening it also folds the Crew and Fleet panels, so the City panel fits the right column.
	void Debug_SetCityExpanded(bool bOn) { bCityExpanded = bOn; if (bOn) { bCrewExpanded = false; bFleetExpanded = false; } }
private:
	// Known Ground panel (director request): surveyed coverage + every
	// discovered deposit's type/tonnage/dig status, visible while Map is on.
	FText GetKnownGroundText() const;
	EVisibility GetKnownGroundVisibility() const;
	FText GetMapLabel() const;
	FReply HandleBuild(FName DefName);
	// C4: the Road card in BUILD > Logistics. UI only - a road is not a building
	// (no DT_Buildings row, no site, no footprint): the card arms the controller's
	// road mode, and its numbers come from the sim's road config.
	FReply HandleRoad();
	TSharedRef<SWidget> BuildRoadCard();
	bool IsRoadDetail() const;
	static const FName RoadCardName;
	// C6b: the Demolish Road card beside it (arms the controller's demolish tool), and the traffic overlay's
	// button and legend panel.
	FReply HandleRoadDemolish();
	TSharedRef<SWidget> BuildRoadDemolishCard();
	bool IsRoadDemolishDetail() const;
	static const FName RoadDemolishCardName;
	FReply HandleTraffic();
	FText GetTrafficLabel() const;
	TSharedRef<SWidget> BuildTrafficLegend();
	EVisibility GetTrafficLegendVisibility() const;
	// Sims-style categorized build menu (M2 Gate D+ UX, part 2): a row of
	// category tabs; picking one shows only that category's buildings. Category
	// comes from the DataTable (FRHBuildingRow.Category), with a code-derived
	// fallback so the menu works before the DT re-sync lands the column.
	FReply HandleSelectCategory(FName Category);
	FText GetCategoryLabel(FName Category) const;
	FSlateColor GetCategoryTabColor(FName Category) const;
	EVisibility GetBuildCatVisibility(FName Category) const;
	FReply HandleDig();
	FReply HandleSurvey();
	// Excavation designation (M1-d): paint carve cells on the active floor.
	FReply HandleExcavate();
	// Bore designation (M1-d): each click orders the trunk one floor deeper
	// (uplink verb; rejected loudly without a Borer online). Label reads the
	// next target so the button says what it will do.
	FReply HandleBore();
	FText GetBoreLabel() const;
	// D2: "Sink a shaft here" on a Borer that anchors no shaft (pre-checks the sim's SinkShaft preflight).
	EVisibility GetSinkShaftVisibility() const;
	FText GetSinkShaftLabel() const;
	FReply HandleSinkShaft();
	// D4: the shaft card on the strip - the shaft nearest the camera: its cars (or "hoist"), the load against capacity this sol,
	// the wait, and "Fit a lift car (N kg Struct)" (pre-checks the sim's FitLift preflight). Gate-D placeholder copy.
	EVisibility GetLiftCardVisibility() const;
	FText GetLiftCardText() const;
	FText GetFitLiftLabel() const;
	FReply HandleFitLift();
	// D3 (Q2a): the Tunnel brush button on the shaft strip, and the card under it listing the viewed floor's tunnels, each with
	// its progress (or "stalled", O11) and a Cancel.
	FReply HandleTunnel();
	FReply HandleCancelTunnel(int32 TunnelId);
	void RefreshTunnelList();
	TSharedPtr<class SVerticalBox> TunnelList;
	uint32 TunnelListKey = MAX_uint32;
	// D6a: the Cavern brush button on the shaft strip (a second press steps the box's height), and the card under it listing the
	// viewed floor's open caverns, each with what is left and a Cancel.
	FText GetCavernLabel() const;
	FReply HandleCavern();
	FReply HandleCancelCavern(int32 CavernId);
	void RefreshCavernList();
	TSharedPtr<class SVerticalBox> CavernList;
	uint32 CavernListKey = MAX_uint32;
	// The elevator (M1-d): SURF / -1 ... -MaxDepth cells, live state.
	FReply HandleFloor(int32 Level);
	// D5 depth: the strip's rows sit in a scroll box capped at 11 rows. Floors past the cool line show only once reached, next to bore
	// or viewed (-1..-5 stay as they were); choosing a floor scrolls its row into view.
	EVisibility GetShaftRowVisibility(int32 Level) const;
	void FollowShaftScroll();
	// D8: the slider's range, SURF to the deepest bored floor (0 while nothing is bored).
	int32 GetSectionSliderDepth() const;
	TSharedPtr<class SScrollBox> ShaftScroll;
	TMap<int32, TSharedPtr<class SWidget>> ShaftRows; // Level -> the row (0 = SURF)
	int32 ShaftScrollLevel = MAX_int32;               // the viewed floor the scroll last followed
	float ShaftRowHeight = 0.f;                       // one row's desired height, measured once
	FText GetFloorLabel(int32 Level) const;
	FSlateColor GetFloorCellColor(int32 Level) const;
	FSlateColor GetFloorTextColor(int32 Level) const;
	FReply HandleMap();
	// Storm power discipline (director ruling, M1-d hand-play): the manual
	// breaker on the inspected structure + the colony-wide fleet hold.
	FReply HandlePowerToggle();
	FText GetPowerToggleLabel() const;
	FReply HandleDayShiftToggle();
	FText GetDayShiftLabel() const;
	FReply HandleFleetHold();
	FText GetFleetHoldLabel() const;
	FReply HandleSpeed(float Tier);
	FReply HandlePause();
	FReply HandleSave();
	FReply HandleLoad();
	FReply HandleCancelOrder(int32 CommandId);

	// --- Onboarding & rewards (alive pass, director's pickup-and-play brief) ---
	// Info cards: a queue of center-screen cards - a short skippable tutorial
	// on a fresh colony, golden celebration cards on milestones. GOT IT pops
	// the next; SKIP ALL TIPS silences the tutorial forever (saved to game ini).
	void PushCard(const FString& Title, const FString& Body, bool bGolden);
	FText GetInfoCardTitle() const;
	FText GetInfoCardBody() const;
	EVisibility GetInfoCardVisibility() const;
	EVisibility GetSkipTipsVisibility() const;
	FReply HandleCardGotIt();
	FReply HandleCardSkipAll();
	// Reward toasts: first-acquisition of each resource gets a one-line cheer
	// (top center); rare materials get a full golden card instead.
	void HandleStockChanged(FName Resource, double Total);
	void HandleMilestone(const FString& Line);
	FText GetToastText() const;
	EVisibility GetToastVisibility() const;

	// --- Interface pass (2026-10-15): titled panels replace the text dumps ---
	// RHCommandDeckPanels.cpp. The vitals strip (top left) puts the colony's
	// numbers on icon tiles; the right column is Objective (quota bars, the
	// supply-ship manifest picker and LAUNCH), Crew, Fleet, Earth & Neighbors
	// (the M3/M4 verbs, until now console-only) and Power history. Collapsible
	// panels keep a one-line summary in their header.
	TSharedRef<SWidget> BuildVitalsStrip();
	TSharedRef<SWidget> BuildObjectivePanel();
	TSharedRef<SWidget> BuildCrewPanel();
	// C5a: the CITY panel - four demand bars with their plain-words prose, the kind chips that arm
	// district painting, and the district list (name field, counts, erase).
	TSharedRef<SWidget> BuildCityPanel();
	EVisibility GetCityVisibility() const;
	void RefreshCityPanel();
	const FRHCityDemand& GetCityDemandShown() const { return CityDemand; }
	TSharedRef<SWidget> BuildFleetPanel();
	TSharedRef<SWidget> BuildPoliticsPanel();
	TSharedRef<SWidget> BuildPowerPanel();
	TSharedRef<SWidget> BuildHelpPanel();
	TSharedRef<SWidget> BuildCommsPanel();
	TSharedRef<SWidget> ExpandToggle(bool* bFlag);
	void RefreshFleetPanel();
	// The build menu (interface pass step 2): category tabs on the command bar
	// open a drawer of building cards - in-game thumbnail, name, cost (red
	// when the colony can't cover it), power - beside a detail panel with the
	// hovered card's whole story. Picking a card starts placement and folds
	// the drawer away.
	TSharedRef<SWidget> BuildBuildDrawer();
	TSharedRef<SWidget> BuildCard(FName DefName);
	// The build bill as words + whether the colony is short of it.
	FString BuildCostLine(FName DefName, bool& bOutShort) const;
	// Which building the drawer's detail panel describes.
	FName GetDetailBuildDef() const;
	static FName CategoryIcon(FName Category);
	void RefreshRosterPanel();
	FReply HandleManifestAdd(FName Item);
	FReply HandleLaunch();
	FReply HandleSolidarity(bool bComply);
	FReply HandleRivalVerb(FName Verb, FName Rival);
	const class URHSimWorldSubsystem* GetSim() const;
	const class URHSimClockSubsystem* GetClock() const;
	const class URHDefinitionsSubsystem* GetDefs() const;

	// --- The first-hour guide (RHCommandDeckGuide.cpp) ---
	// A live "next step" panel atop the right column: the first unfinished
	// step of a fixed chain, read from sim state each Tick (a loaded colony
	// lands on the right step); steps never un-complete. The controls it
	// names pulse amber. Hidden with the tips; back from the "?" card.
	// Interrupts (D2a ice; D4a/D5a reuse the layer) sit outside the chain:
	// while one applies it outranks the current step, and it clears itself
	// the moment nothing can resolve it (the chain comes straight back).
	TSharedRef<SWidget> BuildGuidePanel();
	void RefreshGuide();
	FName GetGuideBuildDef() const;
	FName GetGuideOrder() const;
	bool IsGuideTarget(FName Key) const;
	FLinearColor GuidePulse() const;
	TSharedRef<SWidget> GuideHalo(FName Key, const TSharedRef<SWidget>& Content);
	void SetTipsOff(bool bOff);
	int32 GuideStep = -1;      // -1 until first evaluated
	int32 GuideHighWater = 0;
	int32 GuideInterrupt = INDEX_NONE; // a live interrupt step, outside the high-water chain

	bool bShowHelp = false;
	bool bBuildDrawerOpen = false;
	FName HoveredBuildDef;
	// Orderable buildings (def, category) in data order, and the distinct
	// categories in first-seen order - filled once at Construct.
	TArray<TPair<FName, FName>> BuildEntries;
	TArray<FName> BuildCategoryOrder;
	bool bFleetExpanded = false;
	bool bCrewExpanded = true;
	bool bCityExpanded = true;
	// The City panel's snapshot, refreshed a few times a second (the demand walks every room), and
	// the district list's rebuild signal (the districts' version).
	FRHCityDemand CityDemand;
	TMap<int32, FRHDistrictCounts> CityCounts;
	double CityRefreshedAt = -1.0e9;
	// C6b: the commute line of the City panel (long commuters and the busiest route), refreshed with the demand.
	FString CityCommuteLine;
	uint32 CityVersionShown = MAX_uint32;
	TSharedPtr<class SVerticalBox> CityDistrictList;
	TSharedPtr<class SScrollBox> RightScroll;
	// C5b: the open growth projects (rebuilt when the sim's growth or district version moves; each row's bar reads the live project).
	uint32 CityGrowthVersionShown = MAX_uint32, CityGrowthDistrictsShown = MAX_uint32;
	TSharedPtr<class SVerticalBox> CityGrowthList;
	bool bRosterExpanded = false;
	bool bPoliticsExpanded = false;
	bool bPowerExpanded = false;
	// Fleet rows, refreshed each Tick from the agent snapshot; the list widget
	// rebuilds only when membership changes (rows read by index).
	// C6b: a cargo rover's row also knows its load and whether it is on a road trip (its status line).
	struct FFleetRowView { FString Name; uint8 Task = 0; float Charge = 0.f; float Wear = 0.f; bool bRover = false; float CargoKg = 0.f; bool bOnRoad = false; int32 Level = 0; };
	TArray<FFleetRowView> FleetRows;
	TArray<FName> FleetDefsShown;
	TSharedPtr<class SVerticalBox> FleetList;
	// The roster (C3): a virtualized list - only the rows on screen exist - whose
	// rows bind by colonist Id (never by array index, which shifts under every
	// arrival and departure), with a name search and filters. Rebuilt when the
	// roster's membership or support state changes, so a same-count turnover (one
	// leaves, one arrives) refreshes it too.
	enum class ERosterFilter : uint8 { All, Unsupported, MarsBorn, FirstCrew };
	TSharedPtr<SListView<TSharedPtr<int32>>> RosterListView;
	TArray<TSharedPtr<int32>> RosterItems;
	ERosterFilter RosterFilter = ERosterFilter::All;
	FString RosterSearch;
	uint32 RosterSigShown = 0;
	bool bRosterSigValid = false;
	TSharedRef<ITableRow> GenerateRosterRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner);

	TWeakObjectPtr<ARHPlayerController> PC;
	// Info-card + toast state.
	struct FInfoCard { FString Title; FString Body; bool bGolden = false; };
	TArray<FInfoCard> CardQueue;
	bool bTipsOff = false;
	int32 TutorialStage = 0;      // 0 = not pushed; 1 = basics queued; 2 = crew card done
	TSet<FName> SeenStock;        // resources already toasted (or pre-existing)
	double StockGraceUntil = 0.0; // boot/reload grace: existing stocks don't toast
	FString ToastLine;
	double ToastUntil = 0.0;
	// Uplink panel state: rebuilt when the queue's id set changes.
	TSharedPtr<class SVerticalBox> UplinkList;
	TArray<int32> UplinkIdsShown;
	// In-world action card position (local slate units) + on-screen flag, both
	// refreshed in Tick from the selected building's screen projection.
	FVector2D ActionCardPos = FVector2D::ZeroVector;
	bool bActionCardOnScreen = false;
	// Categorized build menu: the tab currently showing, defaulted to the first
	// category present in the data at Construct.
	FName SelectedBuildCategory;
};
