# Browser touch preparation

The first touch surface is a browser front end to the existing authoritative
kavtor panel API. It is separate from faderOS host/protocol configuration and
requires neither the Sony panel nor a Stream Deck. Stream Deck operation is
outside kavtor: title triggering can be configured in external applications.

## Start

Run kavtor with its panel API enabled (default TCP port 9100), then:

```sh
python3 tools/run_touch_surface.py --listen 0.0.0.0 --port 8098
```

Open `http://HOST_IP:8098` from a tablet on the same LAN. Use `--server` and
`--panel-port` when the mixer API runs elsewhere. The installed launcher is
`kavtor-touch-surface`. Python 3's standard library is sufficient; there are no
npm packages or additional renderer dependencies. By default the launcher binds
localhost. This initial service targets a local/trusted production network,
not anonymous Internet control; authentication and remote deployment remain
separate work.

## Included pages

* MIX: AUTO duration, Dust Mix ratio/size/flash, SUPER MIX gains and DIP color.
* WIPE: native catalogue default preparation, border width/color, general softness,
  center/inner/outer placement and independent inner/outer softness.
* KEYERS: delegated upstream/downstream processing, supported LINEAR/LUMA/CHROMA
  controls, inversion and rectangle masks. On-air edits require confirmation.
* DME: preparation of supported native backgrounds with black/input selection.

The first Pattern page edits the stored default used by API clients that do not
supply an explicit pattern. Physical keypad shortcut selections remain explicit
and take precedence for their own takes; selection synchronization is separate
work. Parameter edits still use the shared configuration.

This is a working first preparation surface, not the complete replacement for
all Qt management tools. Media import, graphical SuperSource editing, output
profiles, source/route browsers, scene storage and the rest of the native effect
parameters will be added as dedicated pages. No empty placeholders are presented
as working operations. Source ID entry in the first DME page is temporary and
will become a visual source browser.

## Interaction contract

The interface uses fixed function menus on the left, contextual parameter pages
on the bottom and a central button matrix. There is no page/list scrolling.
The matrix capacity follows viewport dimensions; catalogues and dense screens
use explicit previous/next page buttons. Full screen uses the browser full-screen
API. Touch targets remain at least 40 px high even in a short viewport.
Numeric fields open an on-screen keypad;
the committed value stays visible while a separate draft is edited. Enter
submits; Cancel leaves the committed value unchanged. Invalid values remain
visible and the next digit replaces them. Backend acknowledgement and subsequent
state determine what the surface displays; it does not invent optimistic tallies.

M/E selection is explicitly shared with the physical panel. An open numeric
edit is pinned to its original M/E; changing delegation while editing causes
rejection rather than applying the old draft to the new M/E. Preparation changes
do not launch a program take. Live key processing can affect program and is
therefore confirmed using current on-air state, with a second server-side check.
The first HTTP bridge only accepts an explicit preparation command allowlist.

The bridge keeps one panel connection and caches state. Browser polling is
500 ms and contains metadata, not video. Disconnection clears the cached state
and shows SERVER LOST. Failed/ambiguous commands are not automatically replayed;
a timed-out session is closed so an old ACK cannot satisfy a later request.
A panel ACK confirms handler acceptance, not a completed renderer operation.

## Reference interfaces

* [Sony XVS menu screen, pp.52–53](https://pro.sony/support/res/manuals/5013/ac0fcd23e79e1fe9ad1e3707c87879a1/50135021M.pdf):
  vertical function buttons, horizontal submenus, status/title and parameter areas.
* [Sony MVS-6530](https://pro.sony/en_CA/products/video-switchers/mvs-6530):
  touch menu preparation complements panel shortcuts.
* [Ross TouchDrive](https://help.rossvideo.com/switcher-panels/Topics/Operation/Panel/TD/Touchscreen-Mnemonics.html)
  and [key delegation](https://help.rossvideo.com/switcher-panels/Tasks/Operation/Keying/Keying_TD.html):
  visible delegation and contextual parameter pages.
* [Grass Valley Karrera user manual](https://wwwapps.grassvalley.com/docs/Manuals/switchers/karrera/071-8876-06_Krr_K-Frame_User_v9.0.pdf):
  function navigation and grouped keyer/mask/matte controls.

These references inform interaction patterns. No manufacturer graphics or
proprietary UI assets are copied.

## Fixed-grid extended operation (0.31.0)

The three surfaces have distinct roles: Qt prepares inputs/media/layouts; the
physical panel operates the live show quickly; the touch surface provides
extended parameter operation. A future Basic Operation mode will expose only
preview/program selection, AUTO TRANS, transition type, key/DSK visibility and
FTB. It is not enabled by this layout change.

Eight equal viewport rows define the layout. Status and footer each consume one;
the rail uses six keys (KEY1–4, TRANS, MISC), and the matrix always retains five
fixed rows even when empty. The breadcrumb belongs in the status bar. Selected
navigation uses deeper blue-gray, parameter functions orange, and changes to an
on-air key purple. FULL SCREEN retains its label in both states.

Browser M/E delegation is local. A red outline identifies the physical panel's
M/E. Key processing carries targetMe, commits to that bank after AMCP success,
and never temporarily changes the panel/MV delegation. Existing transition
preparation values remain shared across M/Es and are labelled accordingly;
independent per-M/E transition preparation is follow-up work. DSKs remain global.
The new surface requires the touchIndependentMe capability (kavtor 0.31.0) so
older servers cannot silently apply an edit to the wrong key.
