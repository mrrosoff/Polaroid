# Enclosure

Two printed parts — front bezel and rear tray — held together by four M2 self-tapping screws that
enter horizontally through the tray's side walls into flanges on the bezel.

**73.6 × 122.6 × 21.5 mm.**

```bash
./render.sh          # regenerates every STL and preview from the .scad
```

The checked-in `stl/` and `preview/` files are outputs. Run `render.sh` after any change.

## Before you print

**Measure `board_t`.** It's an assumption (6.0 mm — the HAT+ driver board's PCB plus components with
the Pi headers removed) and it sets the total thickness directly. Everything else is vendor spec.

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
Side bezel is 2.8 mm of plastic beside the board, 8.6 mm beside the visible image.

**One magnet, dead centre.** Anywhere else and the case rotates until its centre of mass hangs under
the magnet. The pocket recesses it flush so the case sits flat instead of pivoting on a bump.
Its ceiling is a 32 mm circular bridge; if your printer sags there, pause at that layer and drop the
magnet in — it's adhesive-backed.

**There is no physical Polaroid chin, on purpose.** The 101 mm driver board sets the minimum height,
so a chin has to be *added* below it, which pushes the case to a 1:1.5 aspect — further from a real
Polaroid's 1:1.216 than the plain bezel is — and hides a quarter of the panel to do it. The frame is
drawn in ink instead, at 0.890 image-to-width against a real print's 0.898. The long comment at the
top of the `.scad` has the full argument.

**The assertions are load-bearing.** Three of them check that the screw flanges actually exist where
the screws arrive and don't overlap the display board. One caught a 2.5 mm overhang while this file
was being written; that class of error is invisible until something is printed and assembled.
