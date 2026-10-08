# Source-aware MOVE transition

DME keypad **0** is reserved for MOVE. It is enabled when the connected engine reports casparMIX 0.5.0 or newer.
DME 9 selects CUBE; 1–8 retain PUSH/SLIDE directions. ZOOM is also available
through the JSON panel API (`dme`, effect `zoom`).

## Prepared planning layer

`MoveTransition` builds immutable scene snapshots and matches producers by their
canonical identity, not their display name or crosspoint label. A normal source
is one full-frame element. A SuperSource contributes its background, static
images and current runtime input bindings, using the same fill, clip and crop
geometry as its native composition. The identity resolver must canonicalize
M/E aliases to the same explicit M/E program identity, and must omit unbound boxes.
A stable background-only M/E may expose source identity for matching. An M/E
with keys or an unfinished transition stays a single flattened live producer;
its internal composition must not be reconstructed or changed by another take.

Stable box IDs disambiguate repeated producers across related layouts. Otherwise,
only a unique remaining occurrence on each side moves. Ambiguous duplicates and
unmatched elements crossfade in place. Matched elements interpolate position,
size, clip, crop and audio contribution. Progress is absolute and reversible;
the planner never edits live layouts or runtime bindings.

Automatic takes, transition preview and manual T-bar now use the native producer.
Manual commit/cancellation and M/E pickup reuse the existing take lifecycle.
NEXT TRANSITION currently requires background only; existing keys/DSKs stay above
the moving background without being altered.

## Execution and remaining extensions

* The take snapshots the selected M/E's complete outgoing/incoming scenes and runtime
  bindings before starting the take, including ordinary-source geometry.
* A private transition producer routes the matching live producers
  into it using the shared frame epoch. Do not animate the original SuperSource
  channels: they may be on air in other M/Es.
* Keys and DSKs remain outside the background movement. Key-only or combined
  NEXT TRANSITION for MOVE is a later extension.
* A single DMENATIVE producer samples every track at the same frame-local
  progress; geometry no longer travels as per-layer AMCP updates.
* Stacking order interpolates as a depth rank and uses stable sorting. Order
  changes occur at the crossing, rather than snapping on the final frame.
  Explicit box IDs resolve repeated producers; ambiguous occurrences fade.
* Prepare destination readiness before take, then hand off to its normal rendered
  output in the same frame at completion; cancellation restores the origin.
* Engine VERSION gates the new effects; an older patch rejects native DME before
  any take commands are sent. No substitution is performed.

No new dependency or casparMIX patch is needed for the planning module itself.

## Page DME and per-effect background presets

kavtor 0.12.1 adds `page_curl` and `page_roll` when the connected engine reports
casparMIX 0.6.0. Background presets are stored by effect in project settings.
Colour generators are ordinary Matte sources; images, clips and M/E programmes
can also fill the background. An explicit M/E route retains its identity when
panel delegation changes. Direct and indirect feedback, including frozen manual
DME backgrounds, is rejected.

Preset changes never modify a running programme take. Rehearsal may replace its
background live, retaining clock/progress and waiting for source readiness. A
background has no audio. Its source is independent of the page backside material;
exact two-channel Sony face assignments remain a later extension.

Panel MODFY: F1 AUX arms source selection, F2 BLACK restores the default, F3 COLOR
arms selection restricted to configured Matte generators. Available AUX buttons
use LOW, selected uses LOW blink. Re-pressing the active F exits selection;
EXIT returns to the previous keypad function. Double-click MODFY resets that
prepared effect to BLACK without taking LCD focus.

Sony's DME Background effect distinguishes flat colour, mixed colour and external
video (availability depends on the processor). See [Background Settings, page 165](https://pro.sony/support/res/manuals/4177/055946870699011ecc548151c20c8011/41779060M.pdf?cmp=gwt-).
The catalogue/reference boundary is documented in [Sony inventory](sony-pattern-reference.md).
