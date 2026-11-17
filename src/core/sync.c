#include "core/sync.h"
#include "core/app.h"
#include "core/i18n.h"
#include "screens/board.h"
#include "screens/player.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdio.h>

#define BLUR_AWAY_S 30.0   /* the focus gone this long (and nothing else said): counted as the back */
#define BLUR_SYNC_S 10.0   /* the focus back after this long: the library is pulled, as after the back */

static double bootAt, idleAt; static int libBoot, addonBoot;
static int rebuildWanted; static double rebuildSince;

/* what SDL's event watch saw (it may run on another thread than the frame loop): queued under a lock, read once a
   frame. Only the frame loop keeps state */
static pthread_mutex_t evMu = PTHREAD_MUTEX_INITIALIZER; static signed char evQ[32]; static int evN, evLost;
static int SDLCALL on_event(void *ud, SDL_Event *e) {
  (void)ud; int l = plat_lifecycle(e); if (!l) return 0;
  pthread_mutex_lock(&evMu); if (evN < (int)sizeof evQ) evQ[evN++] = (signed char)l; else evLost++; pthread_mutex_unlock(&evMu);
  return 0;
}
/* the frame loop's view: away (at the back), blurred (only the focus is elsewhere) */
static int away, blurred; static double awayAt, blurAt;
int sync_away(void) { return away; }
int sync_blurred(void) { return blurred && !away; }
double sync_away_for(void) { return away ? app_now() - awayAt : 0; }
static void to_back(const char *why) { if (away) return; away = 1; awayAt = app_now(); fprintf(stderr, "[life] to the back (%s)\n", why); }
static int to_front(const char *why) { if (!away) return 0; away = 0; fprintf(stderr, "[life] back in front after %.0f s (%s)\n", app_now() - awayAt, why); return 1; }
static void lifecycle(void) {
  signed char q[32]; int n, lost;
  pthread_mutex_lock(&evMu); n = evN; lost = evLost; for (int i = 0; i < n; i++) q[i] = evQ[i]; evN = 0; evLost = 0; pthread_mutex_unlock(&evMu);
  if (lost) fprintf(stderr, "[life] %d window event(s) came faster than they were read\n", lost);
  double now = app_now(); int resumed = 0;
  for (int i = 0; i < n; i++) switch (q[i]) {
    case -1: blurred = 0; to_back("the TV said so"); break;
    case 1: blurred = 0; resumed |= to_front("the TV said so"); break;
    case -2: if (!blurred && !away) { blurred = 1; blurAt = now; fprintf(stderr, "[life] the focus is elsewhere (a system keyboard or menu over the app): the app stays as it is\n"); } break;
    case 2: if (blurred) { blurred = 0; double for_ = now - blurAt; if (for_ >= BLUR_SYNC_S) resumed = 1; fprintf(stderr, "[life] the focus is back after %.1f s\n", for_); }
            resumed |= to_front("the focus came back"); break; }
  if (blurred && !away && now - blurAt >= BLUR_AWAY_S) { blurred = 0; to_back("the focus stayed away"); }
  if (resumed && sb_signed_in()) sb_sync_soon(1); /* back from another app, or relaunched */
}
/* a pull that changed the library: applied in a pause of the remote (a rebuild in the middle of a move is a
   hitch), at most 5 s later */
static void on_lib(int changed, void *ud) { (void)ud; if (!changed) return; board_invalidate(); if (!rebuildWanted) { rebuildWanted = 1; rebuildSince = app_now(); } }
static void on_addons(int changed, void *ud) {
  (void)ud; if (!changed) return;
  toast(T("Addons updated"), 2600, NULL);
  board_invalidate(); if (SCR == SCR_HOME && !player_open()) board_rebuild();
}
void sb_sync_soon(int force) {
  if (!libBoot) return; /* the pull a moment after start covers it */
  if (!force && sb_lib_sync_age() < 60000) return;
  sb_pull_library(on_lib, NULL);
}
void sync_init(void) {
  bootAt = idleAt = app_now();
  if (!sb_signed_in()) libBoot = addonBoot = 1; /* signing in fetches both itself */
  SDL_AddEventWatch(on_event, NULL); /* (watched, not taken: the frame loop still sees every event) */
}
void sync_tick(void) {
  double now = app_now();
  lifecycle();
  if (!libBoot && now - bootAt >= 0.7) { libBoot = 1; sb_pull_library(on_lib, NULL); }
  if (!addonBoot && now - bootAt >= 0.9) { addonBoot = 1; sb_sync_addons(on_addons, NULL); }
  if (now - idleAt >= 240) { idleAt = now; if (!away && app_idle() > 20 && sb_signed_in()) sb_sync_soon(0); }
  if (rebuildWanted && (app_idle() >= 0.7 || now - rebuildSince >= 5)) {
    rebuildWanted = 0;
    if ((SCR == SCR_HOME || SCR == SCR_LIB) && !player_open()) board_rebuild(); /* elsewhere: rebuilt when shown */
  }
}
