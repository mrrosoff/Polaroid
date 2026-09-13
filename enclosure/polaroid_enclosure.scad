// Polaroid enclosure — 2-part (front bezel + rear tray), screwed together.
//
// The panel is natively 400x600 portrait, so case-X is width and case-Y is
// height, running along the panel's long 101mm edge.
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
// The photo runs full-bleed across all 400x600 and nothing hides behind the
// bezel: a chin kept for looks would have to cover a quarter of a $45 panel.
//
// PRINT IT IN A DARK COLOUR. E-ink white is bone (~78% reflectance); a white
// bezel reads as one dingy slab and the Polaroid shape vanishes.
//
// Screws rather than clips: this is the one enclosure in the house that gets
// deliberately shaken, and snap-fit works loose.
//
// Render one part at a time:
//   openscad -D part=\"bezel\" -o stl/front_bezel.stl polaroid_enclosure.scad
//   openscad -D part=\"tray\"  -o stl/rear_tray.stl   polaroid_enclosure.scad
part = "both"; // "bezel" | "tray" | "both" (both = preview only, side by side)

$fn = 64;

// ---- General ----
wall      = 1.5;   // perimeter wall thickness
clearance = 0.3;   // general fit clearance around boards
// The corner and the screw head compete for the same end of the case: the head
// must seat on flat wall outboard of the radius, so every 0.1 of radius pushes
// the screw 0.1 further in and costs 0.1 more rim behind it.
corner_r  = 2.8;

// ---- Side-entry screws ----
// SCREWS LIVE IN THE TOP AND BOTTOM RIMS, NOT THE SIDE MARGINS. A flange in a
// side margin forces that margin to flange_inset + flange_w + clearance, so
// every millimetre of thread engagement costs a millimetre of bezel on both
// sides. At a Y above or below the board there is nothing to collide with, so
// the side margin drops to a bare wall and the flange can be as deep as the
// joint needs.
//
// 4.5 around a 2.0mm pilot leaves a 1.25mm wall, the section that split on the
// Forager print. It is back because the boss diameter is the rim's whole cost,
// and because this flange is 12mm deep into a screw pad rather than a stub in
// open air. If a flange splits on first assembly, this is the number that did
// it.
side_boss_od      = 4.5;
side_pilot_d      = 2.0;   // M2 self-tapping into plastic
side_clear_d      = 2.9;   // shaft passes freely through the tray wall
// Flat-top heads sink flush in a counterbore outside the tray. A 1.6mm head
// into a 1.5mm wall leaves nothing to pull against, so the wall is thickened
// locally with a pad on the INSIDE that the bezel's flange butts against.
head_clear_d      = 4.2;   // 3.8mm M2 pan/cheese head plus fit
head_recess_t     = 1.6;   // full head height — the head disappears
screw_pad_t       = 1.5;
flange_w          = 4.0;   // thread engagement depth
// How far the flange reaches into the tray cavity. It has to stop above the
// battery bay's retaining wall, which it overlaps in plan: 12.0 was fine under
// a 19mm cavity and drove the flange straight through the bay at 12.5.
side_flange_depth = 8.0;
// Clear of the screw pad's inner face, not of the wall's.
flange_inset      = wall + screw_pad_t + clearance;   // 3.3

assert(head_recess_t <= wall + screw_pad_t - 1.2,
       "counterbore leaves under 1.2mm of wall — the head will pull through");

// ---- Display assembly ----
// The BOARD, not the glass, sets the footprint: it's 2mm bigger in both axes.
board_w = 68.0;
board_h = 101.0;
// Measured with calipers, headers removed — but see measured_stack_t, which
// says this reads about 5mm high. Re-measure before trusting it.
board_t = 7.0;

glass_w = 66.0;
glass_h = 99.0;
glass_t = 0.85;    // the glass is genuinely this thin. Handle it accordingly.

active_w = 56.40;
active_h = 84.60;

// ASSUMPTION: the active area is centred on the glass and the glass on the
// board. Vendor drawings give no offsets. Shift with active_offset_* rather
// than moving the pocket. Positive Y moves the window up; the value below is
// what the second printed bezel measured short.
active_offset_x = 0;
active_offset_y = 1.7;

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
// Dead centre is the only sane place for a single disc: anywhere else and the
// case rotates until its centre of mass swings under the magnet. The pocket
// exists so the magnet sits below the back face — standing proud, it is the
// one bump the case rocks on and it never sits flat against the door.
magnet_d = 29.0;
magnet_t = 3.0;
magnet_pocket_d = magnet_d + 0.6;
magnet_pocket_depth = magnet_t + 0.8;
magnet_back_t = 1.2;    // material left behind the magnet. 0.8 was thin
                        // enough to feel like a membrane; this still holds

// The pocket lives in a boss standing proud of the floor on the INSIDE, so its
// depth is paid for over 34mm of circle rather than by thickening the whole
// back — worth 4mm of case. The cost is that the floor is not flat, and the
// component placements below are asserted clear of the boss.
tray_floor_t = wall;   // 1.5
magnet_boss_h = 3.5;
magnet_boss_d = magnet_pocket_d + 4.0;   // 2mm ring around the pocket

assert(tray_floor_t + magnet_boss_h >= magnet_pocket_depth + magnet_back_t,
       "magnet pocket breaks through the boss into the cavity");

// ---- USB-C access ----
usbc_slot_w = 9.5;
usbc_slot_h = 4.0;

// ---- Derived footprint ----
pocket_w = board_w + 2 * clearance;
pocket_h = board_h + 2 * clearance + fpc_fold_gap;

// The pocket IS the tray's interior, so the board is located in X by the wall
// itself to its 0.3mm clearance — tighter than locator brackets managed.
pocket_x_inset = wall;   // 1.5

// Rims above and below the board, sized to just contain a screw boss clear of
// the rounded corner. This is the one place the case is bigger than the board
// needs, and it buys the thin side bezels.
//
// The head, not the shaft, sets the screw centre: the counterbore has to land
// on flat wall. The 0.1 is not decoration — at exact equality the assertion
// below fails on floating point.
side_y_lo = corner_r + head_clear_d / 2 + 0.1;   // 5.0
// Derived from the screw rather than the screw from the rim, which is worth
// 2mm of height and cannot put the boss over the board.
rim = side_y_lo + side_boss_od / 2 + 0.1;  // 8.25

// The placement assertions below are the test of whether the parts need extra
// floor, and they pass at zero. Kept as a knob because a part that grows is
// the reason it would come back.
chin = 0;

outer_w = pocket_w + 2 * pocket_x_inset;   // ~73.6
outer_h = pocket_h + 2 * rim + chin;

bezel_front_t = 2.2;
glass_pocket_w = glass_w + 2 * clearance;
glass_pocket_h = glass_h + 2 * clearance;
glass_pocket_d = 1.0;   // shallow recess that locates the 0.85mm glass

// Locator bars on the bezel's back face, so a device shaken on purpose does
// not rattle its panel loose. Only Y needs holding — the tray wall takes X.
// Bars at the ends rather than one rib per edge: the FPC leaves the middle of
// the bottom edge and a continuous rib would pinch it.
rib_t = 1.0;
rib_arm = 15.0;
rib_h = glass_t + board_t;

// The bezel is a plate, not a pocket, so the whole display stack lives in the
// tray's cavity on top of the battery layer. MCU, accelerometer and battery
// sit side by side in plan, hence the max; the board spans the full footprint
// above them, hence the sum.
component_h = max(batt_t + batt_puff_clearance, mcu_component_h, accel_h);

measured_stack_t = 12.0;   // second build, dressed flat. Was 19.0.
tray_interior_depth = measured_stack_t + 0.5;
tray_wall_h = tray_interior_depth + tray_floor_t;

// Against the measurement, not the parts list. The list sums to 16.85 and the
// assembled stack calipers at 12.0, so a figure in it reads high — board_t at
// 7.0 is the suspect, since 0.85 + 2.15 + 9.0 is exactly the 12.0 measured.
// Re-measure the display board before trusting the list again.
assert(tray_interior_depth >= measured_stack_t,
       "tray is shallower than the measured stack — raise measured_stack_t");

total_thickness = bezel_front_t + tray_wall_h;

// One pair in each rim, as far toward the corners as the head can sit on flat
// wall — the widest spread against a shake. Defining side_y_lo and rim
// independently is what put a boss 2.5mm over the board on the first cut.
side_y_hi = outer_h - side_y_lo;
side_ys = [side_y_lo, side_y_hi];

// Both parts measure to the shared mating plane, or the screw passes under the
// flange and grabs nothing — which is what "the screws didn't fully work"
// looks like in practice.
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

// ---- Component placement on the tray floor ----
// Hoisted out of rear_tray() because the floor is not flat: the magnet boss
// stands 3.5mm proud dead centre and every part in this layer has to miss it.
// The assertions below are the claim that the parts fit, not the header.
mcu_x = (outer_w - mcu_w) / 2;   // centred, so USB-C lines up with the slot
mcu_y = wall + 3;

// Orientation matters more than position: the breakout's X axis must lie in
// the plane of the fridge door, the axis both a shake and a door swing act
// along, and the one firmware/include/Config.h is tuned against.
accel_x = wall + 3;
accel_y = mcu_y + mcu_l + 1.0;

// Battery across the top wall, clearing the middle and lower floor for the MCU,
// the accelerometer and the harness.
batt_wall_margin = 0.5;
batt_fit_clearance = 2.0;
bay_x_outer = batt_w + 2 * batt_wall_margin + batt_fit_clearance;
bay_y_outer = batt_h + 2 * batt_wall_margin + batt_fit_clearance;
bay_x = (outer_w - bay_x_outer) / 2;
bay_y = outer_h - wall - bay_y_outer;

magnet_cx = outer_w / 2;
magnet_cy = outer_h / 2;

// Gap from a floor-plan rectangle to the magnet boss's outside face. Negative
// means they overlap.
function boss_gap(rx, ry, rw, rl) =
    let (dx = max(rx - magnet_cx, 0, magnet_cx - (rx + rw)),
         dy = max(ry - magnet_cy, 0, magnet_cy - (ry + rl)))
    sqrt(dx * dx + dy * dy) - magnet_boss_d / 2;

assert(boss_gap(mcu_x, mcu_y, mcu_w, mcu_l) >= clearance,
       "MCU footprint runs into the magnet boss");
assert(boss_gap(accel_x, accel_y, accel_w, accel_l) >= clearance,
       "accelerometer footprint runs into the magnet boss");
assert(boss_gap(bay_x, bay_y, bay_x_outer, bay_y_outer) >= clearance,
       "battery bay runs into the magnet boss");

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

// Everything the bezel hangs into the cavity is cut against the tray's own
// profile, rather than four numbers that agree by hand until one moves.
// Without it the flanges' square corners sat 0.67mm inside the tray's rounded
// interior corners down all 12mm, holding the corners proud while the screws
// pulled the ends down — which looks exactly like a bezel printed too long.
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
    usbc_x_center = outer_w / 2;
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

            // The magnet boss, and the only thing standing off the floor.
            translate([magnet_cx, magnet_cy, tray_floor_t - 0.01])
                cylinder(d = magnet_boss_d, h = magnet_boss_h + 0.01);
        }

        // Magnet pocket, dead centre, opening onto the exterior back face.
        //
        // PRINTING: the pocket ceiling is a 29mm circular bridge. Most
        // printers manage it and the surface is invisible under a magnet
        // either way. If yours sags, pause at Z = magnet_pocket_depth and drop
        // the magnet in — it's adhesive-backed, so it will stay put.
        translate([magnet_cx, magnet_cy, -0.1])
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
        // are the inside view. 0.5 deep, not 0.8: the floor here is the wall
        // thickness now, and 0.8 would leave 0.7mm of skin under the letters.
        translate([outer_w / 2, 14.5, -0.1])
            mirror([1, 0, 0])
                linear_extrude(height = 0.5)
                    text("Polaroid", size = 6.5, font = "Futura:style=Bold",
                         halign = "center", valign = "center");

        translate([outer_w / 2, 7.5, -0.1])
            mirror([1, 0, 0])
                linear_extrude(height = 0.5)
                    text("by Max", size = 4.0, font = "Futura:style=Medium",
                         halign = "center", valign = "center");
    }

    // Battery bay: a shallow retaining lip, held by friction and tape. Not a
    // closed box — a swollen pouch needs somewhere to go.
    batt_wall_h = 4.0;   // still clears the boss, which is 3.5
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

// fit_check.scad mates the parts about this plane. `use` imports functions but
// not variables, and a hand-copied 26.8 in that file is how a fit check quietly
// starts checking the wrong thing.
function total_thickness_mm() = total_thickness;

echo(str("outer: ", outer_w, " x ", outer_h, " x ", total_thickness, " mm"));

if (part == "bezel") {
    front_bezel();
} else if (part == "tray") {
    rear_tray();
} else {
    front_bezel();
    translate([outer_w + 20, 0, 0]) rear_tray();
}
