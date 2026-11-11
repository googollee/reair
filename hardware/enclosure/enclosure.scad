// ReAir 风扇外壳，对应 docs/adr/0009-fan-enclosure.md。单位 mm。
//
// 坐标：x 向右、y 向上、z 沿风扇轴向外，z=0 为背板后表面（贴墙面）。
//
// 预览：openscad enclosure.scad
// 导出：openscad -o mid.3mf -D 'part="mid"' enclosure.scad   （3MF 带颜色；也可以导出 .stl，但没有颜色）
//   part: assembly 装配 | exploded 爆炸 | back 背板 | mid 中层 | cap 顶盖 | strip 后压条
//         | mid_body / mid_island 中层的白色主体 / 二维码黑色岛（分开导出，由 scripts/pack_enclosure_3mf.py 合成带两个挤出机的 3MF）
//         | btn 按键导杆 | pipe 导光柱
//   back/mid/cap/strip/btn/pipe 输出为打印方向（无支撑）。零件左右装配位通用。
//   pipe 用透明 PETG，其余不透明。
//   side（L/R）只影响 assembly/exploded 预览里板子、压条和天线的位置：
//     L 电源口在左；R 电源口在右（板子绕 y 轴转 180°，J2 出线边由朝后变为朝前）。
//   section=true 时剖开 x>0 半边，仅用于预览。
//
// 中层前表面左下角印 Matter 配对二维码和手动配对码：图案凹刻，黑色岛靠 M600 换色，
// 细节见 matter_label_plate 的注释。配对信息用 -D qr_passcode=... 等传入，默认是 SDK 测试值。
//
// 左右通用靠这几点：
//   - 圆口插座开口、按键孔、LED 孔两侧都有，外面留 0.4~0.5 mm 薄皮，装配时只捅开用到的那一侧。
//   - J2/风扇引线缺口前后两端都有，风扇线和天线线共用；天线竖贴在电源口一侧的侧壁，两侧壁都有凹槽。
//   - 板子远端的挡块在后压条上，压条掉头使用。

// 配对信息：默认是 SDK 测试值（QR MT:Y.K9042C00KA0648G00，手动配对码 3497-011-2332）。
// make enclosure 会根据 config/ 用 -D 覆盖这四个值。
qr_passcode      = 20202021;
qr_discriminator = 3840;
qr_vid           = 65521;   // 0xFFF1
qr_pid           = 32768;   // 0x8000

part    = "assembly";
side    = "L";
section = false;
$fn     = 64;

// ---- 外形 ----
X    = 80;                  // 外形宽 160
Y1   = 70;                  // 上边缘
bay_drop = 10;              // 电路舱下移量：风扇下平边与舱之间多出一排，放下面中央的柱子
Y0   = -80 - bay_drop;      // 下边缘；下部含 PCB 舱，外形高 160
Sy   = Y1 - Y0;
wall = 2;

// ---- 风道与背板 ----
plate_t    = 7;             // 背板厚度，螺钉自攻深度
spigot_od  = 116;           // 插入管外径；外绕 3 mm 发泡条，压入内径 120 的风道
spigot_id  = 112;
spigot_len = 60;
rib_h = 0.5;                // 插入管外壁凸棱突出量：发泡条在棱上的压缩量 = 平均压缩量 + rib_h
rib_w = 3;                  // 凸棱宽度
rib_ang = [90, 210, 330];   // 三道凸棱的角度，一道在正上方
foam_t     = 3;             // 发泡条厚度

// ---- 风扇 4412FGPR ----
fan_d     = 127;
fan_flat  = 123;
fan_h     = 26.4;
fan_clr   = 0.6;            // 风扇座单边间隙
seat_wall = 2;
front_t   = 1.5;            // 前封板
// 二维码板：中层前表面左下角的空地（腿、螺钉、风扇圆口之外）
lbl_m = 1.3;                // 二维码模块边长
lbl_text = 4;               // 手动配对码字高
lbl_gap = 1;                // 二维码与文字间距
lbl_margin = 3;             // 静区
lbl_iso = 1.2;              // 黑色岛与白色外圈的间隙
lbl_wall = 1.6;             // 白色外圈宽度
lbl_ext = lbl_iso + lbl_wall;
lbl_w = 21 * lbl_m + 2 * lbl_margin + 2 * lbl_ext;                        // 板外形宽
lbl_h = 21 * lbl_m + lbl_gap + lbl_text + 2 * lbl_margin + 2 * lbl_ext;   // 板外形高
lbl_x = -X + 0.5 + lbl_w / 2;                                             // 板中心 x（原点是二维码水平中心）
lbl_y = Y0 + 0.5 + lbl_gap + lbl_text + lbl_margin + lbl_ext;             // 二维码底边的 y
key_w = 4.6;                // 定位块宽（风扇外圈槽口 5 mm）
key_gap = 0.3;              // 定位块端面与槽底的间隙；槽底在 x=±61.5（圆弧 63.5 向内 2）
key_h = 13.2;               // 定位块高度：风扇厚度的一半，靠前封板一侧
wire_y0 = 14; wire_y1 = 28; // 风扇引线出口窗口，距中线 y 范围（正负都开）
front_hole = 120;
eps = 0.01;           // 封板圆口，压在风扇前圈上
foam_comp  = 0.5;           // 发泡条被压缩量

fan_z0  = plate_t + foam_t - foam_comp;
fan_z1  = fan_z0 + fan_h;   // 风扇前面，也是封板内侧
mid_top = fan_z1 + front_t; // 中层前表面

// ---- 顶盖 ----
gap   = 20;                 // 顶盖与封板之间的缝隙
cap_t = 2.5;
cap_z0 = mid_top + gap;

// ---- PCB（hardware/reair.kicad_pcb）----
pcb_l = 82; pcb_w = 26; pcb_t = 1.6; comp_h = 12;
jack_over   = 3;            // 插座壳体伸出板边的长度
jack_recess = 0.5;          // 插座头缩进外壁外表面
ko_gap = 0.1;               // 捅开圆片与外壳之间的间隙
ko_t = 0.2;                 // 捅开圆片厚度，也是 LED 位置留的透光薄壁
jack_hole_d = 9;            // 电源口开孔
btn_hole_d = 4.2;           // 按键导杆孔
led_hole_d = 3.2;           // 导光柱孔（盲孔），导光柱 Ø3
sw_h = 1.5;                 // SW1 本体高度，待核对数据手册
led_h = 0.6;                // D3 本体高度
sleeve_s = 6.4;             // 按键导向套筒外形（菱形，边长），高 sleeve_h
sleeve_h = 5;
btn_d = 4.0; btn_fl_d = 6; btn_fl_t = 1;
pipe_d = 3.0;
j2_u = 33.5;                // J2 中心距电源口端的距离（按 PCB 渲染图量得）
ant_l = 60; ant_w = 8;     // FPC 天线，竖贴在远端侧壁
ant_y0 = -40;               // 天线下端；贴在电源口一侧的外壁内面，竖直向上
seat_slab_y0 = -(fan_flat/2 + fan_clr) - seat_wall;   // 风扇座底壁下表面
bay_floor = Y0 + wall;
bay_top   = bay_floor + comp_h + pcb_t + 0.3;         // 舱内顶面（板子地平面贴着它）
pcb_y1 = bay_top;           // 元件面朝下，地平面朝上贴舱顶
pcb_y0 = pcb_y1 - pcb_t;
comp_y0 = pcb_y0 - comp_h;
jack_y = pcb_y0 - 5.5;      // 圆口插座中心高度
pcb_z0 = fan_z1 - pcb_w;    // 板前边贴封板内侧
pcb_zc = pcb_z0 + pcb_w/2;
strip_t = pcb_z0 - plate_t; // 后压条厚度，装上背板后压紧板子
btn_out = 0.4;                              // 导杆下端伸出外表面的长度
btn_y0 = Y0 - btn_out;                      // 导杆下端
pipe_y0 = Y0 + ko_t;                        // 导光柱下端（立在 0.2 mm 薄壁上）
btn_fl_z = bay_floor + sleeve_h - btn_y0;   // 凸缘下表面离导杆下端的距离
btn_len = (pcb_y0 - sw_h - 0.3) - btn_y0;   // 导杆总长，顶端离按键 0.3
pipe_len = (pcb_y0 - led_h - 0.2) - pipe_y0; // 导光柱长度，顶端离 LED 0.2

xs = (side == "L") ? 1 : -1;    // 电源口在 x 的哪一侧
zs = xs;                        // J2 出线边：L 朝后，R 朝前
pcb_xL = -X + jack_recess + jack_over;  // L 版板子左端
function bx(u) = xs * (pcb_xL + u);     // 板上沿长度方向距电源口端 u 处的 x
board_end = pcb_xL + pcb_l;             // L 版板子右端
function bz(v) = pcb_zc - zs * v;       // 板上距中心 v（向 J2 出线边为正）处的 z

// ---- 螺钉与卡钩位置（四个角柱）----
post_z0 = 22;               // 立柱下端
hook_out = 1.2;             // 卡钩外凸量
post_x = 4; post_y = 6;     // 立柱截面：post_x 是卡钩方向的厚度，post_y 是宽度
pocket_clr = 0.2;           // 插槽外侧（卡钩一侧）间隙
pocket_relief = 1.8;        // 插槽内侧让位，柱子插入时向内弹
slot_z0 = 19;               // 立柱插槽底面，螺钉头坐在这里；低于立柱下端 post_z0，给螺钉头（约 2.4）留位置
pocket_y = post_y + 0.4;
hook_flat = 3;              // 卡钩平直段高度；上下两端都是 45° 斜面
recess_d = 0.8;             // 凹口深度，比卡钩外凸量小 0.2，卡住后柱子保持预紧
leg_x = 9; leg_y = 10;      // 顶盖腿：直接支在角柱前的封板上
col_y = (bay_top + wall + seat_slab_y0) / 2 - (pocket_relief - recess_d) / 2;   // 下面中央柱子所在高度（舱顶板与风扇座底壁之间）；槽加凹口在 y 向偏向让位侧 (pocket_relief - recess_d)/2，减去后两侧壁等厚
latch = [[ X - 9,  62], [-(X - 9),  62], [0, col_y]];
latch_ang = [90, 90, -90];  // 卡钩朝向：以 +x 为 0°，上面两个朝上（+y），下面一个朝下（-y）
screw = [for (i = [0 : len(latch) - 1]) latch[i] + pocket_relief / 2 * [-cos(latch_ang[i]), -sin(latch_ang[i])]];   // 螺钉在插槽中心（让位侧偏 pocket_relief/2），顶盖立柱盖住螺钉头

// ================= 通用 =================
module fan_env(o = 0) {
    // Ø127 圆，仅上下两个平边（相距 123）；左右是圆弧，定位槽口开在圆弧中线上
    offset(delta = o) intersection() { circle(d = fan_d); square([fan_d + 10, fan_flat], center = true); }
}
// 泪滴孔 2D：尖端朝 -y（对应打印时向上）
module tdrop2d(d) { hull() { circle(d = d); translate([-0.01, -0.7071 * d]) square(0.02); } }
// 沿 y 的圆孔，尖端朝 -z
module hole_y(d, y_from, y_to, x, z) {
    translate([x, y_to, z]) rotate([90, 0, 0]) linear_extrude(y_to - y_from) tdrop2d(d);
}
// 沿 x 的圆孔，尖端朝 -z
module hole_x(d, x_from, x_to, y, z) {
    translate([x_from, y, z]) rotate([90, 0, 90]) linear_extrude(x_to - x_from) tdrop2d(d);
}

// ================= 背板：平板 + 插入管 =================
module back() {
    difference() {
        union() {
            translate([-X, Y0, 0]) cube([2*X, Sy, plate_t]);
            translate([0, 0, -spigot_len]) {
                cylinder(d1 = spigot_od - 2, d2 = spigot_od, h = 1);    // 端部倒角
                translate([0, 0, 1]) cylinder(d = spigot_od, h = spigot_len - 1 + 0.01);
            }
            // 轴向凸棱：贴着发泡条内侧，端部 2:1 斜坡便于插入，竖直打印时没有悬垂
            for (a = rib_ang) rotate([0, 0, a]) rotate([90, 0, 0]) linear_extrude(rib_w, center = true) polygon([
                [spigot_od/2 - 0.3, -spigot_len + 1], [spigot_od/2 - 0.3, 0.01], [spigot_od/2 + rib_h, 0.01],
                [spigot_od/2 + rib_h, -spigot_len + 1 + 2*rib_h]]);
        }
        translate([0, 0, -spigot_len - 1]) cylinder(d = spigot_id, h = spigot_len + plate_t + 2);
        for (p = screw) translate([p[0], p[1], -1]) cylinder(d = 2.6, h = plate_t + 2);   // 自攻孔
    }
}

// ================= 中层 =================
module mid_profile() {
    // 外壁
    difference() {
        translate([-X, Y0]) square([2*X, Sy]);
        translate([-X + wall, Y0 + wall]) square([2*(X - wall), Sy - 2*wall]);
    }
    // 风扇座壁
    difference() { fan_env(fan_clr + seat_wall); fan_env(fan_clr); }
    translate([-X, seat_slab_y0]) square([2*X, -(fan_flat/2 + fan_clr) - seat_slab_y0]);
    // 舱顶板，与风扇座底壁之间是一排空腔，中央是柱块
    translate([-X, bay_top]) square([2*X, wall]);
    translate([-11, bay_top]) square([22, seat_slab_y0 - bay_top]);
    for (sx = [-1, 1]) {
        // 上角柱
        translate([sx > 0 ? 62 : -X, 47]) square([X - 62, Y1 - 47]);
    }
}

// 一侧装配位（xs_=1 为 L，-1 为 R）的板上特征
module side_cuts(xs_) {
    zs_ = xs_;
    bx_ = function(u) xs_ * (pcb_xL + u);
    bz_ = function(v) pcb_zc - zs_ * v;
    // 圆口插座：端壁圆孔，外表面留一片可捅开的圆片（见 knockouts）
    wxo = xs_ > 0 ? -X : X;                       // 电源口一侧外壁外表面 x
    translate([xs_ > 0 ? wxo - 1 : wxo - wall - 1, jack_y, pcb_zc]) rotate([0, 90, 0]) cylinder(d = jack_hole_d, h = wall + 2);
    // 按键导杆孔：圆孔，外表面留圆片
    translate([bx_(22), Y0 - 1, bz_(2.2)]) rotate([-90, 0, 0]) cylinder(d = btn_hole_d, h = wall + 2);
    // 套筒内孔：圆孔，与外壁孔同轴；Ø4.2 顶部桥接约 4 mm，不需要支撑
    translate([bx_(22), bay_floor, bz_(2.2)]) rotate([-90, 0, 0]) cylinder(d = btn_hole_d, h = sleeve_h + 0.1);
    // LED：不开孔，从舱内侧做盲孔，外面留 0.2 mm 透光薄壁，导光柱之后装入
    translate([bx_(26), Y0 + ko_t, bz_(7.3)]) rotate([-90, 0, 0]) cylinder(d = led_hole_d, h = wall);
    // J2 与风扇引线缺口：贯通的方孔，从背板一侧一直到前封板，线从背面直接放进去。
    // 先穿过舱顶板进入那一排空腔，再穿过风扇座底壁进入下角空腔，线沿侧壁空腔上到风扇座壁的窗口。
    // 两处缺口位置相同，在 J2 的 x 处；中层左右通用，两侧都开。
    for (ys = [bay_top, seat_slab_y0])
        translate([bx_(j2_u) - 7, ys - 0.5, plate_t - 1]) cube([14, 3, fan_z1 - plate_t + 1]);
    // 天线双面胶浅凹槽：电源口一侧外壁内面，竖直 60 x 8
    translate([xs_ > 0 ? -(X - wall) - 0.3 : (X - wall), ant_y0, pcb_zc - ant_w/2 - 0.2])
        cube([0.3 + 0.001, ant_l, ant_w + 0.4]);
}

// 可捅开的圆片：比孔小 2*ko_gap，厚 ko_t，位于外表面
module knockouts() {
    for (xs_ = [1, -1]) {
        zs_ = xs_;
        bx_ = function(u) xs_ * (pcb_xL + u);
        bz_ = function(v) pcb_zc - zs_ * v;
        wxo = xs_ > 0 ? -X : X;
        translate([xs_ > 0 ? wxo : wxo - ko_t, jack_y, pcb_zc]) rotate([0, 90, 0]) cylinder(d = jack_hole_d - 2*ko_gap, h = ko_t);
        translate([bx_(22), Y0, bz_(2.2)]) rotate([-90, 0, 0]) cylinder(d = btn_hole_d - 2*ko_gap, h = ko_t);
    }
}

// 按键导向套筒：舱底壁内侧，外形为菱形（45° 斜面，不需要支撑）
module sleeves() {
    for (xs_ = [1, -1]) {
        zs_ = xs_;
        translate([xs_ * (pcb_xL + 22), bay_floor - 0.01, pcb_zc - zs_ * 2.2]) rotate([-90, 0, 0])
            linear_extrude(sleeve_h + 0.01) rotate(45) square(sleeve_s, center = true);
    }
}

// 二维码板：外表面朝前（+z），沿 z 反射使板的外表面落在 mid_top
module qr_plate_placed(piece = "all") {
    translate([lbl_x, lbl_y, mid_top]) mirror([0, 0, 1])
        matter_label_plate(qr_passcode, qr_discriminator, qr_vid, qr_pid,
                           m = lbl_m, text_size = lbl_text, gap = lbl_gap, margin = lbl_margin,
                           thickness = front_t, iso_gap = lbl_iso, wall = lbl_wall,
                           mirror_pattern = false, piece = piece);
}

module mid() {
    union() {
    knockouts();
    difference() {
        union() {
          union() {
            translate([0, 0, plate_t]) linear_extrude(fan_z1 - plate_t) mid_profile();
            sleeves();
            // 定位块：风扇座壁左右平边中线处，伸入风扇外圈槽口
            for (sx = [-1, 1]) translate([sx > 0 ? fan_flat/2 + key_gap : -(fan_d/2 + fan_clr + seat_wall - 0.5), -key_w/2, fan_z1 - key_h])
                cube([fan_d/2 + fan_clr + seat_wall - 0.5 - (fan_flat/2 + key_gap), key_w, key_h]);
            // 整面前封板，只开风扇圆口
            translate([0, 0, fan_z1]) linear_extrude(front_t) difference() {
                translate([-X, Y0]) square([2*X, Sy]);
                circle(d = front_hole);
                // 二维码板占据的矩形先挖掉，由 matter_label_plate 填回去
                translate([lbl_x - lbl_w / 2 + eps, lbl_y - (lbl_gap + lbl_text + lbl_margin + lbl_ext) + eps])
                    square([lbl_w - 2 * eps, lbl_h - 2 * eps]);
            }
          }
            qr_plate_placed("body");     // 二维码板的白色部分；黑色岛单独输出（见 mid_island）
        }
        // 圆头螺钉（M3×18）：通孔 Ø3.4，从插槽底面向后穿到背板
        for (p = screw) translate([p[0], p[1], plate_t - 1]) cylinder(d = 3.4, h = slot_z0 - plate_t + 1.01);
        // 立柱插槽：外侧间隙 0.2，内侧让位 1.2；外侧壁上有 45° 斜面凹口，与卡钩配合
        for (i = [0 : len(latch) - 1]) translate([latch[i][0], latch[i][1], 0]) rotate([0, 0, latch_ang[i]]) {
            translate([-(post_x/2 + pocket_clr + pocket_relief), -pocket_y/2, slot_z0])
                cube([post_x + 2*pocket_clr + pocket_relief, pocket_y, mid_top]);
            // 口部倒角，便于插入
            translate([0, 0, mid_top - 1]) hull() {
                translate([-(post_x/2 + pocket_clr + pocket_relief), -pocket_y/2, 0])
                    cube([post_x + 2*pocket_clr + pocket_relief, pocket_y, 0.01]);
                translate([-(post_x/2 + pocket_clr + pocket_relief) - 1, -pocket_y/2 - 1, 1])
                    cube([post_x + 2*pocket_clr + pocket_relief + 2, pocket_y + 2, 0.01]);
            }
            // 凹口：下端斜面 45°；上端斜面 45°，与卡钩上斜面平行，卡钩被压入时贴合
            xo = post_x/2 + pocket_clr;
            rotate([90, 0, 0]) linear_extrude(pocket_y, center = true) polygon([
                [xo - 0.01, post_z0 + 0.8 + (xo - post_x/2)],
                [xo + recess_d, post_z0 + 0.8 + (xo + recess_d - post_x/2)],
                [xo + recess_d, post_z0 + 2*hook_out + hook_flat + 0.2 - (xo + recess_d - post_x/2) + 0.0],
                [xo - 0.01, post_z0 + 2*hook_out + hook_flat + 0.2 - (xo - post_x/2)]]);
        }
        side_cuts(1);
        side_cuts(-1);
        // 风扇引线出口窗口：引线在槽口旁约 20 mm 处出外圈，两侧平边、正负 y 都开
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx > 0 ? 56 : -67, sy > 0 ? wire_y0 : -wire_y1, plate_t - 1])
                cube([11, wire_y1 - wire_y0, fan_z1 - plate_t + 1]);
    }
    }
}

// ================= 顶盖：平板 + 4 立柱 =================
module cap() {
    translate([-X, Y0, cap_z0]) cube([2*X, Sy, cap_t]);
    for (i = [0 : len(latch) - 1]) { p = latch[i];
        // 腿：底面落在封板上，确定顶盖高度，也防止顶盖被压下去
        translate([p[0] - leg_x/2, p[1] - leg_y/2, mid_top]) cube([leg_x, leg_y, cap_z0 - mid_top + 0.01]);
        // 立柱：插入角柱的槽，配合间隙每边 0.2
        translate([p[0], p[1], post_z0]) rotate([0, 0, latch_ang[i]]) translate([-post_x/2, -post_y/2, 0]) cube([post_x, post_y, mid_top - post_z0 + 0.01]);
        // 卡钩：上下两端都是 45° 斜面，中间平直
        translate([p[0], p[1], 0]) rotate([0, 0, latch_ang[i]])
            rotate([90, 0, 0]) linear_extrude(post_y, center = true) polygon([
                [post_x/2 - 0.01, post_z0],
                [post_x/2 + hook_out, post_z0 + hook_out],
                [post_x/2 + hook_out, post_z0 + hook_out + hook_flat],
                [post_x/2 - 0.01, post_z0 + 2*hook_out + hook_flat]]);
    }
}

// ================= 后压条 =================
// 板后的压条 + 板子远端的挡块，L 形，沿 y 拉伸；L 版直接用，R 版绕 z 轴转 180° 掉头
module strip() {
    difference() {
        union() {
            translate([pcb_xL, comp_y0, plate_t]) cube([pcb_l, pcb_y1 - comp_y0, strip_t]);
            translate([board_end + 0.2, comp_y0, plate_t]) cube([1.4, pcb_y1 - comp_y0, fan_z1 - 0.2 - plate_t]);
        }
        // J2 出线边朝后时，线要从压条穿过
        translate([pcb_xL + j2_u - 7, comp_y0 + 1.5, plate_t - 1]) cube([14, pcb_y1 - comp_y0, strip_t + 2]);
    }
}
module strip_placed() {
    if (xs > 0) strip();
    else translate([0, comp_y0 + pcb_y1, 0]) rotate([0, 0, 180]) strip();
}

// ================= 仅用于预览的部件 =================
module fan() {
    translate([0, 0, fan_z0]) {
        color("dimgray") linear_extrude(fan_h) difference() { fan_env(0); circle(d = 116); }
        color("gray") cylinder(d = 40, h = fan_h);
    }
}
module foam_ring() {
    color("darkorange") translate([0, 0, plate_t]) linear_extrude(foam_t - foam_comp) difference() { circle(d = 124); circle(d = 116); }
}
module pcb_model() {
    x_lo = xs > 0 ? pcb_xL : -board_end;
    color("seagreen") translate([x_lo, pcb_y0, pcb_z0]) cube([pcb_l, pcb_t, pcb_w]);
    color("sienna")   translate([x_lo, comp_y0, pcb_z0]) cube([pcb_l, comp_h, pcb_w]);
    color("gold") translate([-xs * (X - wall) + (xs > 0 ? 0 : -0.2), ant_y0, pcb_zc - ant_w/2]) cube([0.2, ant_l, ant_w]);
}

// ================= 按键导杆与导光柱 =================
// 导杆：凸缘搭在套筒顶端挂住，杆向下穿过套筒和孔；z=0 为外端
module btn() {
    cylinder(d = btn_d, h = btn_len);
    translate([0, 0, btn_fl_z]) cylinder(d = btn_fl_d, h = btn_fl_t);
    translate([0, 0, btn_fl_z + btn_fl_t]) cylinder(d1 = btn_fl_d, d2 = btn_d, h = 1);   // 凸缘上侧 45° 倒角
}
module pipe() { cylinder(d = pipe_d, h = pipe_len); }

// ================= Matter 配对二维码与手动配对码 =================
// 下面这一段（QR 编码、七段数字、二维码板）复制自 matter-tbr 项目的 enclosure/enclosure.scad
// （https://github.com/googollee/matter-tbr，Apache-2.0），只改了一处：
// matter_label_plate 增加 mirror_pattern 参数（外表面朝上时不镜像图案）。
// 配对信息由 -D 传入（见 scripts/gen_enclosure_data.py），默认是 SDK 测试值。

/* ---------- 通用位运算工具（OpenSCAD 没有位运算符） ---------- */

function sum(v) = v * [for (x = v) 1];
function concat_all(v) = [for (a = v) for (b = a) b];
function zeros(n) = [for (i = [0 : 151]) if (i < n) 0];
function bits_lsb(n, len) = [for (i = [0 : len - 1]) floor(n / pow(2, i)) % 2];
function bits_msb(n, len) = [for (i = [len - 1 : -1 : 0]) floor(n / pow(2, i)) % 2];
function xor_n(a, b, n) =
  [for (i = [0 : n - 1]) (floor(a / pow(2, i)) + floor(b / pow(2, i))) % 2]
  * [for (i = [0 : n - 1]) pow(2, i)];
function join(l, i = 0) = i >= len(l) ? "" : str(l[i], join(l, i + 1));
function pad_zero(n, w) = let(s = str(n)) len(s) >= w ? s : str(join([for (i = [1 : w - len(s)]) "0"]), s);

/* ---------- Matter：QR payload（Base38）与手动配对码 ---------- */

B38 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";

// 88 bit payload，字段按 LSB 在前拼接
function matter_payload_bits(passcode, disc, vid, pid, flow, rendezvous) =
  concat_all([
    bits_lsb(0, 3),          // version
    bits_lsb(vid, 16),
    bits_lsb(pid, 16),
    bits_lsb(flow, 2),       // commissioning flow
    bits_lsb(rendezvous, 8), // 发现方式：1=SoftAP 2=BLE 4=OnNetwork
    bits_lsb(disc, 12),
    bits_lsb(passcode, 27),
    bits_lsb(0, 4)           // padding
  ]);

function matter_payload_bytes(passcode, disc, vid, pid, flow, rendezvous) =
  let(b = matter_payload_bits(passcode, disc, vid, pid, flow, rendezvous))
  [for (k = [0 : 10]) [for (i = [0 : 7]) b[8 * k + i]] * [1, 2, 4, 8, 16, 32, 64, 128]];

// 把 v 编成 n 个 Base38 字符，低位在前
function b38_chunk(v, n) = join([for (i = [0 : n - 1]) B38[floor(v / pow(38, i)) % 38]]);

function matter_qr_payload(passcode, discriminator, vid = 0, pid = 0, flow = 0, rendezvous = 2) =
  assert(passcode >= 1 && passcode <= 99999998, "passcode 必须在 1 ~ 99999998")
  assert(discriminator >= 0 && discriminator <= 4095, "discriminator 必须在 0 ~ 4095")
  let(b = matter_payload_bytes(passcode, discriminator, vid, pid, flow, rendezvous))
  str("MT:",
      join([for (g = [0 : 2]) b38_chunk(b[3 * g] + b[3 * g + 1] * 256 + b[3 * g + 2] * 65536, 5)]),
      b38_chunk(b[9] + b[10] * 256, 4));

// Verhoeff 校验
VD = [[0,1,2,3,4,5,6,7,8,9],[1,2,3,4,0,6,7,8,9,5],[2,3,4,0,1,7,8,9,5,6],
      [3,4,0,1,2,8,9,5,6,7],[4,0,1,2,3,9,5,6,7,8],[5,9,8,7,6,0,4,3,2,1],
      [6,5,9,8,7,1,0,4,3,2],[7,6,5,9,8,2,1,0,4,3],[8,7,6,5,9,3,2,1,0,4],
      [9,8,7,6,5,4,3,2,1,0]];
VP = [[0,1,2,3,4,5,6,7,8,9],[1,5,7,6,2,8,3,0,9,4],[5,8,0,3,7,9,6,1,4,2],
      [8,9,1,6,0,4,3,5,2,7],[9,4,5,3,1,2,6,8,7,0],[4,2,8,6,5,7,3,9,0,1],
      [2,7,9,3,8,0,6,4,1,5],[7,0,4,6,9,1,3,2,5,8]];
VINV = [0,4,3,2,1,5,6,7,8,9];

function verhoeff_c(ds, i = 0, c = 0) =
  i >= len(ds) ? c
  : verhoeff_c(ds, i + 1, VD[c][VP[(i + 1) % 8][ds[len(ds) - 1 - i]]]);
function verhoeff_check(s) =
  VINV[verhoeff_c([for (i = [0 : len(s) - 1]) search(s[i], "0123456789")[0]])];

// 11 位手动配对码（标准流程，不含 VID/PID），返回不带分隔符的字符串
function matter_manual_code(passcode, discriminator) =
  let(sd = floor(discriminator / 256),                 // 4 位 short discriminator
      digits = str(floor(sd / 4),
                   pad_zero((sd % 4) * 16384 + passcode % 16384, 5),
                   pad_zero(floor(passcode / 16384), 4)))
  str(digits, verhoeff_check(digits));

// 4-3-4 分组，便于阅读
function matter_manual_code_pretty(code) =
  str(substr(code, 0, 4), "-", substr(code, 4, 3), "-", substr(code, 7, 4));
function substr(s, start, n) = join([for (i = [start : start + n - 1]) s[i]]);

/* ---------- QR 编码：固定 Version 1 (21x21)、纠错等级 L、字母数字模式 ---------- */
// Matter payload 共 22 个字符（MT: + 19 位 Base38），Version 1-L 最多容纳 25 个。

QN = 21;
AN_TABLE = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// GF(256) 表（本原多项式 0x11D）
function gf_exp_build(i, e, acc) =
  i >= 255 ? acc
  : gf_exp_build(i + 1, e * 2 >= 256 ? xor_n(e * 2, 285, 9) : e * 2, concat(acc, [e]));
GF_EXP = gf_exp_build(0, 1, []);
GF_LOG = [for (v = [0 : 255]) v == 0 ? 0 : search(v, GF_EXP)[0]];
function gf_mul(a, b) = (a == 0 || b == 0) ? 0 : GF_EXP[(GF_LOG[a] + GF_LOG[b]) % 255];

// 7 个纠错码字的生成多项式系数
RS_GEN = [127, 122, 154, 164, 11, 68, 117];
function rs_step(rem, d) =
  let(f = xor_n(d, rem[0], 8))
  [for (i = [0 : 6]) xor_n(i < 6 ? rem[i + 1] : 0, gf_mul(RS_GEN[i], f), 8)];
function rs_rem(data, i = 0, rem = [0, 0, 0, 0, 0, 0, 0]) =
  i >= len(data) ? rem : rs_rem(data, i + 1, rs_step(rem, data[i]));

function an_val(c) = search(c, AN_TABLE)[0];
function an_bits(s) = concat_all([
  for (i = [0 : 2 : len(s) - 1])
    i + 1 < len(s) ? bits_msb(45 * an_val(s[i]) + an_val(s[i + 1]), 11)
                   : bits_msb(an_val(s[i]), 6)
]);

// 19 个数据码字
function qr_data_cw(s) =
  let(b0 = concat_all([[0, 0, 1, 0], bits_msb(len(s), 9), an_bits(s)]))
  assert(len(b0) <= 152, "文本太长，Version 1-L 放不下")
  let(b1 = concat(b0, zeros(min(4, 152 - len(b0)))),
      b2 = concat(b1, zeros((8 - len(b1) % 8) % 8)),
      bytes = [for (k = [0 : len(b2) / 8 - 1])
                 [for (i = [0 : 7]) b2[8 * k + i]] * [128, 64, 32, 16, 8, 4, 2, 1]])
  concat(bytes, [for (i = [0 : 18]) if (i >= len(bytes)) (i - len(bytes)) % 2 == 0 ? 236 : 17]);

function qr_codeword_bits(s) =
  let(d = qr_data_cw(s))
  concat_all([for (c = concat(d, rs_rem(d))) bits_msb(c, 8)]);

// 功能图形区域（定位图形+分隔符+格式信息、定时线）
function is_func(r, c) =
  (r < 9 && c < 9) || (r < 9 && c >= 13) || (r >= 13 && c < 9) || r == 6 || c == 6;

// 数据模块的蛇形填充顺序，值为 r*QN + c
DATA_ORDER = [
  for (j = [0 : 9])
    let(cr = j < 7 ? 20 - 2 * j : 19 - 2 * j)
    for (t = [0 : 20]) for (dc = [0 : 1])
      let(r = j % 2 == 0 ? 20 - t : t, c = cr - dc)
      if (!is_func(r, c)) r * QN + c
];

// 纠错等级 L 的 15 位格式信息，下标为掩码编号
FMT_L = [30660, 29427, 32170, 30877, 26159, 25368, 27713, 26998];

function fmt_idx(r, c) =
  c == 8 && r <= 5 ? r
  : c == 8 && r == 7 ? 6
  : r == 8 && c == 8 ? 7
  : r == 8 && c == 7 ? 8
  : r == 8 && c <= 5 ? 14 - c
  : r == 8 && c >= 13 ? 20 - c
  : c == 8 && r >= 14 ? r - 6
  : -1;

function finder_dark(r, c) =
  r == 0 || r == 6 || c == 0 || c == 6 || (r >= 2 && r <= 4 && c >= 2 && c <= 4);

function mask_flip(m, r, c) =
  (m == 0 ? (r + c) % 2
 : m == 1 ? r % 2
 : m == 2 ? c % 3
 : m == 3 ? (r + c) % 3
 : m == 4 ? (floor(r / 2) + floor(c / 3)) % 2
 : m == 5 ? (r * c) % 2 + (r * c) % 3
 : m == 6 ? ((r * c) % 2 + (r * c) % 3) % 2
 :          ((r + c) % 2 + (r * c) % 3) % 2) == 0;

function qr_dark(bits, mask, r, c) =
  !is_func(r, c) ? (bits[search(r * QN + c, DATA_ORDER)[0]] == 1) != mask_flip(mask, r, c)
  : (r < 7 && c < 7) ? finder_dark(r, c)
  : (r < 7 && c >= 14) ? finder_dark(r, c - 14)
  : (r >= 14 && c < 7) ? finder_dark(r - 14, c)
  : fmt_idx(r, c) >= 0 ? floor(FMT_L[mask] / pow(2, fmt_idx(r, c))) % 2 == 1
  : (r == 13 && c == 8) ? true
  : (r == 6 && c >= 8 && c <= 12) ? c % 2 == 0
  : (c == 6 && r >= 8 && r <= 12) ? r % 2 == 0
  : false;

// 返回 21x21 的 0/1 矩阵，matrix[行][列]，行 0 在最上
// mask 为掩码编号（0~7，任意一个都是合法的 QR 码）
function qr_matrix(text, mask = 0) =
  let(bits = qr_codeword_bits(text))
  [for (r = [0 : QN - 1]) [for (c = [0 : QN - 1]) qr_dark(bits, mask, r, c) ? 1 : 0]];

/* ---------- 几何 ---------- */

// 2D：深色模块，左下角为原点，m 为模块边长
module qr_2d(matrix, m = 1) {
  n = len(matrix);
  for (r = [0 : n - 1]) for (c = [0 : n - 1])
    if (matrix[r][c] == 1)
      translate([c * m, (n - 1 - r) * m]) square(m + 0.01);
}

// 七段数码管风格的数字，自己用矩形拼，不依赖系统字体。
// 左下角为原点，w×h 为字符框，t 为笔画宽度。段顺序 a b c d e f g：
//   a 上横  b 右上竖  c 右下竖  d 下横  e 左下竖  f 左上竖  g 中横
SEG7 = [[1,1,1,1,1,1,0], [0,1,1,0,0,0,0], [1,1,0,1,1,0,1], [1,1,1,1,0,0,1],
        [0,1,1,0,0,1,1], [1,0,1,1,0,1,1], [1,0,1,1,1,1,1], [1,1,1,0,0,0,0],
        [1,1,1,1,1,1,1], [1,1,1,1,0,1,1]];

module seg7_digit(d, w, h, t) {
  s = SEG7[d];
  half = (h + t) / 2;   // 单侧竖笔的高度，上下两段在中横处重叠
  if (s[0]) translate([0, h - t]) square([w, t]);
  if (s[1]) translate([w - t, (h - t) / 2]) square([t, half]);
  if (s[2]) translate([w - t, 0]) square([t, half]);
  if (s[3]) square([w, t]);
  if (s[4]) square([t, half]);
  if (s[5]) translate([0, (h - t) / 2]) square([t, half]);
  if (s[6]) translate([0, (h - t) / 2]) square([w, t]);
}

// 每个字符的前进距离：数字 w + sp，分隔符 "-" 用 dl + sp
function seg7_adv(c, w, dl, sp) = c == "-" ? dl + sp : w + sp;
function seg7_x(code, i, w, dl, sp) =
  i == 0 ? 0 : sum([for (j = [0 : i - 1]) seg7_adv(code[j], w, dl, sp)]);
function seg7_width(code, w, dl, sp) = seg7_x(code, len(code), w, dl, sp) - sp;

// 以原点为水平中心、垂直中心绘制一串数字（含 "-"）
module seg7_text(code, h, t, sp = 0.45) {
  w = 0.55 * h;       // 字符宽度
  dl = 0.4 * w;       // 分隔符长度
  total = seg7_width(code, w, dl, sp);
  translate([-total / 2, -h / 2])
    for (i = [0 : len(code) - 1]) {
      c = code[i];
      translate([seg7_x(code, i, w, dl, sp), 0])
        if (c == "-") translate([0, (h - t) / 2]) square([dl, t]);
        else seg7_digit(search(c, "0123456789")[0], w, h, t);
    }
}

// 2D：QR 居中于原点上方，配对码在其下方。可直接用于凹刻
// text_size 为数字高度，seg_t 为笔画宽度（建议 ≥ 0.7，即 2 条挤出线）
module matter_label_2d(passcode, discriminator, vid = 0, pid = 0, flow = 0,
                       rendezvous = 2, m = 1.2, text_size = 4, gap = 3, mask = 0,
                       seg_t = 0.7) {
  qr = qr_matrix(matter_qr_payload(passcode, discriminator, vid, pid, flow, rendezvous), mask);
  code = matter_manual_code_pretty(matter_manual_code(passcode, discriminator));
  w = len(qr) * m;

  translate([-w / 2, 0]) qr_2d(qr, m);
  translate([0, -gap - text_size / 2])
    seg7_text(code, text_size, seg_t);
}

// 3D：一块平板，图案凹刻在 z=0 的底面，从底部看是正读的。
// 单色打印机用换色实现黑白：按颜色拆成互不重叠的几块
//   z = 0 ~ depth                  白色整层，深色模块位置镂空（凹槽）
//   z = depth ~ depth + dark_t     黑色"岛"（只覆盖二维码区域）
//                                  + 白色外圈（外壳侧壁），两者之间留 iso_gap 的空隙
//   z = depth + dark_t ~ thickness 白色整层，盖住空隙
// 黑色岛与白色外圈在该层里是互相分离的两块，切片后黑色部分在 G-code 里是
// 一段连续的打印，只需在它前后各插入一次 M600（层高 0.2mm 时就是第 3 层）：
//   白色外圈 → M600 换黑 → 黑色岛 → M600 换白 → 下一层。
// 空隙在内部被上下白层封住，外面看不到；侧壁全程是白色，没有黑线。
// float_gap：黑色岛下表面与下方白色面之间的悬空间隙（岛区域内白色顶面下降 float_gap，
// 凹槽顶面仍是黑色岛）。注意 float_gap 小于层高时，切片软件会把它四舍五入掉。
// 打印面积会比二维码区域大 2 * (iso_gap + wall)，二维码和文字大小不变。
module matter_label_plate(passcode, discriminator, vid = 0, pid = 0, flow = 0,
                          rendezvous = 2, m = 1.5, text_size = 4, gap = 1,
                          mask = 0, thickness = 2, depth = 0.6, dark_t = 0.2,
                          margin = 3, iso_gap = 1.2, wall = 1.6, float_gap = 0.2,
                          mirror_pattern = true, piece = "all") {
  assert(float_gap >= 0 && float_gap < depth, "float_gap 必须在 0 ~ depth 之间");
  assert(thickness > depth + dark_t, "thickness 必须大于 depth + dark_t");
  qr_w = QN * m;
  plate_w = qr_w + 2 * margin;
  plate_h = qr_w + gap + text_size + 2 * margin;
  ext = iso_gap + wall;   // 外壳比黑色岛每边多出的宽度
  // 内容 y 范围：[-gap-text_size, qr_w]；e 为相对黑色岛每边外扩的距离
  module slab(z0, z1, e = 0)
    translate([-plate_w / 2 - e, -gap - text_size - margin - e, z0])
      cube([plate_w + 2 * e, plate_h + 2 * e, z1 - z0]);

  echo(str("黑色层 z=", depth, "~", depth + dark_t, "mm：白色外圈打完后 M600 换黑打黑色岛，",
           "岛打完后 M600 换回白"));
  echo(str("外壳尺寸 ", plate_w + 2 * ext, " x ", plate_h + 2 * ext, " mm"));

  if (piece != "island") color("white") difference() {
    slab(0, depth, ext);
    // 黑色岛正下方的白色顶面下降 float_gap，形成悬空间隙
    // 范围比黑色岛每边多出 iso_gap / 2，避免凹陷边缘与岛的底边重合（非流形）
    slab(depth - float_gap, depth + 0.01, iso_gap / 2);
    translate([0, 0, -0.01])
      linear_extrude(depth + 0.02)
        mirror([mirror_pattern ? 1 : 0, 0, 0])   // 外表面朝下（从底部看）时镜像，朝上时不镜像
          matter_label_2d(passcode, discriminator, vid, pid, flow, rendezvous,
                          m, text_size, gap, mask);
  }
  if (piece != "body") color("black") slab(depth, depth + dark_t);
  if (piece != "island") {
    color("white") difference() {
      slab(depth, depth + dark_t, ext);
      slab(depth - 0.01, depth + dark_t + 0.01, iso_gap);
    }
    color("white") slab(depth + dark_t, thickness, ext);
  }
}


// ================= 打印方向 =================
module print_back()  { translate([0, 0, plate_t]) rotate([180, 0, 0]) back(); }
module print_mid()   { translate([0, 0, mid_top]) rotate([180, 0, 0]) mid(); }
// 二维码的黑色岛：与 print_mid 同一个位置，单独上黑色
module print_mid_island() { translate([0, 0, mid_top]) rotate([180, 0, 0]) qr_plate_placed("island"); }
module print_cap()   { translate([0, 0, cap_z0 + cap_t]) rotate([180, 0, 0]) cap(); }
module print_btn()   { translate([0, 0, btn_len]) rotate([180, 0, 0]) btn(); }
module print_pipe()  { pipe(); }
module print_strip() { translate([0, fan_z1, -comp_y0]) rotate([90, 0, 0]) strip(); }

module cutaway() {
    if (section) difference() { children(); translate([0, -200, -100]) cube([300, 400, 400]); }
    else children();
}

// 打印件：颜色写进 3MF（二维码的黑色岛是黑色，其余白色），切片软件按颜色换料
if (part == "back")       color("white") print_back();
else if (part == "mid") {  // 中层：二维码的黑色岛是黑色，其余白色
    color("white") print_mid();
    color("black") print_mid_island();
}
else if (part == "mid_body")   color("white") print_mid();         // 中层白色主体（不含二维码黑色岛）
else if (part == "mid_island") color("black") print_mid_island();  // 二维码黑色岛

else if (part == "cap")   color("white") print_cap();
else if (part == "strip") color("white") print_strip();
else if (part == "btn")   color("white") print_btn();
else if (part == "pipe")  color("white") print_pipe();
else {
    // 爆炸图：沿 z 方向逐件拉开
    e = (part == "exploded") ? 1 : 0;
    cutaway() {
        translate([0, 0, -120 * e]) color("lightsteelblue") back();
        translate([0, 0,  -90 * e]) foam_ring();
        translate([0, 0,  -60 * e]) fan();
        translate([0, 0,  -60 * e]) color("yellow") strip_placed();
        translate([0, 0,  -30 * e]) pcb_model();
        color("skyblue") mid();
        color("black") qr_plate_placed("island");
        color("silver") translate([bx(22), btn_y0, bz(2.2)]) rotate([-90, 0, 0]) btn();
        color("white") translate([bx(26), pipe_y0, bz(7.3)]) rotate([-90, 0, 0]) pipe();
        translate([0, 0,   70 * e]) color("tomato") cap();
    }
}

// make enclosure 用这两行校验外壳算出的配对码与 config/ 一致
echo(payload = matter_qr_payload(qr_passcode, qr_discriminator, qr_vid, qr_pid));
echo(manual_code = matter_manual_code(qr_passcode, qr_discriminator));
