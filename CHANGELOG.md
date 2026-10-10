# Changelog

## 0.31.0 — fixed-grid extended touch operation

- Use stable eight-row layout, six rail menus and five matrix rows; move breadcrumbs into status and add the existing symbol-only logo.
- Define navigation/function/on-air button colors and a persistent FULL SCREEN label.
- Decouple browser M/E from physical panel delegation; directly target and commit key processing to its bank. Shared transition fields remain explicit.
- Document Qt preparation, physical live operation and touch extended operation, with a future basic control mode.

## 0.30.0 — complete provisional native WIPE catalogue

- Discover all 116 Sony WIPE patterns with casparMIX 0.23.0; enable DIRECT, AUTO, manual T-bar and preparation. Older servers retain their prior lists.
- Document every newly interpreted path and leave operator approval separate. Unknown codes remain invalid; DME inventory is unchanged.
- No new dependencies or firmware update.

## 0.29.0 — native random wipe discovery

- Discover Sony 273/274 only with casparMIX 0.22.0 or later, for DIRECT, AUTO, manual T-bar and preparation. Older engines keep these codes reserved.
- Advertise 89 native wipes and 27 pending identifiers; Dust Mix remains independent. No new dependencies or firmware update.
- Native capture validation is distinct from pending operator morphology approval.

## 0.8.8 — configuration and tally consistency

- Fix missing audio meters on the M/E 3 and 4 side tiles by assigning each tile its own DOM ID.
- Colour source title backgrounds to match preview/program borders, including red preview tally during a transition.
- Preserve the complete previous configuration when a document contains invalid output or stinger settings, including failures late in parsing.
- Notify observers once after a successful configuration import; never expose partially loaded state.

## 0.8.7 — local Linux capture inputs

- Add a dedicated V4L2 input type for Linux cameras and capture devices attached to the CasparCG host.
- Accept numbered video devices and stable udev links, persist assignments and validate descriptors in preparation.
- Use the native FFmpeg device producer with video-only letterboxing; device format selection and separate audio capture remain future work.

## 0.8.6 — native colour DIP

- Add a background-only DIP through a configurable opaque colour using native VFADE and a backing layer. No HTML/video bridge is needed.
- Support AUTO, manual progress/reversal, cancellation and transition preview; snapshot the colour for each take.
- Advertise DIP support to faderOS, which uses it for MIX slot 2 and retains VFADE compatibility with older servers.

## 0.8.5 — matte source preparation

- Add a dedicated opaque MATTE input type with RGB colour picker and persistent source configuration. Existing Bars colour descriptors remain compatible.
- Reject invalid matte colours and empty HTML/stream descriptors instead of treating them as assigned sources. Reject line breaks and NULs in producer descriptors.
- Keep WebGL DME experiments isolated from production while native GPU effects are investigated.

## 0.8.4 — manual wipe startup guard

- Hold an opaque black key producer while the HTML wipe mask loads, preventing the incoming picture from appearing unmasked before the first CEF frame.
- Transition from the guard to the HTML mask with a one-frame MIX; retain normal manual position control.

## 0.8.3 — numbered multiview labels

- Prefix source labels with their one-based crosspoint number, including the upper bank and M/E reentry.
- Normalize matching manually added number prefixes for display without changing saved source names.

## 0.8.2 — manual wipe keyer command

- Use CasparCG MIXER KEYER to enable the manual wipe mask; the invalid IS_KEY command prevented preparation on the real renderer.
- Reject that invalid spelling in the simulated renderer used by engine tests.

## 0.8.1 — manual endpoint handover

- Promote the running incoming producer with SWAP TRANSFORMS at a completed T-bar take instead of creating a fresh route before clearing the cover.
- Hide the outgoing layer before promotion and remove the wipe mask first.
- Test endpoint ordering, MIX with keyers, WIPE and rejected producer promotion.

## 0.8.0 — native mix modes and transition namespaces

- Isolate both configuration-using test suites in unique temporary directories to prevent protocol tests overwriting user settings.

- Add VFADE, FADECUT and CUTFADE for background-only AUTO and manual takes.
- Add native PUSH and SLIDE AUTO transitions in four directions.
- Separate confirmed Sony wipe codes from the existing SMPTE namespace.
- Give transition rehearsal its own black backing and restore manual layer gains on completion or cancellation.

## 0.7.0 — independent key processing

- Add native chroma extraction and rectangular masks per upstream key and DSK, with persistent M/E-specific parameters.
- Add preparation dialogs with draft/cancel behavior and confirmed panel parameter updates.
- Reset manual rehearsal transforms before reuse and avoid processing preview routes twice.

## 0.6.1 — manual-control recovery

- Release panel ownership after all manual takes complete or cancel, retaining it across frozen M/Es.
- Cancel an unapplied M/E delegation when the operator returns to the current M/E.
- Cover ownership handover, panel disconnect, simultaneous key MIX and audio gain restoration with regression tests.

## 0.6.0 — manual transitions and system monitoring

- Drive manual MIX and background WIPE with the panel T-bar, including hold/reverse, confirmed bus swaps, preview rehearsal and M/E freeze/pickup.
- Display system load bars and a prominent multiview warning after lost server communication.

- Add a local CPU/RAM and GPU/VRAM dashboard with asynchronous NVIDIA queries, AMD counters and explicit unavailable readings.

- Keep main multiview PGM/PVW and meters fixed on M/E 1; show M/E 2–4 buses in side columns with four information cells.


## 0.5.0 — banked multiview and routed destinations

- Show twelve multiview source tiles with independent PGM/PVW tally and SHIFT bank paging.
- Preserve next-M/E reentry at crosspoints 12 and 24; migrate eight-input configurations.
- Allow M/E program input assignments and reject direct or indirect bus feedback loops.
- Add NDI/screen destinations and four routable AUX roles with confirmed AMCP setup.
- Add framing ratios, EBU R95 margins, legacy margins and apply-to-both safe-area controls.
- Test output persistence, consumer-preserving AUX changes, routing safety and UI drafts.


## 0.4.2 — reuse prepared key inputs

- Route input layers into upstream keys and both DSKs instead of opening duplicate file/HTML producers.
- Preserve embedded alpha with layer routes and keep shared source playback running when overlays are toggled.
- Map source meters through configured channel assignments and program metering through the effective air channel.
- Retain genuine post-composition program audio readings rather than copying input meters.
- Cover producer reuse, alpha route commands and custom/fallback meter channel mappings with regression tests.

## 0.4.1 — isolated transition rehearsal

- Add per-M/E TRANS PREVIEW for CUT, MIX and WIPE, following NEXT TRANSITION.
- Rehearse in private preview layers without exchanging buses or changing on-air keys.
- Preserve normal preview key layers that are routed into program.
- Wait for batch acknowledgement before timing the rehearsal; acknowledge cleanup before unlocking.
- Keep ping-pong WIPE direction unchanged by rehearsals and reject unsupported stinger rehearsals.
- Expose mode and rehearsal state through the panel protocol for faderOS feedback.
- Test repeated, key-only and failed rehearsals, delegation and destination isolation.

## 0.4.0 — confirmed upstream keys and dual DSK preview

- Confirm direct key on/off and on-air key/DSK source changes only after their AMCP batch succeeds.
- Prevent source reassignment during a pending operation and preserve confirmed assignments on failure.
- Add independent preview for DSK 2 and retain armed preview after DSK fade-out.
- Prepare both DSK layers on ARM and apply the same busy policy to both slots.
- Count all four keys in NEXT TRANSITION and support resetting to background only.
- Publish topology capabilities and bus/key state for all four M/Es without delegation.
- Validate keyer request indices, sources, boolean fields and DSK mix durations.
- Persist source assignments after engine confirmation and document current alpha-only render limits.
- Add failed key/source batches, NEXT invariants, dual DSK preview and malformed panel regressions.

## 0.3.1 — preparation feedback and failed-apply protection

- Show each input's running preparation state and pending saved changes.
- Distinguish saved preparation from running settings and add a bounded session diagnostics log.
- Preserve configuration in memory as well as on disk when a draft cannot be saved.
- Validate NDI feed names and DeckLink device numbers before saving.
- Bind replacement panel listeners before closing a working endpoint; failed binds
  retain existing panel clients and do not apply the prepared configuration.
- Disable repeated application during preparation; offer explicit retries for incomplete preparation.
- Recheck engine activity after the apply confirmation dialog closes.
- Add failed-save, invalid-media, listener-preservation and status feedback regressions.

## 0.3.0 — Qt engine manager

- Replace the on-screen live mixer and keyboard takes with a preparation workspace.
- Group connections, inputs, outputs, M/E/key assignments, multiview, ten stingers
  and transition defaults in a navigable management interface.
- Separate drafts, atomically saved preparation and explicitly applied running settings.
- Validate channel collisions and stinger timing, support discard and unsaved-close feedback.
- Retain hardware HID control and expose accepted preparation status.
- Mark independent auxiliary routing as unavailable until supported by the engine.
- Add offscreen Qt management regression tests.

## 0.2.3 — state-query framing and interrupted takes

- Read complete 201 XML responses with native LF or CRLF-normalised payloads.
  Keep queued commands blocked until the document is complete; malformed or
  incomplete documents retire the session on timeout.
- Cancel local deferred bus/key swaps when the control connection is lost during
  a take. Preserve the last confirmed bus selection instead of pretending the
  renderer completed its transition.
- Add fragmented XML, command correlation, malformed reply and interrupted AUTO
  regressions. Complete render-state reconciliation remains pending.

## 0.2.2 — confirmed render preparation

- Add identified AMCP command batches. A failed command cancels only the
  unsent remainder of its batch; unrelated queued work remains available.
- Prevent reply callbacks from sending a failed batch's next command before
  cancellation can run. Correlate mixer completion by batch identity so an
  identical background command cannot commit preview or program prematurely.
- Cache producer, mixer, route and NDI consumer preparation only after a
  successful reply. Retrying ARM preserves accepted producers/routes and
  retries missing steps instead of restarting all inputs.
- Apply newer source/output configuration after successful pending preparation.
- Expose per-input `ready` and aggregate `outputsReady` in panel state events.
- Abort failed takes without speculative bus swaps or destructive layer cleanup.
- Add failed-batch, partial preparation retry and concurrent configuration tests.

## 0.2.1 — reliability review

- Retire timed-out AMCP sessions and discard queued commands; ambiguous takes
  are never replayed. Switching the configured endpoint replaces the connection.
- Reset response parsing before callbacks and reject multi-line commands.
- Start take/FTB holds and DSK clear timers after AMCP batch acknowledgement;
  invalidate old DSK timers on disconnect.
- Validate WIPE requests before modifying live parameters, publish compound
  wipe changes together, and report configuration persistence failures to panels.
- Save configuration with atomic file replacement and verify the commit.
- Align both CasparCG example files with the current 18-channel topology.
- Add delayed-response, connection recovery and panel validation regressions.

## 0.2.0 — publication preparation

- CLI help/version without a display; opt-in runtime diagnostics.
- CMake test dependencies conditional on BUILD_TESTING.
- All runtime HTML assets available in out-of-tree builds and installations.
- Fixed the bundled stylesheet resource alias.
- Optional Doxygen documentation, source bundle tooling and future GitHub CI.
- English contributor/development guides and sanitized configuration examples.

The existing switcher implementation is retained. Final name and public license
are pending; the current proprietary/internal status is unchanged.
