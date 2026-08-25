// Interference check: the two parts, assembled, must not share any volume.
// Renders empty when they don't. render.sh fails on anything else.
//
// This is the one class of error the assertions in polaroid_enclosure.scad
// cannot see — they check numbers against each other, not solids against
// solids. It caught square flange corners buried 0.67mm in the tray's rounded
// interior corners, which held the bezel proud and bowed its long sides.
use <polaroid_enclosure.scad>

// The parts are drawn in one XY frame and mate by flipping Z about the plane
// where the bezel's back face meets the tray's rim.
mate_z = total_thickness_mm();

intersection() {
    translate([0, 0, mate_z]) mirror([0, 0, 1]) front_bezel();
    rear_tray();
}
