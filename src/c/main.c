/**
 * Clock Town Clock — static design
 *
 * Watchface inspired by the Clock Tower clock from The Legend of Zelda:
 * Majora's Mask, for Pebble Time 2 (emery, 200x228 rectangular display).
 *
 * The minute ring and the hour disc both turn counter-clockwise; the
 * current minute and hour are the marks at the top. The corners show steps,
 * current temperature (via PebbleKit JS + Open-Meteo), heart rate and date.
 * At night (sunset to sunrise, also from Open-Meteo) the sun disc becomes a moon.
 *
 * Design references (workspace res/ folder):
 *  - clocktownclockcolourguide.jpg  (flat colour guide, traced for the face)
 *  - majoras-mask-...-clock.png     (textured "realistic" reference)
 */

#include <pebble.h>

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define R_OUTER 99   // outer edge of the minute ring
#define R_FACE  75   // radius of the hour disc (inner edge of the ring)

// The face artwork is traced from the colour guide in "units": face radius
// = 400 units, x right, y down. 1 unit = 3/16 px, i.e. 3/4 quarter-pixel.
#define UQ(v) ((v) * 3 / 4)

// ---------------------------------------------------------------------------
// Palette (nearest 64-colour matches to the colour guide)
// ---------------------------------------------------------------------------

#define C_BG     GColorBulgarianRose  // screen background (dark wood)
#define C_INFO   GColorWhite          // corner icons + numbers
#define C_RING   GColorBlack          // minute ring
#define C_FRAME  GColorWindsorTan     // wooden frame around the ring
#define C_MARK   GColorPastelYellow   // minute ring carvings
#define C_OVAL   GColorVividCerulean  // 3/6/9 ovals on the ring
#define C_LINE   GColorBlack          // outlines
#define C_PEACH  GColorRajah
#define C_YELLOW GColorYellow
#define C_BLUE   GColorVeryLightBlue
#define C_TEAL   GColorCobaltBlue
#define C_SLATE  GColorDarkGray
#define C_GRAY   GColorLightGray
#define C_RED    GColorRed
#define C_WOOD   GColorWindsorTan     // hub + sun/moon disc
#define C_MOON   GColorJaegerGreen
#define C_WHITE  GColorWhite

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer *s_canvas_layer;
static GPoint s_center;
static int s_face_rot;   // hour disc rotation in degrees (counter-clockwise < 0)
static int s_ring_rot;     // minute ring rotation as drawn
static int s_ring_target;  // rotation for the current minute
static AppTimer *s_wiggle_timer;
static int s_wiggle_step;

// Corner texts
static char s_steps[12] = "--";
static char s_temp[8] = "--\u00b0";
static char s_hr[12] = "--";
static char s_wday[8] = "";
static char s_mday[4] = "";
static char s_time[6] = "";

// Weather icon (official Pebble timeline icons, see tools/weather_icons.py)
static GBitmap *s_weather_bmp;
static uint32_t s_weather_res;

// Sun is shown between sunrise and sunset (minutes after local midnight),
// the moon otherwise. Defaults until the phone sends the real times.
static int s_sunrise = 6 * 60;
static int s_sunset = 18 * 60;
static bool s_night;

// Sneaker icon for steps (relative to its top-left corner)
static GPoint s_shoe_points[9] = {
  { 1, 0 }, { 6, 0 }, { 7, 3 }, { 11, 4 }, { 15, 6 }, { 17, 9 }, { 17, 12 },
  { 0, 12 }, { 0, 1 }
};
static GPathInfo s_shoe_info = { .num_points = 9, .points = s_shoe_points };
static GPath *s_shoe = NULL;

// ---------------------------------------------------------------------------
// Geometry helpers (angle convention: 0 deg = 12 o'clock, clockwise)
// ---------------------------------------------------------------------------

static int rdiv(int32_t v, int32_t d) {
  return v >= 0 ? (v + d / 2) / d : -((-v + d / 2) / d);
}

// Point at radius r (outward) and tangential offset t (clockwise) around c,
// for a frame rotated by deg. t and r are in quarter pixels.
static GPoint local_pt(GPoint c, int deg, int t, int r) {
  int32_t a = DEG_TO_TRIGANGLE(((deg % 360) + 360) % 360);
  int32_t s = sin_lookup(a), co = cos_lookup(a);
  return GPoint(c.x + rdiv(r * s + t * co, 4 * TRIG_MAX_RATIO),
                c.y + rdiv(-r * co + t * s, 4 * TRIG_MAX_RATIO));
}

static GPoint polar_around(GPoint c, int deg, int r) {
  return local_pt(c, deg, 0, 4 * r);
}

// Face unit coordinates (x right, y down) -> screen
static GPoint face_pt(int x, int y) {
  return local_pt(s_center, s_face_rot, UQ(x), UQ(-y));
}

static GRect radial_rect(int r) {
  return GRect(s_center.x - r, s_center.y - r, 2 * r + 1, 2 * r + 1);
}

static void fill_circle(GContext *ctx, GPoint c, int r, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, c, r);
}

static void stroke_circle(GContext *ctx, GPoint c, int r, GColor color, int w) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, w);
  graphics_draw_circle(ctx, c, r);
}

// Fill (and optionally outline) a polygon given in screen points
static void draw_poly(GContext *ctx, GPoint *pts, int n, GColor fill, int line_w) {
  GPathInfo info = { .num_points = n, .points = pts };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, fill);
  gpath_draw_filled(ctx, p);
  if (line_w > 0) {
    graphics_context_set_stroke_color(ctx, C_LINE);
    graphics_context_set_stroke_width(ctx, line_w);
    gpath_draw_outline(ctx, p);
  }
  gpath_destroy(p);
}

#define MAX_PTS 48

// Polygon in face units; mirror = -1 flips it to the right-hand side
static void face_poly(GContext *ctx, const int16_t (*u)[2], int n, int mirror,
                      GColor fill) {
  GPoint pts[MAX_PTS];
  for (int i = 0; i < n; i++) pts[i] = face_pt(mirror * u[i][0], u[i][1]);
  draw_poly(ctx, pts, n, fill, 1);
}

// Left/right pair of a polygon traced on the left half
static void face_pair(GContext *ctx, const int16_t (*u)[2], int n, GColor fill) {
  face_poly(ctx, u, n, 1, fill);
  face_poly(ctx, u, n, -1, fill);
}

// Symmetric polygon: the left half runs from the axis back to the axis,
// the right half is its mirror image in reverse order
static void face_sym(GContext *ctx, const int16_t (*u)[2], int n, GColor fill) {
  GPoint pts[MAX_PTS];
  for (int i = 0; i < n; i++) {
    pts[i] = face_pt(u[i][0], u[i][1]);
    pts[2 * n - 1 - i] = face_pt(-u[i][0], u[i][1]);
  }
  draw_poly(ctx, pts, 2 * n, fill, 1);
}

// Polygon in a local (t, r) frame, quarter pixels, rotated by deg around c
static void local_poly(GContext *ctx, GPoint c, int deg, const int16_t (*q)[2],
                       int n, GColor fill, int line_w) {
  GPoint pts[MAX_PTS];
  for (int i = 0; i < n; i++) pts[i] = local_pt(c, deg, q[i][0], q[i][1]);
  draw_poly(ctx, pts, n, fill, line_w);
}

// Ellipse centred at (t, r) in the local frame, half axes ht x hr (quarter px)
static void local_oval(GContext *ctx, GPoint c, int deg, int t, int r,
                       int ht, int hr, GColor fill, int line_w) {
  GPoint pts[16];
  for (int i = 0; i < 16; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 16;
    pts[i] = local_pt(c, deg, t + ht * cos_lookup(a) / TRIG_MAX_RATIO,
                      r + hr * sin_lookup(a) / TRIG_MAX_RATIO);
  }
  draw_poly(ctx, pts, 16, fill, line_w);
}

// ---------------------------------------------------------------------------
// Corner metrics: icon + number in each screen corner, outside the clock
// ---------------------------------------------------------------------------

#define CORNER_W 40  // width of a corner column

static void draw_value(GContext *ctx, const char *text, int x, int y) {
  graphics_context_set_text_color(ctx, C_INFO);
  graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(x, y, CORNER_W, 22), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

static void draw_heart(GContext *ctx, GPoint c) {
  graphics_context_set_fill_color(ctx, C_INFO);
  graphics_fill_circle(ctx, GPoint(c.x - 3, c.y - 2), 3);
  graphics_fill_circle(ctx, GPoint(c.x + 3, c.y - 2), 3);
  GPoint tri[3] = { { c.x - 6, c.y - 1 }, { c.x + 6, c.y - 1 }, { c.x, c.y + 6 } };
  GPathInfo info = { .num_points = 3, .points = tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void draw_calendar(GContext *ctx, GPoint c, const char *day) {
  GRect r = GRect(c.x - 11, c.y - 9, 22, 18);
  graphics_context_set_fill_color(ctx, C_INFO);
  graphics_fill_rect(ctx, r, 2, GCornersAll);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, GRect(r.origin.x + 2, r.origin.y + 5, 18, 11), 0, GCornerNone);
  graphics_context_set_text_color(ctx, C_INFO);
  graphics_draw_text(ctx, day, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(r.origin.x, r.origin.y + 1, 22, 16), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

static void draw_corners(GContext *ctx, GRect bounds) {
  int w = bounds.size.w, h = bounds.size.h;
  int xl = 0, xr = w - CORNER_W, cxl = CORNER_W / 2, cxr = w - CORNER_W / 2;

  // Top-left: steps
  graphics_context_set_fill_color(ctx, C_INFO);
  GPoint shoe = GPoint(cxl - 9, 4);
  gpath_move_to(s_shoe, shoe);
  gpath_draw_filled(ctx, s_shoe);
  // Sole stripe and laces cut out in the background colour
  graphics_context_set_stroke_color(ctx, C_BG);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(shoe.x + 1, shoe.y + 9), GPoint(shoe.x + 16, shoe.y + 9));
  graphics_draw_line(ctx, GPoint(shoe.x + 8, shoe.y + 4), GPoint(shoe.x + 9, shoe.y + 6));
  graphics_draw_line(ctx, GPoint(shoe.x + 11, shoe.y + 5), GPoint(shoe.x + 12, shoe.y + 7));
  draw_value(ctx, s_steps, xl, 12);

  // Top-right: current weather
  if (s_weather_bmp) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_weather_bmp, GRect(cxr - 8, 1, 16, 16));
  }
  draw_value(ctx, s_temp, xr, 14);

  // Bottom-left: heart rate
  draw_value(ctx, s_hr, xl, h - 38);
  draw_heart(ctx, GPoint(cxl, h - 10));

  // Bottom-right: date (weekday + calendar with day of month)
  draw_value(ctx, s_wday, xr, h - 41);
  draw_calendar(ctx, GPoint(cxr, h - 11), s_mday);
}
// ---------------------------------------------------------------------------
// Minute ring: 60 carvings, hour positions marked by shields, the 12 o'clock
// position by a red omega and 3/6/9 by teal ovals. Shapes are in a local
// (t, r) frame in quarter pixels, so the whole ring can rotate later.
// ---------------------------------------------------------------------------

static const int16_t s_shield[9][2] = {
  { -24, 384 }, { 24, 384 }, { 22, 364 }, { 16, 344 }, { 8, 320 },
  { 0, 314 }, { -8, 320 }, { -16, 344 }, { -22, 364 }
};
static const int16_t s_shield_hole[5][2] = {
  { -10, 372 }, { 10, 372 }, { 6, 352 }, { 0, 340 }, { -6, 352 }
};
static const int16_t s_omega[15][2] = {
  { 0, 394 }, { 15, 382 }, { 22, 368 }, { 18, 354 }, { 8, 346 }, { 8, 332 },
  { 22, 324 }, { 22, 314 }, { -22, 314 }, { -22, 324 }, { -8, 332 },
  { -8, 346 }, { -18, 354 }, { -22, 368 }, { -15, 382 }
};

static void draw_ring(GContext *ctx, int rot) {
  fill_circle(ctx, s_center, R_OUTER, C_RING);
  stroke_circle(ctx, s_center, R_OUTER - 1, C_FRAME, 2);

  for (int m = 0; m < 60; m++) {
    int a = rot + m * 6;
    if (m == 0) {
      local_poly(ctx, s_center, a, s_omega, 15, C_RED, 1);
      local_oval(ctx, s_center, a, 0, 366, 8, 11, C_RING, 0);
    } else if (m % 15 == 0) {
      local_oval(ctx, s_center, a, 0, 350, 18, 30, C_OVAL, 0);
      local_oval(ctx, s_center, a, 0, 350, 7, 16, C_RING, 0);
    } else if (m % 5 == 0) {
      local_poly(ctx, s_center, a, s_shield, 9, C_MARK, 0);
      local_poly(ctx, s_center, a, s_shield_hole, 5, C_RING, 0);
    } else {
      // Tally bar; every other one is shorter with a dot beyond it
      bool dot = (m % 5 == 2 || m % 5 == 4);
      graphics_context_set_stroke_color(ctx, C_MARK);
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_line(ctx, polar_around(s_center, a, 80),
                         polar_around(s_center, a, dot ? 87 : 91));
      if (dot) fill_circle(ctx, polar_around(s_center, a, 93), 1, C_MARK);
    }
  }
}

// ---------------------------------------------------------------------------
// Hour disc: traced from the colour guide (face units, see UQ)
// ---------------------------------------------------------------------------

// Dark profile at the lower left rim (mirrored for the right)
static const int16_t s_slate[11][2] = {
  { -396, 56 }, { -380, 124 }, { -353, 188 }, { -315, 246 }, { -300, 200 },
  { -292, 160 }, { -305, 120 }, { -288, 90 }, { -300, 55 }, { -340, 35 },
  { -380, 40 }
};
// Yellow band along the bottom rim (left half, axis to axis)
static const int16_t s_bottom[10][2] = {
  { 0, 250 }, { -120, 305 }, { -175, 295 }, { -235, 250 }, { -275, 210 },
  { -315, 246 }, { -257, 306 }, { -169, 363 }, { -69, 394 }, { 0, 400 }
};
// Gray band: spikes behind the feathers, profiles at the sides
static const int16_t s_gray[20][2] = {
  { 0, -40 }, { -60, -40 }, { -110, -60 }, { -150, -70 }, { -180, -110 },
  { -195, -50 }, { -225, -120 }, { -235, -45 }, { -265, -100 }, { -262, -20 },
  { -248, 8 }, { -215, 30 }, { -212, 55 }, { -200, 72 }, { -196, 92 },
  { -180, 115 }, { -130, 100 }, { -90, 82 }, { -60, 120 }, { 0, 140 }
};
// Peach column between the wings, from the eye down to the hub
static const int16_t s_column[5][2] = {
  { 0, -300 }, { -90, -280 }, { -150, -80 }, { -100, -15 }, { 0, -15 }
};
// Upper wing with five feathers pointing down
static const int16_t s_wing[21][2] = {
  { -70, -378 }, { -140, -360 }, { -200, -335 }, { -250, -300 }, { -285, -250 },
  { -305, -200 }, { -320, -150 }, { -325, -105 }, { -300, -150 }, { -280, -70 },
  { -262, -140 }, { -240, -55 }, { -222, -130 }, { -195, -50 }, { -183, -135 },
  { -150, -75 }, { -140, -175 }, { -100, -235 }, { -65, -250 }, { -95, -290 },
  { -80, -340 }
};
// Lower wing with three feathers pointing up and out
static const int16_t s_low_wing[10][2] = {
  { -35, 128 }, { -90, 82 }, { -195, 148 }, { -140, 150 }, { -200, 205 },
  { -138, 190 }, { -180, 245 }, { -120, 270 }, { -100, 200 }, { -60, 150 }
};
// Red flame below each wing
static const int16_t s_claw[12][2] = {
  { -82, -202 }, { -108, -178 }, { -124, -148 }, { -118, -118 }, { -100, -102 },
  { -80, -98 }, { -70, -55 }, { -60, -100 }, { -52, -135 }, { -70, -150 },
  { -88, -158 }, { -85, -180 }
};
// Red needle pointing out from the hub at 9 (mirrored for 3)
static const int16_t s_needle[4][2] = {
  { -255, 0 }, { -98, -34 }, { -110, 0 }, { -98, 30 }
};
// Red arrowhead below the top eye
static const int16_t s_arrow[5][2] = {
  { 0, -262 }, { -18, -240 }, { -72, -215 }, { -30, -195 }, { 0, -130 }
};

static void draw_sun_disc(GContext *ctx, GPoint c, int rot) {
  fill_circle(ctx, c, 22, C_WOOD);
  stroke_circle(ctx, c, 22, C_LINE, 1);
  for (int i = 0; i < 8; i++) {
    // Squiggle between two dots
    int a = rot + 22 + i * 45;
    GPoint s[4] = { local_pt(c, a, 0, 44), local_pt(c, a, 4, 50),
                    local_pt(c, a, -4, 56), local_pt(c, a, 0, 62) };
    graphics_context_set_stroke_color(ctx, C_RED);
    graphics_context_set_stroke_width(ctx, 2);
    for (int k = 0; k < 3; k++) graphics_draw_line(ctx, s[k], s[k + 1]);
    fill_circle(ctx, polar_around(c, rot + i * 45, 17), 2, C_RED);
  }
  fill_circle(ctx, c, 8, C_RED);
  stroke_circle(ctx, c, 8, C_LINE, 1);
}

// Face sector from deg `from` clockwise to `to` (face frame), inset px deep
static void face_sector(GContext *ctx, int inset, int from, int to, GColor fill) {
  int a0 = (((from + s_face_rot) % 360) + 360) % 360;
  int a1 = a0 + (((to - from) % 360) + 360) % 360;
  GRect face = radial_rect(R_FACE);
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_radial(ctx, face, GOvalScaleModeFitCircle, inset,
                       DEG_TO_TRIGANGLE(a0), DEG_TO_TRIGANGLE(a1 < 360 ? a1 : 360));
  if (a1 > 360) {
    graphics_fill_radial(ctx, face, GOvalScaleModeFitCircle, inset,
                         0, DEG_TO_TRIGANGLE(a1 - 360));
  }
}

static void draw_moon_disc(GContext *ctx, GPoint c, int rot) {
  fill_circle(ctx, c, 22, C_WOOD);
  stroke_circle(ctx, c, 22, C_LINE, 1);
  // Green crescent: green disc with a wooden bite towards the upper left
  fill_circle(ctx, c, 17, C_MOON);
  stroke_circle(ctx, c, 17, C_LINE, 1);
  GPoint bite = local_pt(c, rot, -16, 20);
  fill_circle(ctx, bite, 13, C_WOOD);
  stroke_circle(ctx, bite, 13, C_LINE, 1);
  // Repaint the disc edge the bite overlapped, then the crater
  stroke_circle(ctx, c, 22, C_LINE, 1);
  GPoint crater = local_pt(c, rot, 32, -24);
  fill_circle(ctx, crater, 3, C_WOOD);
  stroke_circle(ctx, crater, 3, C_LINE, 1);
}

static void draw_face(GContext *ctx) {
  int rot = s_face_rot;

  // Background zones: blue sides, peach top, teal rim bands
  fill_circle(ctx, s_center, R_FACE, C_BLUE);
  face_sector(ctx, R_FACE, 308, 52, C_PEACH);
  face_sector(ctx, 10, 255, 318, C_TEAL);
  face_sector(ctx, 10, 42, 105, C_TEAL);
  face_pair(ctx, s_slate, 11, C_SLATE);
  face_sym(ctx, s_bottom, 10, C_YELLOW);

  // Figures, back to front
  face_sym(ctx, s_gray, 20, C_GRAY);
  face_sym(ctx, s_column, 5, C_PEACH);
  face_pair(ctx, s_low_wing, 10, C_YELLOW);
  face_pair(ctx, s_wing, 21, C_YELLOW);
  face_pair(ctx, s_claw, 12, C_RED);
  face_pair(ctx, s_needle, 4, C_RED);
  face_sym(ctx, s_arrow, 5, C_RED);

  // Hub
  fill_circle(ctx, s_center, 14, C_WOOD);
  stroke_circle(ctx, s_center, 14, C_LINE, 1);
  fill_circle(ctx, s_center, 10, C_TEAL);
  stroke_circle(ctx, s_center, 10, C_LINE, 1);
  fill_circle(ctx, s_center, 7, C_WOOD);
  stroke_circle(ctx, s_center, 7, C_LINE, 1);

  // Hour markers on one circle: eye at 12, red rings at 3/9, sun disc at 6,
  // white dots elsewhere
  local_oval(ctx, s_center, rot, 0, UQ(315), UQ(55), UQ(62), C_RED, 1);
  local_oval(ctx, s_center, rot, 0, UQ(315), UQ(24), UQ(32), C_WHITE, 1);
  for (int i = 1; i < 12; i++) {
    int a = rot + i * 30;
    if (i == 6) {
      GPoint p = polar_around(s_center, a, 49);
      if (s_night) draw_moon_disc(ctx, p, rot); else draw_sun_disc(ctx, p, rot);
    } else if (i % 3 == 0) {
      GPoint p = polar_around(s_center, a, 62);
      fill_circle(ctx, p, 10, C_RED);
      stroke_circle(ctx, p, 10, C_LINE, 2);
      fill_circle(ctx, p, 5, C_WHITE);
      stroke_circle(ctx, p, 5, C_LINE, 1);
    } else {
      GPoint p = polar_around(s_center, a, 58);
      fill_circle(ctx, p, 5, C_WHITE);
      stroke_circle(ctx, p, 5, C_LINE, 2);
    }
  }

  stroke_circle(ctx, s_center, R_FACE, C_LINE, 2);

  // Halo around the hour marker at the top (eye, ring, dot or sun disc)
  int h = ((-rot / 30) % 12 + 12) % 12;
  static const int8_t halo_y[4] = { 59, 62, 58, 49 };   // marker centre radius
  static const int8_t halo_r[4] = { 15, 13, 8, 25 };    // halo radius
  int kind = (h == 0) ? 0 : (h == 6) ? 3 : (h % 3 == 0) ? 1 : 2;
  GPoint top = GPoint(s_center.x, s_center.y - halo_y[kind]);
  stroke_circle(ctx, top, halo_r[kind], C_LINE, 4);
  stroke_circle(ctx, top, halo_r[kind], C_WHITE, 2);
}

// Pointer above the ring marking the 12 o'clock reading position
static void draw_pointer(GContext *ctx) {
  int y = s_center.y - R_OUTER;
  GPoint tri[3] = { { s_center.x - 7, y - 12 }, { s_center.x + 7, y - 12 },
                    { s_center.x, y - 1 } };
  draw_poly(ctx, tri, 3, C_WHITE, 0);
}

// Current time in the centre of the disc: white digits with a 2 px black
// outline. LECO 32 ink is 84x22 px for "20:08", so with the outline it stays
// inside the hour markers (inner edge 52 px from the centre).
static void draw_time_text(GContext *ctx) {
  GFont font = fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
  GRect box = GRect(s_center.x - 60, s_center.y - 21, 120, 34);  // ink centred
  graphics_context_set_text_color(ctx, C_LINE);
  for (int dx = -2; dx <= 2; dx++) {
    for (int dy = -2; dy <= 2; dy++) {
      if (dx || dy) {
        graphics_draw_text(ctx, s_time, font, GRect(box.origin.x + dx, box.origin.y + dy,
                           box.size.w, box.size.h), GTextOverflowModeFill,
                           GTextAlignmentCenter, NULL);
      }
    }
  }
  graphics_context_set_text_color(ctx, C_WHITE);
  graphics_draw_text(ctx, s_time, font, box, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_ring(ctx, s_ring_rot);
  draw_face(ctx);
  draw_pointer(ctx);
  draw_time_text(ctx);
  draw_corners(ctx, bounds);
}

// ---------------------------------------------------------------------------
// Data: time, health, weather
// ---------------------------------------------------------------------------

#define PERSIST_TEMP    1  // last temperature received from the phone
#define PERSIST_SUNRISE 2  // minutes after midnight
#define PERSIST_SUNSET  3
#define PERSIST_WEATHER 4  // WMO weather code

static void update_time(struct tm *t) {
  // Both turn counter-clockwise so the current value sits at the top
  s_ring_target = -t->tm_min * 6;
  s_face_rot = -(t->tm_hour % 12) * 30;
  int now = t->tm_hour * 60 + t->tm_min;
  s_night = now < s_sunrise || now >= s_sunset;
  strftime(s_time, sizeof(s_time), "%H:%M", t);
  strftime(s_wday, sizeof(s_wday), "%a", t);
  for (char *c = s_wday; *c; c++) {
    if (*c >= 'a' && *c <= 'z') *c -= 'a' - 'A';
  }
  snprintf(s_mday, sizeof(s_mday), "%d", t->tm_mday);
}

static bool health_available(HealthMetric metric, time_t start, time_t end) {
  return health_service_metric_accessible(metric, start, end) &
         HealthServiceAccessibilityMaskAvailable;
}

static void update_health(void) {
  time_t now = time(NULL);
  if (health_available(HealthMetricStepCount, time_start_of_today(), now)) {
    int steps = (int)health_service_sum_today(HealthMetricStepCount);
    if (steps < 10000) {
      snprintf(s_steps, sizeof(s_steps), "%d", steps);
    } else {
      snprintf(s_steps, sizeof(s_steps), "%d.%dk", steps / 1000, steps % 1000 / 100);
    }
  }
  HealthValue bpm = 0;
  if (health_available(HealthMetricHeartRateBPM, now, now)) {
    bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  }
  if (bpm > 0) {
    snprintf(s_hr, sizeof(s_hr), "%d", (int)bpm);
  } else {
    snprintf(s_hr, sizeof(s_hr), "--");
  }
}

// WMO weather code (as reported by Open-Meteo) -> icon resource
static uint32_t weather_icon(int code) {
  switch (code) {
    case 0: case 1:                     return RESOURCE_ID_WEATHER_SUN;
    case 2:                             return RESOURCE_ID_WEATHER_PARTLY_CLOUDY;
    case 3: case 45: case 48:           return RESOURCE_ID_WEATHER_CLOUDY;      // + fog
    case 51: case 53: case 55: case 56: case 57:                                 // drizzle
    case 61: case 80:                   return RESOURCE_ID_WEATHER_LIGHT_RAIN;
    case 63: case 65: case 81: case 82:
    case 95: case 96: case 99:          return RESOURCE_ID_WEATHER_HEAVY_RAIN;  // + storm
    case 66: case 67:                   return RESOURCE_ID_WEATHER_RAIN_SNOW;   // freezing rain
    case 71: case 77: case 85:          return RESOURCE_ID_WEATHER_LIGHT_SNOW;
    case 73: case 75: case 86:          return RESOURCE_ID_WEATHER_HEAVY_SNOW;
    default:                            return RESOURCE_ID_WEATHER_GENERIC;
  }
}

static void set_weather_code(int code) {
  uint32_t res = weather_icon(code);
  if (res == s_weather_res && s_weather_bmp) return;
  if (s_weather_bmp) gbitmap_destroy(s_weather_bmp);
  s_weather_bmp = gbitmap_create_with_resource(res);
  s_weather_res = res;
}

static void set_temp(int32_t temp) {
  snprintf(s_temp, sizeof(s_temp), "%d°", (int)temp);
}

static void request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *temp = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  if (temp) {
    set_temp(temp->value->int32);
    persist_write_int(PERSIST_TEMP, temp->value->int32);
  }
  Tuple *code = dict_find(iter, MESSAGE_KEY_WEATHER_CODE);
  if (code) {
    set_weather_code(code->value->int32);
    persist_write_int(PERSIST_WEATHER, code->value->int32);
  }
  Tuple *rise = dict_find(iter, MESSAGE_KEY_SUNRISE);
  Tuple *set = dict_find(iter, MESSAGE_KEY_SUNSET);
  if (rise && set) {
    s_sunrise = rise->value->int32;
    s_sunset = set->value->int32;
    persist_write_int(PERSIST_SUNRISE, s_sunrise);
    persist_write_int(PERSIST_SUNSET, s_sunset);
    time_t now = time(NULL);
    update_time(localtime(&now));
  }
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void health_handler(HealthEventType event, void *context) {
  update_health();
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// Minute change: the ring rocks back and forth like a mechanical escapement,
// then snaps to the new minute. Offsets are degrees from the old position
// (positive = backwards), one frame each.
#define WIGGLE_FRAME_MS 40
static const int8_t s_wiggle[] = { 1, -1, 1, -1 };

static void wiggle_cb(void *data) {
  int base = s_ring_rot - (s_wiggle_step ? s_wiggle[s_wiggle_step - 1] : 0);
  if (s_wiggle_step < (int)sizeof(s_wiggle)) {
    s_ring_rot = base + s_wiggle[s_wiggle_step++];
    s_wiggle_timer = app_timer_register(WIGGLE_FRAME_MS, wiggle_cb, NULL);
  } else {
    s_ring_rot = s_ring_target;
    s_wiggle_timer = NULL;
  }
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  update_health();
  if (tick_time->tm_min % 30 == 0) request_weather();
  if (s_wiggle_timer) {
    // Still wiggling from a previous tick (shouldn't happen): just snap
    app_timer_cancel(s_wiggle_timer);
    s_wiggle_timer = NULL;
    s_ring_rot = s_ring_target;
  } else {
    s_wiggle_step = 0;
    wiggle_cb(NULL);
  }
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  s_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_shoe = gpath_create(&s_shoe_info);
}

static void main_window_unload(Window *window) {
  (void)window;
  if (s_shoe) { gpath_destroy(s_shoe); s_shoe = NULL; }
  if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
}

// ---------------------------------------------------------------------------
// Application lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  if (persist_exists(PERSIST_TEMP)) set_temp(persist_read_int(PERSIST_TEMP));
  set_weather_code(persist_exists(PERSIST_WEATHER) ? persist_read_int(PERSIST_WEATHER) : -1);
  if (persist_exists(PERSIST_SUNSET)) {
    s_sunrise = persist_read_int(PERSIST_SUNRISE);
    s_sunset = persist_read_int(PERSIST_SUNSET);
  }
  time_t now = time(NULL);
  update_time(localtime(&now));
  s_ring_rot = s_ring_target;
  update_health();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_window, true);

  // The phone sends the weather on startup; the watch asks again every 30 min
  app_message_register_inbox_received(inbox_received);
  app_message_open(64, 64);
  health_service_events_subscribe(health_handler, NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  if (s_weather_bmp) gbitmap_destroy(s_weather_bmp);
  if (s_wiggle_timer) app_timer_cancel(s_wiggle_timer);
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  app_message_deregister_callbacks();
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
