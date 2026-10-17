/**
 * Clock Town Clock — static design iteration v2
 *
 * Watchface inspired by the Clock Tower clock from The Legend of Zelda:
 * Majora's Mask, for Pebble Time 2 (emery, 200x228 rectangular display).
 *
 * This is the STATIC design pass: the decorative clock face only, drawn
 * entirely with paint primitives. The time logic (minute ring rotating
 * counter-clockwise, hour disc rotating counter-clockwise, current value
 * always at the top) will be added in a later pass.
 *
 * Design references (workspace res/ folder):
 *  - clocktownclockcolourguide.jpg  (flat/abstract colour reference)
 *  - majoras-mask-...-clock.png     (textured "realistic" reference)
 */

#include <pebble.h>

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define R_OUTER    98   // outer edge of the minute ring
#define R_FACE     85   // radius of the face disc (inner edge of the ring)
#define R_TICK_IN  87   // minute tick marks
#define R_TICK_OUT 95
#define R_HOUR     58   // circle of the hour markers (hour disc at 6)

// ---------------------------------------------------------------------------
// Palette (from the Clock Town clock colour guide)
// ---------------------------------------------------------------------------

#define C_BG        GColorFromHEX(0xaa5500)  // screen background (wood)
#define C_INFO      GColorFromHEX(0xffffaa)  // corner icons + numbers
#define C_RING      GColorFromHEX(0x3a2a1a)  // minute ring (dark wood)
#define C_RING_EDGE GColorFromHEX(0x5c452a)  // ring outer edge highlight
#define C_RIM       GColorFromHEX(0x20150c)  // face rim + shape outlines
#define C_PEACH     GColorFromHEX(0xf2c49b)  // face base
#define C_YELLOW    GColorFromHEX(0xeec13a)  // wings + bottom collar
#define C_RED       GColorFromHEX(0xd43a2a)  // markings
#define C_WHITE     GColorFromHEX(0xf7f2e7)  // rivets / eye centres
#define C_TEAL      GColorFromHEX(0x1e8fa8)  // side bands + 3/6/9 markers
#define C_BLUE      GColorFromHEX(0x8aa6cf)  // side panels
#define C_GRAY      GColorFromHEX(0x9b9b97)  // muzzle
#define C_BROWN     GColorFromHEX(0x7c4a26)  // hub + hour disc
#define C_TAN       GColorFromHEX(0xcaa06c)  // minute ticks / screws
#define C_DARK      GColorFromHEX(0x1c130b)  // holes

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer *s_canvas_layer;
static GPoint s_center;

// Wing path: outer arc + zig-zag inner edge (13 points, relative coords)
static GPoint s_wing_r_points[13];
static GPoint s_wing_l_points[13];
static GPathInfo s_wing_r_info = { .num_points = 13, .points = s_wing_r_points };
static GPathInfo s_wing_l_info = { .num_points = 13, .points = s_wing_l_points };
static GPath *s_wing_r = NULL;
static GPath *s_wing_l = NULL;

// Brow flame (6 points, relative coords)
static GPoint s_brow_points[6] = {
  { -6, -14 }, { 4, -7 }, { -1, 0 }, { 9, 14 }, { -4, 4 }, { -9, 7 }
};
static GPathInfo s_brow_info = { .num_points = 6, .points = s_brow_points };
static GPath *s_brow_r = NULL;
static GPath *s_brow_l = NULL;

// Gray muzzle: shield shape with zig-zag top edge (22 points, relative)
static GPoint s_muzzle_points[22] = {
  { -42, -2 }, { -36, -14 }, { -28, -8 }, { -22, -20 }, { -14, -10 },
  { -7, -22 }, { 0, -12 }, { 7, -22 }, { 14, -10 }, { 22, -20 },
  { 28, -8 }, { 36, -14 }, { 42, -2 },
  { 44, 16 }, { 40, 34 }, { 30, 46 },
  { 16, 52 }, { 0, 54 }, { -16, 52 },
  { -30, 46 }, { -40, 34 }, { -44, 16 }
};
static GPathInfo s_muzzle_info = { .num_points = 22, .points = s_muzzle_points };
static GPath *s_muzzle = NULL;

// Nose teardrop triangle (relative to circle centre)
static GPoint s_nose_tri_points[3] = { { -5, 2 }, { 5, 2 }, { 0, 16 } };
static GPathInfo s_nose_tri_info = { .num_points = 3, .points = s_nose_tri_points };
static GPath *s_nose_tri = NULL;

// 12 o'clock teardrop triangle (relative to circle centre)
static GPoint s_top_drop_points[3] = { { -5, -2 }, { 5, -2 }, { 0, -8 } };
static GPathInfo s_top_drop_info = { .num_points = 3, .points = s_top_drop_points };
static GPath *s_top_drop = NULL;

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

static GPoint polar_off(int deg, int r) {
  int32_t a = DEG_TO_TRIGANGLE(deg);
  return GPoint((r * sin_lookup(a)) / TRIG_MAX_RATIO,
                -(r * cos_lookup(a)) / TRIG_MAX_RATIO);
}

static GPoint polar_c(int deg, int r) {
  GPoint p = polar_off(deg, r);
  return GPoint(s_center.x + p.x, s_center.y + p.y);
}

static GPoint polar_around(GPoint c, int deg, int r) {
  GPoint p = polar_off(deg, r);
  return GPoint(c.x + p.x, c.y + p.y);
}

static GRect radial_rect(int r) {
  return GRect(s_center.x - r, s_center.y - r, 2 * r, 2 * r);
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

static void draw_filled_path(GContext *ctx, GPath *path, GPoint at, GColor fill) {
  gpath_move_to(path, at);
  graphics_context_set_fill_color(ctx, fill);
  gpath_draw_filled(ctx, path);
}

static void outline_path(GContext *ctx, GPath *path, GColor color, int w) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, w);
  gpath_draw_outline(ctx, path);
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

static void draw_cloud_sun(GContext *ctx, GPoint c) {
  // Sun with short rays, peeking out behind the cloud
  GPoint sun = GPoint(c.x + 3, c.y - 2);
  graphics_context_set_stroke_color(ctx, C_INFO);
  graphics_context_set_stroke_width(ctx, 1);
  for (int a = 0; a < 360; a += 45) {
    graphics_draw_line(ctx, polar_around(sun, a, 5), polar_around(sun, a, 7));
  }
  fill_circle(ctx, sun, 3, C_INFO);
  // Cloud: bg-coloured halo first so it separates from the sun
  static const int puffs[3][3] = { { -4, 2, 4 }, { 1, -1, 5 }, { 6, 2, 3 } };
  for (int pass = 0; pass < 2; pass++) {
    GColor col = pass ? C_INFO : C_BG;
    int grow = pass ? 0 : 1;
    graphics_context_set_fill_color(ctx, col);
    for (int i = 0; i < 3; i++) {
      graphics_fill_circle(ctx, GPoint(c.x + puffs[i][0], c.y + puffs[i][1]),
                           puffs[i][2] + grow);
    }
    graphics_fill_rect(ctx, GRect(c.x - 4 - grow, c.y + 2, 10 + 2 * grow, 4 + grow),
                       0, GCornerNone);
  }
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
  draw_value(ctx, "4521", xl, 12);

  // Top-right: current weather
  draw_cloud_sun(ctx, GPoint(cxr - 2, 11));
  draw_value(ctx, "18\u00b0", xr, 14);

  // Bottom-left: heart rate
  draw_value(ctx, "72", xl, h - 38);
  draw_heart(ctx, GPoint(cxl, h - 10));

  // Bottom-right: date (weekday + calendar with day of month)
  draw_value(ctx, "SAT", xr, h - 41);
  draw_calendar(ctx, GPoint(cxr, h - 11), "17");
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // -- 1. Background --------------------------------------------------------
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // -- 2. Minute ring -------------------------------------------------------
  fill_circle(ctx, s_center, R_OUTER, C_RING);
  stroke_circle(ctx, s_center, R_OUTER - 1, C_RING_EDGE, 2);

  // -- 3. Face base ---------------------------------------------------------
  fill_circle(ctx, s_center, R_FACE, C_PEACH);

  // -- 4. Teal side bands ---------------------------------------------------
  graphics_context_set_stroke_color(ctx, C_TEAL);
  graphics_context_set_stroke_width(ctx, 6);
  graphics_draw_arc(ctx, radial_rect(79), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(65), DEG_TO_TRIGANGLE(115));
  graphics_draw_arc(ctx, radial_rect(79), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(245), DEG_TO_TRIGANGLE(295));

  // -- 5. Blue side panels --------------------------------------------------
  GPoint panel_r = GPoint(s_center.x + 48, s_center.y);
  GPoint panel_l = GPoint(s_center.x - 48, s_center.y);
  fill_circle(ctx, panel_r, 24, C_BLUE);
  fill_circle(ctx, panel_l, 24, C_BLUE);
  stroke_circle(ctx, panel_r, 24, C_RIM, 2);
  stroke_circle(ctx, panel_l, 24, C_RIM, 2);

  // -- 6. Gray muzzle (zig-zag top shield) ----------------------------------
  draw_filled_path(ctx, s_muzzle, s_center, C_GRAY);
  outline_path(ctx, s_muzzle, C_RIM, 2);

  // -- 7. Yellow wings ------------------------------------------------------
  graphics_context_set_fill_color(ctx, C_YELLOW);
  gpath_move_to(s_wing_r, s_center);
  gpath_draw_filled(ctx, s_wing_r);
  gpath_move_to(s_wing_l, s_center);
  gpath_draw_filled(ctx, s_wing_l);
  outline_path(ctx, s_wing_r, C_RIM, 2);
  outline_path(ctx, s_wing_l, C_RIM, 2);

  // -- 8. Red markings: brows, eye, nose ------------------------------------
  draw_filled_path(ctx, s_brow_r, GPoint(s_center.x + 27, s_center.y - 39), C_RED);
  draw_filled_path(ctx, s_brow_l, GPoint(s_center.x - 27, s_center.y - 39), C_RED);
  outline_path(ctx, s_brow_r, C_RIM, 2);
  outline_path(ctx, s_brow_l, C_RIM, 2);

  GPoint nose = GPoint(s_center.x, s_center.y - 38);
  fill_circle(ctx, nose, 7, C_RED);
  draw_filled_path(ctx, s_nose_tri, nose, C_RED);

  // -- 9. Yellow bottom collar (annular sector, carved with gray) -----------
  graphics_context_set_fill_color(ctx, C_YELLOW);
  graphics_fill_radial(ctx, radial_rect(76), GOvalScaleModeFitCircle, 0,
                       DEG_TO_TRIGANGLE(128), DEG_TO_TRIGANGLE(232));
  graphics_context_set_fill_color(ctx, C_GRAY);
  graphics_fill_radial(ctx, radial_rect(40), GOvalScaleModeFitCircle, 0,
                       DEG_TO_TRIGANGLE(123), DEG_TO_TRIGANGLE(237));
  // Collar outline: outer arc, inner arc, two end caps
  graphics_context_set_stroke_color(ctx, C_RIM);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_arc(ctx, radial_rect(76), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(128), DEG_TO_TRIGANGLE(232));
  graphics_draw_arc(ctx, radial_rect(40), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(128), DEG_TO_TRIGANGLE(232));
  graphics_draw_line(ctx, polar_c(128, 40), polar_c(128, 76));
  graphics_draw_line(ctx, polar_c(232, 40), polar_c(232, 76));

  // -- 10. Center hub -------------------------------------------------------
  fill_circle(ctx, s_center, 15, C_BROWN);
  fill_circle(ctx, s_center, 10, C_TEAL);
  fill_circle(ctx, s_center, 5, C_TAN);
  stroke_circle(ctx, s_center, 15, C_RIM, 2);

  // -- 11. Hour disc (bottom) -----------------------------------------------
  GPoint disc = polar_c(180, R_HOUR);
  fill_circle(ctx, disc, 24, C_BROWN);
  stroke_circle(ctx, disc, 24, C_RIM, 2);
  fill_circle(ctx, disc, 8, C_RED);
  // 12 hour dots arranged in a circle around the disc, each with a black outline
  for (int i = 0; i < 12; i++) {
    GPoint p = polar_around(disc, -90 + i * 30, 16);
    fill_circle(ctx, p, 3, C_RED);
    stroke_circle(ctx, p, 3, GColorBlack, 2);
  }

  // -- 12. Hour markers: 12 positions on one circle (0 deg = 12 o'clock).
  // 12/3/9 are red rings, 6 is the hour disc, the rest are white dots.
  for (int i = 0; i < 12; i++) {
    if (i == 6) continue;
    GPoint p = polar_c(i * 30, R_HOUR);
    if (i % 3 == 0) {
      int r = (i == 0) ? 11 : 9;
      fill_circle(ctx, p, r, C_RED);
      fill_circle(ctx, p, 4, C_WHITE);
      stroke_circle(ctx, p, r, GColorBlack, 2);
      stroke_circle(ctx, p, 4, GColorBlack, 1);
    } else {
      fill_circle(ctx, p, 5, C_WHITE);
      stroke_circle(ctx, p, 5, GColorBlack, 2);
    }
  }

  // -- 13. Face rim ----------------------------------------------------------
  stroke_circle(ctx, s_center, R_FACE - 1, C_RIM, 2);

  // -- 14. Minute ticks ------------------------------------------------------
  graphics_context_set_stroke_color(ctx, C_TAN);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 0; i < 60; i++) {
    graphics_draw_line(ctx, polar_c(i * 6, R_TICK_IN), polar_c(i * 6, R_TICK_OUT));
  }

  // -- 15. Screws at the diagonals ------------------------------------------
  static const int screw_angles[4] = { 45, 135, 225, 315 };
  for (int i = 0; i < 4; i++) {
    GPoint p = polar_c(screw_angles[i], 91);
    fill_circle(ctx, p, 3, C_TAN);
    fill_circle(ctx, p, 1, C_DARK);
  }

  // -- 16. Special ring markers ----------------------------------------------
  // Teal ovals at 3, 6 and 9 o'clock
  static const int oval_angles[3] = { 90, 180, 270 };
  for (int i = 0; i < 3; i++) {
    GPoint p = polar_c(oval_angles[i], 91);
    fill_circle(ctx, p, 6, C_TEAL);
    fill_circle(ctx, p, 3, C_DARK);
  }
  // Red teardrop at 12 o'clock
  GPoint top = polar_c(0, 90);
  fill_circle(ctx, top, 5, C_RED);
  draw_filled_path(ctx, s_top_drop, top, C_RED);
  fill_circle(ctx, top, 2, C_DARK);

  // -- 17. Corner metrics (static placeholder values) ------------------------
  draw_corners(ctx, bounds);
}

// ---------------------------------------------------------------------------
// Time handling (static design: redraw only, no time logic yet)
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
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

  // Build the wing polygons (outer arc + zig-zag inner edge)
  static const int wing_angles[13] = { 10, 25, 40, 55, 70, 80, 70, 62, 52, 44, 34, 26, 16 };
  static const int wing_radii[13]  = { 72, 72, 72, 72, 72, 72, 48, 30, 46, 28, 46, 30, 48 };
  for (int i = 0; i < 13; i++) {
    GPoint p = polar_off(wing_angles[i], wing_radii[i]);
    s_wing_r_points[i] = p;
    s_wing_l_points[i] = GPoint(-p.x, p.y);
  }

  s_wing_r = gpath_create(&s_wing_r_info);
  s_wing_l = gpath_create(&s_wing_l_info);
  s_brow_r = gpath_create(&s_brow_info);
  s_brow_l = gpath_create(&s_brow_info);
  s_muzzle = gpath_create(&s_muzzle_info);
  s_nose_tri = gpath_create(&s_nose_tri_info);
  s_top_drop = gpath_create(&s_top_drop_info);
  s_shoe = gpath_create(&s_shoe_info);
}

static void main_window_unload(Window *window) {
  (void)window;
  if (s_wing_r) { gpath_destroy(s_wing_r); s_wing_r = NULL; }
  if (s_wing_l) { gpath_destroy(s_wing_l); s_wing_l = NULL; }
  if (s_brow_r) { gpath_destroy(s_brow_r); s_brow_r = NULL; }
  if (s_brow_l) { gpath_destroy(s_brow_l); s_brow_l = NULL; }
  if (s_muzzle) { gpath_destroy(s_muzzle); s_muzzle = NULL; }
  if (s_nose_tri) { gpath_destroy(s_nose_tri); s_nose_tri = NULL; }
  if (s_top_drop) { gpath_destroy(s_top_drop); s_top_drop = NULL; }
  if (s_shoe) { gpath_destroy(s_shoe); s_shoe = NULL; }
  if (s_canvas_layer) { layer_destroy(s_canvas_layer); s_canvas_layer = NULL; }
}

// ---------------------------------------------------------------------------
// Application lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "clocktown-clock static design v4 (12 hour dots)");

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
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
