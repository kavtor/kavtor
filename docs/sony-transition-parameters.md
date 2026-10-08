# Sony transition preparation parameters

Source: local `icons/sonywipes/50135021M.pdf`, printed pp.142–153 and 156–168.
The engine remains responsible for video geometry and frame timing; kavtor
stores per-effect preparation and the panel edits it without taking program.

## Wipe families

MODFY offers polygon corner count (49), rounded corner radius (300–304),
and square tile size (matrix wipes), already implemented. Sony separates
horizontal and vertical tile counts; this project intentionally keeps cells
square, using one side percentage. Karaoke 220–223 need Start, Rows and
Phase on separate soft keys. Random 273 needs tile width/height and
Volatility; diamond dust 274 needs particle size and Flash Rate. The supplied
manual does not define the distinct distributions of 270–272.

POS ON edits the origin, ROTATION the angle, ASPCT a continuous proportion,
MULTI repetition, BORD the edge width/color and SOFT its softness. Additional
Sony modifiers include edge matte mixtures, pattern mixing and dust mixing;
these are separate preparation layers, not extra Sony pattern numbers.

## DME families

Printed pp.161–162: Page Turn/Roll expose Radius (0% sharp page-turn fold, 100% normal curl,
200% double radius), Magnitude and Start Angle. MODFY should show these only
for applicable surfaces. Sony’s angular values are pattern-dependent and must not be interpreted directly as degrees. Background is an independent utility signal or matte.
Backside brightness and cast shadows remain separate project requirements.

Printed pp.161–162: Squeeze 1032/1033 and Crop Slide 2661/2662 have Holding Level
and Tolerance: entry and width of the dead band. These need explicit prepared
parameters and a reproducible T-bar progress mapping, not a timer-only pause.

Printed pp.163–165: positioner supports absolute H/V, relative H/V, center
and corner presets. Size is 100% at neutral. Crop has Top/Left/Right/Bottom
and grouped H/V/All editing. Crop release modes are CUT, LAST 5% and LINEAR;
completion timing is also selectable. Keep crop geometry and time law separate.

Edge can be BORDER or SOFT BORDER. The latter exposes Width and Inner Soft;
edge color is luminance/saturation/hue. Our numeric/color dialogs can store an
RGB equivalent, keeping color outside the texture geometry. Brick presets
have independently scaled faces, height and image centers; they need distinct
face assignments before their controls are advertised.

## Panel interaction contract

Use F1 onward, with UP/DOWN for parameter pages and EXIT to leave preparation.
Encoders update prepared values directly. Only pressing a field's F key lends
the keypad to that field; its draft is committed by ENTER, cancelled by leaving
the function, and indicated by blinking ENTER. F keys toggle their edit focus.
Non-neutral modifiers remain LOW. Double click restores their actual neutral
values without taking LCD focus. Parameter changes affect private transition
preview live and are captured at the start of a program take.

Show only parameters consumed by the current native effect. Existing Sony
knobs whose renderer is pending must not become inert controls in the panel.

## OBS reference for the next phase

`plugins/obs-transitions/transition-stinger.c` defines horizontal, vertical,
separate-file and mask matte layouts, inversion, frame/time cut points, optional
media preloading, muted matte playback and separate audio fade choices. Adopt
these concepts while keeping Caspar's frame clock authoritative.
Source: https://github.com/obsproject/obs-studio/blob/master/plugins/obs-transitions/transition-stinger.c

## First native Sony DME batch (0.10.0)

Ready for operator validation: 1001–1008 Slide, 1011–1013 Split,
1021–1031 Squeeze, 1041–1044 edge-hinged Door, 1384–1385 interleaved
Split Slide, 2601–2608 two-channel Slide, 2621–2628 two-channel Squeeze.
These use live image transforms/projections and do not borrow wipe masks.
Linear progress and 16 strips are project defaults, not measured Sony curves.
MODFY assigns a separate background for each Sony ID (source, matte or black).
Prepared backgrounds are captured for program takes; rehearsal may update live.

`sony-dme-inventory.json` covers all 269 predefined DME/resizer identifiers.
72 have captured native implementations; 197 remain explicitly pending with
reasons. Eight Frame I/O reference pictograms are absent from the local assets.
Generic Cube/Zoom/Page Turn/Roll remain available independently of Sony IDs.

## Centre doors and flip/tumble (casparMIX 0.12)

DIRECT 1045–1048 selects four incoming centre-hinged doors. DIRECT 1101/1102
selects vertical/horizontal half-turns that replace A with B over the prepared
background; 1103/1104 adds scale change and 1121/1122 reverses rotation sense
with the same scale change. All accept AUTO, T-bar, REV and TRANSITION PREVIEW.
MODFY assigns the independently stored background for each preset.

Interpretation defaults: one half-turn for flips, a quarter-turn for doors, a
centre pivot, linear angular progress, perspective camera 3.5 frame heights
away, and 65% scale at the midpoint of scaled flips. These are explicit project
choices inferred from the pictograms, not measured Sony motion curves. The
new parameters are not exposed as editable controls until the engine accepts
them. Zero-width edge-on faces disappear visually without suppressing their
audio contribution; both source audio feeds retain normal crossfade weights.

## Planar 2D interpretations (casparMIX 0.14)

DIRECT 1051–1058 uses incoming B with growing scale and a quarter-turn. The
operator specified corner pivots LL, UL, UR, LR for both groups: 1051–1054 turns
clockwise, 1055–1058 counterclockwise. In 1061–1064 the side-centre of B initially
aligns to a corner of A: UL/left/CCW, UR/right/CW, LR/right/CCW, LL/left/CW.
The hinge moves from that corner to B's final full-frame side-centre as B grows.
1068 starts at lower centre and completes one full turn; the operator confirmed
this motion, and it is unchanged. These corrections require casparMIX 0.14.1.

Growth and angle retain linear T-bar progress. Physical raster coordinates
preserve texture proportions. AUTO and REV use the same geometry. Exact temporal
curves remain project defaults; corrected pivots await operator validation.

## Mirror entries

Sony 1355–1358 require casparMIX 0.16.0 and are available through DME DIRECT.
The normal incoming image grows from UL, UR, LR or LL while adjacent tiles
reflect its texture horizontally, vertically and on both axes. At completion
only the normal destination remains. Geometry and audio capture tests passed;
operator review of the pictogram interpretation is still required.

## Frame In 1201

DME DIRECT 1201 requires casparMIX 0.17.0. Incoming B scales from the screen
centre over stationary A with a linear size curve. Texture, endpoints, rewind,
reverse and program/MV identity pass native captures. Operator review remains
pending; Sony multi-stage/key preparation and dead bands are not represented.
