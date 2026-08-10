// Mini Studio Audio Cap Rev A — two-part no-screw shell
// Units: mm. Status: PRE-HARDWARE FIT PROTOTYPE.
// Generate in OpenSCAD with part="base" or part="lid".

part = "assembly"; // [base,lid,assembly]
$fn = 36;

outer_x = 84;
outer_y = 24;
base_z = 13;
wall = 2;
clearance = 0.25;
board_x = 80;
board_y = 20;

module rounded_box(size, radius=1.6) {
  hull() for (x=[-size[0]/2+radius,size[0]/2-radius])
    for (y=[-size[1]/2+radius,size[1]/2-radius])
      translate([x,y,0]) cylinder(h=size[2],r=radius);
}

module base() {
  difference() {
    rounded_box([outer_x,outer_y,base_z],2);
    translate([0,0,2]) rounded_box([outer_x-2*wall,outer_y-2*wall,base_z],1);
    // 2x7 Cardputer connector, keyed by the offset notch.
    translate([0,-8,-0.5]) cube([20,7,4],center=true);
    translate([8,-5.5,-0.5]) cube([4,3,4],center=true);
    // Line jack, USB-C, pair button, and LED light pipe.
    translate([-outer_x/2,0,6]) rotate([0,90,0]) cylinder(h=6,r=3.4,center=true);
    translate([outer_x/2,0,5]) cube([6,10,5],center=true);
    translate([28,outer_y/2,8]) cube([8,6,5],center=true);
    translate([18,outer_y/2,9]) rotate([90,0,0]) cylinder(h=6,r=1.2,center=true);
  }
  // Board rails. The assembly house provides the header; no loose wiring.
  for (x=[-32,32]) translate([x,0,2.6]) cube([8,15,1.2],center=true);
  // Long PETG latch shelves; lid skirts flex outward to release.
  for (y=[-outer_y/2+1.2,outer_y/2-1.2])
    translate([0,y,10]) cube([62,1.2,1.2],center=true);
  // RF keep-out marking at the WROOM antenna end.
  translate([30,0,2.1]) linear_extrude(0.45)
    text("RF KEEP CLEAR",size=2.2,halign="center",valign="center");
}

module lid() {
  difference() {
    rounded_box([outer_x+0.8,outer_y+0.8,5],2);
    translate([0,0,-0.1]) rounded_box([outer_x-2.8,outer_y-2.8,3.2],1);
  }
  // Compliant skirt catches; squeeze both long sides gently to open.
  for (y=[-outer_y/2+1.1,outer_y/2-1.1])
    translate([0,y,0.7]) cube([62,1.2,1.4],center=true);
  translate([0,0,5]) linear_extrude(0.6)
    text("MINI STUDIO AUDIO CAP  REV A",size=3.2,halign="center",valign="center");
}

if (part=="base") base();
else if (part=="lid") lid();
else {
  base();
  translate([0,0,13+0.5]) lid();
}
