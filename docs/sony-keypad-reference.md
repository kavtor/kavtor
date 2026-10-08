# Sony dual-function keypad reference

Research only; no new control assignment or implementation. Reviewed 2026-10-07.
The DVS-7250 BZS-7061A operating manual uses the same dual labels as the panel:
+/− / EFF DIS, CLR / AUTO TRANS and TRIM / XPT DSBL. The DVS-9000/XVS manuals
corroborate the numerical and snapshot operations, although some generations
replace XPT DSBL with GPI ENBL on that key. Do not transfer that later label to
the BKDS-2010.

## Numerical editing

- **+/−** toggles the sign of the entered numeric value.
- **TRIM** confirms relative entry: it adds the entered value to the current
  parameter, rather than replacing that parameter as ENTER does. A signed delta
  allows subtraction. Example: current 300, entry −20, TRIM gives 280.

CLEAR was not part of the research request.

## Snapshot attributes

When the snapshot mode is selected, the same keys set attributes attached to
saving/recalling a snapshot. Their second labels are context dependent, not
ordinary numerical operations.

- **EFF DISS** requests a smooth change from the existing settings to recalled
  snapshot settings. The DVS-7250 manual specifies the transition rate stored
  with the snapshot. This is distinct from simply selecting MIX between two bus
  sources; interpolation of the recalled state is the underlying idea.
- **AUTO TRANS** automatically starts a transition after the snapshot is
  recalled. The keypad key sets that attribute; it is not necessarily a duplicate
  of the immediate AUTO TRANS execution key in the transition block.
- **XPT DSBL** recalls settings while retaining current crosspoint selections.
  It protects current signal selections from the recalled snapshot; it does not
  disable an input or lock an individual physical source-selection key. The
  FlexiPad description explicitly mentions background buses A/B, while the
  numeric keypad register tables also allow the attribute for an AUX subregister.
  KEY DISABLE is a different attribute for preserving key settings.

The manual allows these attributes to be enabled/removed while saving or recalling
registers; scope depends on the selected subregister. No generic snapshot/macro
engine with these exact semantics is implemented in kavtor/faderOS yet. Define
ownership, allowed scopes, interpolation and on-air confirmation before adopting
these labels in our workflow.

## Primary references

- Sony DVS-7250 BZS-7061A User's Guide, printed pages **2-26**, **7-4** and
  **7-7–7-9** (PDF pages 56, 278 and 281–283, one-based):
  https://pro.sony/s3/cms-static-content/operation-manual/aae0600011.pdf
- Same manual, FlexiPad description, printed **2-11**: XPT DISABLE preserves
  background A/B selections independently from KEY DISABLE.
- Sony DVS-9000/9000SF System User's Guide, Numeric Keypad and Snapshot sections:
  https://pro.sony/s3/cms-static-content/operation-manual/3704674111.pdf
- Sony XVS User's Guide, printed **44**, **341** and following snapshot section:
  https://pro.sony/support/res/manuals/5013/ac0fcd23e79e1fe9ad1e3707c87879a1/50135021M.pdf
