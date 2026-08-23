# Enclosure

Two printed parts — front bezel and rear tray — held together by four M2 self-tapping screws that
enter horizontally through the tray's side walls into flanges on the bezel.

**71.6 × 130.6 × 26.8 mm.**

```bash
./render.sh          # regenerates every STL and preview from the .scad
```

The checked-in `stl/` and `preview/` files are outputs. Run `render.sh` after any change.

## Before you print

**Measure `board_t`.** 7.0 mm, calipered on the real board with the Pi headers removed. It sets the
total thickness directly, and the vendor doesn't give it.

**Check which edge the FPC leaves on.** `fpc_fold_gap` puts a 2.5 mm gap at the case's bottom edge
for the ribbon to fold into. On the wrong edge, assembly crushes it.

**Print the bezel in a dark colour.** E-ink white is bone, around 78% reflectance. A white bezel
around it reads as one big dingy slab and the Polaroid shape drawn on the panel disappears. Dark
grey, black, or a wood-fill makes the white pop.

## Notes

**Screws, not clips.** This is the one enclosure here that gets deliberately shaken.

**Screws live in the top and bottom rims, not the side margins.** That's what let the flanges get
longer *and* the side bezels get thinner — otherwise those trade directly against each other, since
a flange in the side margin forces that margin to be `flange_inset + flange_w + clearance` wide.
Side bezel is 1.5 mm of plastic beside the board — the wall, and nothing else — and 8.1 mm beside
the visible image. The rims are 8.25 mm, which is the screw head clearing the corner radius plus the
boss behind it, and nothing more.

**One magnet, dead centre.** Anywhere else and the case rotates until its centre of mass hangs under
the magnet. The pocket recesses it flush so the case sits flat instead of pivoting on a bump.
The pocket swallows it with 0.8 mm to spare, so the hold comes through a thin skin rather than the
magnet's own face — a magnet standing even 0.2 mm proud is what the case then rocks on. Its ceiling
is a 29 mm circular bridge; if your printer sags there, pause at that layer and drop the magnet in —
it's adhesive-backed.

**The chin is 10 mm, and it is there for fit.** The photo still runs full-bleed across the whole
panel — nothing is drawn on it and nothing hides behind it. The parts simply do not fit under a
board that spans the entire pocket, and the chin is the floor they needed. It happens to be what an
instant print looks like anyway.

**The assertions are load-bearing.** They check that the screw flanges exist where the screws
arrive, that the head's counterbore lands on flat wall rather than the corner radius, and that
neither reaches the display board. One caught a 2.5 mm overhang while this file was being written;
that class of error is invisible until something is printed and assembled.

**`render.sh` also intersects the two parts.** The assertions compare numbers to numbers and
structurally cannot see two solids sharing space. That is what let the bezel's square flange corners
sit 0.67 mm inside the tray's rounded interior corners, down all 12 mm of flange — which held the
bezel proud at four points and bowed its long sides when the screws pulled the ends down. The
intersection must come out empty; `fit_check.scad` is the model, and the build fails on anything
else.
