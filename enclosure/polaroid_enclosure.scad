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
//   - Magnet: one 29mm dia x 3mm neodymium disc, adhesive-backed
//
// NOTHING IS DRAWN ON THE PANEL AND NOTHING HIDES BEHIND IT.
//
// The photo runs full-bleed across all 400x600. The 10mm chin below the board
// is not a styling decision and does not cover any of the glass: the parts do
// not fit under a board that spans the whole pocket, and the chin is the floor
// they needed. It is what an instant print looks like anyway, which is luck,
// not intent. A chin added for looks would have been the wrong call — it would
// have to hide a quarter of a $45 panel to earn its aspect ratio.
//
// SIZE: 71.6 x 130.6 x 26.8mm, against a board that is 68 x 101. That is
// 1.5mm of bezel either side of the board — the wall, and nothing else — and
// 8.1mm either side of the visible image. Above and below the board it is
// 8.25mm of rim, which is the screw and nothing else. See the screw note below
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
// Rounded exterior corners — a real Polaroid's are nearly square. 2.8 rather
// than 3.0 because the corner and the screw head compete for the same end of
// the case: the head has to seat on flat wall outboard of the radius, so every
// 0.1 of radius is 0.1 further in the screw sits, and 0.1 more rim behind it.
corner_r  = 2.8;

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
// to a bare wall and the flange grows anyway.
// 6.3 around a 2.0mm pilot is a 2.15mm wall — still well clear of the 1.25mm
// that split on the Forager print, and the 0.35mm it gives up off the radius
// is what lets the rim come in a full millimetre at each end of the case.
side_boss_od      = 6.3;
side_pilot_d      = 2.0;   // M2 self-tapping into plastic
side_clear_d      = 2.9;   // shaft passes freely through the tray wall
// Flat-top heads, sunk flush in a counterbore on the outside of the tray.
// A 1.6mm head into a 1.5mm wall would leave nothing for it to pull against,
// so the wall is thickened locally instead: a pad on the INSIDE at each screw,
// which the bezel's flange then butts against rather than the wall. Local
// thickness is wall + screw_pad_t = 3.0, so a flush head still bears on 1.4mm.
head_clear_d      = 4.2;   // 3.8mm M2 pan/cheese head plus fit
head_recess_t     = 1.6;   // full head height — the head disappears
screw_pad_t       = 1.5;
flange_w          = 6.0;   // thread engagement depth
side_flange_depth = 12.0;  // how far the flange reaches into the tray cavity
// Clear of the screw pad's inner face, not of the wall's.
flange_inset      = wall + screw_pad_t + clearance;   // 3.3

assert(head_recess_t <= wall + screw_pad_t - 1.2,
       "counterbore leaves under 1.2mm of wall — the head will pull through");

// ---- Display assembly ----
// The BOARD, not the glass, sets the footprint: it's 2mm bigger in both axes.
board_w = 68.0;
board_h = 101.0;
// Measured with calipers on the real board, headers removed. Was assumed at
// 6.0, which would have left the case 1mm short of closing.
board_t = 7.0;

glass_w = 66.0;
glass_h = 99.0;
glass_t = 0.85;    // the glass is genuinely this thin. Handle it accordingly.

active_w = 56.40;
active_h = 84.60;

// ASSUMPTION: the active area is centred on the glass, and the glass centred
// on the board. Vendor drawings don't give the offsets. If your panel sits
// off-centre, shift it with active_offset_* rather than moving the pocket.
// Measured against the printed bezel: the board sits where it should, but
// the visible image lands 1.8mm above the window.
active_offset_x = 0;
active_offset_y = -1.8;

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
batt_puff_clearance = 2.0;  // Same cell as Forager, same allowance: its
                            // 2.0 came off a real print, this was a guess.

// ---- Accelerometer: Adafruit LIS3DH breakout ----
accel_w = 25.0;
accel_l = 19.0;
accel_h = 3.0;

// ---- Magnet ----
// One 29mm disc, dead centre. A single 29mm N35 holds far more than this
// ~150g object needs, so the count is a geometry decision, not a strength one.
//
// Centred is the only sane place for a single magnet: anywhere else and the
// case hangs off-axis, rotating until its centre of mass swings under the
// magnet. Dead centre, gravity has no lever arm and the frame hangs square.
//
// The pocket exists so the magnet sits below the back face rather than
// standing 3mm proud — without it the case pivots on a single bump and never
// sits flat against the door. The first cut left it 0.2mm proud on purpose,
// to put the magnet face against the steel; printed, that 0.2mm is what the
// case rocks on, so the pocket now swallows the magnet with 0.8mm to spare
// and the hold comes through a thin skin instead.
magnet_d = 29.0;
magnet_t = 3.0;
magnet_pocket_d = magnet_d + 0.6;
magnet_pocket_depth = magnet_t + 0.8;
magnet_back_t = 1.8;    // material left behind the magnet. 0.8 was thin
                        // enough to feel like a membrane where the pocket
                        // hollows the floor out; this is still a strong hold

// The floor swallows the pocket entirely, so nothing intrudes into the cavity
// and component placement stays unconstrained. Costs ~3.8mm of thickness.
tray_floor_t = magnet_pocket_depth + magnet_back_t;  // = 5.6

// ---- USB-C access ----
usbc_slot_w = 9.5;
usbc_slot_h = 4.0;

// ---- Derived footprint ----
pocket_w = board_w + 2 * clearance;
pocket_h = board_h + 2 * clearance + fpc_fold_gap;

// Side margin is the wall and nothing else. The hair that used to sit inside
// it existed only to give the locator brackets somewhere to stand; with the
// side arms gone (see rib_arm) the pocket IS the tray's interior, and the
// board is located in X by the wall itself, to its 0.3mm clearance — tighter
// than the 1.0mm brackets managed.
pocket_x_inset = wall;   // 1.5

// Rims above and below the board, sized to just contain a screw boss clear of
// the rounded corner. This is the one place the case is bigger than the board
// needs, and it buys the thin side bezels.
// Screw centre, in from the end of the case. The counterbore has to land on
// flat wall, so the head is what sets this: any closer to the corner and it
// seats on a sliver of the rounded corner instead of the flat. The 0.1 is not
// decoration — at exact equality the assertion below fails on floating point.
side_y_lo = corner_r + head_clear_d / 2 + 0.1;   // 5.0
// The rim is exactly what it takes to hold that screw's boss clear of the
// board, and nothing more. It used to run the other way — a rim with a
// millimetre of slack in it, and the screw pushed as far toward the corner as
// that allowed — which left the slack sitting in the rim at both ends of the
// case. Deriving the rim from the screw instead of the screw from the rim is
// worth 2mm of height, and neither version can put the boss over the board.
rim = side_y_lo + side_boss_od / 2 + 0.1;  // 8.25

// Extra height below the board pocket. The parts do not fit in the footprint
// the board alone gives: battery, MCU, accelerometer and the harness all live
// in one layer under a board that spans the whole pocket, and they ran out of
// floor. This buys that floor back, and it happens to be the chin an instant
// print has anyway.
chin = 10.0;

outer_w = pocket_w + 2 * pocket_x_inset;   // ~73.6
outer_h = pocket_h + 2 * rim + chin;

bezel_front_t = 2.2;
glass_pocket_w = glass_w + 2 * clearance;
glass_pocket_h = glass_h + 2 * clearance;
glass_pocket_d = 1.0;   // shallow recess that locates the 0.85mm glass

// Locator brackets on the bezel's back face, so a device that gets shaken on
// purpose doesn't rattle its panel loose.
//
// Four bars, at the ends of the board's top and bottom edges. They were Ls,
// with a second arm down each side holding X — but the side arms could only
// stand in the side margin, so every one of those millimetres cost two off
// the case's width. Narrowing the pocket to the board plus its clearance
// hands X to the tray wall and gets the width back; only Y still needs
// holding, and only these bars hold it.
//
// Bars at the ends rather than one rib per edge: the FPC leaves the middle of
// the bottom edge and a continuous rib would pinch it.
rib_t = 1.0;
rib_arm = 15.0;
rib_h = glass_t + board_t;

// The bezel is a PLATE plus flanges and ribs — there is no deep pocket in it.
// So the whole display stack (glass + board) lives in the tray's cavity, on
// top of the battery layer. That is a SUM, not a max: the board spans the
// entire footprint, so nothing can sit beside it.
component_h = max(batt_t + batt_puff_clearance, mcu_component_h, accel_h);

// The cavity, floor excluded — 28.0mm of stack was the parts standing on the
// curve their wires were bent into, and dressed flat against the floor they
// pack down to this. The assertion is what keeps it honest: the parts list
// still sets the floor, so a taller battery or a thicker board fails the
// render rather than the print.
tray_interior_depth = 19.0;
tray_wall_h = tray_interior_depth + tray_floor_t;   // 24.6

assert(tray_interior_depth >= glass_t + board_t + component_h,
       "tray is shallower than the parts stacked in it — raise tray_interior_depth");

total_thickness = bezel_front_t + tray_wall_h;

// Screw centres, one pair in each rim, as far toward the corners as the head
// can sit on flat wall — the widest spread against a shake. side_y_lo is up
// with the rim it defines; the two are one chain, and the assertions below
// check it end to end. Defining them independently is what put the boss 2.5mm
// over the board on the first cut of this file.
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
assert(side_y_lo - side_clear_d / 2 >= corner_r,
       "screw hole is drilled into the rounded corner, not through flat wall");
// The head is wider than the shaft, so clearing the corner is a second check:
// a counterbore that runs off the flat leaves the head seated on a sliver.
assert(side_y_lo - head_clear_d / 2 >= corner_r,
       "screw head overhangs the rounded corner — widen rim");
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

// The pads that thicken the tray wall at each screw. Shared: the tray adds
// them, and the bezel cuts itself against them.
module screw_pads_2d() {
    for (sy = side_ys) {
        translate([wall, sy - side_boss_od / 2])
            square([screw_pad_t, side_boss_od]);
        translate([outer_w - wall - screw_pad_t, sy - side_boss_od / 2])
            square([screw_pad_t, side_boss_od]);
    }
}

// Everything the bezel hangs into the cavity — flanges, locator brackets —
// has to miss the tray. Cutting them against the tray's own profile is what
// guarantees it, rather than four numbers that agree by hand until one moves.
//
// The first version had neither the cut nor the clearance, and the flanges'
// square outboard corners sat 0.67mm inside the tray's rounded interior
// corners, down all 12mm of flange. That holds the four corners proud, the
// screws pull the ends down regardless, and the bezel bows along both long
// sides. It looks exactly like a bezel printed a hair too long.
module cavity_keepout_2d() {
    difference() {
        rounded_rect(outer_w, outer_h, corner_r);
        offset(r = -clearance)
            difference() {
                translate([wall, wall])
                    rounded_rect(outer_w - 2 * wall, outer_h - 2 * wall,
                                 max(corner_r - wall, 0.5));
                screw_pads_2d();
            }
    }
}

module front_bezel() {
    pocket_x = pocket_x_inset;
    pocket_y = rim + chin;   // the chin is below the pocket, not around it

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

            // Board locator bars, at the ends of the top and bottom edges.
            board_y0 = pocket_y + fpc_fold_gap;
            board_y1 = pocket_y + pocket_h;
            for (cx = [0, 1]) {
                for (cy = [0, 1]) {
                    hx = cx == 0 ? pocket_x : pocket_x + pocket_w - rib_arm;
                    ya = cy == 0 ? board_y0 - rib_t : board_y1;
                    translate([hx, ya, bezel_front_t - 0.01])
                        cube([rib_arm, rib_t, rib_h]);
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

        // Trim every hanging feature back to what the tray actually leaves free.
        translate([0, 0, bezel_front_t])
            linear_extrude(height = side_flange_depth + 1)
                cavity_keepout_2d();

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

    // Battery across the top wall, long edge along case-X, centred in width
    // and flush to the top interior wall. That clears the whole middle and
    // lower half of the floor for the MCU, the accelerometer and the harness,
    // which otherwise had to thread between the cell and a side wall.
    batt_wall_margin = 0.5;
    batt_fit_clearance = 2.0;
    bay_x_outer = batt_w + 2 * batt_wall_margin + batt_fit_clearance;
    bay_y_outer = batt_h + 2 * batt_wall_margin + batt_fit_clearance;
    bay_x = (outer_w - bay_x_outer) / 2;
    bay_y = outer_h - wall - bay_y_outer;
    batt_x = bay_x + batt_wall_margin + batt_fit_clearance / 2;
    batt_y = bay_y + batt_wall_margin + batt_fit_clearance / 2;

    // Accelerometer below the battery. Orientation matters more than
    // position: mount it so its X axis lies in the plane of the fridge door,
    // which is the axis both a shake and a door swing act along, and the one
    // the thresholds in firmware/include/Config.h are tuned against.
    accel_x = wall + 3;
    accel_y = mcu_y + mcu_fp_y + 6.0;

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

            // Screw pads, floor to rim so they stiffen the wall as well as
            // give the counterbore something to bite into.
            translate([0, 0, tray_floor_t - 0.01])
                linear_extrude(height = tray_interior_depth + 0.01)
                    screw_pads_2d();
        }

        // Magnet pocket, dead centre, opening onto the exterior back face.
        //
        // PRINTING: the pocket ceiling is a 29mm circular bridge. Most
        // printers manage it and the surface is invisible under a magnet
        // either way. If yours sags, pause at Z = magnet_pocket_depth and drop
        // the magnet in — it's adhesive-backed, so it will stay put.
        translate([outer_w / 2, outer_h / 2, -0.1])
            cylinder(d = magnet_pocket_d, h = magnet_pocket_depth + 0.1);

        // USB-C access, through the bottom wall.
        translate([usbc_x_center - usbc_slot_w / 2, -0.1, usbc_slot_z])
            cube([usbc_slot_w, wall + 0.2, usbc_slot_h]);

        // Screw clearance holes, through the tray wall into the bezel flanges,
        // each with a counterbore on the outside for the flat head to seat in.
        for (sy = side_ys) {
            translate([-0.1, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = side_clear_d, h = flange_inset + flange_w + 0.2);
            translate([-0.1, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = head_clear_d, h = head_recess_t + 0.1);

            translate([outer_w - flange_inset - flange_w - 0.1, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = side_clear_d, h = flange_inset + flange_w + 0.2);
            translate([outer_w - head_recess_t, sy, side_boss_z_tray])
                rotate([0, 90, 0])
                    cylinder(d = head_clear_d, h = head_recess_t + 0.1);
        }

        // Indented on the exterior back, below the magnet pocket. mirror()
        // flips them to read correctly from outside, since model coordinates
        // are the inside view.
        translate([outer_w / 2, 14.5, -0.1])
            mirror([1, 0, 0])
                linear_extrude(height = 0.8)
                    text("Polaroid", size = 6.5, font = "Futura:style=Bold",
                         halign = "center", valign = "center");

        translate([outer_w / 2, 7.5, -0.1])
            mirror([1, 0, 0])
                linear_extrude(height = 0.8)
                    text("by Max", size = 4.0, font = "Futura:style=Medium",
                         halign = "center", valign = "center");
    }

    // Battery bay: a shallow retaining lip, held by friction and tape. Not a
    // closed box — a swollen pouch needs somewhere to go.
    batt_wall_h = 4.0;
    batt_wire_gap_w = 8.0;
    translate([bay_x, bay_y, tray_floor_t - 0.01]) {
        difference() {
            cube([bay_x_outer, bay_y_outer, batt_wall_h + 0.01]);
            translate([batt_wall_margin, batt_wall_margin, -0.1])
                cube([batt_w + batt_fit_clearance, batt_h + batt_fit_clearance,
                      batt_wall_h + 0.5]);
            // Wire exit down the case toward the MCU, through the bay's
            // low-Y wall.
            translate([batt_wall_margin, -0.1, -0.1])
                cube([batt_wire_gap_w, batt_wall_margin + 0.2, batt_wall_h + 0.5]);
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
