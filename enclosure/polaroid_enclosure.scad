// Polaroid enclosure — 2-part (front bezel + rear tray), screwed together.
//
// PORTRAIT LAYOUT: the panel is natively 400x600 portrait, so unlike Forager
// there's no rotation to reason about. Case-X is width, case-Y is height, and
// case-Y runs along the panel's long 101mm edge.
//
// Hardware (vendor spec, waveshare.com/wiki/4inch_e-Paper_HAT+_(E)_Manual):
//   - Driver board (HAT+):  101.0 x 68.0mm      <- sets the case footprint
//   - Raw panel glass:       99.0 x 66.0 x 0.85mm
//   - Active area:           84.6 x 56.40mm
//   - MCU: Seeed XIAO ESP32S3, 21 x 17.5mm
//   - Battery: Adafruit #2011 LiPo, 60 x 36 x 7mm
//   - Accelerometer: Adafruit LIS3DH breakout, 25 x 19mm
//   - Magnet: one 32mm dia x 3mm neodymium disc, adhesive-backed
//
// WHY THERE IS NO PHYSICAL POLAROID CHIN.
//
// The obvious move is to make the case a Polaroid shape — square window up
// top, deep opaque chin below. It doesn't work here, and it's worth writing
// down why so nobody re-litigates it:
//
//   The driver board is 101mm tall and sets the minimum case height. A
//   physical chin has to be ADDED below that, pushing the case past 130mm at
//   ~87mm wide — a 1:1.5 aspect. A real Polaroid 600 print is 1:1.216. So the
//   physical chin makes the object LESS Polaroid-shaped, not more, and hides
//   ~24% of a $45 panel behind opaque plastic to do it.
//
//   The frame is drawn in ink instead (see api/polaroid/frame.ts): a 3.1mm
//   white border, a 50.2mm square image, and a 28.2mm chin, all inside the
//   84.6 x 56.4mm active area. Image-to-width there is 0.890 against a real
//   Polaroid's 0.898 — within a percent, using every pixel.
//
// SIZE: 73.6 x 122.6 x 21.5mm, against a board that is 68 x 101. That is
// 2.8mm of bezel either side of the board and 8.6mm either side of the visible
// image — about as tight as a 68mm-wide board allows. See the screw note below
// for how the side margin got down there.
//
// PRINT IT IN A DARK COLOUR. E-ink white is bone (~78% reflectance); a white
// bezel around it reads as one big dingy slab and the Polaroid shape vanishes.
// Dark grey, black or a wood-fill makes the white print pop.
//
// SCREWS, NOT CLIPS: this is the one enclosure in the house that gets
// deliberately shaken. Snap-fit would work loose.
//
// Render one part at a time:
//   openscad -D part=\"bezel\" -o stl/front_bezel.stl polaroid_enclosure.scad
//   openscad -D part=\"tray\"  -o stl/rear_tray.stl   polaroid_enclosure.scad
part = "both"; // "bezel" | "tray" | "both" (both = preview only, side by side)

$fn = 64;

// ---- General ----
wall      = 1.5;   // perimeter wall thickness
clearance = 0.3;   // general fit clearance around boards
corner_r  = 3.0;   // rounded exterior corners — a real Polaroid's are nearly square

// ---- Side-entry screws ----
// Sized generously after the Forager print, where the screws did not bite.
// Three things were wrong there and all three are fixed here:
//
//   1. flange_w was the entire thread engagement, and 3.2mm is about two
//      threads of an M2 self-tapper. 6.0mm is four or five — the difference
//      between gripping and stripping the hole on first assembly.
//   2. side_boss_od was 4.5mm around a 2.0mm pilot, leaving a 1.25mm wall
//      that splits when the screw taps itself in. 7.0mm leaves 2.5mm.
//   3. Nothing checked that the flange still existed at the height the screw
//      arrives at. The assertions below do; one of them caught a 2.5mm
//      overhang while this file was being written.
//
// SCREWS LIVE IN THE TOP AND BOTTOM RIMS, NOT THE SIDE MARGINS.
//
// This is what lets the flanges get longer and the side bezels get thinner at
// the same time, which otherwise trade directly against each other: a flange
// sitting in the side margin forces that margin to be flange_inset + flange_w
// + a board clearance wide, so every millimetre of thread engagement cost a
// millimetre of bezel on both sides. Putting the screws at Y positions above
// and below the board means the flange can reach as far inboard as it likes —
// there is no board at that Y to collide with. Side margin drops from 10.0mm
// to 2.5mm and the flange grows anyway.
side_boss_od      = 7.0;
side_pilot_d      = 2.0;   // M2 self-tapping into plastic
side_clear_d      = 2.9;   // shaft passes freely through the tray wall
flange_w          = 6.0;   // thread engagement depth
side_flange_depth = 12.0;  // how far the flange reaches into the tray cavity
flange_inset      = wall;  // flush against the tray's wall

// ---- Display assembly ----
// The BOARD, not the glass, sets the footprint: it's 2mm bigger in both axes.
board_w = 68.0;
board_h = 101.0;
// ASSUMPTION: PCB + components + FPC connector with the Pi headers removed.
// Measure yours before printing — this is the single number most likely to be
// wrong, and it sets the total device thickness.
board_t = 6.0;

glass_w = 66.0;
glass_h = 99.0;
glass_t = 0.85;    // the glass is genuinely this thin. Handle it accordingly.

active_w = 56.40;
active_h = 84.60;

// ASSUMPTION: the active area is centred on the glass, and the glass centred
// on the board. Vendor drawings don't give the offsets. If your panel sits
// off-centre, shift it with active_offset_* rather than moving the pocket.
active_offset_x = 0;
active_offset_y = 0;

// The bezel laps 0.5mm over the active area on every side. Sizing the window
// to exactly active_w/h would expose the glass's inactive margin as a visible
// grey band whenever the panel is showing white paper.
win_overlap = 0.5;
win_w = active_w - 2 * win_overlap;
win_h = active_h - 2 * win_overlap;

// ASSUMPTION: the FPC exits the glass on the case's BOTTOM edge and folds back
// to the board's connector. This is the gap it folds into. Verify which edge
// yours is on before printing — on the wrong edge it crushes the ribbon.
fpc_fold_gap = 2.5;

// ---- MCU: Seeed XIAO ESP32S3 ----
mcu_w = 21.0;
mcu_l = 17.5;
mcu_component_h = 4.0;  // USB-C shell is the tall part

// ---- Battery: Adafruit #2011, 2000mAh ----
batt_w = 60.0;
batt_h = 36.0;
batt_t = 7.0;
batt_puff_clearance = 1.5;  // LiPo pouches swell over their life

// ---- Accelerometer: Adafruit LIS3DH breakout ----
accel_w = 25.0;
accel_l = 19.0;
accel_h = 3.0;

// ---- Magnet ----
// One 32mm disc, dead centre. A single 32mm N35 holds far more than this
// ~150g object needs, so the count is a geometry decision, not a strength one.
//
// Centred is the only sane place for a single magnet: anywhere else and the
// case hangs off-axis, rotating until its centre of mass swings under the
// magnet. Dead centre, gravity has no lever arm and the frame hangs square.
//
// The pocket exists so the magnet sits FLUSH with the back rather than
// standing 3mm proud — without it the case pivots on a single bump and never
// sits flat against the door. Depth is 0.2mm shy of the magnet thickness so
// the face still makes real contact with the steel.
magnet_d = 32.0;
magnet_t = 3.0;
magnet_pocket_d = magnet_d + 0.6;
magnet_pocket_depth = magnet_t - 0.2;
magnet_back_t = 0.8;    // material left behind the magnet — thinner is
                        // both slimmer and a stronger hold on the steel

// The floor swallows the pocket entirely, so nothing intrudes into the cavity
// and component placement stays unconstrained. Costs ~1.8mm of thickness.
tray_floor_t = magnet_pocket_depth + magnet_back_t;  // = 3.6

// ---- USB-C access ----
usbc_slot_w = 9.5;
usbc_slot_h = 4.0;

// ---- Derived footprint ----
pocket_w = board_w + 2 * clearance;
pocket_h = board_h + 2 * clearance + fpc_fold_gap;

// Side margin is now only wall + a hair, since nothing lives out there.
pocket_x_inset = wall + 1.0;   // 2.5

// Rims above and below the board, sized to just contain a screw boss clear of
// the rounded corner. This is the one place the case is bigger than the board
// needs, and it buys the thin side bezels.
side_screw_edge_clearance = corner_r + side_clear_d / 2 + 0.3;
rim = side_screw_edge_clearance + side_boss_od / 2 + 1.0;   // ~9.2

outer_w = pocket_w + 2 * pocket_x_inset;   // ~73.6
outer_h = pocket_h + 2 * rim;              // ~122.5

bezel_front_t = 2.2;
glass_pocket_w = glass_w + 2 * clearance;
glass_pocket_h = glass_h + 2 * clearance;
glass_pocket_d = 1.0;   // shallow recess that locates the 0.85mm glass

// Locator brackets on the bezel's back face, so a device that gets shaken on
// purpose doesn't rattle its panel loose.
//
// Four L-shaped corner brackets rather than a full ring: the FPC leaves the
// middle of the bottom edge and a continuous rib would pinch it. Corners also
// constrain both axes at once, which four straight bars would not.
//
// rib_t is capped at pocket_x_inset - wall so the bracket meets the tray's
// inner wall face instead of trying to occupy the same space as it.
rib_t = pocket_x_inset - wall;      // 1.0
rib_arm = 15.0;
rib_h = glass_t + board_t;

// The bezel is a PLATE plus flanges and ribs — there is no deep pocket in it.
// So the whole display stack (glass + board) lives in the tray's cavity, on
// top of the battery layer. That is a SUM, not a max: the board spans the
// entire footprint, so nothing can sit beside it.
component_h = max(batt_t + batt_puff_clearance, mcu_component_h, accel_h);
tray_interior_depth = glass_t + board_t + component_h + clearance;
tray_wall_h = tray_interior_depth + tray_floor_t;

total_thickness = bezel_front_t + tray_wall_h;

// Screw centres, one pair in each rim, pushed as far toward the corners as the
// rim allows for the widest spread against a shake. Derived FROM the rim, not
// independently of it — the first cut of this had them defined separately and
// the boss overhung into the board by 2.5mm.
side_y_lo = rim - side_boss_od / 2 - 0.5;
side_y_hi = outer_h - side_y_lo;
side_ys = [side_y_lo, side_y_hi];

// Screw height. The flange hangs from the bezel plate's back face down into
// the tray, so both parts must measure to that shared mating plane or the
// screw passes under the flange and grabs nothing — which is what "the screws
// didn't fully work" looks like in practice.
side_boss_z_bezel = bezel_front_t + side_flange_depth / 2;
side_boss_z_tray  = tray_wall_h - side_flange_depth / 2;

// The flange must reach past the mating plane far enough for the screw to be
// inside it, and must not punch through the tray floor.
assert(side_flange_depth < tray_interior_depth,
       "flange would hit the tray floor — reduce side_flange_depth");
assert(side_boss_z_tray > tray_floor_t,
       "screw would enter below the cavity — flange is too short to reach");
assert(side_y_lo >= side_screw_edge_clearance,
       "screw hole is drilled into the rounded corner, not through flat wall");
// The flanges reach inboard past the side margin, which is only safe because
// they sit at Y values outside the board. Assert exactly that.
assert(side_y_lo + side_boss_od / 2 <= rim,
       "bottom screw boss reaches into the display board");
assert(side_y_hi - side_boss_od / 2 >= outer_h - rim,
       "top screw boss reaches into the display board");

module rounded_rect(w, h, r) {
    hull() {
        translate([r, r]) circle(r = r);
        translate([w - r, r]) circle(r = r);
        translate([r, h - r]) circle(r = r);
        translate([w - r, h - r]) circle(r = r);
    }
}

module front_bezel() {
    pocket_x = pocket_x_inset;
    pocket_y = rim;

    // Glass centred on the board pocket in X; in Y it sits above the FPC fold
    // gap, which is at the pocket's low edge.
    glass_x = pocket_x + (pocket_w - glass_pocket_w) / 2;
    glass_y = pocket_y + fpc_fold_gap + (pocket_h - fpc_fold_gap - glass_pocket_h) / 2;

    win_x = glass_x + (glass_pocket_w - active_w) / 2 + win_overlap + active_offset_x;
    win_y = glass_y + (glass_pocket_h - active_h) / 2 + win_overlap + active_offset_y;

    difference() {
        union() {
            linear_extrude(height = bezel_front_t)
                rounded_rect(outer_w, outer_h, corner_r);

            // Screw flanges — four, reaching into the tray cavity so the
            // horizontal screws have real material to thread into.
            for (sy = side_ys) {
                translate([flange_inset, sy - side_boss_od / 2, bezel_front_t - 0.01])
                    cube([flange_w, side_boss_od, side_flange_depth]);
                translate([outer_w - flange_inset - flange_w, sy - side_boss_od / 2,
                           bezel_front_t - 0.01])
                    cube([flange_w, side_boss_od, side_flange_depth]);
            }

            // Board locator brackets, one L at each corner of the board.
            board_y0 = pocket_y + fpc_fold_gap;
            board_y1 = pocket_y + pocket_h;
            // Both arms are anchored on the corner square they share. Butting
            // them edge-to-edge instead leaves a degenerate contact that makes
            // the whole bezel non-manifold, with no visible symptom until a
            // slicer refuses it.
            for (cx = [0, 1]) {
                for (cy = [0, 1]) {
                    xa = cx == 0 ? pocket_x - rib_t : pocket_x + pocket_w;
                    ya = cy == 0 ? board_y0 - rib_t : board_y1;
                    vy = cy == 0 ? ya : ya + rib_t - rib_arm;
                    hx = cx == 0 ? xa : xa + rib_t - rib_arm;
                    translate([0, 0, bezel_front_t - 0.01]) {
                        translate([xa, vy, 0]) cube([rib_t, rib_arm, rib_h]);
                        translate([hx, ya, 0]) cube([rib_arm, rib_t, rib_h]);
                    }
                }
            }
        }

        // Viewing window, through the front face.
        translate([win_x, win_y, -0.1])
            cube([win_w, win_h, bezel_front_t + 0.2]);

        // Glass recess, cut into the back of the plate. Leaves
        // bezel_front_t - glass_pocket_d = 1.4mm of face around the window.
        translate([glass_x, glass_y, bezel_front_t - glass_pocket_d])
            cube([glass_pocket_w, glass_pocket_h, glass_pocket_d + 0.01]);

        // Screw pilot holes through the flanges (self-tapping).
        for (sy = side_ys) {
            translate([flange_inset - 0.1, sy, side_boss_z_bezel])
                rotate([0, 90, 0]) cylinder(d = side_pilot_d, h = flange_w + 0.2);
            translate([outer_w - flange_inset - flange_w - 0.1, sy, side_boss_z_bezel])
                rotate([0, 90, 0]) cylinder(d = side_pilot_d, h = flange_w + 0.2);
        }
    }
}

module rear_tray() {
    // MCU against the bottom wall so its USB-C lines up with the access slot.
    mcu_fp_x = mcu_w;
    mcu_fp_y = mcu_l;
    usbc_x_center = outer_w / 2;
    mcu_x = usbc_x_center - mcu_fp_x / 2;
    mcu_y = wall + 3;

    // Battery above the MCU, against the case-X low wall. Rotated 90deg from
    // its native labels so its long edge runs along case-Y.
    batt_x = wall + 3;
    batt_y = mcu_y + mcu_fp_y + 4.0;

    // Accelerometer beside the battery. Orientation matters more than
    // position: mount it so its X axis lies in the plane of the fridge door,
    // which is the axis both a shake and a door swing act along, and the one
    // the thresholds in firmware/include/Config.h are tuned against.
    accel_x = batt_x + batt_h + 4.0;
    accel_y = batt_y + 10.0;

    usbc_slot_z = tray_floor_t + 1.0;

    difference() {
        union() {
            linear_extrude(height = tray_floor_t)
                rounded_rect(outer_w, outer_h, corner_r);

            // Perimeter wall, overlapping 0.01mm into the floor to avoid a
            // coincident face producing a degenerate shell.
            translate([0, 0, tray_floor_t - 0.01])
                difference() {
                    linear_extrude(height = tray_interior_depth + 0.01)
                        rounded_rect(outer_w, outer_h, corner_r);
                    translate([wall, wall, -0.1])
                        linear_extrude(height = tray_interior_depth + 0.2)
                            rounded_rect(outer_w - 2 * wall, outer_h - 2 * wall,
                                         max(corner_r - wall, 0.5));
                }
        }

        // Magnet pocket, dead centre, opening onto the exterior back face.
        //
        // PRINTING: the pocket ceiling is a 32mm circular bridge. Most
        // printers manage it and the surface is invisible under a magnet
        // either way. If yours sags, pause at Z = magnet_pocket_depth and drop
        // the magnet in — it's adhesive-backed, so it will stay put.
        translate([outer_w / 2, outer_h / 2, -0.1])
            cylinder(d = magnet_pocket_d, h = magnet_pocket_depth + 0.1);

        // USB-C access, through the bottom wall.
        translate([usbc_x_center - usbc_slot_w / 2, -0.1, usbc_slot_z])
            cube([usbc_slot_w, wall + 0.2, usbc_slot_h]);

        // Screw clearance holes, through the tray wall into the bezel flanges.
        for (sy = side_ys) {
            translate([-0.1, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = side_clear_d, h = flange_inset + flange_w + 0.2);
            translate([outer_w - flange_inset - flange_w - 0.1, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = side_clear_d, h = flange_inset + flange_w + 0.2);
        }

        // "Polaroid" — indented on the exterior back, clear of the magnet.
        // mirror() flips it to read correctly from outside; model coordinates
        // are the inside view.
        translate([outer_w / 2, 9.0, -0.1])
            mirror([1, 0, 0])
                linear_extrude(height = 0.8)
                    text("Polaroid", size = 5.0, font = "Noteworthy:style=Bold",
                         halign = "center", valign = "center");
    }

    // Battery bay: a shallow retaining lip, held by friction and tape. Not a
    // closed box — a swollen pouch needs somewhere to go.
    batt_wall_margin = 0.5;
    batt_fit_clearance = 2.0;
    batt_wall_h = 4.0;
    batt_wire_gap_w = 8.0;
    translate([batt_x - batt_wall_margin - batt_fit_clearance / 2,
               batt_y - batt_wall_margin - batt_fit_clearance / 2, tray_floor_t - 0.01]) {
        difference() {
            cube([batt_h + 2 * batt_wall_margin + batt_fit_clearance,
                  batt_w + 2 * batt_wall_margin + batt_fit_clearance, batt_wall_h + 0.01]);
            translate([batt_wall_margin, batt_wall_margin, -0.1])
                cube([batt_h + batt_fit_clearance, batt_w + batt_fit_clearance,
                      batt_wall_h + 0.5]);
            // Wire exit toward the MCU, at the bay's low-Y corner.
            translate([batt_wall_margin + batt_h + batt_fit_clearance - 0.1, 0, -0.1])
                cube([batt_wall_margin + 0.2, batt_wire_gap_w, batt_wall_h + 0.5]);
        }
    }
}

echo(str("outer: ", outer_w, " x ", outer_h, " x ", total_thickness, " mm"));

if (part == "bezel") {
    front_bezel();
} else if (part == "tray") {
    rear_tray();
} else {
    front_bezel();
    translate([outer_w + 20, 0, 0]) rear_tray();
}
