# Sony pictogram review workflow

Review catalogue entries in numerical order with the operator. Use the original
manual pictogram for geometry and the operator description for temporal intent.
The corresponding 20×16 asset is for the panel LCD, not a sufficient temporal
specification. Do not infer unspecified stages, timing or secondary inputs from
an ambiguous still drawing.

For each entry record family (WIPE mask / DME video transform / other), incoming
and outgoing roles, start/end geometry, direction and reverse behaviour, expected
modifiers, and any intermediate/holding state. Compare an actual captured native
render before marking it validated. A disagreement or insufficient reference
keeps that entry pending. Existing implemented entries are still subject to this
operator review; reference inventory membership alone is not approval.

The operator has supplied originals and LCD reductions in the local Sony icon
asset directory. The initial inspection found 401 PNG files in each collection;
`1.png` has an original 62×50 raster and a 20×16 reduction. Start with number 1
and await its description before changing the interpretation. No icon library is
embedded in the firmware by this preparation step.

The operator subsequently authorized provisional implementation of every remaining
WIPE on 2026-10-10, followed by review. See sony-wipe-interpretations.json;
implementation membership never implies morphological approval.
