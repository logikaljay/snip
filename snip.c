// snip — macOS-style region screenshot for Hyprland (Cmd+Shift+4 feel).
// Drag a region: saves to ~/Pictures/Screenshots.
// Hold Ctrl when releasing: copies to the clipboard instead.
// Esc or right-click cancels.
// snip --full saves the whole screen; snip --full --copy copies it.
#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

typedef struct {
  GtkWidget *win, *area;
  GdkPixbuf *pixbuf;
  double scale;
  gboolean dragging;
  double x0, y0, x1, y1;
} Shot;

static GPtrArray *shots;
static gboolean ctrl_held = FALSE;
static gboolean done = FALSE;

static void redraw_all(void) {
  for (guint i = 0; i < shots->len; i++)
    gtk_widget_queue_draw(((Shot *)g_ptr_array_index(shots, i))->area);
}

static void draw(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer data) {
  Shot *s = data;
  if (!s->dragging) return;
  double x = MIN(s->x0, s->x1), y = MIN(s->y0, s->y1);
  double rw = fabs(s->x1 - s->x0), rh = fabs(s->y1 - s->y0);

  // Dim everything outside the selection.
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_rectangle(cr, x, y, rw, rh);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_fill(cr);

  // Border: white when saving, blue when copying (Ctrl held).
  cairo_rectangle(cr, x + 0.5, y + 0.5, rw, rh);
  if (ctrl_held) cairo_set_source_rgba(cr, 0.25, 0.6, 1.0, 1);
  else cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);

  // Size label next to the cursor, macOS-style.
  char label[64];
  snprintf(label, sizeof label, "%d × %d%s", (int)(rw * s->scale), (int)(rh * s->scale),
           ctrl_held ? "  ⧉ copy" : "");
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12);
  cairo_text_extents_t te;
  cairo_text_extents(cr, label, &te);
  double lx = s->x1 + 14, ly = s->y1 + 24;
  if (lx + te.width + 12 > w) lx = s->x1 - te.width - 22;
  if (ly + 8 > h) ly = s->y1 - 14;
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_rectangle(cr, lx - 6, ly - te.height - 5, te.width + 12, te.height + 10);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_move_to(cr, lx, ly);
  cairo_show_text(cr, label);
}

static char *output_dir(void) {
  const char *env = g_getenv("OMARCHY_SCREENSHOT_DIR");
  if (env && *env) return g_strdup(env);
  const char *pics = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
  return g_build_filename(pics ? pics : g_get_home_dir(), "Screenshots", NULL);
}

// Copy a capture to the clipboard, or save it and notify.
static void output(GdkPixbuf *img, gboolean copy) {
  GError *err = NULL;
  if (copy) {
    gchar *buf; gsize len;
    if (gdk_pixbuf_save_to_buffer(img, &buf, &len, "png", &err, NULL)) {
      GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE, &err,
                                        "wl-copy", "--type", "image/png", NULL);
      if (p) {
        GBytes *b = g_bytes_new_take(buf, len);
        g_subprocess_communicate(p, b, NULL, NULL, NULL, &err);
        g_bytes_unref(b);
        g_object_unref(p);
      }
    }
  } else {
    char *dir = output_dir();
    g_mkdir_with_parents(dir, 0755);
    GDateTime *now = g_date_time_new_now_local();
    char *stamp = g_date_time_format(now, "%Y-%m-%d at %H.%M.%S");
    char *name = g_strdup_printf("Screenshot %s.png", stamp);
    char *path = g_build_filename(dir, name, NULL);
    if (gdk_pixbuf_save(img, path, "png", &err, NULL)) {
      const char *argv[] = {"notify-send", "-a", "Screenshot", "-i", path,
                            "Screenshot saved", name, NULL};
      g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    }
    g_free(path); g_free(name); g_free(stamp); g_free(dir);
    g_date_time_unref(now);
  }
  if (err) { g_printerr("snip: %s\n", err->message); g_error_free(err); }
}

static void finish(Shot *s, gboolean copy) {
  done = TRUE;
  double x = MIN(s->x0, s->x1), y = MIN(s->y0, s->y1);
  double rw = fabs(s->x1 - s->x0), rh = fabs(s->y1 - s->y0);
  int pw = gdk_pixbuf_get_width(s->pixbuf), ph = gdk_pixbuf_get_height(s->pixbuf);
  int cx = CLAMP((int)round(x * s->scale), 0, pw - 1);
  int cy = CLAMP((int)round(y * s->scale), 0, ph - 1);
  int cw = CLAMP((int)round(rw * s->scale), 1, pw - cx);
  int ch = CLAMP((int)round(rh * s->scale), 1, ph - cy);
  GdkPixbuf *crop = gdk_pixbuf_new_subpixbuf(s->pixbuf, cx, cy, cw, ch);

  for (guint i = 0; i < shots->len; i++)
    gtk_widget_set_visible(((Shot *)g_ptr_array_index(shots, i))->win, FALSE);

  output(crop, copy);
  g_object_unref(crop);
  g_application_quit(g_application_get_default());
}

// snip --full [--copy]: capture every monitor (one image), no overlay.
static int full_capture(gboolean copy) {
  char *path = g_strdup_printf("%s/snip-%d-full.ppm", g_get_user_runtime_dir(), getpid());
  const char *argv[] = {"grim", "-t", "ppm", path, NULL};
  int status = 1;
  GdkPixbuf *pb = NULL;
  if (g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL,
                   &status, NULL) && status == 0)
    pb = gdk_pixbuf_new_from_file(path, NULL);
  g_unlink(path);
  g_free(path);
  if (!pb) { g_printerr("snip: capture failed\n"); return 1; }
  output(pb, copy);
  g_object_unref(pb);
  return 0;
}

static void drag_begin(GtkGestureDrag *g, double x, double y, gpointer data) {
  Shot *s = data;
  s->dragging = TRUE;
  s->x0 = s->x1 = x; s->y0 = s->y1 = y;
  gtk_widget_queue_draw(s->area);
}

static void drag_update(GtkGestureDrag *g, double dx, double dy, gpointer data) {
  Shot *s = data;
  s->x1 = s->x0 + dx; s->y1 = s->y0 + dy;
  gtk_widget_queue_draw(s->area);
}

static void drag_end(GtkGestureDrag *g, double dx, double dy, gpointer data) {
  Shot *s = data;
  s->x1 = s->x0 + dx; s->y1 = s->y0 + dy;
  GdkModifierType st = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(g));
  gboolean copy = (st & GDK_CONTROL_MASK) || ctrl_held;
  if (fabs(dx) < 3 && fabs(dy) < 3) {  // a click, not a drag: keep waiting
    s->dragging = FALSE;
    gtk_widget_queue_draw(s->area);
    return;
  }
  if (!done) finish(s, copy);
}

static gboolean on_modifiers(GtkEventControllerKey *k, GdkModifierType st, gpointer d) {
  gboolean c = (st & GDK_CONTROL_MASK) != 0;
  if (c != ctrl_held) { ctrl_held = c; redraw_all(); }
  return FALSE;
}

static gboolean on_key(GtkEventControllerKey *k, guint kv, guint kc, GdkModifierType st, gpointer d) {
  if (kv == GDK_KEY_Escape) { g_application_quit(g_application_get_default()); return TRUE; }
  if (kv == GDK_KEY_Control_L || kv == GDK_KEY_Control_R) { ctrl_held = TRUE; redraw_all(); }
  return FALSE;
}

static void on_key_release(GtkEventControllerKey *k, guint kv, guint kc, GdkModifierType st, gpointer d) {
  if (kv == GDK_KEY_Control_L || kv == GDK_KEY_Control_R) { ctrl_held = FALSE; redraw_all(); }
}

static void on_right_click(GtkGestureClick *g, int n, double x, double y, gpointer d) {
  g_application_quit(g_application_get_default());
}

static void activate(GtkApplication *app, gpointer data) {
  GListModel *mons = gdk_display_get_monitors(gdk_display_get_default());
  guint n = g_list_model_get_n_items(mons);
  const char *rt = g_get_user_runtime_dir();
  GPtrArray *procs = g_ptr_array_new_with_free_func(g_object_unref);
  GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

  // Freeze every monitor first (grim in parallel), then show the overlays.
  for (guint i = 0; i < n; i++) {
    GdkMonitor *m = g_list_model_get_item(mons, i);
    char *path = g_strdup_printf("%s/snip-%d-%u.ppm", rt, getpid(), i);
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "grim", "-t", "ppm",
                                      "-o", gdk_monitor_get_connector(m), path, NULL);
    g_ptr_array_add(procs, p);
    g_ptr_array_add(paths, path);
    g_object_unref(m);
  }

  for (guint i = 0; i < n; i++) {
    GSubprocess *p = g_ptr_array_index(procs, i);
    const char *path = g_ptr_array_index(paths, i);
    if (!p || !g_subprocess_wait_check(p, NULL, NULL)) continue;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, NULL);
    g_unlink(path);
    if (!pb) continue;

    GdkMonitor *m = g_list_model_get_item(mons, i);
    GdkRectangle geo;
    gdk_monitor_get_geometry(m, &geo);

    Shot *s = g_new0(Shot, 1);
    s->pixbuf = pb;
    s->scale = (double)gdk_pixbuf_get_width(pb) / geo.width;

    s->win = gtk_application_window_new(app);
    gtk_layer_init_for_window(GTK_WINDOW(s->win));
    gtk_layer_set_namespace(GTK_WINDOW(s->win), "snip");
    gtk_layer_set_layer(GTK_WINDOW(s->win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_monitor(GTK_WINDOW(s->win), m);
    gtk_layer_set_exclusive_zone(GTK_WINDOW(s->win), -1);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(s->win), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    for (int e = 0; e < GTK_LAYER_SHELL_EDGE_ENTRY_NUMBER; e++)
      gtk_layer_set_anchor(GTK_WINDOW(s->win), e, TRUE);

    GtkWidget *overlay = gtk_overlay_new();
    GdkTexture *tex = gdk_texture_new_for_pixbuf(pb);
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(tex));
    g_object_unref(tex);
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_FILL);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), pic);

    s->area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(s->area), draw, s, NULL);
    gtk_widget_set_cursor_from_name(s->area, "crosshair");
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), s->area);
    gtk_window_set_child(GTK_WINDOW(s->win), overlay);

    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), s);
    g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), s);
    g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), s);
    gtk_widget_add_controller(s->area, GTK_EVENT_CONTROLLER(drag));

    GtkGesture *rc = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rc), GDK_BUTTON_SECONDARY);
    g_signal_connect(rc, "pressed", G_CALLBACK(on_right_click), NULL);
    gtk_widget_add_controller(s->area, GTK_EVENT_CONTROLLER(rc));

    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key), NULL);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_release), NULL);
    g_signal_connect(key, "modifiers", G_CALLBACK(on_modifiers), NULL);
    gtk_widget_add_controller(s->win, key);

    g_ptr_array_add(shots, s);
    gtk_window_present(GTK_WINDOW(s->win));
    g_object_unref(m);
  }
  g_ptr_array_unref(procs);
  g_ptr_array_unref(paths);
  if (shots->len == 0) { g_printerr("snip: capture failed\n"); g_application_quit(G_APPLICATION(app)); }
}

int main(int argc, char **argv) {
  gboolean full = FALSE, copy = FALSE;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--full")) full = TRUE;
    else if (!strcmp(argv[i], "--copy")) copy = TRUE;
  }
  if (full) return full_capture(copy);
  shots = g_ptr_array_new();
  GtkApplication *app = gtk_application_new("dev.local.snip", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int r = g_application_run(G_APPLICATION(app), 1, argv);
  g_object_unref(app);
  return r;
}
