#include "core/accounts.h"
#include "core/store.h"
#include "core/stremio.h"
#include "core/app.h"
#include "core/i18n.h"
#include "ui/widgets.h"
#include "screens/board.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#define KEY "accounts"        /* [ { "_id", "email", "authKey" } ], the account in use among them */
#define RET "accountReturn"   /* the account to go back to when Add account's sign-in is left with Back */
extern void login_finish(const char *ak);
static unsigned ser = 1;

static cJSON *list(void) { cJSON *a = store_node(KEY); return cJSON_IsArray(a) ? a : NULL; }
static const char *field(const cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsString(v) && v->valuestring ? v->valuestring : ""; }
int accounts_n(void) { cJSON *a = list(); return a ? cJSON_GetArraySize(a) : 0; }
const char *accounts_email(int i) { cJSON *a = list(); return a ? field(cJSON_GetArrayItem(a, i), "email") : ""; }
int accounts_is_current(int i) { cJSON *a = list(); const char *ak = sb_auth_key(); return a && ak && *ak && !strcmp(field(cJSON_GetArrayItem(a, i), "authKey"), ak); }
unsigned accounts_serial(void) { return ser; }

/* the account in use, once its user is known: added, or its key brought up to date */
static void remember(void) {
  const char *ak = sb_auth_key(), *id = sb_user_id(), *em = sb_user_email(); if (!ak || !*ak || !id || !*id) return;
  cJSON *a = list(); if (!a) { a = cJSON_CreateArray(); store_set_node(KEY, a); a = list(); if (!a) return; }
  cJSON *x; cJSON_ArrayForEach(x, a) if (!strcmp(field(x, "_id"), id)) {
    if (strcmp(field(x, "authKey"), ak) || strcmp(field(x, "email"), em)) { cJSON_ReplaceItemInObjectCaseSensitive(x, "authKey", cJSON_CreateString(ak)); cJSON_ReplaceItemInObjectCaseSensitive(x, "email", cJSON_CreateString(em)); store_set_node(KEY, a); ser++; }
    return; }
  if (cJSON_GetArraySize(a) >= ACCOUNTS_MAX) return;
  cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "_id", id); cJSON_AddStringToObject(o, "email", em); cJSON_AddStringToObject(o, "authKey", ak);
  cJSON_AddItemToArray(a, o); store_set_node(KEY, a); store_flush(); ser++;
}
void accounts_tick(void) { static unsigned seen; if (store_serial() == seen) return; seen = store_serial(); remember(); }

static void sign_in(const char *ak, const char *email) {
  char k[256]; snprintf(k, sizeof k, "%s", ak); /* (the list may change under it) */
  if (sb_signed_in()) sb_forget_local(); board_invalidate();
  toast(T1("Switching to {e}", "e", email && *email ? email : T("Stremio account")), 2500, NULL);
  fprintf(stderr, "[accounts] switching account\n");
  if (SCR != SCR_LOGIN) app_go(SCR_LOGIN, NULL, 1); login_finish(k); /* the sign-in's own loading: library and addons, then Home */
}
void accounts_switch(int i) {
  remember(); cJSON *a = list(); if (!a || i < 0 || i >= cJSON_GetArraySize(a) || accounts_is_current(i)) return;
  const cJSON *x = cJSON_GetArrayItem(a, i); char em[160]; snprintf(em, sizeof em, "%s", field(x, "email")); const char *ak = field(x, "authKey"); if (!*ak) return;
  char k[256]; snprintf(k, sizeof k, "%s", ak); sign_in(k, em);
}
void accounts_add(void) {
  remember(); store_set(RET, sb_user_id() ? sb_user_id() : "");
  sb_forget_local(); board_invalidate(); store_flush(); fprintf(stderr, "[accounts] adding an account\n");
  app_go(SCR_LOGIN, NULL, 1);
}
void accounts_logout(void) {
  const char *id0 = sb_user_id(); char id[96]; snprintf(id, sizeof id, "%s", id0 ? id0 : ""); const char *ak0 = sb_auth_key(); char ak[256]; snprintf(ak, sizeof ak, "%s", ak0 ? ak0 : "");
  cJSON *a = list(); if (a) for (int i = cJSON_GetArraySize(a) - 1; i >= 0; i--) { const cJSON *x = cJSON_GetArrayItem(a, i); if ((*id && !strcmp(field(x, "_id"), id)) || (*ak && !strcmp(field(x, "authKey"), ak))) { cJSON_DeleteItemFromArray(a, i); ser++; } }
  if (a) store_set_node(KEY, a);
  sb_logout(); board_invalidate(); store_flush();
  if (accounts_n()) { accounts_switch(0); return; } /* another kept here takes over */
  app_go(SCR_LOGIN, NULL, 1);
}
int accounts_back(void) {
  if (sb_signed_in() || !accounts_n()) return 0;
  const char *r = store_get(RET, ""); int pick = 0; cJSON *a = list();
  for (int i = 0; a && i < cJSON_GetArraySize(a); i++) if (*r && !strcmp(field(cJSON_GetArrayItem(a, i), "_id"), r)) pick = i;
  store_remove(RET); accounts_switch(pick); return 1;
}
