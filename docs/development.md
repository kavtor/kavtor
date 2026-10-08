# Development and publication

kavtor is a Qt/C++17 control application. CasparCG renders video; the Qt process
owns switcher state, panel commands and configuration. English is the primary
language for code, UI and documentation. Project naming is provisional.

## Build

Requires CMake 3.16+, a C++17 compiler, Qt 6.3+ Core/Gui/Widgets/Network,
pkg-config and hidapi-hidraw. Qt Test is required only with BUILD_TESTING=ON.
On Debian/Ubuntu, install build-essential cmake pkg-config qt6-base-dev
libhidapi-dev. CMake reports missing development dependencies during configure.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build/release --output-on-failure
cmake --install build/release --prefix /tmp/kavtor-install
```

`--help` and `--version` work without a display. `--verbose` enables runtime
protocol tracing; warnings and errors are retained by default. With Doxygen
installed, build the `docs` target for generated HTML API documentation.

## Layout

- src/core: AMCP, panel protocol, OSC and switcher logic.
- src/config: persisted user configuration.
- src/ui: Qt engine management and prepared configuration widgets.
- src/video: experimental video helpers outside the current build.
- tests: Qt protocol/engine regression tests.
- resources and share: UI assets and Caspar HTML overlays.
- docs: protocol and contributor documentation.
- config/examples: sanitized deployable configuration templates.
- sources: third-party reference trees, excluded from source bundles.

## Publication

kavtor is published under GNU GPLv3. Use the standalone repository rather
than a private workspace history. The source bundle excludes local configuration,
builds, reference dependencies and private workflow data:

```sh
python3 tools/prepare_publication.py --output /tmp/kavtor-source.tar.gz
```

Review and test an extracted bundle before initializing the public Git repository.
Do not import the reference projects or local captures. Future changes follow
issue → branch → pull request → reviewed merge. CI templates assume the published
project directory is the GitHub repository root.

See the [dependency and asset review](third-party.md).

Local validation completed: clean source extraction, Release build, four CTest
tests (including both Qt suites), Doxygen and installation. Protocol peers are
local; this does not certify rendering on a production CasparCG installation.

## Runtime reliability and current boundaries

AMCP replies are ordered and do not carry a request identifier. A response
timeout leaves the effect of the in-flight command unknown. The client closes
that session, fails its queued commands and follows the normal reconnect policy;
it never retries the ambiguous command. The engine still re-arms configured
sources/outputs on reconnection. This is not reconciliation with an independently
modified CasparCG state and must be tested before use on an on-air system.

Normal take and FTB hold timers start after the associated command batch has
been acknowledged. DSK cleanup follows the same rule. This deliberately avoids
clearing a layer or swapping software buses before queued commands are accepted.
With a slow AMCP round trip, feedback may therefore remain busy longer than the
actual effect. ACKs are not frame-completion notifications.

The current engine assumes 50 fps throughout transitions, HTML wipe animation,
clip clocks and jog/shuttle. Both supplied Caspar configurations use 1080p50.
Other frame rates are not yet supported consistently. Four M/Es and eight inputs
are fixed in the engine; M/E 2–4 reserve channels 13–18. Configuration should
not assign input, output or multiview channels to those reserved channels.

Further work should address these boundaries before adding more effects:

- Reconcile complete mixer state after reconnection or partial take failure.
  Preparation caches now record accepted steps, but this does not reconstruct
  independently changed buses, keys or an already-started video transition.
- Bind stinger cut/end timing to accepted playback (ideally render timing).
  Stingers and HTML wipe `t0` currently start from the local scheduling clock.
- Replace the global active-M/E transition lock with independent M/E runtime
  state before supporting simultaneous takes on different banks.
- Add an explicit manual transition API for T-bar control; the current panel
  server exposes CUT/AUTO/WIPE, not continuous transition position.
- Separate coalesced multiview telemetry from time-critical control commands.
  Meter/clock updates and takes currently share the same FIFO AMCP connection.
- Validate channel topology and source IDs when loading configuration, and
  distinguish command receipt from execution acceptance in panel responses.

The Qt tests use simulated peers; passing them does not establish video-frame
accuracy, real producer availability, alpha behavior or audio continuity in
CasparCG. Validate these with the actual renderer and physical panel.

## Confirmed preparation and command batches

Producer PLAY, source FILL, output routing and NDI consumer changes are grouped
into identified AMCP batches. Successful replies update the corresponding cache
field. Mixer take/preview completion also uses batch identity rather than command
text, so an identical background reply cannot commit a pending bus operation. A failed command cancels the unsent remainder of that batch and surfaces
an error, while unrelated queued work is retained. There is no automatic loop
retrying a rejected batch. Use ARM after resolving the renderer error. If the
producer was accepted but FILL failed, ARM retries FILL without reloading the
clip. If an output route was accepted but NDI ADD failed, ARM preserves the route
and retries the missing consumer operations. Configuration that changes while
preparation is pending is applied after successful completion of the older batch.

Panel `state.ready` distinguishes each configured input from one whose producer
and source mixer setup have both been accepted. `state.outputsReady` compares
accepted route/consumer setup with the current output configuration. Neither
field certifies valid incoming signal, frame delivery or consumer health after
initialisation; continued render monitoring remains future work.

A rejected take stops its local timers and cancels unsent dependent commands.
It does not swap software buses or automatically CLEAR key layers as if the take
had succeeded. An earlier accepted command can already have changed the picture;
there is no safe inferred rollback. Check the renderer after a partial failure.
Full state reconciliation and an explicit recovery operation remain necessary.

## State queries and interrupted takes

CasparCG 2.5.1's [INFO implementation](https://github.com/CasparCG/server/blob/v2.5.1-stable/src/protocol/amcp/AMCPCommandsImpl.cpp)
returns `201 INFO OK` followed by a pretty-printed XML document. Its native XML
uses LF inside the AMCP payload; clients or intermediaries may normalise those
line breaks to CRLF. The AMCP reader supports both forms, including fragmented
delivery and empty lines inside the document. It releases the next queued
command only after XML completion. Invalid or truncated XML stays in flight
until the normal response timeout closes the session. Plain-text 201 replies
(such as VERSION) and 200 lists retain their existing framing.

Losing the control socket during a take now cancels local completion timers and
deferred bus/key swaps. The last confirmed selection remains visible; it is not
a claim about the actual renderer, which can keep running the transition without
kavtor. No automatic rollback or layer cleanup is issued by this cancellation.

This is preparation for read-only recovery, not complete recovery itself. The
current reconnect path still calls ARM and reinitialises producers, multiview and
DSK setup. Do not rely on reconnection to preserve an independently running
output. The next recovery step must inspect routes, key/DSK/FTB transforms and
active transitions, and explicitly report states it cannot reconcile before
allowing automatic restoration.

## Audio/video timing diagnostics

See [the synchronization investigation](av-sync-investigation.md) for the
optional NDI receiver probe, its limitations and the controlled content test
plan. Diagnostics do not change running mixer state.

See [planned mixer functionality](roadmap.md) and [transition controls](transitions.md).

## casparMIX 0.2.0 native engine path (kavtor 0.9.0)

kavtor requires CasparCG 2.5.1 with casparMIX 0.2.1 or later. It always uses
the ready-aware normal PLAY command for cuts and rendered routes for M/E reentry.
There is no older-engine capability switch or legacy compatibility path.

Background reentries from another M/E use whole-channel `RENDERED` routes, so the
engine flattens the composition on the GPU before destination keys or opacity act
on it. This avoids fading each upstream layer separately. Panel delegations,
next-transition selection and M/E topology remain mixer application concerns.

Confirmed native Sony patterns are 1, 3, 5, 6, 9, 17, 18, 21, 23 and 24. kavtor
sends WIPESONY with the original Sony number, duration and modifiers. Manual T-bar
positions become one CALL per accepted update; preview modifiers use that same
CALL. Native borders are drawn by default, with the same shader geometry and
softness as the video mask. Other patterns and transitions starting with a shadow
keep the HTML implementation. Native shadow changes during preview are pending.

The canonical engine source and installable patch series live in
`/path/to/casparMIX`, rather than the historical snippets under contrib.
The release introduces no new engine dependencies. Engine regressions capture
lossless frames and routed audio in a separate test server; Qt tests additionally
check native commands, manual commit/cancel and rendered M/E reentry commands.
Neither replaces testing moving sources and every modifier on the physical panel.

Native WIPE PREVIEW modifier changes are coalesced at 20 ms, including with a
stationary manual position. Automatic preview edits omit PROGRESS so they do not
change the engine's timed clock. Neither path reloads the video producer.
faderOS forwards SOFT edits during preview, and its ASPECT field uses continuous
width/height percentages for kavtor: 100% is neutral, 125% widens the shape and
75% narrows it, matching the application and web. ATEM retains its native 0–100
aspect control. Native iris geometry is corrected in casparMIX 0.2.1.

## Cadence and transport follow-up (0.22.0)

The operator's slow-motion observation was reproduced: approximately 39.6
sync epochs/second at nominal 50 and 0.792 media seconds per wall second.
casparMIX 0.15 removes idle-channel work, bounds transient recovery at 200 ms
and avoids per-frame static-graphics serialization. The real configured-load
check then measured 50.00 fps and 1.000x playback. This is not a lip-sync test
or a guarantee under sustained exhaustion. Inputs at other native FPS remain
resampled by the producer, with independent time/transport feedback.

Development runtimes must use an immutable copied executable, not a build
output that will be relinked while running. Live experiments were moved to
/tmp/casparmix-live-0.15.0-final after crashes while rebuilding the earlier
runtime path; that ambiguity must not be mistaken for proof of a code defect.
