# Open work — kavtor

Reviewed against kavtor 0.30.0, casparMIX 0.23.0 and faderOS 0.29.1 on 2026-10-10.
This file lists outstanding work only. Implemented behaviour belongs in feature
guides and the Git history. Related historical investigations are evidence, not
an additional implementation queue.

## Current work

- Complete key processing: separate fill/key and SOURCE modes, pattern keys,
  additional masks and DVE/PinP geometry, borders and shadows. LINEAR, CHROMA,
  LUMA, KEY INV and rectangular MAIN MASK/inversion are implemented.
- Review the complete 116-pattern WIPE catalogue, especially provisional
  224–247 and 270–272. Complete DME: 80 executable presets and 189 reserved
  DME/Resizer IDs. See
  [the per-ID inventory](sony-dme-inventory.json). Operator review remains
  required for inferred geometry, defaults and temporal curves.
- Add per-effect page/roll material, backside highlights and projected shadows;
  expose only parameters actually consumed by the renderer. Programme takes
  capture preparation; private transition preview may update it live.
- Extend prepared background selection in the application to every native Sony
  DME preset; panel MODFY already stores backgrounds independently per ID.
- Implement complete stinger and stinger+track-matte preparation/execution after
  the DME review: preloading, frame/time cut points, matte layouts/inversion,
  duration, alpha and independent audio fades. OBS is the reference. Initial
  clip stingers do not satisfy this complete feature.

## Mixer operation and resilience

- Support concurrent independent M/E takes and DSK operations; the current
  scheduler still serializes video operations globally.
- Extend combined NEXT TRANSITION to advanced background effects. Current native
  DME and alternate MIX modes require BKGD-only takes.
- Reconcile actual renderer state after a partially accepted take batch. Reconnect
  now reconstructs confirmed background/key compositions for all M/Es, respecting
  key-only preview. Failed recovery requires an explicit ARM retry; retained logical
  state is still not proof of rollback of a partially accepted live take.
- Make M/E/keyer/DSK topology configurable beyond the initial 4/4/2 allocation.
- Keep hard-overload media-clock policy explicit: improved normal-load cadence
  now measures 50 fps / 1x, but sustained exhaustion can still reduce output rate.
- Measure the reported progressive A/V content drift through prolonged CUT,
  AUTO/manual mixes, keyers, nested M/Es and external NDI reception. The video
  route/frame-sync fixes do not close this separate audio investigation. See
  [the measurement plan](av-sync-investigation.md).

## Inputs and playback

- Discover V4L2 devices/formats on the renderer host and validate real capture.
  Basic V4L2 descriptors are implemented; associated audio remains independent.
- Add explicit per-clip rate/timebase control with replay; ordinary PLAY is native
  rate and cancels transient browsing. Independent transport icons are implemented.
- Implement clip policies: Continuous, Restart on air, Resume on air and Manual.
  Use aggregate on-air contribution through keys, SuperSources and M/E routes.
  LOOP is already implemented independently of these policies.
- General audio-track selection remains deferred; retain explicit HLS variants
  for now rather than modifying CasparCG audio-track selection.
- Add isolated same-engine CG/playout inputs only after casparMIX enforces channel
  ownership through a restricted external AMCP endpoint. See engine roadmap.

## Outputs, telemetry and management

- Add streaming, recording, virtual cameras/V4L2 loopback and SDI/DeckLink outputs.
  NDI, Screen, independent destination routing and AUX1–4 are already operational.
- Prepare codecs, containers, hardware encoder/GPU selection and service presets
  (YouTube, Twitch, custom and broader OBS-catalogue coverage), with private keys,
  independently updateable presets and reconnect/failure state.
- Display measured streaming duration, bitrate, throughput and loss counters,
  distinguishing encoding/network losses where the backend supports them.
- Consume native output telemetry when exposed by casparMIX; unavailable values
  must remain unavailable, not nominal FPS or invented zero counters.
- Add an editable multiview layout and multiple independent MV outputs. The
  current native MV and its widgets are implemented, not replacement work.
- Continue manager/SuperSource editor ergonomics; preserve draft/save/apply and
  merge semantics across panel and application edits.
- Provide a management web UI and remote API in a later phase.

## Explicitly deferred

- PipeWire/Ardour audio routing and automatic participant mix-minus returns.
- VDO.Ninja room/source discovery and return-feed preparation.
- OMT: track/evaluate upstream work; do not build an external bridge now.
- ST 2110: no current implementation requirement.
- Instant replay: bounded lossless recording per enabled source, an external
  marks/playback client and a shared replay input, with storage/timing monitoring.
- Procedural GPU/WebGL stingers; define readiness, covered cut, completion and
  failure/audio policy before enabling live use.
- Regional backdrop blur under translucent keyers/CG using a separate effect
  coverage mask and a same-epoch GPU background, without CPU/encoder loopbacks.
- Measured multi-GPU render/encode allocation; device display selection alone
  does not select a render GPU.

## Publication

- Choose the final project name and license; review third-party code/assets.
- Validate the allowlisted public tree with clean builds/tests, excluding private
  media, credentials, Sony documents and unrelated workspace/history.
- Create the standalone GitHub repository and enable required CI/reviews.
- Use issue → branch → PR → reviewed merge after publication.

## Validation still required

- Reinterpret 1041–1048 doors after operator reported geometry differences.
  New flip/tumble 1101–1104 and 1121–1122 are operator-validated. Continue review
  of the remaining Sony catalogue.
- Real capture/output hardware tests and prolonged failure/reconnect/load tests.
- Key alpha/chroma pixels and take semantics on the real renderer, beyond AMCP
  peer tests; extend this evidence as each new processing mode is implemented.

Resolved engine migrations (rendered M/E routes, coherent frame clocks, native
wipes/borders and native MV), SuperSource instances/perspective, source-aware
MOVE, matte/DIP, bars and ordinary panel operation are deliberately absent from
this open-work list.

## Colour-selection ergonomics (deferred design)

Retain exact numeric colour entry. Also investigate a colour wheel rendered in
preview or an auxiliary monitoring window and selected with the panel joystick
for approximate choices. Define explicit preview/commit/cancel ownership and
keep the wheel away from programme. No colour-wheel implementation yet.
