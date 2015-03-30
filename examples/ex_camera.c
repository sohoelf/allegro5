#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_color.h>
#include <allegro5/allegro_font.h>
#include <math.h>

#include "common.c"

#define pi ALLEGRO_PI

typedef struct {
   float x, y, z;
} Vector;

typedef struct {
   Vector position;
   Vector xaxis, yaxis, zaxis;
   double vertical_field_of_view;
} Camera;

typedef struct {
   Camera camera;

   /* controls sensitivity */
   double mouse_look_speed;
   double movement_speed;

   /* keyboard and mouse state */
   int button[10];
   int key[ALLEGRO_KEY_MAX];
   int keystate[ALLEGRO_KEY_MAX];
   int mouse_dx, mouse_dy;

   /* control scheme selection */
   int controls;
   char const *controls_names[3];

   /* the vertex data */
   int n, v_size;
   ALLEGRO_VERTEX *v;

   /* used to draw some info text */
   ALLEGRO_FONT *font;
} Example;

Example ex;

/* Calculate the dot product between two vectors. This corresponds to the
 * angle between them times their lengths.
 */
static double dot_product(Vector a, Vector b)
{
   return a.x * b.x + a.y * b.y + a.z * b.z;
}

/* Rotate the camera around the given axis. */
static void camera_rotate_around_axis(Camera *c, Vector axis, double radians)
{
   ALLEGRO_TRANSFORM t;
   al_identity_transform(&t);
   al_rotate_transform_3d(&t, axis.x, axis.y, axis.z, radians);
   al_transform_coordinates_3d(&t, &c->xaxis.x, &c->xaxis.y, &c->xaxis.z);
   al_transform_coordinates_3d(&t, &c->yaxis.x, &c->yaxis.y, &c->yaxis.z);
   al_transform_coordinates_3d(&t, &c->zaxis.x, &c->zaxis.y, &c->zaxis.z);
}

/* Move the camera along its x axis and z axis (which corresponds to
 * right and backwards directions).
 */
static void camera_move_along_direction(Camera *camera, double right,
   double forward)
{
   camera->position.x += camera->xaxis.x * right;
   camera->position.y += camera->xaxis.y * right;
   camera->position.z += camera->xaxis.z * right;
   camera->position.x += camera->zaxis.x * forward;
   camera->position.y += camera->zaxis.y * forward;
   camera->position.z += camera->zaxis.z * forward;
}

/* Get a vector with y = 0 looking in the same direction as the camera z axis.
 * If looking straight up or down returns a 0 vector instead.
 */
static Vector get_ground_forward_vector(Camera *camera)
{
   Vector move = {0, 0, 0};
   double zx = camera->zaxis.x;
   double zz = camera->zaxis.z;
   double z = sqrt(zx * zx + zz * zz);
   if (z > 0) {
      move.x += zx / z;
      move.z += zz / z;
   }
   return move;
}

/* Get a vector with y = 0 looking in the same direction as the camera x axis.
 * If looking straight up or down returns a 0 vector instead.
 */
static Vector get_ground_right_vector(Camera *camera)
{
   Vector move = {0, 0, 0};
   double xx = camera->xaxis.x;
   double xz = camera->xaxis.z;
   double x = sqrt(xx * xx + xz * xz);
   if (x > 0) {
      move.x += xx / x;
      move.z += xz / x;
   }
   return move;
}

/* Like camera_move_along_direction but moves the camera along the ground plane
 * only.
 */
static void camera_move_along_ground(Camera *camera, double right,
   double forward)
{
   Vector f = get_ground_forward_vector(camera);
   Vector r = get_ground_right_vector(camera);
   camera->position.x += f.x * forward + r.x * right;
   camera->position.z += f.z * forward + r.z * right;
}

/* Calculate the pitch of the camera. This is the angle between the z axis
 * vector and our direction vector on the y = 0 plane.
 */
static double get_pitch(Camera *c)
{
   Vector f = get_ground_forward_vector(c);
   return asin(dot_product(f, c->yaxis));
}

/* Calculate the yaw of the camera. This is basically the compass direction.
 */
static double get_yaw(Camera *c)
{
   return atan2(c->zaxis.x, c->zaxis.z);
}

/* Calculate the roll of the camera. This is the angle between the x axis
 * vector and its project on the y = 0 plane.
 */
static double get_roll(Camera *c)
{
   Vector r = get_ground_right_vector(c);
   return asin(dot_product(r, c->yaxis));
}

/* Set up a perspective transform. We make the screen span
 * 2 vertical units (-1 to +1) with square pixel aspect and the camera's
 * vertical field of view. Clip distance is always set to 1.
 */
static void setup_3d_projection(void)
{
   ALLEGRO_TRANSFORM projection;
   ALLEGRO_DISPLAY *display = al_get_current_display();
   double dw = al_get_display_width(display);
   double dh = al_get_display_height(display);
   al_identity_transform(&projection);
   al_translate_transform_3d(&projection, 0, 0, -1);
   double f = tan(ex.camera.vertical_field_of_view / 2);
   al_perspective_transform(&projection, -1 * dw / dh * f, f,
      1,
      f * dw / dh, -f, 1000);
   al_set_projection_transform(display, &projection);
}

/* Adds a new vertex to our scene. */
static void add_vertex(double x, double y, double z, ALLEGRO_COLOR color)
{
   int i = ex.n++;
   if (i >= ex.v_size) {
      ex.v_size += 1;
      ex.v_size *= 2;
      ex.v = realloc(ex.v, ex.v_size * sizeof *ex.v);
   }
   ex.v[i].x = x;
   ex.v[i].y = y;
   ex.v[i].z = z;
   ex.v[i].color = color;
}

/* Adds two triangles (6 vertices) to the scene. */
static void add_quad(double x, double y, double z,
   double ux, double uy, double uz,
   double vx, double vy, double vz, ALLEGRO_COLOR c1, ALLEGRO_COLOR c2)
{
   add_vertex(x, y, z, c1);
   add_vertex(x + ux, y + uy, z + uz, c1);
   add_vertex(x + vx, y + vy, z + vz, c2);
   add_vertex(x + vx, y + vy, z + vz, c2);
   add_vertex(x + ux, y + uy, z + uz, c1);
   add_vertex(x + ux + vx, y + uy + vy, z + uz + vz, c2);
}

/* Create a checkerboard made from colored quads. */
static void add_checkerboard(void)
{
   int x, y;
   ALLEGRO_COLOR c1 = al_color_name("yellow");
   ALLEGRO_COLOR c2 = al_color_name("green");

   for (y = 0; y < 20; y++) {
      for (x = 0; x < 20; x++) {
         double px = x - 20 * 0.5;
         double py = 0.2;
         double pz = y - 20 * 0.5;
         ALLEGRO_COLOR c = c1;
         if ((x + y) & 1) {
            c = c2;
            py -= 0.1;
         }
         add_quad(px, py, pz, 1, 0, 0, 0, 0, 1, c, c);
      }
   }
}

/* Create a skybox. This is simply 5 quads with a fixed distance to the
 * camera.
 */
static void add_skybox(void)
{
   Vector p = ex.camera.position;
   ALLEGRO_COLOR c1 = al_color_name("black");
   ALLEGRO_COLOR c2 = al_color_name("blue");
   ALLEGRO_COLOR c3 = al_color_name("white");

   /* Back skybox wall. */
   add_quad(p.x - 50, 0, p.z - 50, 100, 0, 0, 0, 50, 0, c1, c2);
   /* Front skybox wall. */
   add_quad(p.x - 50, 0, p.z + 50, 100, 0, 0, 0, 50, 0, c1, c2);
   /* Left skybox wall. */
   add_quad(p.x - 50, 0, p.z - 50, 0, 0, 100, 0, 50, 0, c1, c2);
   /* Right skybox wall. */
   add_quad(p.x + 50, 0, p.z - 50, 0, 0, 100, 0, 50, 0, c1, c2);

   /* Top of skybox. */
   add_vertex(p.x - 50, 50, p.z - 50, c2);
   add_vertex(p.x + 50, 50, p.z - 50, c2);
   add_vertex(p.x, 50, p.z, c3);

   add_vertex(p.x + 50, 50, p.z - 50, c2);
   add_vertex(p.x + 50, 50, p.z + 50, c2);
   add_vertex(p.x, 50, p.z, c3);

   add_vertex(p.x + 50, 50, p.z + 50, c2);
   add_vertex(p.x - 50, 50, p.z + 50, c2);
   add_vertex(p.x, 50, p.z, c3);

   add_vertex(p.x - 50, 50, p.z + 50, c2);
   add_vertex(p.x - 50, 50, p.z - 50, c2);
   add_vertex(p.x, 50, p.z, c3);
}

static void draw_scene(void)
{
   Camera *c = &ex.camera;
   ALLEGRO_DISPLAY *display = al_get_current_display();
   /* We save Allegro's projection so we can restore it for drawing text. */
   ALLEGRO_TRANSFORM projection = *al_get_projection_transform(display);

   setup_3d_projection();

   ALLEGRO_COLOR back = al_color_name("black");
   ALLEGRO_COLOR front = al_color_name("white");
   al_clear_to_color(back);

   /* We use a depth buffer. */
   al_set_render_state(ALLEGRO_DEPTH_TEST, 1);
   al_clear_depth_buffer(1);

   /* Recreate the entire scene geometry - this is only a very small example
    * so this is fine.
    */
   ex.n = 0;
   add_checkerboard();
   add_skybox();

   /* Construct a transform corresponding to our camera. This is an inverse
    * translation by the camera position, followed by an inverse rotation
    * from the camera orientation.
    */
   ALLEGRO_TRANSFORM t;
   double x = ex.camera.position.x;
   double y = ex.camera.position.y;
   double z = ex.camera.position.z;
   t.m[0][0] = ex.camera.xaxis.x;
   t.m[1][0] = ex.camera.xaxis.y;
   t.m[2][0] = ex.camera.xaxis.z;
   t.m[3][0] = t.m[0][0] * -x + t.m[1][0] * -y + t.m[2][0] * -z;
   t.m[0][1] = ex.camera.yaxis.x;
   t.m[1][1] = ex.camera.yaxis.y;
   t.m[2][1] = ex.camera.yaxis.z;
   t.m[3][1] = t.m[0][1] * -x + t.m[1][1] * -y + t.m[2][1] * -z;
   t.m[0][2] = ex.camera.zaxis.x;
   t.m[1][2] = ex.camera.zaxis.y;
   t.m[2][2] = ex.camera.zaxis.z;
   t.m[3][2] = t.m[0][2] * -x + t.m[1][2] * -y + t.m[2][2] * -z;
   t.m[0][3] = 0;
   t.m[1][3] = 0;
   t.m[2][3] = 0;
   t.m[3][3] = 1;
   al_use_transform(&t);
   al_draw_prim(ex.v, NULL, NULL, 0, ex.n, ALLEGRO_PRIM_TRIANGLE_LIST);

   /* Restore projection. */
   al_identity_transform(&t);
   al_use_transform(&t);
   al_set_projection_transform(display, &projection);

   /* Draw some text. */
   int th = al_get_font_line_height(ex.font);
   al_draw_textf(ex.font, front, 0, th * 0, 0,
      "look: %+3.1f/%+3.1f/%+3.1f (change with left mouse button and drag)",
         -c->zaxis.x, -c->zaxis.y, -c->zaxis.z);
   double pitch = get_pitch(c) * 180 / pi;
   double yaw = get_yaw(c) * 180 / pi;
   double roll = get_roll(c) * 180 / pi;
   al_draw_textf(ex.font, front, 0, th * 1, 0,
      "pitch: %+4.0f yaw: %+4.0f roll: %+4.0f", pitch, yaw, roll);
   al_draw_textf(ex.font, front, 0, th * 2, 0,
      "vertical field of view: %3.1f (change with Z/X)",
         c->vertical_field_of_view * 180 / pi);
   al_draw_textf(ex.font, front, 0, th * 3, 0, "move with WASD or cursor");
   al_draw_textf(ex.font, front, 0, th * 4, 0, "control style: %s (space to change)",
      ex.controls_names[ex.controls]);
}

static void setup_scene(void)
{
   ex.camera.xaxis.x = 1;
   ex.camera.yaxis.y = 1;
   ex.camera.zaxis.z = 1;
   ex.camera.position.y = 2;
   ex.camera.vertical_field_of_view = 60 * pi / 180;

   ex.mouse_look_speed = 0.03;
   ex.movement_speed = 0.05;

   ex.controls_names[0] = "FPS";
   ex.controls_names[1] = "airplane";
   ex.controls_names[2] = "spaceship";

   ex.font = al_create_builtin_font();
}

static void handle_input(void)
{
   double x = 0, y = 0;
   if (ex.key[ALLEGRO_KEY_A] || ex.key[ALLEGRO_KEY_LEFT]) x = -1;
   if (ex.key[ALLEGRO_KEY_S] || ex.key[ALLEGRO_KEY_DOWN]) y = 1;
   if (ex.key[ALLEGRO_KEY_D] || ex.key[ALLEGRO_KEY_RIGHT]) x = 1;
   if (ex.key[ALLEGRO_KEY_W] || ex.key[ALLEGRO_KEY_UP]) y = -1;

   /* Change field of view with Z/X. */
   if (ex.key[ALLEGRO_KEY_Z]) {
      ex.camera.vertical_field_of_view -= 0.01;
      double m = 20 * pi / 180;
      if (ex.camera.vertical_field_of_view < m)
         ex.camera.vertical_field_of_view = m;
   }
   if (ex.key[ALLEGRO_KEY_X]) {
      ex.camera.vertical_field_of_view += 0.01;
      double m = 120 * pi / 180;
      if (ex.camera.vertical_field_of_view > m)
         ex.camera.vertical_field_of_view = m;
   }

   /* In FPS style, always move the camera to height 2. */
   if (ex.controls == 0) {
      if (ex.camera.position.y > 2)
         ex.camera.position.y -= 0.1;
      if (ex.camera.position.y < 2)
         ex.camera.position.y = 2;
   }

   /* Set the roll (leaning) angle to 0 if not in airplane style. */
   if (ex.controls == 0 || ex.controls == 2) {
      double roll = get_roll(&ex.camera);
      camera_rotate_around_axis(&ex.camera, ex.camera.zaxis, roll / 60);
   }

   /* Move the camera, either freely or along the ground. */
   double xy = sqrt(x * x + y * y);
   if (xy > 0) {
      x /= xy;
      y /= xy;
      if (ex.controls == 0) {
         camera_move_along_ground(&ex.camera, ex.movement_speed * x,
            ex.movement_speed * y);
      }
      if (ex.controls == 1 || ex.controls == 2) {
         camera_move_along_direction(&ex.camera, ex.movement_speed * x,
            ex.movement_speed * y);
      }
         
   }

   /* Rotate the camera, either freely or around world up only. */
   if (ex.button[1]) {
      if (ex.controls == 0 || ex.controls == 2) {
         Vector up = {0, 1, 0};
         camera_rotate_around_axis(&ex.camera, ex.camera.xaxis,
            -ex.mouse_look_speed * ex.mouse_dy);
         camera_rotate_around_axis(&ex.camera, up,
            -ex.mouse_look_speed * ex.mouse_dx);
      }
      if (ex.controls == 1) {
         camera_rotate_around_axis(&ex.camera, ex.camera.xaxis,
            -ex.mouse_look_speed * ex.mouse_dy);
         camera_rotate_around_axis(&ex.camera, ex.camera.zaxis,
            -ex.mouse_look_speed * ex.mouse_dx);
      }
   }
}

int main(int argc, char **argv)
{
   ALLEGRO_DISPLAY *display;
   ALLEGRO_TIMER *timer;
   ALLEGRO_EVENT_QUEUE *queue;
   int redraw = 0;

   (void)argc;
   (void)argv;

   if (!al_init()) {
      abort_example("Could not init Allegro.\n");
   }
   al_init_font_addon();
   al_init_primitives_addon();
   init_platform_specific();
   al_install_keyboard();
   al_install_mouse();

   al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
   al_set_new_display_option(ALLEGRO_SAMPLES, 8, ALLEGRO_SUGGEST);
   al_set_new_display_option(ALLEGRO_DEPTH_SIZE, 16, ALLEGRO_SUGGEST);
   al_set_new_display_flags(ALLEGRO_RESIZABLE);
   display = al_create_display(640, 360);
   if (!display) {
      abort_example("Error creating display\n");
   }

   timer = al_create_timer(1.0 / 60);

   queue = al_create_event_queue();
   al_register_event_source(queue, al_get_keyboard_event_source());
   al_register_event_source(queue, al_get_mouse_event_source());
   al_register_event_source(queue, al_get_display_event_source(display));
   al_register_event_source(queue, al_get_timer_event_source(timer));

   setup_scene();

   al_start_timer(timer);
   while (true) {
      ALLEGRO_EVENT event;

      al_wait_for_event(queue, &event);
      if (event.type == ALLEGRO_EVENT_DISPLAY_CLOSE)
         break;
      else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
         al_acknowledge_resize(display);
      }
      else if (event.type == ALLEGRO_EVENT_KEY_DOWN) {
         if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE)
            break;
         if (event.keyboard.keycode == ALLEGRO_KEY_SPACE) {
            ex.controls++;
            ex.controls %= 3;
         }
         ex.key[event.keyboard.keycode] = 1;
         ex.keystate[event.keyboard.keycode] = 1;
      }
      else if (event.type == ALLEGRO_EVENT_KEY_UP) {
         /* In case a key gets pressed and immediately released, we will still
          * have set ex.key so it is not lost.
          */
         ex.keystate[event.keyboard.keycode] = 0;
      }
      else if (event.type == ALLEGRO_EVENT_TIMER) {
         handle_input();
         redraw = 1;

         /* Reset keyboard state for keys not held down anymore. */
         int i;
         for (i = 0; i < ALLEGRO_KEY_MAX; i++) {
            if (ex.keystate[i] == 0)
               ex.key[i] = 0;
         }
         ex.mouse_dx = 0;
         ex.mouse_dy = 0;
      }
      else if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
         ex.button[event.mouse.button] = 1;
      }
      else if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
         ex.button[event.mouse.button] = 0;
      }
      else if (event.type == ALLEGRO_EVENT_MOUSE_AXES) {
         ex.mouse_dx += event.mouse.dx;
         ex.mouse_dy += event.mouse.dy;
      }

      if (redraw  && al_is_event_queue_empty(queue)) {
         draw_scene();

         al_flip_display();
         redraw = 0;
      }
   }

   return 0;
}