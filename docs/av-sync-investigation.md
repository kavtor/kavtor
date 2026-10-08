# Audio/video synchronization investigation

## Status

Progressive content desynchronization after transitions has been reported, but
has not yet been reproduced with a measured flash/beep reference. No audio
latency compensation or CasparCG buffer changes have been applied. The report
predates the new manual-transition implementation.

## Reviewed paths

kavtor controls routes and volumes through AMCP. It does not maintain its own
sample queue or output clock. Source layers feed M/E routes; complete M/E
programs can feed downstream M/Es and the final air channel. NDI consumers
receive composed CasparCG frames. Input decoding, resampling, composition and
NDI scheduling therefore need to be distinguished when locating a fault.

The locally available CasparCG 2.5.1 source was inspected in:

- `core/producer/route/route_producer.cpp`: bounded route queues, frame repeat
  on underrun and frame drop on overflow. Repeated frames also carry audio.
- `core/producer/transition/transition_producer.cpp`: native transitions consume
  outgoing/incoming pictures and their audio, applying complementary MIX gains.
- `core/mixer/audio/audio_mixer.cpp`: sample cadence and volume ramps. At the
  current 50 fps / 48 kHz format, each frame requires 960 samples per channel.
- `modules/ffmpeg/producer/av_producer.cpp`: input audio filtering/resampling and
  producer buffering. HLS sources add their own network/decoder latency.
- `modules/newtek/consumer/newtek_ndi_consumer.cpp`: an unbounded frame queue,
  eight-frame initial buffering and a separately paced send thread. Audio and
  video are sent from the same queued frame, with synthesized NDI timecodes.

These are investigation candidates, not confirmed runtime bugs. The local
source tree is not proof of the exact source used to build the running binary.
A growing NDI queue can add output latency; by itself that does not prove audio
and video content are separating. Normal synthesized timecodes also cannot prove
that the audio samples belong to the picture being shown.

The currently running channels use 50 fps. A 15-second passive PGM reception
on 2026-10-02 measured approximately 50 video frames and 50 audio packets per
second, with no detected timecode discontinuities. Latest packet start times
alternated between 0 and 20 ms apart. This is consistent with packet arrival
ordering; it is not a measurement of content lip sync or transition behavior.

## Optional receiver timing probe

The probe only subscribes to an existing NDI output. It does not send panel or
AMCP commands. The NDI SDK headers and library are optional build dependencies:

```sh
cmake -S . -B build -DKAVTOR_BUILD_NDI_DIAGNOSTICS=ON
cmake --build build --target kavtor-ndi-timing
build/kavtor-ndi-timing --list
build/kavtor-ndi-timing --source 'CHARLIE (KAVTOR_PGM)' --seconds 300 > pgm.csv
```

Use the exact discovered source name; repeat for clean feed. CSV records wall
elapsed time, received frame/packet counts, interval rates, cumulative audio
sample duration, latest audio/video timecode difference and discontinuities.
A discontinuity means a timecode increment differs from the expected packet
length by more than half that length. Missing audio or video makes the probe
exit unsuccessfully. Network startup and packet arrival jitter must be allowed
for when comparing sample duration and elapsed time.

## Controlled content test, when an operator is available

Use a local 50 fps / 48 kHz clip with simultaneous periodic full-frame flashes
and short beeps. Start with no keys/DSKs and no nested M/E. Do not start with HLS:
its changing network buffers would obscure the baseline.

1. Measure flash/beep offset on the unchanged input and external NDI PGM and
   clean-feed receivers for several minutes. Record receiver buffering settings.
2. Repeat with CUTs, then repeated timed MIXes. Record offset before/after each
   take, not just the amount of time elapsed.
3. Test manual MIX: hold midway, reverse, cancel at origin, complete at the
   opposite endpoint and repeat both lever directions.
4. Add one key/DSK at a time, then one M/E cascade at a time. Compare clean feed
   and final PGM to separate upstream and downstream contributions.
5. Repeat the implicated case with the original source type, especially HLS.
   Record CPU/GPU load, source identity, frame rate, number of takes and whether
   the offset continues growing after the final take.

A constant offset suggests fixed buffering or route depth. Growth while idle
suggests a clock/cadence problem; a step after each take suggests source/route
lifecycle behavior. These patterns guide investigation but are not sufficient
on their own to establish a cause. Compare visible/audible content with the CSV
transport timing rather than treating timecodes as the content reference.

Future PipeWire output/mixing must retain a defined video master clock and
resampling policy. Moving audio to a separate engine is not itself a remedy
for an unmeasured synchronization fault.

## NDI transport counters

The optional `kavtor-ndi-timing` probe now appends SDK total video frames,
video drops and audio drops to its CSV. Timecode gaps remain independent:
synthesized wall-clock jitter can occur with zero SDK drops. Ignore receiver
connection startup when comparing stable delivery counters. These counters
measure transport/receiver behavior and do not establish content lip sync.
