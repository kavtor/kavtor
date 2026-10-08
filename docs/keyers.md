# Keyers and downstream composition

## Initial topology

The current engine provides four M/Es, four upstream keys per M/E and two global
DSKs. Each M/E owns its program, preview, key assignments, on-air flags and NEXT
selection. Delegation changes the controlled bank; it does not reset the other
banks. M/E 1 feeds air. The two DSKs are composed after that M/E and covered by
FTB, rather than becoming upstream keys of the delegated M/E.

This is the initial fixed topology, not runtime-unlimited allocation. The panel
reports counts in `state.capabilities`; clients should discover these counts
instead of assuming the physical panel's number of keys. Additional banks need
engine/configuration storage and a corresponding Caspar channel allocation.
The supplied eighteen-channel template supports this initial topology.

## Upstream operation

Prepare an assigned, enabled source for each key in **M/Es & keyers**. Current
composition uses the producer's embedded LINEAR alpha. Opaque producers cover
the picture beneath them. LINEAR preserves embedded alpha. CHR uses CasparCG's native chroma extraction.
Each upstream key and DSK has independent hue, hue width, minimum saturation,
minimum brightness, softness and spill suppression settings. MAIN MASK clips
an independent rectangle (left/top/right/bottom, normalized 0–1). Defaults
retain LINEAR with masking disabled; enabling an untouched mask leaves the
central 80% in both axes. With casparMIX 0.13.0, LUMA extraction, KEY INV and independent MAIN MASK
inversion are implemented. Separate fill/key, pattern keys, additional masks,
borders and DVE key processing remain unavailable.

A direct key command cuts only that key on or off; it does not exchange the
background buses. NEXT TRANSITION selects the background and/or individual keys.
Preview represents the result of the next take: a selected key will change its
on-air state, while an unselected key will retain it. A key-only take leaves the
background buses alone. At least one of the background and all four keys must
remain selected. `next` with `reset:true` selects background only and leaves the
current on-air keys untouched.

Direct on/off and on-air source replacement commit their logical state after
all commands in their AMCP batch have succeeded. Source reassignment is rejected
while the engine is busy. An off-air assignment can be saved without connection;
preview preparation occurs when connected. Source assignment persistence follows
the engine commit, so an unconfirmed on-air replacement is not written as the
accepted source.

## Downstream operation

Both DSK slots support direct on/off, timed dissolve and independent preview.
Arming preview does not put the DSK on air. An on-air DSK is also shown on M/E 1
preview. If preview is armed, it stays visible there after its on-air fade-out.
DSK 2 follows the same rules as DSK 1; `dsk_preview.slot` selects the slot.

A mixed DSK reports `mixing` until its local duration expires, beginning after
successful command acknowledgement. Durations currently assume the engine's
fixed 50 fps render setup. DSK source changes during that DSK's fade are rejected.

## Panel state and confirmation

`state.keys` and `state.next` describe the delegated M/E. `state.mes` additionally
reports buses and all key states for every bank without changing delegation.
`state.dsks` contains each global DSK's source, on-air, preview and mixing flags.
Indices are zero based. An `ack` means handler acceptance, not render completion;
use subsequent state and error events for operation outcome.

The engine currently serializes pending video operations and M/E takes globally.
It does not yet provide concurrent takes on separate M/Es or DSK cuts during an
M/E take. This is a current scheduling limitation, not a final mixer design.

## Validation and remaining render work

Automated tests use a simulated AMCP peer and validate commands, acknowledgement
ordering, failures, bank state and panel framing. They do not validate alpha
pixels or the appearance of a transition in a real CasparCG renderer.

A partially accepted batch may have changed some render layers before failure;
software retains the last confirmed logical state without claiming rollback.
Full render-state reconciliation after disconnect remains pending. Preview key
preparation still uses legacy command/cache handling. Keys and DSKs now route the already prepared input layer rather than instantiate
another producer. Layer routing retains alpha and avoids media restarts.
These paths require real-renderer validation before production key processing
is considered complete.

## Transition preview

TRANS PREVIEW rehearses the selected background/key take on the delegated M/E's
preview. It leaves program, on-air keys and assignments intact, even if an
on-air key routes its producer from the underlying preview key layer. Private
layers cover the normal preview only for the rehearsal and are cleared at the
end. Rehearsing an entering or leaving key does not toggle that key on air.
The normal prepared preview returns after completion. The mode remains armed
until disabled and is independent per M/E. Stinger rehearsals are not available.

## Producer reuse and audio metering

Keys and DSKs use `route://INPUT_CHANNEL-1`, never the whole input channel,
so the alpha-bearing producer can be shared without flattening it over black.
The source must be prepared by ARM; accepted preparation still does not prove
live signal presence. File/HTML sources are not reopened on each key press.

Source meters follow the configured channel for each input. Preview metering
uses the delegated preview channel; program metering uses the effective air
output, matching the program multiview tile. Program includes the audio mix and
DSKs, and can differ from an input meter. Routing and asynchronous OSC reports
can produce a small timing offset; meters do not substitute source readings for
the actual output mix.

## Processing operation

Delegate KEY 1–4 or DSK 1–2 on the panel, then use LINEAR, LUMA or CHR to select the
mode, MAIN MASK to toggle its stored rectangle and KEY INV to invert key alpha. KEY BUS retains source
assignment. Unsupported type/modifier buttons reject the operation with a beep;
LUMA extracts the fill signal's luminance; it is not a synonym for embedded alpha. Detailed parameters are prepared using
the **Processing…** buttons in the M/Es & keyers workspace. Dialog changes are
workspace drafts until saved/applied, like other preparation changes.

Panel edits use `key_processing` (see the panel protocol). Settings are persisted
only after the whole AMCP batch is accepted. A failed batch may have partially
changed the renderer: the software reports failure rather than publishing an
unconfirmed configuration. Processing is applied to logical key layers, not
shared source producers, so changing one key does not change another input use.
Routes of an already processed preview key use neutral destination processing.
Manual rehearsal layers are reset before reuse.

Native chroma and clipping syntax are described in the [CasparCG AMCP protocol](https://github.com/CasparCG/help/wiki/AMCP-Protocol#mixer-chroma).

## Native LUMA and inversion (kavtor 0.19 / casparMIX 0.13)

LUMA uses Rec.709 coefficients on the unpremultiplied fill, with a linear
black-to-white threshold interval. Below the black threshold coverage is zero;
above the white threshold it is one. Thresholds must satisfy 0 ≤ black < white
≤ 1. Embedded source alpha still limits LUMA coverage. This is self-luma, not a
separate key source. LINEAR continues to preserve embedded alpha and CHR retains
its existing hue/spill processing.

KEY INV inverts the extracted key alpha without turning the picture into a
colour negative. With premultiplied alpha sources, fully transparent pixels
contain no recoverable fill; inversion there produces black fill. Separate
fill/key support will provide an independent opaque fill for that use case.
MAIN MASK applies an output-space rectangle after key extraction/inversion.
**Invert MAIN MASK** in the preparation dialog retains the outside of that
rectangle. It is independent of KEY INV and is inactive when MAIN MASK is off.

The processing dialog exposes LUMA thresholds and the two inversion toggles.
Settings use normal draft/save/apply, remain independent per USK/DSK and survive
reloads. Panel LUMA is LOW when available, HIGH when selected; KEY INV reflects
the confirmed modifier state. Old engines do not advertise these controls.
Processed preview-key routes reset destination ALPHAKEY processing, so alpha is
not extracted/inverted twice. Native captures verify extraction, both mask
senses, alpha/chroma inversion, source isolation and program/MV equality.
