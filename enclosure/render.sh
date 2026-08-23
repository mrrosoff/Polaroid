#!/usr/bin/env bash
# Regenerates every STL and preview PNG from polaroid_enclosure.scad.
# Run after any change to the .scad -- the checked-in artifacts are outputs.
set -euo pipefail
cd "$(dirname "$0")"

SCAD=polaroid_enclosure.scad
IMG="--imgsize=1600,1200 --colorscheme=Tomorrow --viewall --autocenter"

echo "STLs..."
openscad -D 'part="bezel"' -o stl/front_bezel.stl "$SCAD"
openscad -D 'part="tray"'  -o stl/rear_tray.stl   "$SCAD"

echo "fit check..."
# An empty intersection is the pass. OpenSCAD refuses to export a file at all
# when the result is empty, so "no output" and "no interference" are the same
# thing here, and its non-zero exit in that case is expected.
FIT=/tmp/polaroid_fit.stl
rm -f "$FIT"
openscad -o "$FIT" fit_check.scad >/dev/null 2>&1 || true
if [ -s "$FIT" ]; then
    echo "FAIL: bezel and tray share volume — render fit_check.scad to see where" >&2
    exit 1
fi

echo "previews..."
# camera=tx,ty,tz,rot_x,rot_y,rot_z,dist  (dist ignored under --viewall)
openscad -D 'part="bezel"' $IMG --camera=0,0,0,0,0,0,0    -o preview/01_bezel_front.png    "$SCAD"
openscad -D 'part="bezel"' $IMG --camera=0,0,0,55,0,25,0  -o preview/02_bezel_iso.png      "$SCAD"
openscad -D 'part="tray"'  $IMG --camera=0,0,0,55,0,25,0  -o preview/03_tray_interior.png  "$SCAD"
openscad -D 'part="tray"'  $IMG --camera=0,0,0,0,180,0,0  -o preview/04_tray_back.png      "$SCAD"
openscad -D 'part="both"'  $IMG --camera=0,0,0,55,0,25,0  -o preview/05_assembled.png      "$SCAD"
openscad -D 'part="tray"'  $IMG --camera=0,0,0,90,0,90,0  -o preview/06_side_profile.png   "$SCAD"

echo "done"
