# SuperSources

A SuperSource is a reusable composition layout, assigned to an ordinary input
slot. It has its own Caspar channel and is routed as one GPU-rendered picture,
with alpha and audio. The manager owns designs and bindings; the engine owns
composition and frame synchronization. No HTML compositor or decoder loopback
is used.

## Prepare a layout

1. Open **SuperSources**, choose **New layout**, and name the design.
2. Add **Input** boxes and optionally static **Image** elements.
3. Drag boxes or use Left/Top/Width/Height percentages. Resize using the selected
   box's lower-right handle. The frame boundary is distinct from the editor canvas.
4. Ctrl-click or use a marquee to select several boxes. **Align** can align edges
   or centres to the selection or whole frame, and match widths/heights.
   **Arrange** provides equal-gap distribution and an automatic grid.
5. **Move back** / **Move front** changes stacking order. Overlap is allowed.
   Duplicate, copy/cut/paste and Undo/Redo are available. Pasted boxes get new IDs,
   unused KEY mappings and muted audio to avoid accidental summation.
   **Keep box proportions** locks sizing; Shift during resize locks it temporarily.
   Drag snap uses an adjustable percentage grid. Numeric fields remain precise.
6. Choose each box's **Default input**, fit mode, zoom and image centre. Crop
   describes the visible region of the input texture; box geometry describes
   where the result appears. **Window selection** assigns each input box to a
   unique KEY button (1–12 or SHIFT + 1–12). Image elements have no input selector.
   Image elements have an editable engine-host path
   and width/height ratio, allowing remote assets too.
7. Choose an opaque colour or a transparent background. An empty transparent
   layout still produces a ready transparent frame.
8. In **Inputs**, choose type **SuperSource**, then **Choose** its layout. Name
   that input and enable it. Use a dedicated channel; M/E, output and other input
   channels cannot be reused.
9. **Save configuration**, then **Apply saved configuration** to prepare the
   design in the engine. Saving alone does not change air. Applying may change
   a composition already in use and requires the normal manager confirmation.

The canvas shows geometry and static images, not live video. Designs remain
in the draft until saved. Discard restores the saved design. Layout and box IDs
are stable across ordinary edits; duplication preserves box identities for future
source-aware transitions. Up to 24 layouts and 32 ordered boxes per layout are
supported in this first iteration.

Only the first new video box includes audio by default. Audio from checked boxes
is summed; avoid checking repeated occurrences of the same input unless that
summation is intended. Static images never contribute audio.

## Use as an input

A prepared SuperSource works with preview/program selection, CUT, MIX, manual
transitions, upstream/downstream keys, multiview and outputs. Whole compositions
use `RENDERED` routes so destination opacity or keys affect the flattened result,
not its internal layers. The existing shared frame clock preserves nested routing
synchronization.

Boxes can use ordinary inputs, other SuperSources or explicit M/E-program inputs.
The fixed cascade crosspoints 12/24 cannot be box inputs because their meaning
changes with delegation; use an explicitly configured M/E input for saved layouts.
At runtime, AUX 12 resolves the next M/E to an explicit program binding (1000–1003
for M/E 1–4), so it remains stable when panel delegation changes. Its rendered
output is flattened and uses the same feedback checks as configured M/E inputs.
Composition cycles and feedback through M/E program/preview or active/armed keys
are rejected. An M/E-dependent composition is disabled on an M/E into which it
would feed back. A failed preparation is never marked ready and needs an explicit
ARM/reconnect or changed configuration before retry.

## Runtime bindings

The panel API uses zero-based input indices. Bindings are session state and do
not overwrite the saved layout defaults. Update a box using its stable ID:

```json
{"cmd":"supersource_input","source":6,"box":"BOX_ID","input":1}
```

`input: -1` leaves the box unbound. Restore its saved default with:

```json
{"cmd":"supersource_input","source":6,"box":"BOX_ID","reset":true}
```

Read active composition bindings with:

```json
{"cmd":"supersource_state"}
```

The response lists layout/box identities, effective inputs and preparation state.
Changes emit `supersource_binding` events. ACK means the request was accepted;
preparation state separately confirms the submitted engine commands. Rebinding
updates affected layers rather than clearing the whole composition. Invalid IDs,
cycles or routes that would feed back into an active M/E are rejected.

## Deferred MOVE

The first iteration provides composition and ordinary transitions. Source-aware
MOVE (DME 0), matching shared inputs across layouts/ordinary sources/M/Es, is not
implemented yet. Stable source/box identity and separate geometry/bindings are
retained for that next step. Do not advertise morphing or animate boxes on air
through the preparation editor.

## Validation

Tests cover serialization, draft isolation, geometry, cycles, rendered bus routes,
runtime rebinding/reset, preparation failures and explicit retries. The isolated
capture tool verifies split-screen, PinP, zoom/pan, static-image alpha, selected
input audio and flattened re-entry against deterministic synthetic inputs:

```bash
KAVTOR_SUPERSOURCE_FIXTURE=/tmp/ss-fixture QT_QPA_PLATFORM=offscreen \
  build/test-management superSourceValidationAndGeometry
python3 tools/validate_supersources.py --binary /path/to/casparcg \
  --fixture /tmp/ss-fixture --output /tmp/ss-capture
```

The tool creates a separate temporary engine and never connects to the running
mixer. It needs FFmpeg command-line tools and a usable GPU session.


## Sony panel delegation

The two empty delegation buttons to the right of KEY1/KEY2/DSK are used as:

* **Left (104): SuperSource on the selected M/E's PREVIEW bus.**
* **Right (105): SuperSource on the selected M/E's PROGRAM bus.**

A delegation is available only if that bus contains a SuperSource. Press its
button, select the window on **KEY**, then punch the desired source on **AUX**.
SHIFT accesses the second source/window bank. The selected KEY crosspoint is
retained across bus swaps and resolved against the new design; an unmapped
crosspoint beeps and never falls back to another window. The delegation remains
on the explicitly chosen bus. A preview-to-program take never activates program
delegation automatically; only the right button does that. Re-pressing the active
delegation exits this mode. KEY1/KEY2/DSK, frame-memory and AUX-output delegation
also return the rows to their original function.

Inside a box, source replacement is always a cut/hot punch, including during a
running background transition. For other effects, route an explicit M/E input
into that box and perform the effect in the M/E. Requests from the panel include
bus, M/E, instance and crosspoint context; stale requests are rejected rather than
editing the source that has since moved to program. Existing protocol clients
can still target a specific source/box explicitly.

A SuperSource input is a shared composition wherever that same input is routed.
It does not create independent content merely by appearing in both preview and
program. Use separate input instances (optionally sharing one layout) for separate
runtime bindings. Design geometry is shared by instances of the same layout;
duplicate the layout when geometry must differ as well.

## Independent M/E instances

A source selects a layout template. Each M/E owns separate runtime window bindings
and a separate rendered composition; selecting the same source in another M/E
does not share its window assignments. Cameras and clips remain shared producers,
so this separation does not create extra decoders. Nested layouts inherit the
owner M/E. Per-M/E compositions must still pass feedback checks.

M/E 1 keeps each input's existing Caspar channel. M/E 2–4 use reserved channels
39–110. Updated example server configurations provide these channels; restart
Caspar with that channel allocation before using independent instances. The
new channels have no consumers and no GPU readback unless explicitly routed.

## Perspective preparation (0.15.0)

Each box stores four local corners in UL, UR, LR, LL order. Edit the percentage
coordinates or enable **Edit perspective corners on canvas** and drag the pink
handles. Reset perspective restores the rectangle. Coordinates must preserve a
finite convex quadrilateral; invalid edits keep the previous geometry. Copy/paste,
layout save/load and independent M/E instances carry the corners unchanged.

The native renderer combines corner pinning with the existing fill, crop, zoom
and clip. MOVE captures and interpolates perspective with each source track;
it requires casparMIX 0.9.0 for this pose data. Legacy layout files default to the
identity rectangle. This is projective corner control, not a 3D camera editor.

To extend an existing server configuration without replacing its paths, consumers
or custom multiview format, run:

```sh
python3 tools/extend_ss_channels.py /path/to/caspar.config --dry-run
python3 tools/extend_ss_channels.py /path/to/caspar.config
```

The tool retains existing channel settings, makes a backup and writes atomically.
New channels inherit the first source channel's mode and sync group. Restart
CasparCG afterward. The tool leaves an already sufficient configuration untouched.
