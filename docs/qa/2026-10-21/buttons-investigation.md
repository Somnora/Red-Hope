# Buttons investigation: "none of the buttons worked" - 2026-10-21

Read-only research against main @ 218f672. Nothing was built, run or edited;
engine citations are UE 5.8 source under /Volumes/Unreal/UE_5.8/Engine/Source.

## Verdict

The menus widget blocks every click aimed at the deck, from the moment the
menus first closed, ever since they landed in daaf86b (2026-10-15). Nobody
has clicked the deck since then: every check drives the game by console.

- `SRHMenus` is a full-viewport `SCompoundWidget` added at Z 20 above the
  deck (RHPlayerController.cpp:57). Its SLATE_BEGIN_ARGS sets no visibility
  (RHMenus.h:28), so it takes the Slate default, `EVisibility::Visible`
  (DeclarativeSyntaxSupport.h:663).
- "Collapsed when closed" applies only to the inner SOverlay
  (RHMenus.cpp:143-144). The SRHMenus root stays Visible, fills its viewport
  overlay slot (GameViewportClient.cpp:3376-3384), and is still painted.
- Slate puts every painted, hit-test-visible widget into the hit-test grid
  (SWidget.cpp:1575-1577, HittestGrid.cpp:862). It then takes the single
  front-most hit (HittestGrid.cpp:1015-1016) and routes the event only up
  that widget's own parent chain (HittestGrid.cpp:189-230).
- The deck is a sibling branch at a lower Z, so it is never on that path.
  Clicks, hovers and wheel events over a deck button go SRHMenus -> overlay
  -> SViewport -> the player controller, which treats them as world clicks
  (RHPlayerController.cpp:158, OnClick at :1326).
- Every other layout container in the tree defaults to SelfHitTestInvisible
  (SOverlay.h:80, SBox.h:45, SBoxPanel.h:279, SConstraintCanvas.h:120). The
  two custom compound roots, SRHMenus and SRHCommandDeck, are the only
  full-screen widgets that are Visible.

## 1. Everything that could stop a click reaching a deck or menu button

| # | Candidate | Finding |
|---|---|---|
| a | A full-screen Visible widget above the deck | **SRHMenus root, as above. This is the blocker.** The deck root SRHCommandDeck is Visible and full-screen too (RHCommandDeck.h:20, re-set by RHPlayerController.cpp:1729). That is harmless, because the buttons are its own children, but it is the same pattern. |
| b | Welcome or tip card, guide, alerts, toasts, PAUSED banner, hint | All are inside the deck, and all are content-sized slots that collapse when empty (RHCommandDeck.cpp:731-831, 1015-1100, 2582-2660). None covers the command bar. The guide halo is an SBorder that wraps its button and does not sit over it (RHCommandDeckGuide.cpp:684-696). The action card is SelfHitTestInvisible (RHCommandDeck.cpp:1202-1205), on a SelfHitTestInvisible canvas. |
| c | Loading, fade, crew or agent labels, debug text | None is a Slate widget. The only AddViewportWidgetContent calls in Source/ are RHPlayerController.cpp:53 and :57. There are no UMG CreateWidget or AddToViewport calls, and the visualizers draw in-world. |
| d | OnMouseButtonDown returning Handled | Only `Backdrop()` (RHMenus.cpp:56-62), which lives inside each menu screen and is collapsed with it. It is correct while a menu is open. |
| e | The player controller eating clicks | Not first in line. Under FInputModeGameAndUI, Slate offers each click to the widget path first (PlayerController.cpp:6398-6412), and the controller only sees what Slate leaves unhandled. A stuck tool mode can't block a button that is reachable. Under (a), every deck click leaks into OnClick: it can place an armed build, anchor a tunnel, or clear a selection under the bar. |
| f | Focus, capture, input-mode timing | SetInputMode runs after both widgets are added (RHPlayerController.cpp:50-67), and capture is CaptureDuringMouseDown, so nothing is wrong here. `-RHNoTitle` (midgame or late) only means the title never opens, so the deck is dead from the first frame. With `opening` or editor Play, the title shows (ShouldShowTitle, :1672-1677), so NEW COLONY works. After NEW COLONY, CloseMenus (:1721-1735) leaves the Visible root behind. CONTINUE is QuickLoad plus StartNewColony (:1745-1749) and has the same outcome. |
| g | macOS first click only focuses the window | Not the cause. UE's Cocoa view returns YES from acceptsFirstMouse (ApplicationCore/Private/Mac/CocoaTextView.cpp:41-44), so the first click is delivered. |
| h | Retina DPI or r.setres changing the hit geometry | Unlikely. Slate hit-tests against the same paint geometry it draws, and rebuilds the grid every frame, so a resize or DPI scale moves both together. The only DPI-sensitive code is the action-card offset (build-log Session 31), which places the card and has no effect on hits. |
| i | Disabled buttons | Build cards grey out when the colony can't afford them (RHCommandDeckPanels.cpp:2398). This is a partial and visible cause, and does not explain "none". |
| j | Hygiene, not the cause | EndPlay removes the deck but not the menus (RHPlayerController.cpp:70-77). A level reload would stack a second Visible SRHMenus. |

## 2. Was a button press ever seen working?

- **Yes, before the menus existed.** build-log.md:486 (Session 12c, 2026-07-06):
  the director placed a dig, two SolarArrays, a BatteryBank, a ChargePad and
  the Forge "through the deck's click-to-place flow", checked against the
  editor log. The deck was the only viewport widget then.
- **Never since daaf86b (2026-10-15).** Every capture runs `-unattended` with
  `-ExecCmds` (scripts/unreal/rh_capture.sh:61-63), and the commits and memory
  say so: "captures can't move the mouse" (build-log.md:2789, :3316, :3505),
  and project-state memory says "Not hand-tested: ... bay/cell clicks".
  Nothing in Source synthesises a Slate pointer event (grep for
  ProcessMouseButton, RH.Click: no hits).
- **No logs survive.** The ~/Library/Logs/Unreal Engine/red_hopeEditor logs
  are all `-run=RHSim` commandlets. No `-game` or editor session log from the
  director's run is left. Saved/RHCapture logs are harness runs.

## 3. Ranked hypotheses

1. **SRHMenus' Visible root covers the deck (near certain).**
   - Evidence: section 1a, and the timeline in section 2.
   - How the director can tell:
     - Deck buttons never show a hover highlight, while title and pause
       buttons do.
     - The mouse wheel over the bar zooms the camera, and the bar's scroll
       boxes don't scroll.
     - Keys still work: 1-4, Space, Esc, and Esc -> RESUME.
     - The pause and title menus click fine.
     - A deck click acts like a world click: it closes the inspection card,
       or drops an armed build under the bar.
   - Fix (small, about 3 lines plus a compile):
     - RHMenus.h:28 becomes
       `SLATE_BEGIN_ARGS(SRHMenus) { _Visibility = EVisibility::SelfHitTestInvisible; }`.
       The open screens keep their own Visible backdrops, so the menus still
       swallow clicks while up.
     - Do the same on RHCommandDeck.h:20, and make RHPlayerController.cpp:1729
       restore SelfHitTestInvisible.
     - Optionally, remove Menus in EndPlay.
2. **Build cards disabled for cost (partial).**
   - Tell: greyed cards and the "can't afford" state.
   - Fix: none needed, but say why on the card.
3. **Sim paused or slow, so an order looks dead (low).**
   - Uplink lag means a build only appears after the delay. The button itself
     still presses and posts a confirm line.
4. **Editor-Play specifics (low).**
   - PIE viewport focus or the "game gets mouse control" setting. GameAndUI
     already handles this.
   - Tell: it fails only in PIE, and is fine in rh_sandbox.
5. **Mac first click, Retina hit offset, or r.setres (very low).**
   - See 1g and 1h.
   - Tell: only the first click fails, or clicks land consistently offset
     from the drawn button.

## 4. An automated click test (design)

A cheat in RHCheats.cpp, which is in the presentation module, so the sim
boundary is untouched. It runs inside a normal `rh_capture.sh` `-game` boot.
Slate paints in captures, as UI snapshots show.

- **Finding a button.** Walk the tree from `GEngine->GameViewport->GetWindow()`
  through `GetAllChildren()`. Match an `SButton` with a descendant
  `STextBlock` whose text equals the label, ignoring case; an optional `nth`
  breaks ties. Keep only buttons for which `FSlateApplication::FindPathToWidget(W,
  Path, EVisibility::Visible)` succeeds, so the hidden title SETTINGS does not
  shadow the pause one. Take the centre of
  `GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates({0.5,0.5})`.
- **Reachability: the check that would have caught this bug.**
  - Call `FSlateApplication::Get().LocateWindowUnderMouse(Centre,
    GetInteractiveTopLevelWindows())`, then `Path.ContainsWidget(Button)`.
  - Log `RHClick: 'Traffic' at (x,y) leaf=<type>@<created-at> reached=YES|NO`.
  - `RH.ClickSweep` does this for every visible SButton and prints a
    `REACH n/m` line, naming each unreachable label.
- **Pressing.**
  - Over two ticks (an FTSTicker), send ProcessMouseMoveEvent, then
    ProcessMouseButtonDownEvent, then ProcessMouseButtonUpEvent.
  - Each event is an `FPointerEvent` built with that screen position and
    EKeys::LeftMouseButton.
  - The press goes through real hit-testing, capture and the SViewport
    fallback, exactly like a hand.
  - Don't move the OS cursor.
- **Leak guard.** Add a debug counter that OnClick increments. A click on a
  button must leave it unchanged.
- **World clicks for tools.** Add `RH.ClickWorld <Xm> <Ym>` and
  `RH.DragWorld ...`. Each projects a ground point to the screen
  (`ProjectWorldLocationToScreen`, divided by the DPI scale) and sends the
  same events.
- **Assertions.** `RH.ClickTest` runs the script below and prints one
  `CLICKTEST <name> PASS|FAIL <why>` per step, plus `CLICKTEST TOTAL`.

| Step | Setup | Click | Assert |
|---|---|---|---|
| Title NEW COLONY | boot with `-RHTitle` | `NEW COLONY` | `!PC->IsMenuOpen()`, deck visible, clock speed 1 |
| Title CONTINUE | `RH.Save quick`, then `RH.Menu title` | `CONTINUE` | OnColonyReloaded fired, menus closed |
| Pause menu | `RH.Menu pause` | `RESUME` | menus closed, speed restored |
| BUILD bar | none | a category tab, then a card (e.g. `Solar Array`) | drawer open; `GetPendingBuildDef()=="SolarArray"`. Then RH.ClickWorld at a valid spot queues one Build command |
| Speed buttons | none | `II`, `1x`, `3x`, `8x`, `60x` | `Clock->GetSpeed()` is 0, 1, 3, 8, 60 |
| Panel ADD | quota phase AwaitingManifest (RH.Playtest checkpoint or a cheat) | `ADD` on a manifest row | manifest line count +1 (HandleManifestAdd, RHCommandDeckPanels.cpp:621) |
| Traffic | surface | `Traffic` | `PC->IsTrafficOverlayOn()` flips; legend visible |
| Tunnel tool | `RH.Floor -1`, shaft depth >= 1 | `Tunnel` (SHAFT strip) | `PC->IsTunnelMode()`. Then RH.DragWorld queues one DigTunnel |
| Every step | none | none | OnClick counter unchanged by any button click; `REACH m/m` from RH.ClickSweep |

Wire it into the battery as one capture: `rh_capture.sh clicktest
"r.setres 1600x900w, RH.Demo, RH.After 3 RH.ClickTest"`. The judge greps
`CLICKTEST TOTAL` and `REACH`. Run it once before the fix: the sweep should
fail on every deck button and pass on the menu buttons, which confirms the
diagnosis. Run it again after the fix, where it should pass, and then on
every UI commit.
