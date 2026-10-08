# Sony pattern reference inventory

The user-supplied comparison of DVS-9000 and XVS manuals has been imported into
`data/sony-patterns-reference.json`: 385 predefined identifiers (116 wipes, 238
DME and 31 Resizer DME), plus three separate 99-register user-effect ranges.
The input document is fingerprinted by SHA-256; each record retains its source
line for traceability. All exported labels and metadata are English. Generic
labels are development labels, not claimed official Sony pattern names.

This is an inventory, not an implementation or a capability advertisement. Native
MOVE/CUBE/PAGE primitives are not automatically equivalent to a numbered Sony
preset. Only verified patterns may be enabled on the panel or reported by the API.

Wipes reveal an unchanged texture through a moving mask; DME transforms the
video texture. Programme must match the intended endpoint, except for families
such as Frame in-out/Picture-in-picture that deliberately retain an intermediate
state. Do not manufacture fixed effects for the user-register ranges.

DVS describes an outgoing page revealing the new image; XVS describes the new
page moving over the old image. Keep that model distinction open until animated
references establish the exact texture/backside assignments. Both describe Roll
as the incoming image unrolling over the old one. Background fill and a two-channel
page backside are independent signals; selecting a background must not implicitly
change the backside texture.

Primary reference: [Sony DVS-9000 manual](https://pro.sony/s3/cms-static-content/operation-manual/3704674111.pdf), printed pages 353–372;
XVS-9000/8000/7000/6000 manual 50135021M, pages 497–503.

## Completed rotary group

With casparMIX 0.9.2 or newer, DIRECT additionally accepts 150, 151, 156,
158, 160, 162, 516, 518, 604, 606, 624 and 661. The panel capability list
contains 53 reviewed native wipes. Older engines do not advertise the new
rotary numbers. Automatic and manual transitions use the same masks and
modifiers. The existing printed keypad presets remain unchanged.

## Mosaic traversal (casparMIX 0.9.3)

Implemented: 200–203 snakes and 206–213 clockwise/counterclockwise spirals,
65 native Sony masks total. Blocks are squares in producer pixels. Default
side is 10% of output height; MODFY accepts 2–50%. Edge blocks are clipped.
F1 opens a transactional keypad edit; ENTER commits; F2 resets; the encoder
changes preparation directly. Tile size can update a private wipe preview.

The Sony XVS operation manual classifies 224–247, 250–257 and 260–269 as
mosaic patterns with horizontal/vertical tile counts. It does not provide
a textual traversal algorithm. Their pictograms are insufficient to establish
exact dual-path timing, so they remain unimplemented.

Karaoke 220–223 have Start, Row No and Phase: Phase spans simultaneous rows
to starting each row after the preceding row completes. They remain pending
a separate preparation model rather than borrowing the snake algorithm.
Reference: https://pro.sony/support/res/manuals/5013/ac0fcd23e79e1fe9ad1e3707c87879a1/50135021M.pdf (printed p.143).
