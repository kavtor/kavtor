# WebGL DME experiment

`share/experimental/dme-cube.html` is an isolated renderer experiment, not a
production transition or a PGM/PST capture implementation. Existing live
transitions and panel mappings are unchanged.

## Run

Serve the repository with `python3 -m http.server 8099 --bind 127.0.0.1` and open
`http://127.0.0.1:8099/share/experimental/dme-cube.html`.
The two animated test textures continue updating throughout rotation, including
when progress is held. The slider allows reversal; Auto runs a 1500 ms pass.
Exact endpoints display only the outgoing or incoming texture, respectively.

Optional `from` and `to` URL query parameters accept browser-playable videos.
Cross-origin servers must permit CORS, and codecs must be supported by the actual
CEF build. A URL is not access to an existing CasparCG layer. Use two independent
live feeds to investigate latency before proposing any bridge for production.
No audio is played or mixed by this template.

On an isolated CasparCG test channel, load this page as an HTML producer and
control it using `CALL <channel>-<layer> "setProgress(0.5)"`.
Use `controls=0` to hide the test controls. This page intentionally remains outside
the installed runtime templates while video routing is unresolved.

## Integration boundary

The inspected CasparCG 2.5.1 HTML module enables WebGL with GPU mode. Its CEF
JavaScript context initializes `window.casparcg` as an empty object; no layer-video
texture API was found there. `OnPaint`/`OnAcceleratedPaint` export browser output
back to CasparCG, rather than giving source layers to WebGL.

CEF rendering capability therefore proves the geometry path, not access to PGM
and PST. Reopening input URLs inside CEF is insufficient: it loses the established
producer timeline, M/E composition, keyers and source timing. An external video
bridge needs latency and synchronization measurements. A native bridge, if needed,
must transport both composed signals and synchronize their lifetime and frames.
Do not switch this experiment into the program path until that is validated.

Validate the actual CasparCG CEF backend, endpoints and reversal, running texture
updates at held progress, context loss, source failures, and input/output frame
latency before connecting AUTO or T-bar. Browser validation alone does not prove
CasparCG performance or live continuity.

## Validation so far

The in-app browser compiled both shaders and displayed the two rotating faces at
50% progress, as well as full-frame outgoing and incoming endpoints. Animated
textures updated while progress was held. JavaScript syntax also passed Node's
syntax check. A temporary CasparCG instance then rendered the cube at 720p50 with
`<html><enable-gpu>true</enable-gpu></html>`, a separate AMCP port and cache,
and a local empty artnet consumer keeping the output clock running. `CALL`
changed progress; `PRINT` snapshots confirmed the two faces at 50% and the
incoming full-frame endpoint at 100%. The temporary server was stopped afterward.
The live server configuration was not changed. Its initial test failed with
`WebGL is unavailable`; its configuration omits `html.enable-gpu`, which defaults
to false in the inspected source.

This is functional validation, not a frame-rate or performance measurement.
External videos, frame latency and physical T-bar control remain unvalidated.


## Source-layer investigation

Current findings from the installed 2.5.1 source:

- `modules/html/html.cpp`: CEF GPU mode is optional. JavaScript receives an empty
  `casparcg` object; there is no registered layer-texture accessor here.
- `modules/html/producer/html_producer.cpp`: `CALL` schedules JavaScript execution;
  its AMCP acknowledgement is not a render-ready or frame-displayed acknowledgement.
- On Linux, the HTML output uses `OnPaint` and a CPU buffer. The accelerated shared
  texture output implementation is guarded by `WIN32`. Enabling GPU rendering
  does not by itself eliminate output readback on Linux.
- A persistent consumer is needed for continuous output/frame scheduling. A
  one-shot `PRINT` on an otherwise idle channel can capture an older HTML frame.

Investigate an existing composed-channel video bridge before designing a native
extension. A suitable bridge must preserve the established input timeline and
M/E composition, transport two independently timed signals, and expose a frame
identity or readiness signal so the output can enter/leave the effect without
replaying stale frames. Avoid claiming GPU texture sharing from CEF output APIs:
they transport browser output, not Caspar layers into browser input textures.
