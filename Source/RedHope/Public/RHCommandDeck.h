#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "RHDistricts.h"

class ARHPlayerController;
class URHPowerLookSubsystem;
struct FRHBuildingInstance;
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
	void Debug_ExpandAll(bool bRoster) { bFleetExpanded = bCrewExpanded = bPoliticsExpanded = bPowerExpanded = bMarketExpanded = true; bRosterExpanded = bRoster; }
	// The settings menu's "Guide and tips" switch.
	bool AreTipsOn() const { return !bTipsOff; }
	void SetTipsOn(bool bOn) { SetTipsOff(!bOn); }
	// Captures (RH.ClearTips): lets go of every card now queued (tips and milestones), as GOT IT on each would, without turning tips off
	// (nothing is remembered).
	void Debug_ClearTipCards();
	// Capture/QA (RH.DeckScroll): scroll the right-hand column to a pixel offset (a tall city outgrows the screen).
	void Debug_ScrollRight(float OffsetPx);
	void Debug_SetOrderDraft(FName Rival, FName Resource, int32 Kg); // E4 QA/captures: set one resource's draft on a rival's market card (the buttons' own state)
	void Debug_SetDeskDraft(bool bToMarks, int64 Amount);            // E5 QA/captures: the bloc desk's draft (the buttons' own state)
	// G1 (2026-11-04), nothing flashes: what the POWER tile, the power fold header, the build menu's spare-watts line, the selected
	// card's status and station lines, the Earth tension bar and the 60x button show. The widgets call these same functions, so the
	// flash probe and the click checks read exactly what is drawn. Power state comes from the shared helper (URHPowerLookSubsystem),
	// never the per-step shed flag.
	FText GetPowerTileValueText() const;
	FSlateColor GetPowerTileValueColour() const;
	FText GetPowerTileSubText() const;
	FSlateColor GetPowerTileSubColour() const;
	TOptional<float> GetPowerTileBarFrac() const;
	FSlateColor GetPowerTileBarColour() const;
	FText GetPowerFoldHeaderText() const;
	FText GetSpareWattsText(FName Def) const;
	FSlateColor GetSpareWattsColour(FName Def) const;
	FSlateColor GetTensionColour() const;
	void GetTensionMarks(double& OutDemand, double& OutEmbargo) const { OutDemand = TensionDemandAt; OutEmbargo = TensionEmbargoAt; }
	FText GetTensionNoteText() const;
	FString GetCardStatusLine(int32 BuildingId) const;   // no leading newline; empty for a site or an unknown Id
	FString GetCardStationLine(int32 BuildingId) const;  // the INFIRMARY / IDLE / SEATS line, no leading newline; empty if none
	// G1 r3: the zoned station cell's (or bay's) card lines - SEATS or NOT STAFFED, then its place in the order - as the card prints them
	// (held and latched like a module's seats), no leading newline and no dismiss line.
	FString GetCellStationLine(const FIntVector& Key) const;
	FString Debug_GetInspectText() const;                // G1 r3 QA (the flash probe): the selected card's whole text, as it draws it
	// G1 r3: what the Crew panel and the City panel show of the per-step station set (RefreshJobs), held over a rationed seat module's
	// steps (LatchServed): a roster row's right-hand text, the research line's staffed-Lab test and the research bar's visibility. The
	// widgets call these same functions.
	FString GetRosterPostLine(int32 ColonistId) const;
	// G2a (A7): the roster row's home, "surface habitat" or the floor signed ("floor -1"); the Earth credit readout in whole credits (the
	// OBJECTIVE panel's line); (A6) a site's stage line under its card's title; (QA) the uplink rows' labels as the panel built them, ' | '
	// between rows. The widgets call these same functions.
	FString GetRosterHomeText(int32 ColonistId) const;
	FString GetEarthCreditLine() const;
	FString GetSiteStageLine(const FRHBuildingInstance& B, const TMap<FName, double>& Bill) const;
	FString Debug_GetUplinkText() const;
	// G2a (A3): a build category's tab label (public for the click test's words.sample; the Borer's "Boring" reads "Underground").
	FText GetCategoryLabel(FName Category) const;
	bool IsLabSeatStaffedShown() const;
	bool IsResearchBarShown() const;
	const FRHCityDemand& Debug_GetCityDemandShown() const { return CityDemand; } // G1 r3 QA: the City panel's demand as it shows it
	bool IsSixtyRefusedShown(FString* OutWhy = nullptr) const; // CanEnterEraMode, polled at most four times a real second, latched
	// G1 r1: the Crew panel's food, garden and layout lines (each empty when the panel shows none), as the panel prints them. The
	// garden's grow lights and the layout's circulation follow per-step reads, so the producing count and the clash count come through
	// the power helper (held over the flip, latched). The panel passes its own Hope breakdown's food penalty and clashing pairs.
	FString GetCrewFoodLine(bool bFoodPenalty) const;
	FString GetCrewGardenLine() const;
	FString GetCrewLayoutLine(int32 OffendedPairs) const;
	FString Debug_GetGuideText() const;                  // the guide's shown stage | title | body, as its panel draws them
	const URHPowerLookSubsystem* GetPowerLook() const;
	// E8 QA (the click test): what the MARKET panel built - its own cached rows and strings, never the sim.
	int32 Debug_GetMarketRowCount() const { return MarketShown.Num(); }
	int32 Debug_GetMarketSparkGlyphs(FName Resource) const { return MarketSparkGlyphs.FindRef(Resource); } // drawn, non-space glyphs
	FString Debug_GetMarketChangeText(FName Resource) const { return MarketChangeText.FindRef(Resource); }
	FName Debug_GetMarketSelected() const { return MarketSelected; }
	FString Debug_GetMarketDetailText() const // the DETAIL text as drawn: its head, Earth's line and the holdings and tally
	{
		return MarketDetailText.ToString() + (MarketDetailEarth.IsEmpty() ? FString() : TEXT("\n") + MarketDetailEarth.ToString()) + (MarketDetailRest.IsEmpty() ? FString() : TEXT("\n") + MarketDetailRest.ToString());
	}
	FString Debug_GetMarketOrdersText() const;                     // the rows as drawn now (a convoy's percent read live, as its row reads it)
	int32 Debug_GetMarketRowRebuilds() const { return MarketRowRebuilds; }
	int32 Debug_GetMarketPricesRowSets() const { return MarketPricesRowSets; } // PRICES' row sets made (its rows cleared and made again), alone
	FString Debug_GetMarketUnitText() const { return MarketUnitText.ToString(); }       // the PRICES sub-heading as drawn
	FString Debug_GetMarketCreditCaption() const { return MarketCreditCaption.ToString(); } // DETAIL's credit-view caption (empty: no credit view)
	FString Debug_GetMarketRowTip(FName Resource) const { const TSharedRef<FMarketPriceRowText>* R = MarketPriceRows.Find(Resource); return R ? R->Get().Tip.ToString() : FString(); }
	bool Debug_HasMarketRow(FName Resource) const { return MarketPriceRows.Contains(Resource); } // a PRICES row was made for it
	uint64 Debug_GetMarketRowTag(FName Resource) const { const TSharedRef<FMarketPriceRowText>* R = MarketPriceRows.Find(Resource); return R ? (uint64)(UPTRINT)&R->Get() : 0; } // which row object: kept or made again
	int32 Debug_GetMarketTextUpdates() const { return MarketTextUpdates; }
	bool Debug_IsMarketRateShown() const { return bMarketRateShown; }
	int32 Debug_GetMarketRegionButtons() const { return MarketRegionButtons; }
	int32 Debug_GetMarketRouteCount() const { return MarketRouteChips.Num(); }
	FString Debug_GetMarketRouteChip(FName Rival) const { return MarketRouteChips.FindRef(Rival); }
	void Debug_GetMarketChipColours(TArray<TPair<FLinearColor, FLinearColor>>& Out) const { Out = MarketChipColours; } // (fill, text) per chip
	double Debug_GetMarketWorstRebuildMs() const { return MarketWorstRebuildMs; }
	TMap<FName, int32> GetOrderDraft(FName Rival) const { return OrderDrafts.FindRef(Rival); } // a rival card's draft, kg by resource
	EVisibility Debug_GetRivalCardVisibility(FName Rival) const;   // E8 (m5): a rival card's visibility attribute, on screen or not
	void Debug_SetMarketSelected(FName Resource) { MarketSelected = Resource; } // E8 captures and the frame check (RH.Market panel)
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
	// E5 review: the banner wraps at the gap left of the right column (local units, from the last tick's width).
	float GetAlertWrapAt() const;
	float DeckLocalWidth = 0.f;
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
	FReply HandleMove(); // BM-7: the Furnish and Walls palettes' "Move" (Move mode)
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
	// UI polish P3: the foldable panels of the right column (Politics, Crew, City, Fleet, Power, and E8's Market) are an accordion. Opening one
	// folds the others and scrolls it to the top of the column; a panel that is shut folds itself alone. The state lives here, as it always did,
	// and is not saved. E8 (Q-C (a)): MARKET joins it as the sixth; Count stays the "no scroll" sentinel.
	enum class EFold : uint8 { Politics, Crew, City, Fleet, Power, Market, Count };
	void SetFoldOpen(EFold Which, bool bOpen);
	bool IsFoldOpen(EFold Which) const { return *FoldFlag(Which); }
	// Capture/QA (RH.DeckCity): fold the City panel open or shut. Opening it is the accordion's rule: the other panels fold, and the column
	// scrolls the City panel into view.
	void Debug_SetCityExpanded(bool bOn) { SetFoldOpen(EFold::City, bOn); }
	// E8 QA: scroll a fold's header to the top of the column without opening it (the click test reaches MARKET's toggle this way).
	void Debug_ScrollFoldIntoView(EFold Which);
	// UI polish P8: the free band between the shaft strip and the right column, as fractions (0..1) of the deck's width: the deep view frames
	// the slab and its labels into it. Falls back to fixed fractions before the first layout.
	void GetFreeBand(float& OutLeft01, float& OutRight01) const;
	// P8c: the deep view's floor-name ladder (RHSectionLadder.h), drawn by the deck beside the cut; the controller sizes it off the band, the
	// section floors its world labels at its em (physical px), and the click test reads its rows.
	const TSharedPtr<class SRHSectionLadder>& GetLadder() const { return Ladder; }
	float GetLadderEmPx() const;
private:
	TSharedPtr<class SRHSectionLadder> Ladder;
	const bool* FoldFlag(EFold Which) const;
	bool* FoldFlag(EFold Which) { return const_cast<bool*>(static_cast<const SRHCommandDeck*>(this)->FoldFlag(Which)); }
	TSharedRef<SWidget> FoldToggle(EFold Which, FName Tag);
	TSharedRef<SWidget> FoldAnchor(EFold Which, const TSharedRef<SWidget>& Panel);
	void ApplyFoldScroll();
	// UI polish P4: where the shaft strip stands. Centred on the left edge, unless the top-left stack (vitals, uplink queue, TRAFFIC legend)
	// would reach it: then it sits 8 px below that stack and its floor list drops rows to stay above the command bar.
	void GetStripLayout(float& OutTopPx, int32& OutRows) const;
	// UI polish P2: the shaft card's fit button says only the verb; why it cannot be ordered is a line under it and its tooltip.
	FText GetFitLiftReason() const;
	EVisibility GetFitLiftReasonVisibility() const;
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
	// E8: the Routes overlay's button (beside Traffic, tagged RH.TradeOverlay) and its legend (under TRAFFIC's, while the overlay is on and
	// the markers show).
	FReply HandleTrade();
	FText GetTradeLabel() const;
	TSharedRef<SWidget> BuildTradeLegend();
	EVisibility GetTradeLegendVisibility() const;
	// Sims-style categorized build menu (M2 Gate D+ UX, part 2): a row of
	// category tabs; picking one shows only that category's buildings. Category
	// comes from the DataTable (FRHBuildingRow.Category), with a code-derived
	// fallback so the menu works before the DT re-sync lands the column.
	FReply HandleSelectCategory(FName Category);
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
	FReply HandleAtrium(int32 CavernId, bool bOpen); // D7: Open as atrium / Close the atrium (uplink orders)
	bool bAtriumGhostFromCard = false;               // D7: the ghost is up because a card button is hovered (cleared with the list)
	void RefreshCavernList();
	TSharedPtr<class SVerticalBox> CavernList;
	uint32 CavernListKey = MAX_uint32;
	// D6d: the card under the cavern list - the viewed floor's tram lines (their riders and wait, read live) and, for each finished tunnel long
	// enough to take one, a Lay tram button (the LayTramLine order, people only).
	FReply HandleLayTram(int32 TunnelId);
	void RefreshTramList();
	TSharedPtr<class SVerticalBox> TramList;
	uint32 TramListKey = MAX_uint32;
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
	FReply HandleManifestRemove(FName Item);         // v54: one off again (the way out of "Over budget at launch")
	FReply HandleLaunch();
	FReply HandleLaunchShort();                      // E3: a ship that cannot load the whole quota, after the window
	FReply HandleExportAdd(FName Resource, int32 Kg); // E3: sell surplus to Earth
	FReply HandleExportRemove(FName Resource);
	FReply HandleSolidarity(bool bComply);
	FReply HandleRivalVerb(FName Verb, FName Rival);
	// E6 (the levers, minimal; E8 is the full panel): a rival card's SIGN / BREAK - a Treaty order with Text Sign or Break (HandleRivalVerb
	// sets no Text). The sim's CanSignPact greys SIGN with the reason in its tooltip.
	FReply HandleTreaty(FName Rival, bool bSign);
	// E5 (the Martian currency, minimal; E8 is the full panel): the Dilemma card's third answer (Defy, and issue), the panel's issue button,
	// and the bloc desk's draft - a direction and whole units, nudged by To marks +100 / To credits +100 (a press against the draft's
	// direction takes 100 off it, as the market card's Buy / Sell net), emptied by CLEAR and transmitted by SEND as an Exchange order (the
	// sim's CanExchange greys SEND with the reason in its tooltip).
	FReply HandleDefyIssue();
	FReply HandleIssueCurrency();
	bool bDeskDraftToMarks = true;
	int64 DeskDraftAmount = 0;
	FReply HandleDeskNudge(bool bToMarks);
	FReply HandleDeskClear();
	FReply HandleDeskSend();
	TSharedRef<SWidget> BuildCurrencyBlock();
	// E4: the draft order on a rival's card. OrderDrafts holds, per rival, the draft kg by resource (positive buys from the rival, negative
	// sells to it); Buy +100 / Sell +100 move one resource's draft by 100 kg, CLEAR empties it, SEND ORDER transmits it as a MarketOrder
	// through the uplink (the sim's CanPlaceOrder reads the same text, so the button is greyed with the reason in its tooltip).
	TMap<FName, TMap<FName, int32>> OrderDrafts;
	FString DraftOrderText(FName Rival) const;       // "Res:kg;Res:kg" in resource-name order, empty with nothing drafted
	// G2a r1: the same draft in words for the screen ("buy 100 kg Hydrogen and sell 100 kg Ice"), from DraftOrderText's form; cached per rival
	// (DraftWordsCache: the form and its words) so the card's per-frame line formats the words only when the draft changes.
	const FString& DraftOrderWords(FName Rival, const FString& Text) const;
	mutable TMap<FName, TPair<FString, FString>> DraftWordsCache;
	FReply HandleOrderNudge(FName Rival, FName Resource, int32 Kg);
	FReply HandleOrderClear(FName Rival);
	FReply HandleOrderSend(FName Rival);
	TSharedRef<SWidget> BuildMarketCard(FName RowName, const struct FRHRivalRow& Row);
	TMap<FName, TSharedPtr<class SBox>> RivalCardBoxes;              // E8: each rival card's root on EARTH & NEIGHBORS (Debug_GetRivalCardVisibility)
	// E8: the MARKET panel (RHCommandDeckPanels.cpp; docs/qa/2026-10-25/e8-market-panel-design.md, REVISION 3, and the director's Rebuild (a),
	// 2026-10-31). RefreshMarketPanel (from Tick) does nothing while MARKET is folded but keep the header's one-line summary; it builds once
	// when the fold opens. Each section has two keys: its ROW SET (re-creates its widgets: a convoy booked or gone, a slot or route shown or
	// hidden, the region or the selection, a load, the fold opening) and its VALUES (rewrites the cached texts its kept widgets read through
	// bound lambdas, no widget made). A convoy's progress is read by its row's own lambda, never by a key. A paint reads the cached FTexts.
	TSharedRef<SWidget> BuildMarketPanel();
	void RefreshMarketPanel();
	void HandleMarketReloaded();
	void ShownMarketSlots(int32 RegionId, TArray<FName>& Out) const; // R2-1: the slots an available economy rival in RegionId trades
	static constexpr int32 MarketSparkWidth = 30;                  // Q-B: a 30-glyph sparkline on its own line
	// One PRICES row's texts, one OPEN ORDERS row's and one ROUTES row's: each row's widgets keep a reference and read them by lambda.
	struct FMarketPriceRowText { FText Price, Spark, Change, Tip; };
	struct FMarketOrderRowText
	{
		int32 ConvoyId = INDEX_NONE;                              // INDEX_NONE: a desk order (its line is Head alone)
		FText Head, Extra, StateFixed;
		bool bProgress = false, bPriced = false;                  // on the road: the state is "on the road, N%" with the live percent
		int32 ClearSol = -1;
		int32 ShownPct = -1;                                      // the percent CachedState was made with (-1 none yet)
		FText CachedState;
	};
	struct FMarketRouteRowText { FName Rival; FText Line, Chip, Terms; FLinearColor ChipFill = FLinearColor::White; };
	FText MarketOrderStateText(FMarketOrderRowText& Row) const;     // the row's state line now (a progress row re-made only when its percent moves)
	// PRICES' value parts, each rewritten only when its own inputs move: the rows and the unit's sub-heading, DETAIL's chart and texts (with
	// a roll, these and every part below), DETAIL's Earth line alone (Earth's quote moves with the requisition multiplier between rolls),
	// TRADE BALANCE (the lifetime totals) and EXCHANGE RATE (the desk's totals today).
	enum EMarketPart : uint8 { MarketPartRows = 1, MarketPartDetail = 2, MarketPartBalance = 4, MarketPartRate = 8, MarketPartEarth = 16, MarketPartAll = 31 };
	void FillMarketPrices(const class URHSimWorldSubsystem& S, const class URHDefinitionsSubsystem& Df, uint8 Parts, int32 RowFrom = 0, int32 RowTo = MAX_int32);
	// Values that move without a row-set change are rewritten a slice a pass (two price rows, or one other part), so no frame carries a whole
	// roll's rewrite: the parts still to do, and the next price row.
	uint8 MarketPendingParts = 0;
	int32 MarketPendingRow = 0;
	static constexpr int32 MarketRowsPerSlice = 2;
	void FillMarketOrders(const class URHSimWorldSubsystem& S, const class URHDefinitionsSubsystem& Df);
	void FillMarketRoutes(const class URHSimWorldSubsystem& S, const class URHDefinitionsSubsystem& Df);
	void UpdateMarketSummary(const class URHSimWorldSubsystem& S, const class URHDefinitionsSubsystem& Df, bool bFolded);
	bool bMarketExpanded = false;
	int32 MarketRegion = 0;
	FName MarketSelected;
	int32 MarketReloads = 0;
	uint32 MarketPricesSetKey = 0, MarketOrdersSetKey = 0, MarketRoutesSetKey = 0;  // the row sets
	uint32 MarketPricesKey = 0, MarketOrdersKey = 0, MarketRoutesKey = 0;           // the values
	uint32 MarketDetailKey = 0, MarketLifetimeKey = 0, MarketDeskKey = 0;          // PRICES' parts beyond its rows
	uint32 MarketSummaryKey = 0;
	bool bMarketKeysValid = false, bMarketSummaryValid = false, bMarketOrdersBuilt = false;
	FName MarketRowsSelected;                                      // the selection DETAIL was last written for (the rows draw the selection's gold by lambda)
	TSharedPtr<class SVerticalBox> MarketRowsBox, MarketOrdersBox, MarketRoutesBox;
	TSharedPtr<class SHorizontalBox> MarketRegionBox;
	FText MarketSummary, MarketUnitText, MarketDetailChart, MarketCreditChart, MarketCreditCaption, MarketDetailText, MarketDetailEarth, MarketDetailRest, MarketHoldersTip, MarketBalanceText, MarketRateHead, MarketRateChart, MarketRateText;
	bool bMarketRateShown = false;
	int64 MarketBalanceMcr = MIN_int64;                            // the 8-sol balance in the live unit; MIN_int64 = nothing traded
	int32 MarketOrdersCount = 0;
	TArray<FName> MarketShown;
	TMap<FName, TSharedRef<FMarketPriceRowText>> MarketPriceRows;
	TArray<TSharedRef<FMarketOrderRowText>> MarketOrderRows;
	TArray<TSharedRef<FMarketRouteRowText>> MarketRouteRows;
	TMap<FName, int32> MarketSparkGlyphs;
	TMap<FName, FString> MarketChangeText;
	int32 MarketRegionButtons = 0;
	TMap<FName, FString> MarketRouteChips;
	TArray<TPair<FLinearColor, FLinearColor>> MarketChipColours;
	double MarketWorstRebuildMs = 0.0;
	int32 MarketRowRebuilds = 0, MarketTextUpdates = 0;             // QA and [FrameStats]: passes that re-created rows, and passes that only rewrote texts
	int32 MarketPricesRowSets = 0;                                  // QA (market.history): PRICES' own row sets made, never the orders' or routes'
	const class URHSimWorldSubsystem* GetSim() const;
	const class URHSimClockSubsystem* GetClock() const;
	const class URHDefinitionsSubsystem* GetDefs() const;
	// G1: the Earth tension bar's thresholds, read from config when the Politics panel is built (EarthTensionDemandThreshold,
	// EarthPreemptiveThreshold), and the 60x button's refusal poll.
	double TensionDemandAt = 45.0;
	double TensionEmbargoAt = 75.0;
	mutable double SixtyPollAtS = 0.0;
	mutable bool bSixtyRefused = false;
	mutable FString SixtyWhy;
	// G2a (A6): the open site card's builder poll (a claimed Build task to it), at most four times a real second.
	mutable double SiteStagePollAtS = 0.0;
	mutable int32 SiteStagePollId = 0;
	mutable bool bSiteBuilderShown = false;
	bool IsSiteBuilderShown(int32 BuildingId) const;
	// G1: a keyed boolean through the power helper's latch (the value itself when there is no helper).
	bool LatchHeld(uint8 Kind, uint32 Value, bool bWant) const;
	// G1 r3: a value read from what RefreshJobs rebuilds every step (a station's seats, a colonist's post, the colony's filled and total
	// seats, a staffed Lab). RefreshJobs lists a prefab station only on a step GetProvidedRoom(B, true) returns it, which reads that step's
	// bPowered, so while the colony rations these follow the shed flag. Encoded so that higher is the served steps' view, the value is the
	// highest seen inside the colony's hold while it rations, else itself, latched through the floor (URHPowerLookSubsystem::LatchWorst).
	uint32 LatchServed(uint8 Kind, uint32 Value, uint32 Want) const;
	// G1 r3: the City panel's colony seat counts as last refreshed (the sim's, unheld); RefreshCityPanel writes them into CityDemand
	// through LatchServed every frame the panel is open.
	int32 CityFilledRaw = 0;
	int32 CityTotalRaw = 0;
	uint8 GetPowerTileState() const; // G1: the POWER tile's latched state (0 rationing, 1 short, 2 surplus)

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
	// UI polish P3: each foldable panel's root in the column, and the scroll to its header that an opening asks for (a few frames later, once
	// the fold has been laid out, and kept up for a second while the panel's body fills).
	TWeakPtr<SWidget> FoldAnchors[(int32)EFold::Count];
	EFold FoldScrollTarget = EFold::Count;
	uint64 FoldScrollFromFrame = 0;
	double FoldScrollUntil = 0.0;
	// UI polish P4: the top-left stack and the shaft strip, measured for the strip's place.
	TSharedPtr<class SWidget> TopLeftStack;
	TSharedPtr<class SWidget> ShaftStripBorder;
	TSharedPtr<class SWidget> ShaftRowsBox;
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
	TArray<FString> UplinkLabelsShown; // G2a QA: each row's label as built (Debug_GetUplinkText)
	// In-world action card position (local slate units) + on-screen flag, both
	// refreshed in Tick from the selected building's screen projection.
	FVector2D ActionCardPos = FVector2D::ZeroVector;
	bool bActionCardOnScreen = false;
	// Categorized build menu: the tab currently showing, defaulted to the first
	// category present in the data at Construct.
	FName SelectedBuildCategory;
};
