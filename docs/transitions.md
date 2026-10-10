# Transition controls

## MIX

Panel MIX slots are 1 MIX, 2 VFADE, 3 FADECUT and 4 CUTFADE. VFADE fades
outgoing video/audio down before fading the incoming source up. FADECUT fades
out then cuts in; CUTFADE cuts out then fades in. The last three modes require
background-only NEXT TRANSITION. Normal MIX also supports selected keyers.
AUTO and manual position are supported. These are CasparCG native transition
names, not AlphaFade or LumaFade equivalents.

## DME

AUTO slots 1–4 are PUSH left, right, top and bottom; slots 5–8 are SLIDE in the
same order. REV swaps each direction with its opposite. These are local panel
slots, not Sony DME catalogue numbers. DME requires background-only selection;
manual DME control is not available yet.

## Wipe numbering

The protocol keeps `sony` and `smpte` separate. Supplying both is rejected.
Confirmed Sony shortcuts map as follows:

| Sony | SMPTE | Shape | Inherent reverse |
| --- | --- | --- | --- |
| 1 | 1 | Horizontal travel bar | No |
| 3 | 2 | Vertical travel bar | No |
| 5 | 3 | Top-left box | No |
| 6 | 4 | Top-right box | No |
| 9 | 41 | Top-left diagonal | No |
| 17 | 21 | Centre-opening vertical barn door | Yes |
| 18 | 22 | Centre-opening horizontal barn door | Yes |
| 21 | 101 | Rectangle iris | No |
| 23 | 102 | Diamond iris | No |
| 24 | 119 | Circle iris | No |

Unknown Sony codes are rejected. Legacy SMPTE requests retain their previous
meaning. New panel clients use the advertised `sonyWipes` list. Other wipe
patterns remain available through the existing catalogue namespace.

## Verification boundary

Regression tests use simulated CasparCG and panel peers. They verify commands,
validation and state changes, not rendered pixels or real audio continuity.
Validate direction, endpoints, centre-opening shortcuts, rehearsal and audio on
the physical panel and deployed renderer. See [planned work](roadmap.md) for DIP,
matte generators and the HTML-renderer review.

At a completed manual background take, the incoming producer and its transforms
are promoted from temporary layer 3 to background layer 1 with `SWAP ...
TRANSFORMS`. No fresh route is started during that handover. The old background
is hidden before promotion and removed afterwards. A rejected swap stops the
remaining cleanup and does not confirm a software bus swap; partial renderer
changes still require operator inspection.

Manual HTML masks start over a black key producer. A one-frame MIX hands that
producer over only when the HTML destination has a frame, instead of revealing
the incoming picture while CEF starts. AMCP PLAY/CALL acknowledgements alone do
not indicate browser frame readiness. Validate the initial stroke on the deployed
renderer; simulated tests check the guard and command order, not CEF timing.

## Expanded Sony DIRECT catalogue (0.13.0)

WIPE DIRECT adds Sony 2, 4, 7, 8, 10, 11, 12 and 22 with casparMIX 0.7.0.
Together with the original shortcuts, 18 masks are executable. Older engines
advertise only the original ten. The geometry registry stays separate from the
SMPTE registry; cross 22 has a Sony-specific pattern ID.

DME DIRECT accepts Sony 1001–1004 (Slide right, left, down, up) and 2601–2604
(Push in the same directions). They reuse the existing native movements, with
AUTO, T-bar and reversal. DIRECT in WIPE cannot select a DME and vice versa.
Unknown codes are rejected without altering the active selection. Page/Cube
primitives retain their current keypad slots; no unverified Sony numbers are
assigned to them. Other reference patterns remain unavailable.

## Operator-reviewed Sony groups (0.15.0 / casparMIX 0.9.0)

The native catalogue contains 41 masks: Standard 1–24; Enhanced 26, 27, 29, 49 and
300–304; Rotary 100–107. White denotes incoming B. In particular, 13 enters from
the left and 14 from the right. Standard box, cross and diamond geometry follows
the output raster at neutral ASPECT; circle 24 keeps equal displayed width and
height. Enhanced heart, sharp star, arrow and regular polygon retain their natural
pixel proportions. Rounded boxes inherit the corresponding screen-relative boxes.

MODFY opens polygon 49's vertex count (3–64), or rounded presets' radius (0–50%,
default 15%). Encoders adjust the live preparation; F1 transfers the value to
numeric entry and ENTER commits the draft. Reset restores the pattern default.
NORM/REV and live preview retain their existing semantics. Timing laws remain
subject to operator review against animated references; the pictograms specify
shape and direction, not acceleration curves. No other Sony identifiers become
executable merely because they occur in the reference inventory.

POS ON edits an origin with a spring-centred joystick: deflection moves it, and
release retains its position. Encoders 1/2 provide fine X/Y adjustment. Origin
settings are retained per pattern by the host and are visible in WIPE PREVIEW.
Only shapes with an origin, such as irises and clock sweeps, currently consume it.

DME takes route complete rendered pictures and reject empty/unavailable scenes
instead of falling through to a cut. Page Turn/Roll back faces sample the same
live video, mirrored by their surface orientation; the old neutral paper material
is removed. Existing independent, silent DME backgrounds remain supported.

## Dust Mix and asymmetric borders (0.26.0)

With casparMIX 0.19.0, `dustmix` is an additional MIX mode. faderOS assigns it to
MIX 8. Preparation stores ratio 0–100%, square particle side 1–100% of picture
height and flash steps 0–100. Default 100 / 2 / 0 uses a pure stable diamond-shaped reveal. Flash steps change the deterministic particle
sequence as progress advances; stopping/rewinding a manual take holds/retraces it.
This is a project interpretation of Sony's Dust Mix concept, not a measured
implementation of its random generator or clock. Standalone Sony 274 remains
reserved pending its separate review.

Dust parameters are captured for program takes. Rehearsal can update them live.
MODFY provides transactional keypad fields, immediate encoder preparation and
reset to the actual defaults. New features are advertised only with a capable
renderer; unsupported MIX 8 does not light up or silently fall back to dissolve.

Wipe border placement can be centered, inner (incoming B) or outer (outgoing A).
Inner and outer softness are independent; -1 follows general SOFT. This produces
a geometric colored trail, not temporal video feedback. Both contour and picture
mask use the same frame-local distance field. BORD F4 cycles placement; detailed
softness is available in Qt preparation and the touch surface. The profile is a
shared preparation default in this first version, while existing width/softness
local overrides retain their prior behavior.

From 0.26.1 default recall uses 100/2/0. Explicit saved ratios are preserved;
a lower ratio includes a uniform dissolve. This first Dust Mix variant shares
the alternate MIX restriction: background-only NEXT TRANSITION.
