// ═══════════════════════════════════════════════════════════════
// Triptech Desktop Enclosure — Parametric Sheet Metal Design
// ═══════════════════════════════════════════════════════════════
// Material: 1.2mm aluminum or steel sheet, bent into a U-shell
// with removable top panel (lid secured by screws).
//
// Convention: X = width, Y = depth, Z = height
// Origin: bottom-left-rear corner (exterior)
//
// To export panel DXFs: set render_mode to the desired panel
// and use OpenSCAD's projection() or export as DXF.

// ─── Global parameters ──────────────────────────────────────

// Enclosure exterior dimensions (mm)
encl_width  = 144.78;  // 5.7 inches
encl_depth  =  95.25;  // 3.75 inches
encl_height =  42.0;   // to-be-confirmed; fits tallest component

// Sheet metal thickness
sheet_t = 1.2;

// PCB dimensions (matches KiCad board outline)
pcb_width  = 144.78;
pcb_depth  =  95.25;
pcb_thick  =   1.6;

// PCB standoff height (bottom of enclosure to bottom of PCB)
standoff_h = 5.0;
standoff_d = 6.0;   // M3 standoff outer diameter
standoff_drill = 3.2;  // M3 clearance

// PCB mounting hole positions (from PCB origin = top-left)
pcb_holes = [
    [4, 4],
    [140.78, 4],
    [4, 91.25],
    [140.78, 91.25]
];

// Lid screw positions (near corners, tapped into base flanges)
lid_screws = [
    [6, 6],
    [encl_width - 6, 6],
    [6, encl_depth - 6],
    [encl_width - 6, encl_depth - 6]
];

// Clearance between PCB edge and enclosure interior wall
pcb_clearance = 0.5;  // per side (encl interior = pcb + 2*clearance)
// Note: encl_width already matches pcb_width. In practice the
// enclosure interior should be slightly larger. Adjust if needed.

// ─── Top panel cutout parameters ────────────────────────────

// Encoder shaft holes (3x, centered below each OLED)
// Positions from PCB origin (top-left), Y measured from rear
enc_positions = [
    [36.2,  87.0],   // Encoder 1 (left)
    [72.39, 87.0],   // Encoder 2 (center)
    [108.58, 87.0]   // Encoder 3 (right)
];
enc_shaft_d = 7.0;  // PEC11 shaft clearance diameter

// OLED display windows (3x, horizontal row)
// 0.91" 128x32 OLED active area ≈ 22.4mm x 6.0mm
// Module PCB is ≈ 28mm x 11mm; window should clear the active area
oled_positions = [
    [36.2,  76.0],   // OLED 1 (left)
    [72.39, 76.0],   // OLED 2 (center)
    [108.58, 76.0]   // OLED 3 (right)
];
oled_window_w = 25.0;
oled_window_h =  8.0;
oled_corner_r =  1.0;

// Key switch holes (7x Kailh Choc V1)
// Choc V1 cutout: 13.8mm x 13.8mm (per Kailh spec)
choc_cutout = 13.8;
choc_corner_r = 0.5;

// Left column keys (SW1=RUN, SW2=BYP, SW3=SHIFT)
// Positioned at x ≈ 18mm, spaced 18mm apart vertically
key_left_col = [
    [18, 57],    // SW1 RUN
    [18, 64],    // SW2 BYP
    [18, 71]     // SW3 SHIFT
];

// Top row keys (SW4=CH1, SW5=CH2, SW6=CH3, SW7=GLOBAL)
// Positioned at y ≈ 57mm, spaced 19mm apart horizontally
key_top_row = [
    [50, 57],    // SW4 CH1
    [69, 57],    // SW5 CH2
    [88, 57],    // SW6 CH3
    [107, 57]    // SW7 GLOBAL
];

// Page indicator LED windows (4x small circles)
indicator_positions = [
    [128, 57],   // Page 1
    [128, 63],   // Page 2
    [128, 69],   // Page 3
    [128, 75]    // Page 4
];
indicator_d = 3.0;  // LED window diameter

// ─── Rear panel cutout parameters ───────────────────────────

// All rear panel connectors are at y ≈ 0 (rear edge of PCB)
// Heights are Z positions relative to enclosure bottom interior

// DC barrel jack (2.1mm, panel mount)
dc_jack_pos   = [10, 0];
dc_jack_d     = 8.0;    // panel hole diameter
dc_jack_z     = standoff_h + pcb_thick + 5.0;  // center height

// MIDI DIN-5 connectors (3x)
midi_positions = [
    [35, 0],     // MIDI IN
    [52, 0],     // MIDI OUT
    [69, 0]      // MIDI THRU
];
din5_d = 15.0;  // DIN-5 panel hole diameter

// USB-C
usbc_pos = [86, 0];
usbc_w   = 9.0;   // cutout width
usbc_h   = 3.5;   // cutout height
usbc_r   = 1.0;   // corner radius

// Audio TRS jacks (4x 1/4")
audio_positions = [
    [105, 0],    // Audio In L
    [115, 0],    // Audio In R
    [125, 0],    // Audio Out L
    [135, 0]     // Audio Out R
];
trs_d = 9.5;  // 1/4" TRS panel hole diameter

// ─── Render mode ────────────────────────────────────────────
// Set to control what gets rendered:
//   "assembled" — full 3D enclosure with cutouts
//   "top_panel" — flat top panel for DXF export
//   "base"      — U-shell base
//   "exploded"  — exploded assembly view

render_mode = "assembled";

// ─── Modules ────────────────────────────────────────────────

module rounded_rect(w, h, r) {
    // 2D rounded rectangle centered at origin
    offset(r) square([w - 2*r, h - 2*r], center=true);
}

module rounded_rect_cutout(w, h, r, depth) {
    // 3D cutout (extruded rounded rect)
    linear_extrude(depth)
        rounded_rect(w, h, r);
}

module top_panel() {
    difference() {
        // Panel sheet
        cube([encl_width, encl_depth, sheet_t]);

        // Encoder shaft holes
        for (pos = enc_positions) {
            translate([pos[0], pos[1], -1])
                cylinder(d=enc_shaft_d, h=sheet_t+2, $fn=32);
        }

        // OLED windows
        for (pos = oled_positions) {
            translate([pos[0], pos[1], -1])
                linear_extrude(sheet_t + 2)
                    rounded_rect(oled_window_w, oled_window_h, oled_corner_r);
        }

        // Key switch cutouts — left column
        for (pos = key_left_col) {
            translate([pos[0], pos[1], -1])
                linear_extrude(sheet_t + 2)
                    rounded_rect(choc_cutout, choc_cutout, choc_corner_r);
        }

        // Key switch cutouts — top row
        for (pos = key_top_row) {
            translate([pos[0], pos[1], -1])
                linear_extrude(sheet_t + 2)
                    rounded_rect(choc_cutout, choc_cutout, choc_corner_r);
        }

        // Indicator LED windows
        for (pos = indicator_positions) {
            translate([pos[0], pos[1], -1])
                cylinder(d=indicator_d, h=sheet_t+2, $fn=24);
        }

        // Lid screw holes
        for (pos = lid_screws) {
            translate([pos[0], pos[1], -1])
                cylinder(d=standoff_drill, h=sheet_t+2, $fn=24);
        }
    }
}

module base_shell() {
    // U-shaped base: bottom plate + front wall + rear wall
    // Side walls are flanges from the bottom plate bends
    
    inner_h = encl_height - sheet_t;  // interior height (lid sits on top)
    
    difference() {
        union() {
            // Bottom plate
            cube([encl_width, encl_depth, sheet_t]);
            
            // Rear wall (y=0 side)
            translate([0, 0, 0])
                cube([encl_width, sheet_t, inner_h]);
            
            // Front wall (y=depth side)
            translate([0, encl_depth - sheet_t, 0])
                cube([encl_width, sheet_t, inner_h]);
            
            // Left wall
            translate([0, 0, 0])
                cube([sheet_t, encl_depth, inner_h]);
            
            // Right wall
            translate([encl_width - sheet_t, 0, 0])
                cube([sheet_t, encl_depth, inner_h]);
            
            // Lid screw bosses (raised pads on top of walls for lid screws)
            for (pos = lid_screws) {
                translate([pos[0], pos[1], inner_h - 3])
                    cylinder(d=8, h=3, $fn=24);
            }
        }
        
        // ── Rear panel cutouts ──
        
        // DC barrel jack
        translate([dc_jack_pos[0], -1, dc_jack_z])
            rotate([-90, 0, 0])
                cylinder(d=dc_jack_d, h=sheet_t+2, $fn=32);
        
        // MIDI DIN-5 connectors
        for (pos = midi_positions) {
            translate([pos[0], -1, dc_jack_z])
                rotate([-90, 0, 0])
                    cylinder(d=din5_d, h=sheet_t+2, $fn=48);
        }
        
        // USB-C
        translate([usbc_pos[0], -1, dc_jack_z])
            rotate([-90, 0, 0])
                linear_extrude(sheet_t + 2)
                    rounded_rect(usbc_w, usbc_h, usbc_r);
        
        // Audio TRS jacks
        for (pos = audio_positions) {
            translate([pos[0], -1, dc_jack_z])
                rotate([-90, 0, 0])
                    cylinder(d=trs_d, h=sheet_t+2, $fn=32);
        }
        
        // PCB standoff holes in bottom plate
        for (pos = pcb_holes) {
            translate([pos[0], pos[1], -1])
                cylinder(d=standoff_drill, h=sheet_t+2, $fn=24);
        }
        
        // Lid screw tapped holes
        for (pos = lid_screws) {
            translate([pos[0], pos[1], -1])
                cylinder(d=2.5, h=inner_h+2, $fn=24);  // M3 tap drill = 2.5mm
        }
        
        // Ventilation slots on bottom (optional)
        // 3 rows of slots near the center
        for (i = [-1, 0, 1]) {
            translate([encl_width/2 + i*20, encl_depth/2, -1])
                linear_extrude(sheet_t + 2)
                    rounded_rect(15, 1.5, 0.5);
        }
    }
}

module pcb_placeholder() {
    // Visual placeholder for the PCB
    color("green", 0.5)
        translate([0, 0, sheet_t + standoff_h])
            cube([pcb_width, pcb_depth, pcb_thick]);
}

module standoffs() {
    color("silver")
        for (pos = pcb_holes) {
            translate([pos[0], pos[1], sheet_t])
                difference() {
                    cylinder(d=standoff_d, h=standoff_h, $fn=6);  // hex standoff
                    translate([0, 0, -1])
                        cylinder(d=standoff_drill, h=standoff_h+2, $fn=24);
                }
        }
}

// ─── Assembly ───────────────────────────────────────────────

module assembled() {
    color("DimGray", 0.9) base_shell();
    
    color("LightGray", 0.7)
        translate([0, 0, encl_height - sheet_t])
            top_panel();
    
    standoffs();
    pcb_placeholder();
}

module exploded() {
    color("DimGray", 0.9) base_shell();
    
    // Lid raised above
    color("LightGray", 0.7)
        translate([0, 0, encl_height + 20])
            top_panel();
    
    standoffs();
    pcb_placeholder();
}

// ─── Render selection ───────────────────────────────────────

if (render_mode == "assembled") {
    assembled();
} else if (render_mode == "exploded") {
    exploded();
} else if (render_mode == "top_panel") {
    // Flat 2D panel for DXF export
    projection() top_panel();
} else if (render_mode == "base") {
    base_shell();
} else {
    assembled();
}
