# Planned media integrations

Research checked on 2026-10-02. These are planned integrations, not currently
available input/output types. Both integrations are deferred; track CasparCG OMT
upstream rather than implementing a bridge now. Keep this document as input for
future GitHub issues.

## Open Media Transport

The inspected local CasparCG 2.5.1 source tree has no OMT module. Upstream has
[draft pull request 1790](https://github.com/CasparCG/server/pull/1790), open and
unmerged when checked. Its proposed module implements both a producer and a
consumer, and `OMT LIST` for discovery. Proposed producer descriptors include
`[OMT] SourceName` and `omt://host:port`. These are proposal syntax, not commands
that can be assumed to work on the deployed server.

Prefer testing that module on a separate CasparCG build before implementing an
external bridge. kavtor should probe availability and distinguish an unavailable
module from an absent network source. Integrate discovered sources in input
preparation and OMT destinations in output management, with persistent routes and
explicit reconnect feedback. Test discovery, video/audio timing, pixel formats,
alpha, multiple senders, receiver reconnection and resource use.

The [OMT project](https://github.com/openmediatransport) documents C-style exports
through libomt for C/C++ applications and Linux support. If native integration
cannot be deployed, evaluate an optional supervised bridge to an existing renderer
input/output transport. That adds buffering, conversions and potentially another
transport dependency; measure latency and synchronization before selecting it.
Do not require the bridge for installations with working native support.

## VDO.Ninja

Offer a dedicated input-preparation workflow alongside the generic HTML URL:

1. Configure a room and any required credentials.
2. Join with the permissions needed for discovery and display connected guests.
3. Assign a selected guest stream to a switcher input.
4. Preserve that assignment during participant list changes; do not substitute
   another guest into an on-air input when the selected guest disconnects.
5. Show connection and missing-participant status, with explicit reassignment.

The official [director iframe API](https://docs.vdo.ninja/guides/iframe-api-documentation/iframe-api-for-directors)
documents guest-list requests and targeting by slot or stream identifier. Prefer
stream identifiers over changing room slots, while checking their lifetime during
reconnects. Room discovery requires a browser/API session and appropriate room
permissions; a room name alone does not imply access to every participant.
Evaluate an external browser session or a controlled wrapper before adding another
embedded browser dependency to the Qt management application. Validate the actual
WebRTC media path in the deployed CasparCG CEF runtime rather than assuming generic
HTML playback guarantees compatibility. Decode only assigned feeds where possible.

Keep credentials out of ordinary diagnostics. A wrapper must validate message
origin and source and restrict commands to the configured room/session. Store
room settings separately from public source labels and generated diagnostics.

### Return feeds

Treat a return as a prepared output destination with its own source selection:
M/E program/preview, an AUX route, a source or a dedicated composition. It must
not depend on which input is currently being configured in the application.

On Linux, evaluate `v4l2loopback` as an optional camera-output backend for a
separate VDO.Ninja publisher. This is an output, distinct from V4L2 capture input.
It requires an installed/configured kernel module and access permissions; do not
silently install or reconfigure it. Browser device selection, capture format,
resolution and frame rate need validation. V4L2 loopback does not provide the
audio return: configure audio routing separately through the planned PipeWire
and Ardour integration.

The official [return-feed guide](https://docs.vdo.ninja/guides/send-an-obs-return-feed-to-guests)
describes both director and dedicated-publisher workflows and mix-minus audio.
The agreed future audio architecture delegates return mixing to Ardour sends,
operated from the audio console controlling Ardour. PipeWire connects participant
audio, Ardour and the VDO.Ninja return publisher. Each Ninja participant is assigned
an input channel and a return bus. Automatically exclude that participant's own
input from their return bus by default (mix-minus), retaining other routed audio.
Offer an explicit option to include their own voice, for example when a performer
wants it in their headphones. Preserve participant/channel/bus identity when room
slots change or guests reconnect; do not silently change an operator's explicit
self-return choice.

kavtor should coordinate these assignments and show routing state rather than
implementing a second return mixer. The existing audio path remains in place
until this integration is implemented and validated. Do not advertise per-guest
mix-minus as currently available or equate a clean video feed with mix-minus.
Validate permission prompts, microphone/camera access, routing and reconnection,
and the bandwidth cost of delivering returns to several guests.
