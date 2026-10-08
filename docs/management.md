# Engine management workspace

kavtor 0.5 separates Qt preparation from hardware live operation. No on-screen
CUT, AUTO, program/preview selectors or live keyboard shortcuts are installed.
The Speed Editor HID integration remains available as a hardware control.

## Sections

- **Connections**: CasparCG endpoint, panel TCP and incoming OSC ports.
- **Inputs**: two source banks with 22 assignable slots, labels, producer types,
  M/E program routes, server descriptors and channels.
- **Outputs**: M/E 1 program/preview, air and multiview channels, NDI program/clean
  feeds, DSK 1 source and multiview HTML asset.
- **M/Es & keyers**: fixed four-bank topology, four prepared key sources per bank
  and DSK 2 source. Channels 13–18 are reserved for M/E 2–4.
- **Multiview**: layout, labels, clocks, safe areas and audio meters.
- **Media & stingers**: ten slots with media, reverse media and frame timings.
- **Transition defaults**: prepared duration, wipe, direction and edge parameters.
- **Auxiliaries**: operating instructions for the four routable output roles
  configured under Outputs.

## Edit, save, apply

Fields form a draft. **Discard** reloads the last saved preparation. **Save
configuration** validates channel collisions and stinger timing, then atomically
writes the prepared configuration. Failed saves do not replace running settings.
The editor reports unsaved changes and asks before discarding them on close.

**Apply saved configuration** is separate and disabled while edits are unsaved
or a take is busy. When connected it asks for confirmation: changed inputs,
key sources, multiview and DSK routes can affect the output. Applying updates
running configuration, prepared key assignments and renderer preparation. This
is not a transactional renderer reconfiguration; the engine reports AMCP errors.
Endpoint changes are used on the next connection. Disconnect/reconnect to switch
CasparCG host/port and restart the incoming OSC listener with its new port.

A saved preparation is loaded when kavtor starts again. Closing the manager does
not overwrite it with older running configuration. Panel commands can continue
to change running settings independently; preparation is a separate snapshot and
applying it intentionally replaces those settings. Avoid simultaneous setup from
the hardware and application until profile conflict handling is implemented.

The summary reports accepted input/output preparation, not video signal health.
The engine still assumes 1080p50 and fixed four-M/E topology. The
manager does not claim device discovery or arbitrary M/E channel allocation. Qt peer tests verify draft isolation and command boundaries;
real CasparCG validation remains necessary for renderer reconfiguration.

## Preparation feedback and failure handling

Inputs report **Disabled**, **Unassigned**, **Offline**, **Not prepared** or
**Prepared** for the running settings. **Pending apply** marks a saved input
that differs from its running definition; the global preparation indicator also
includes output, endpoint, key, multiview and transition differences. Neither
indicator certifies a valid video signal. Updating status does not dirty the editor.

Diagnostics retains the last 200 session log lines with timestamps. It records
save/apply results, validation messages and engine errors. It does not stream
per-frame protocol traffic and can be cleared without changing configuration.

A failed save leaves the target Configuration object unchanged. Applying first
checks the panel listener: if the proposed port cannot be bound, running settings,
the old listener and its connected clients are retained. Successful port changes
still disconnect clients on the previous endpoint. Renderer application is not
atomic; accepted AMCP operations cannot be rolled back safely. A confirmation
dialog also rechecks take activity on return, because the physical panel can
start a take while the dialog is open.

Apply is disabled while render preparation batches are pending. With identical
saved and running settings it displays **No pending changes**, or **Retry
preparation** when connected inputs/outputs have missing accepted steps. This
prevents gratuitous re-arming of an already prepared engine. Readiness still
refers to command acceptance rather than video health.


## Source banks and M/E routes (0.5.0)

Two twelve-crosspoint banks follow panel SHIFT. Positions 12 and 24 are the same
fixed next-M/E program reentry; the last M/E disables both. The remaining 22
positions accept normal producers. The first eight channels are preserved.
Additional inputs use channels 19–33 with gaps for the reserved positions;
renderer templates now contain 38 channels. Restart CasparCG with the updated
template before testing additional inputs or AUX1–AUX4. Restart kavtor and
faderOS to use the new protocol together. No Sony firmware update is needed.

For a different M/E on a normal background crosspoint, choose **M/E Program**
and enter its number (1–4). The whole mix, including upstream keyers, is routed.
The engine rejects self-routing and indirect cycles across program/preview buses.
These routes are background/output sources; alpha keyers use prepared input
layers. The multiview has two fixed M/E 1 bus pictures and twelve smaller 16:9 pictures,
with green preview and red program borders (red wins when both select the same
source). SHIFT pages the source grid without switching either bus.

## Additional destinations

Under **Outputs**, add a destination and independently select its source:
input, any M/E program/preview, multiview or final program including DSK/FTB.
This release supports **NDI** and **Screen**. NDI names must be unique. Screen
uses CasparCG display numbering (0 selects its default); fullscreen is optional.
Display outputs are not GPU render-device selectors.

Assign up to four enabled destinations **AUX1–AUX4** roles. Roles are unique
and reserve renderer channels 35–38. Other destinations remain fixed. Switching
an AUX changes its route without destroying its consumer. On the Sony, AUX1–3
and EDIT PVW (AUX4) select a role; the AUX bus selects its input, with SHIFT for
the upper bank. The last crosspoint chooses the next M/E program, except on
M/E 4. Hold AUX1–3 or EDIT PVW (AUX4) and press UTILITY1–4 to route the complete
program of that M/E to the held AUX, without changing the active M/E.
Press the role again to release delegation. Other M/E programs, previews
and multiview can be selected in the application. Runtime AUX selections are
not automatically written to disk; save configuration to retain them.

Outputs use explicit AMCP consumer ports. Apply/remove waits for confirmation;
preparation status does not prove NDI discovery or frame delivery. Original
program and clean NDI controls remain for compatibility. Streaming, recording,
DeckLink/SDI, virtual cameras, encoder profiles and GPU selection remain future
work; they are not advertised as operational.

## Safe-area guides

Preview and program independently select 16:9, 4:3, 9:16, 14:9, 1:1 or 4:5 framing.
**EBU** uses EBU R95's 3.5% action and 5% graphics inset on each edge. **Classic**
preserves the earlier 5%/10% overlay. **ALL** copies preview framing to program
and enables both guides. EBU R95 specifies the 16:9 production area; margins on
other ratios are composition aids, not a standards-compliance claim.
Reference: https://tech.ebu.ch/docs/r/r095.pdf . Native ATEM safe-area controls
remain capability-dependent on/off controls; these framing selections affect kavtor.

## Planned streaming profiles

Streaming destinations will provide service presets, including YouTube, Twitch
and a custom endpoint, with broad coverage based on the OBS service catalog.
Before importing catalog data, check its license and separate reusable endpoint
and encoder recommendations from OBS-specific integration. Presets must remain
updateable independently of the application and allow manual endpoint overrides.
Stream keys and authentication are private configuration, never published in
example files or diagnostic logs. This is planned work; streaming destinations
are not operational in this release.

## Multiview bus monitoring

The main program tile always shows final M/E 1 air (including DSK/FTB), and
the main preview tile always shows M/E 1 preview. Delegating panel control to
another M/E does not repurpose these monitors or their audio meters. The left
column contains M/E 2–4 previews; the right contains their complete programs.
All video tiles retain 16:9 proportions. The source grid still follows the panel
source bank and shows tallies for the selected M/E.

Four remaining side cells show local time, control delegation/source bank,
output preparation and local system load. Preparation means accepted
renderer commands, not verified signal health. Streaming telemetry will be incorporated when streaming destinations are implemented. Layout personalization and independent
multiview layouts remain planned.

The future streaming monitor must report elapsed live time, measured bitrate,
network send throughput and dropped frames. Distinguish encoding drops from
network drops when the output backend supplies them, and expose connecting,
live, reconnecting and failed states. Values must come from output telemetry;
unavailable counters must not be displayed as zero or inferred from the profile.

The current multiview uses a dedicated **2304×1080 at 50 fps** custom CasparCG
format, `KAVTOR_MV_2304`, on channel 11 in the supplied renderer configuration.
Only the multiview format changes; all other channels retain 1080p50. Restart
CasparCG with the updated configuration and then restart kavtor. The native
screen consumer adopts the wider canvas; NDI destinations also receive its
native dimensions. Main tiles are 768×432, and each small tile is 384×216,
so the complete grid fills the canvas with 16:9 pictures and no unused rows.
Changing only the screen window dimensions would stretch or letterbox the old
canvas and does not replace this renderer format change.

## Output telemetry investigation (CasparCG 2.5.1)

The installed server publishes channel `framerate` as the configured rational
rate, not measured delivery FPS. OSC message cadence is not a reliable substitute
for frame delivery or dropped-frame counters. The NDI consumer monitor publishes
only `ndi/name` and `ndi/allow_fields`. Its diagnostics graph records buffer,
tick and frame timings, but those are not consumer telemetry exposed to kavtor.
The Screen consumer similarly tags dropped frames in diagnostics without
exporting a dropped-frame counter in its monitor state.

NDI's sender SDK provides `NDIlib_send_get_no_connections(instance, 0)` for a
nonblocking receiver count. Exposing this requires an extension in CasparCG's
NDI consumer, alongside measured send FPS and buffer underrun counters. A send
counter alone does not prove reception at the remote end. The inspected SDK
sender interface does not expose per-output network bitrate; whole-interface
traffic must not be presented as NDI output bitrate. Unknown metrics should
remain unavailable rather than showing fabricated zeros or nominal FPS.

Source references in the CasparCG 2.5.1 tree: `src/core/video_channel.cpp`,
`src/core/consumer/output.cpp`,
`src/modules/newtek/consumer/newtek_ndi_consumer.cpp`,
`src/modules/newtek/interop/Processing.NDI.Send.h`, and
`src/modules/screen/consumer/screen_consumer.cpp`.

All M/E side monitors have stereo meters. The reserved cascade crosspoints and
M/E program aliases read the complete M/E program meter, not an unused input
channel. casparMIX publishes source and M/E audio peaks without video consumers.
The supplied configurations leave meter-only channels consumer-free. Empty Art-Net
consumers force full-raster video rendering and readback and must not be added
for meters. Only actual picture outputs require consumers.

## Local system load

The lower right information cell reports **kavtor host** load, sampled once
per second. Linux CPU use is derived from `/proc/stat` deltas; RAM use is total
minus `MemAvailable`, so reclaimable caches are not treated as application use.
NVIDIA utilization and VRAM come from an optional, asynchronous `nvidia-smi`
query with a timeout. AMD uses available DRM sysfs counters. Each reported GPU
is identified by name and PCI address; readings are not assumed to belong to
the renderer. Missing counters display a dash, not zero. Multiple NVIDIA/AMD
GPUs are listed independently; integrated GPUs without these counters remain
unavailable. This panel does not measure a remote CasparCG host.

The monitor temporarily occupies the future streaming information cell. GPU
load is a current observation, not a guarantee of frame delivery or an estimate
of remaining mixer capacity. GPU allocation for multiview and hardware encoding
is a separate future configuration feature.

The multiview displays a large **SERVER LOST** overlay if no kavtor heartbeat
arrives for five seconds, and removes it upon recovery. The heartbeat runs
independently of clock, meter and load changes. This indicates lost control
communication; it cannot draw a warning if CasparCG or the HTML renderer itself
has stopped rendering. System bars retain numeric values and use amber/red
above 80%/95%; these are visual load thresholds, not frame-loss measurements.


## Matte inputs

Choose **Matte** in the Inputs producer selector for a constant opaque colour.
Enter `#RRGGBB` or use the **RGB** colour-picker button. Save preparation and
apply it using the same workflow as other inputs; a matte can then be selected
on the buses, used as a key fill, or routed to an output. Invalid colours prevent
application and are never sent as Caspar commands.

Existing Bars inputs containing colours still work for compatibility. Use Matte
for new solid-colour inputs and Bars for test patterns. Empty HTML and Stream
descriptors are unassigned rather than attempted as incomplete producers.
**Transition defaults → DIP colour** prepares the opaque colour used for DIP.
The second MIX slot on faderOS is DIP when the connected kavtor advertises it;
older servers retain VFADE. The default colour is black. AUTO and T-bar use the
same prepared colour, copied when the take starts. DIP currently requires
background-only NEXT TRANSITION. Keys that are already on air remain above the
background transition. Transition preview uses its own backing layer and does
not alter the program bus.

### Local Linux capture (V4L2)

Choose **V4L2** in Inputs and enter `/dev/video0` (or another numbered video
node). Stable `/dev/v4l/by-id/...` and `/dev/v4l/by-path/...` udev links are also
accepted. The device must exist on the **CasparCG server**, not necessarily on
the computer running kavtor preparation. Prefer a stable link when several
capture devices are attached.

CasparCG opens its native FFmpeg `v4l2://` producer, with seeking disabled and
video-only letterboxing. The installed CasparCG/FFmpeg build must include V4L2
support and its service user must have permission to read the device. This
initial implementation uses the device's negotiated default format; resolution,
frame rate, pixel format and separate sound-device selection are not exposed.
It does not imply audio capture from a webcam. Physical capture validation
requires a connected Linux video device.


## Standard color-bar inputs

Select **Bars** as the input type, then choose a pattern from its dropdown:
EBU 75% (default), EBU 100%, SMPTE SD (EG 1-1990), or SMPTE HD (RP 219-2002).
The saved descriptor is a stable pattern identifier; it is applied with the same
Save/Apply workflow as other sources. casparMIX 0.3.1 generates a single static
image at the channel raster, instead of stretching a seven-pixel color strip.
The old `pal`/`ebu` names now correctly resolve to EBU 75%; `hd` still selects
SMPTE HD. Existing solid-color source descriptors remain readable; use **Matte**
for new solid-color inputs. Regenerate bars after a channel-format change.

Definitions follow FFmpeg's named source patterns. They are SDR patterns and do
not claim every later SMPTE revision or calibrated signal levels through arbitrary
codecs/output chains. Reference: https://www.ffmpeg.org/ffmpeg-filters.html,
section 18.13; https://pub.smpte.org/pub/rp219-1/rp0219-1-2014.pdf.


### Native multiview rendering

kavtor 0.10.0 requires casparMIX 0.4.2 for the multiview graphics layer. Video
routes and all layout preferences remain unchanged. Text and shapes are cached in
a native static graphics sheet; audio levels use a compact value update protocol with
one shared envelope for each stereo-meter colour stack. Local clock, cue blinking
and the five-second server-loss warning are evaluated by the engine. Proportional
and monospace font families are independent, preserving compact source titles,
two-colour clip timers and outlined NO SOURCE messages.

The engine API is generic; it contains no hard-coded kavtor layout. Each native
producer owns its own scene and viewport, providing the basis for future custom
layouts and multiple independently configured multiview outputs. The current Qt
manager still prepares one multiview. This does not yet implement a layout editor.

## Clip repeat (0.12.2)

The **Loop** checkbox under **Inputs** controls repetition for Clip inputs. It is
not available for stills, live streams or other producer types. Each clip retains
its own choice in the saved project. Older projects keep their existing repeating
behaviour when the new field is absent.

Loop follows the same draft/save/apply workflow as other source options. Applying
a changed choice reloads that clip from its beginning; configure it before using
the source on air. Saving alone does not change the running producer. This option
does not restart a clip when selecting it on a bus or pause it off air; those
playback policies are separate future features.

## DME background preparation

**DME backgrounds** prepares a GLOBAL fill shared by default across all DME
presets. Move, Cube, Zoom, Page turn and Page roll offer explicit CUSTOM overrides;
individual Sony presets can also be prepared through panel MODFY. Choose Black, a prepared input (including a Matte colour generator) or
an explicit M/E program. Reserved next-M/E crosspoints are not offered because
their meaning changes with delegation. The background is silent and independent
of the paper backside.

Choices use the normal draft/save/apply workflow and require casparMIX 0.6.0 to
render. They do not alter a running take; the next take captures the preset.
The running mixer still validates readiness and feedback before using a source,
so a preset cannot route the current M/E back into itself. The panel MODFY menu
provides operational background selection and live preview adjustment.

## Static DME backgrounds (0.13.1)

Choose **Static image** under DME backgrounds and use **Choose image…** to select
an asset, independently for each effect. PNG, JPEG, WebP, BMP and TIFF filenames
are supported. The image does not occupy a source crosspoint or source channel.
It fills the frame while preserving aspect (centred cropping if necessary), has
no audio and uses the same save/apply workflow as the other background options.
The take captures its asset filename; subsequent preset changes cannot replace
an in-progress background. Source and M/E choices remain available for moving
content, alongside Black and Matte colour-generator inputs.

As with SuperSource static assets, filenames refer to files readable by the
CasparCG host. Local image selection is not a remote file-upload service; use a
shared asset path when the engine runs on another machine. casparMIX 0.7.1 fixes quoted filenames in native DME producer specifications and
is required for static backgrounds. No panel firmware change is needed. MODFY
displays STATIC IMAGE for the configured asset.

### Concurrent panel changes

Saving preparation preserves live panel settings unless the same field was
explicitly edited in the preparation workspace. A three-way merge covers the
configuration, including transition rates, wipe modifiers, key processing,
DME backgrounds and multiview settings. Concurrent changes to the same field
stop Save or Apply and identify the conflicting setting; no silent rollback
is performed. Widget normalization of legacy defaults is not a user edit.

### Authoritative multiview bus images

All bus tiles and M/E reentry tiles use rendered routes from the actual output
channel. They display the final composition rather than recomposing its layers
at the multiview raster. The shared frame synchronization group supplies the
same epoch. Scaling the tile changes its sampling resolution, not the content
or geometry of the bus. At equal raster size, regression captures compare
program and multiview RGB pixels exactly.

## Common and individual DME backgrounds (0.20.0)

The first background row is GLOBAL. Choose a looping Clip input there to share
one animated brand background across effects; image, Matte input and M/E
choices retain their existing meaning. Per-effect selectors default to
**GLOBAL — use common background**. An explicit choice creates a CUSTOM
override. Returning to GLOBAL preserves the stored custom value.

The panel MODFY menu uses F1 AUX, F2 BLACK, F3 COLOR, F4 GLOBAL, F5 CUSTOM and
F6 RSTGL. Switching to CUSTOM creates a snapshot of GLOBAL only if that effect
has no saved custom value. RSTGL copies today's GLOBAL into this effect's CUSTOM
value; future global edits no longer affect that copy. Assignments made while
GLOBAL is selected edit the shared background; CUSTOM edits only this preset.
Old explicit non-black assignments migrate to CUSTOM; untouched legacy black
defaults inherit GLOBAL. Programme takes retain their captured background. A
running private transition preview updates only if its effective assignment
changes. Feedback loops are rejected and settings use normal save/apply merge
semantics. No engine/firmware upgrade is required for this preparation feature.

## Independent clip transport feedback (0.22.0)

With casparMIX 0.15, each clip timer includes a transport icon on its left:
right triangle for normal playback, two bars for pause and two directional
triangles during controller scrubbing. Confirmed pause state comes from each
source's OSC foreground, independently of deck delegation. The temporary
scrubbing indicator follows the controller direction and expires on a monotonic
clock. Missing transport metadata does not invent a playback state.

OSC source FPS is retained per clip, including rational 24000/1001 metadata.
Media time/duration remain seconds from the producer; changing input FPS does
not change intended playback speed. Caspar adapts native input cadence to the
common output cadence. SEEK currently uses the output/channel frame time base.

PLAY/RESUME clears transient shuttle/jog accumulators and timers. A late SEEK
acknowledgement cannot send another queued seek once normal playback resumes.
Cue-to-program playback applies the same reset. Persistent slow motion is not
a side effect of browsing; explicit rate control remains future replay work.

Empty logical key/bus layers now use Caspar's native #00000000 producer instead
of allocating transparent CEF pages. HTML sources/templates remain supported.
