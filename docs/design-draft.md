# Historical design draft

This is the original design specification, preserved for context. Some planned
features and architectural names differ from the current implementation. See
README.md and panel-protocol.md for current build and control information.

# Core Switcher MVP — Specifications

## Domain: backend/

### Purpose
Service connecting to CasparCG, executing switches, maintaining state, broadcasting updates.

### Requirements

**B1** — The backend SHALL establish and maintain persistent TCP connection to CasparCG server (port 5250).\
**B2** — The backend SHALL expose REST endpoints for PREVIEW, PROGRAM, CUT, FADE commands per channel.\
**B3** — The backend SHALL broadcast channel state (preview/program/tally) to all WebSocket clients on change.\
**B4** — The backend SHALL monitor CasparCG TALLY messages and reflect tally status per channel.\
**B5** — The backend SHALL read configuration (host, port, channel count, source mappings) from a file and map logical source names to CasparCG layer numbers and media paths.\
**B6** — The backend SHALL define TypeScript interfaces for AMCP commands, responses, and channel state.

### Scenarios

**B1‑1** Successful connection\
- GIVEN CasparCG server running\
- WHEN backend starts with valid config\
- THEN backend connects successfully\

**B1‑2** Connection failure\
- GIVEN CasparCG server not reachable\
- WHEN backend attempts to connect\
- THEN backend logs error and retries every 10s\

**B2‑1** Preview command\
- GIVEN channel 1 program = "CAM1"\
- WHEN operator sends PREVIEW command with source "CAM2" to channel 1\
- THEN backend sends AMCP "PLAY 1‑1 CAM2" with MIX 0 frames\
- AND updates internal state: channel 1 preview = "CAM2"\

**B2‑2** Cut command\
- GIVEN channel 1 preview = "CAM2", program = "CAM1"\
- WHEN operator sends CUT command for channel 1\
- THEN backend sends AMCP "PLAY 1‑1 CAM2" with CUT transition\
- AND updates state: channel 1 program = "CAM2", preview = null\

**B3‑1** State change broadcast\
- GIVEN WebSocket client connected\
- WHEN channel 2 program source changes\
- THEN backend emits WebSocket message with updated channel state\
- AND client receives update within 100ms\

**B4‑1** Tally update\
- GIVEN channel 3 is in program\
- WHEN CasparCG sends TALLY message indicating channel 3 program active\
- THEN backend updates tally state for channel 3\
- AND broadcasts tally update via WebSocket\

**B5‑1** Load config\
- GIVEN config file with host "192.168.1.100", port 5250, channels 4\
- WHEN backend starts\
- THEN it connects to CasparCG at that address and creates 4 channel rows\

**B6‑1** Source mapping\
- GIVEN source mapping "CAM1" → layer 1, "amb/background.mp4"\
- WHEN operator selects "CAM1" for preview\
- THEN backend sends AMCP PLAY command with layer 1 and mapped media path\

**B7‑1** Type safety\
- GIVEN developer imports AMCP command types\
- WHEN they construct PLAY command with invalid parameters\
- THEN TypeScript compilation fails with type error\

**B7‑2** Consistent state shape\
- GIVEN backend and frontend both import ChannelState\
- WHEN backend emits WebSocket event\
- THEN frontend can parse event data without transformation\

## Domain: frontend/

### Purpose
Web interface displaying channel rows, source selector, tallies, switching buttons, with real‑time updates.

### Requirements

**F1** — The UI SHALL display one row per configured channel showing program source, preview source, tally status.\
**F2** — The UI SHALL provide dropdown per channel to select video source for preview.\
**F3** — The UI SHALL provide CUT and FADE buttons per channel, enabled only when preview source set.\
**F4** — The UI SHALL subscribe to WebSocket channel‑state events and update displayed channel data immediately.

### Scenarios

**F1‑1** Render channel row\
- GIVEN channel 1 program = "CAM1", preview = "CAM2", tally = program\
- WHEN UI loads\
- THEN channel 1 row shows "CAM1" as program, "CAM2" as preview, red tally indicator\

**F2‑1** Select source\
- GIVEN available sources list includes "CAM1", "CAM2", "GFX1"\
- WHEN operator selects "GFX1" from channel 2 preview dropdown\
- THEN UI sends PREVIEW command with source "GFX1" to backend\
- AND channel 2 preview cell updates to "GFX1"\

**F3‑1** Cut button enabled\
- GIVEN channel 3 has preview source different from program\
- WHEN UI renders channel 3 row\
- THEN CUT button is enabled\
- AND clicking it sends CUT command for channel 3\

**F4‑1** Live tally update\
- GIVEN UI shows channel 4 tally = off\
- WHEN backend broadcasts tally update (channel 4 tally = preview)\
- THEN UI updates channel 4 tally indicator to amber within 200ms\
