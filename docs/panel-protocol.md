# kavtor panel protocol

TCP, one JSON object per line (LF or CRLF). Default bind: all interfaces, port `9100`.

## Commands (client → kavtor)

```json
{"cmd":"pvw","source":0}
{"cmd":"pgm","source":2}
{"cmd":"cut"}
{"cmd":"ftb"}
{"cmd":"ftb","frames":25}
{"cmd":"mix"}
{"cmd":"auto"}
{"cmd":"trans_preview","on":true}
{"cmd":"wipe"}
{"cmd":"wipe","smpte":5,"dir":"fwd","edge":"soft","amount":12,"multi":4,"border":8,"aspectW":16,"aspectH":9,"posX":500,"posY":500}
{"cmd":"wipe_style","multi":1,"border":0,"aspectW":1,"aspectH":1,"posX":500,"posY":500,"cursor":true,"save":false}
{"cmd":"wipe_presets","presets":[23,5,21,24,18,9,6,1,3,17]}
{"cmd":"wipe","pattern":"wipe_horizontal","dir":"rev"}
{"cmd":"stinger","slot":0,"reverse":false}
{"cmd":"stingers","slots":[{"media":"STING","reverse":"","cutFrames":12,"lengthFrames":50}]}
{"cmd":"wipe_pattern","pattern":"wipe_horizontal"}
{"cmd":"catalog"}
{"cmd":"wipe_dir","mode":"fwd"}
{"cmd":"wipe_dir","mode":"rev"}
{"cmd":"wipe_dir","mode":"pingpong"}
{"cmd":"wipe_edge","mode":"soft","amount":12}
{"cmd":"wipe_edge","mode":"border","amount":8,"color":"#ffcc00"}
{"cmd":"dsk","on":true}
{"cmd":"dsk","slot":1,"mix":true,"frames":25}
{"cmd":"dsk_source","slot":0,"source":7}
{"cmd":"key_on","slot":0}
{"cmd":"key_source","slot":0,"source":1}
{"cmd":"next","target":"background"}
{"cmd":"next","target":"key","slot":0}
{"cmd":"next","reset":true}
{"cmd":"me","slot":0}
{"cmd":"dsk_preview","slot":0,"on":true}
{"cmd":"dsk_preview","slot":1,"on":true}
{"cmd":"state"}
{"cmd":"rate","frames":25}
{"cmd":"layout","programLeft":true,"nameEdge":"bottom","nameAlign":"center","clockEdge":"top","clockAlign":"center","safePreview":true,"safeProgram":false,"meters":false}
```

`source` is the logical source id (`0`–`23`). Source `11` is the unshifted re-entry of the next M/E and is not a configurable input. `me.slot` is `0`–`3` (M/E 1–4). The desk follows that M/E's buses, keyers and next transition. M/E 1 is the default and the only one that feeds the line; DSK stays on the air channel after it. The last unshifted crosspoint of M/E 1, 2 and 3 routes the next M/E's whole mix. On M/E 4 that crosspoint does nothing. `pgm` is a hot punch (no swap, and it does not flip keyers). `cut` / `mix` / `wipe` follow NEXT TRANSITION: `background` swaps PRG and PRV, and each armed key flips. With the background out, an empty preview is allowed. While `transitioning` is true, further takes are ignored. `take` is `cut`, `mix`, or `wipe` for the active transition (empty when idle). The WIPE LED follows `wipeLit` and the MIX/AUTO LED follows `mixLit` (on until that take ends, then off and armed). Wipe patterns are families (`wipe_horizontal`, `smil_clockWipe_clockwiseTwelve`, `smil_boxWipe_topLeft`, …); opposite directions are the FWD/REV switch, not extra combo items. `wipe_dir.mode` is `fwd`, `rev`, or `pingpong`. `wipe_edge.mode` is `hard`, `soft`, or `border`. Omit `on` on DSK commands to toggle. `rate.frames` sets the AUTO duration used by mix and wipe, from 1 to 1000 frames. `autoFrames` in each `state` event is that duration. `dsk` with `mix` and `frames` dissolves that DSK slot (`0` or `1`); omit `on` to toggle. `key_on` does the same for keyers `0`–`3`. `next` toggles the background or one key and refuses to clear the last selected component across background and all four keys. `next` with `reset:true` selects only background without changing on-air keys. `state` includes `keys`, `dsks` and `next`. `ftb` fades the air output to black, covering the M/E picture, both DSKs and the audio of that output. `frames`, when present, is that duration (1..1000) and does not change the stored AUTO rate; without it the fade uses the AUTO rate. A second `ftb` fades back. `state.ftb` stays true while the line is black. `assigned` is one boolean per source, in order. A false entry is NO SOURCE: preview and program punches for that button are ignored, with no tally change. `layout` accepts any subset of its fields, saves them, and moves the multiview immediately. `programLeft` puts program on the left bus. `nameEdge` is `top` or `bottom` and `nameAlign` is `left`, `center`, or `right`; preview and program labels follow the source names. `clockEdge` and `clockAlign` place clip timers the same way. `safePreview` and `safeProgram` show or hide the safe-area marks. Defaults: program on the left, names bottom center, timers top center, safe area on preview only, audio meters off. `meters` shows stereo bars on each input, on preview and on program. The levels are Caspar OSC peaks (`/channel/N/mixer/audio/volume`), drawn by clipping a fixed bar. `wipe` with `smpte` selects that SMPTE code (`0` is the plain horizontal wipe) and `dir` `fwd` or `rev`; a reverse SMPTE number already points at the opposite side, and `dir` flips it again. `stinger` plays slot `0`–`9`. `stingers` replaces the ten slots and saves them. `catalog` returns the wipe list. `state` includes `stingers`. The current layout is also in every `state` event.

## Events

```json
{"event":"state","connected":true,"autoFrames":25,"preview":0,"program":1,"transitioning":false,"take":"","wipeLit":false,"mixLit":false,"dsk":false,"dskPreview":true,"wipePattern":"wipe_horizontal","wipeDir":"fwd","wipeSense":"fwd","wipeEdge":"hard","wipeEdgeAmount":8,"wipeBorderColor":"#ffffff","assigned":[true,true,true,true,true,true,false,false],"deck":-1,"cued":-1,"layout":{"programLeft":true,"nameEdge":"bottom","nameAlign":"center","clockEdge":"top","clockAlign":"center","safePreview":true,"safeProgram":false,"meters":false}}
{"event":"tally","preview":0,"program":1,"wipeLit":false,"mixLit":false}
{"event":"ack","cmd":"cut"}
{"event":"error","message":"No preview source selected"}
```

## Example

```bash
printf '%s\n' '{"cmd":"pvw","source":1}' '{"cmd":"cut"}' | nc 127.0.0.1 9100
```


## Error and persistence semantics

An `ack` reports that the panel handler accepted a request, not that CasparCG
completed a video operation. Observe subsequent `state`/`tally` events and
`error` events; some busy engine operations are still ignored by their handlers.
WIPE parameters are validated before live configuration changes; malformed
compound WIPE requests leave the active parameters unchanged. Persistence
failures produce an `error` instead of an `ack`: the message explicitly states
that the in-memory change could not be saved. Configuration file replacement is
atomic. AMCP response timeout closes that session and discards queued commands;
an ambiguous on-air command is not replayed.


## Render preparation status

`state.ready` contains one boolean per configured source ID (`0`–`23`), separate
from `assigned`. A source is ready when its current producer and FILL commands
have both received successful AMCP replies. `state.outputsReady` is true when
accepted route and NDI consumer configuration matches the requested outputs.
These are preparation acknowledgements, not live signal-presence checks.
Failed preparation reports an `error`; retry ARM after correcting its cause.
Accepted producer/route steps survive an explicit retry. A failed take cancels
unsent commands in its batch without applying a guessed rollback. Some earlier
commands may already have executed: retained software bus state is then the
last confirmed logical state, not proof of the current render output.

## Keyer topology and all-bank state

`state.capabilities` reports `meCount`, `keyersPerMe`, `dskCount` and `keyMode`
(currently 4, 4, 2 and `linear-alpha`). `state.mes` contains each bank's
`program`, `preview`, `nextBackground` and `keys`; each key has `source`, `on`
and `next`. Legacy top-level bus/key fields still describe the delegated M/E.
Each `dsks` entry now includes an independent `preview` flag.

Key/DSK slots and source IDs must be integral and within range. Source assignment
requires an assigned, enabled producer. Explicit `on`, `mix` and `reset` fields
must be booleans. DSK mixes require `frames` in 1..1000; NEXT requires a known
`target` unless `reset:true`. Busy key/DSK/NEXT requests receive an error.
On-air source assignments are persisted after the accepted AMCP batch; a save
failure is therefore reported asynchronously after handler acknowledgement.
See [keyer operation and current limitations](keyers.md).

## TRANS PREVIEW

`trans_preview` requires a boolean `on` and selects rehearsal mode for the
currently delegated M/E. It cannot change during a pending operation or take.
CUT, MIX and WIPE then run only on private preview layers, following NEXT
TRANSITION, and return to the normal preview after completion. Program buses,
on-air keys, background/key selection and next ping-pong WIPE direction are
preserved. A stinger command in this mode is rejected until isolated stinger
rehearsal is implemented.

`state.transitionPreview` and `tally.transitionPreview` report the mode.
`state.previewTransitioning` distinguishes a rehearsal from an on-air take;
`transitioning` remains the general take/busy indication for either. Clients
must not mark preview sources as on air during a rehearsal. faderOS discovers
support from the presence of the mode field; double-click TRANSITION TYPE to
arm rehearsal and click once to return to normal operation.

Rehearsal uses preview layers 101, 111–115 and 120–121. These are reserved and
must not be used by external overlays. Timing begins after accepted AMCP replies.
Automated tests check command destinations and logical state; image appearance
and layer routes still need real CasparCG validation.


## Multiview banks and AUX

`{"cmd":"mv_bank","bank":0}` pages the multiview grid (0 or 1) without
changing program/preview. faderOS sends SHIFT state and resends on reconnect.
`assigned` has 24 booleans for the delegated M/E, including feedback guards.
Source IDs 11 and 23 refer to its next M/E reentry.

`{"cmd":"aux","role":1,"source":1003}` routes AUX1 to M/E 4 program.
Roles range 1–4 and must exist on an enabled destination. Sources are normal
inputs 0–23 (excluding 11/23), 1000–1003 M/E programs, 1100–1103 M/E previews,
1200 multiview and 1201 final program. `state.aux` has four source IDs, with -1
for an unavailable role. Handler acceptance precedes renderer confirmation;
`outputsReady` confirms current consumer and route preparation.

Layout adds `safePreviewAspect`, `safeProgramAspect` and `safePreset` (`ebu-r95`
or `legacy`). Ratios accept 16:9, 4:3, 9:16, 14:9, 1:1 and 4:5.
Invalid new guide fields reject the request before mutating layout fields.

## Manual transitions

M/E delegation freezes an unfinished take in its original M/E. faderOS requires
an endpoint before driving another M/E. On return, crossing the saved physical
position picks up the frozen take; reaching the origin endpoint first cancels at cut, while reaching the
opposite endpoint completes at cut. The two T-bar direction lamps blink while pickup is pending.


Send `{"cmd":"manual","type":"mix","position":2048}` with position 0..4095.
Position 0 cancels back to the outgoing picture; 4095 completes the take and
swaps PGM/PVW only after CasparCG accepts the final command batch. Intermediate
positions may move in either direction or remain held indefinitely. Subsequent
strokes are normalized by faderOS, regardless of the physical lever direction.

WIPE adds `"type":"wipe","smpte":23,"dir":"fwd","amount":0` (softness 0..40).
MIX includes armed upstream keys. Manual WIPE currently requires background-only
NEXT TRANSITION; stingers and push/slide are not manually driven yet. TRANS
PREVIEW uses private preview layers without committing buses or key states.

State messages include boolean `manual` and integer `position`. faderOS uses
`manual` to suppress AUTO TRANS tally. Updates are coalesced, with at most one
AMCP batch in flight. Leaving an endpoint does not launch another take until
completion has been confirmed. Disconnecting the owning panel cancels the currently delegated
unfinished stroke; a stroke already latched at completion finishes normally.

Manual ownership lasts while any M/E has an active or frozen manual take. Once
all takes finish or cancel, another connected panel may acquire the lever.
Returning to the current M/E cancels a queued delegation that has not yet been
applied; the latest delegation request wins.

## Key processing

```json
{"cmd":"key_processing","target":"key","slot":0,"settings":{"mode":"chroma","hue":120,"softness":0.05}}
{"cmd":"key_processing","target":"dsk","slot":1,"settings":{"mask":true,"left":0.1,"top":0.1,"right":0.9,"bottom":0.9}}
```

`target` is `key` (delegated M/E, slots 0–3) or `dsk` (global slots 0–1).
`settings` is a nonempty atomic patch. Modes are `linear` and `chroma`; unsupported
fields/types/ranges reject the entire patch. Hue is 0–360 degrees, spill 0–180;
hue width, saturation, brightness, softness, spillSaturation and mask edges are
0–1. Mask is boolean, left < right and top < bottom even when disabled.
The command is rejected while the mixer is busy. ACK acknowledges admission;
subsequent state publishes settings only after renderer confirmation.
`processing` appears in current keys, every M/E's keys and DSKs; capabilities
adds `keyModes` and `keyMask`, retaining legacy `keyMode` compatibility.

## Native transition modes and Sony codes

```json
{"cmd":"mix","mode":"vfade"}
{"cmd":"auto","mode":"fadecut"}
{"cmd":"dme","effect":"push","direction":"left"}
{"cmd":"wipe","sony":23,"dir":"fwd"}
```

MIX modes are `mix`, `vfade`, `fadecut`, `cutfade`. Alternate modes and DME
require background-only NEXT selection. DME supports `push` and `slide`, with
`left`, `right`, `top`, `bottom` directions. Manual MIX accepts the same `mode`;
manual WIPE accepts `sony` instead of `smpte`. Both numeric namespaces together
are rejected. Capabilities expose `mixModes`, `dmeEffects` and `sonyWipes`;
catalogue rows retain SMPTE fields and add confirmed Sony aliases.

See [transition controls](transitions.md) for numbering and limitations.


## Native colour DIP

`{"cmd":"mix","mode":"dip"}` runs AUTO through the configured opaque DIP
colour. `{"cmd":"manual","type":"mix","mode":"dip","position":2048}`
uses the same effect with explicit progress. `capabilities.mixModes` includes
`dip` alongside legacy `vfade`; clients must test the capability. Existing mode
names remain valid. DIP requires background-only NEXT TRANSITION, supports
transition preview, and copies the configured colour when starting a take.
Colour preparation is persisted as `transitions.dipColor` (`#RRGGBB`, default
`#000000`) and is edited in the preparation application. Changing preparation
while a manual take is active does not recolour that take. This is an opaque
colour snapshot, not a live routable matte source. The engine owns layer 0 for
its temporary DIP backing; external clients must not use that layer on M/E
program channels. Audio follows the native/manual VFADE fade through silence.

## Spatial wipe modifiers

`wipe_style` accepts `shadow` (0..40) alongside `border`, `multi`, aspect and
position. `state.wipeShadow` reports the stored value. SOFT filters the monochrome
mask; the colour border and its black drop shadow use a separate transparent
producer. Both endpoints are exact and decoration disappears at either end.
Border width comes from spatial dilation minus erosion, using a square pixel
neighbourhood, not from the distance travelled between two transition samples.
All widths are resolution-independent units: one unit is four pixels at 1080
lines. AUTO and the manual T-bar use the same modifiers. These HTML decorations
add a CEF producer and CPU contour filtering; sustained frame rate still needs
checking on the intended production machine.

A web form can apply its entire configuration with one request, without taking
anything to air:

```json
{"cmd":"wipe_settings","pattern":"smil_kavtorWipe_checker","direction":"fwd","edge":"soft","amount":12,"color":"#ff8800","multi":1,"border":8,"shadow":5,"aspectW":4,"aspectH":3,"posX":500,"posY":500}
```

The request is fully validated before any configuration changes and saved once.
`catalog` also includes kavtor checkerboard, circle-mosaic and venetian-blind
patterns. These have no Sony/SMPTE number; use their catalogue IDs. Existing Sony
keypad assignments are unchanged pending the confirmed Sony pattern catalogue.

## Separate Sony WIPE/DME namespaces (0.13.0)

State capabilities publish `sonyWipes` and `sonyDmes` separately. WIPE uses
`{"cmd":"wipe","sony":22}` or `{"cmd":"manual","type":"wipe","sony":22,"position":2000}`.
DME uses `{"cmd":"dme","sony":1001}` or
`{"cmd":"manual","type":"dme","sony":2604,"position":2000}`.
A DME request may supply either `sony` or `effect`/`direction`, never both.
The optional `reverse` boolean reverses the entry direction for numbered DME.
Numbers must be integers and belong to the advertised family; capabilities are
implementation lists, not the reference inventory.

### Mosaic wipe block size

Capability `sonyMosaic` requires casparMIX 0.9.3+. `wipe_style.tileSize` is an
integer 2–50, the square side as a percentage of producer height, default 10.
State reports `wipeTileSize`; native commands carry `TILESIZE`. Unsupported
engines do not advertise mosaic codes. The existing application configuration
merge preserves panel tile-size changes when independent settings are saved.

## Sony-style NAM and SUPER MIX preparation

kavtor 0.17 advertises `nam` and `supermix` in `capabilities.mixModes` only when
casparMIX 0.11.0 or newer is connected. Both use the native rendered-input
operator for AUTO and manual transitions, with BKGD-only NEXT TRANSITION.
`capabilities.mixPreparation` exposes DIP colour preparation and
`capabilities.broadcastMixes` exposes SUPER MIX gains.

```json
{"cmd":"mix","mode":"nam"}
{"cmd":"manual","type":"mix","mode":"supermix","position":2048}
{"cmd":"mix_params","aGain":70,"bGain":80}
{"cmd":"dip_color","color":"#21ABCD"}
```

Gains are integer percentages 0–100. Settings are persisted and reported as
`superMixGainA`, `superMixGainB`, and `dipColor`. Running programme takes retain
their captured preparation; a running TRANSITION PREVIEW updates immediately.
DIP colour and SUPER MIX gains also appear in the application's preparation
workspace. Audio uses a standard crossfade for NAM and SUPER MIX.

## Native LUMA/alpha processing

With casparMIX 0.13, capabilities include `luma` in `keyModes`, plus booleans
`keyInversion` and `maskInversion`. `key_processing.settings` additionally accepts
`lumaLow`, `lumaHigh` (finite 0–1, low < high), `invert` and `maskInvert` (booleans).
They are persisted independently in each USK/DSK processing object and returned
in state. Unsupported engine processing is rejected rather than replaced with
LINEAR. The rectangular mask applies after key inversion; mask inversion only
affects an enabled MAIN MASK. See the keyers guide for the fill/alpha contract.

## GLOBAL/CUSTOM DME backgrounds

`capabilities.dmeBackgroundScopes` advertises background inheritance. State
`dmeBackgrounds` gives resolved backgrounds, including the `global` descriptor;
`dmeBackgroundScopes` stores per-effect CUSTOM booleans (absent = GLOBAL).
Existing numeric effect assignments still create explicit CUSTOM values.

```json
{"cmd":"dme_background","effect":"global","source":2}
{"cmd":"dme_background","effect":"sony_1101","custom":false}
{"cmd":"dme_background","effect":"sony_1101","custom":true}
{"cmd":"dme_background","effect":"sony_1101","custom":true,"copyGlobal":true}
```

Optional `me` must match the controlled M/E. Scope changes cannot also specify
a source. Live preview follows effective changes; programme takes remain
snapshots. Configurations retain raw custom descriptors while GLOBAL is active.
