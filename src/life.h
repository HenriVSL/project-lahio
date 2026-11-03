/* life.h - cosmetic life: cars, chimney smoke, water shimmer timing. WINDOWS SIDE. Owner: T11.
   DESIGN 5.8 (cosmetic cars), 9 (animation). Cosmetic only: never changes the simulation; uses its own
   Rng (not saved). The car logic is the portable module lifecar.h (Mac-tested); this file adds the
   view/camera glue, smoke, shimmer and drawing.

   Cars (up to LIFE_MAX_CARS, Lada sprites car<N>_<dir>) spawn on road tiles near the camera (in the ring
   just outside the view once the view is populated) with probability ~ road flow (traffic_flow(), or an
   estimate from nearby lots' occupants until traffic.cpp writes flow), drive tile to tile in the
   right-hand lane (STYLE.md 9: lane 3.6 units, gravel 2.5, main 4.8, motorway 5.0/9.6), turn at junctions
   biased toward higher-flow neighbours, U-turn at dead ends, keep a gap to the car ahead in their lane,
   leave the map at edge links and despawn off-screen or after a lifetime. Count = flow over the view x
   car ownership by year (1955 ~25%, 1970 100%) capped by population. Cars whose road disappears vanish.
   Smoke puffs rise from the chimney hotspot of active BDF_SMOKE lots near the camera, rate by occupancy,
   drifting with a light NE wind. Water shimmer: render_set_anim_frame() every 300 ms (keeps running
   while paused). Drawing goes through render_sprite (occlusion fix included), whole world pixels.

   game.cpp calls (GS_PLAY):
     life_init()                      once, after art_init/render_init
     life_reset()                     after a new city / load (zoom changes need nothing)
     life_update(dt)                  per frame in game_update, REAL wall-clock dt (also while paused):
                                      cars and smoke freeze by themselves when sim_speed() == SPEED_PAUSE;
                                      speeds 1-3 all animate at the same real-time rate. dt = 0 freezes
                                      everything (pause menu).
     life_draw()                      in game_draw between render_world() and render_flush_sprites()
   Optional: life_set_enabled (settings), life_traffic_level for the AMB_TRAFFIC ambient layer. */
#ifndef LIFE_H
#define LIFE_H
#include "common.h"

#define LIFE_MAX_CARS  160
#define LIFE_MAX_SMOKE 96

void life_init(void);
void life_reset(void);                   /* new city / load: drop all cars and puffs */
void life_update(double dt);             /* per frame, wall-clock seconds (see above) */
void life_draw(void);                    /* render_sprite submissions for the view */
void life_set_enabled(int cars, int smoke);
int  life_car_count(void);

/* ---- T11 additions */
int  life_traffic_level(void);           /* 0..100 from the cars near the view (snd_ambient AMB_TRAFFIC) */
typedef struct {
    int cars, cars_target, cars_visible, puffs, smoke_lots;
    int spawned, despawned;              /* totals since life_reset */
    double update_ms, draw_ms;           /* last frame */
} LifeStats;
void life_stats(LifeStats *out);
/* -test life (main.cpp dispatch): test town (render_build_test_town), cars + smoke + shimmer in a frame
   loop at sim speed 1, logs counts/spawns/despawns per second and ms per frame; honours -bench N
   (writes bench.txt), -shot T file, -exitafter T, -zoom, -cam X,Y. Returns 0 on pass. */
int  life_selftest(void);

#endif
