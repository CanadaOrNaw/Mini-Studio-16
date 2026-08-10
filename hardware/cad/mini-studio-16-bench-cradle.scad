// Mini Studio 16 open bench cradle for M5Stack Cardputer-ADV.
// Original parametric design; pre-hardware fit prototype.
// Published device envelope: 84 x 54 mm. Default clearance: 0.30 mm/side.

device_x = 84.0;
device_y = 54.0;
clearance = 0.30;
outer_x = 88.0;
outer_y = 58.0;
base_height = 2.0;
wall_height = 6.0;
center_opening_x = 70.0;
center_opening_y = 40.0;
side_clip_span_y = 32.0;
end_clip_span_x = 62.0;

cavity_x = device_x + 2 * clearance;
cavity_y = device_y + 2 * clearance;
wall_x = (outer_x - cavity_x) / 2;
wall_y = (outer_y - cavity_y) / 2;

module centered_cube(size) {
    translate([-size[0] / 2, -size[1] / 2, 0]) cube(size);
}

union() {
    // Ventilated bottom frame.
    difference() {
        centered_cube([outer_x, outer_y, base_height]);
        translate([0, 0, -0.1])
            centered_cube([center_opening_x, center_opening_y, base_height + 0.2]);
    }

    // Four open corner guides. Side/end centers stay clear for ports.
    for (sx = [-1, 1])
        for (sy = [-1, 1]) {
            translate([sx * (cavity_x / 2 + wall_x / 2),
                       sy * (cavity_y / 2 - side_clip_span_y / 4), 0])
                centered_cube([wall_x, side_clip_span_y / 2, wall_height]);
            translate([sx * (cavity_x / 2 - end_clip_span_x / 4),
                       sy * (cavity_y / 2 + wall_y / 2), 0])
                centered_cube([end_clip_span_x / 2, wall_y, wall_height]);
        }
}
