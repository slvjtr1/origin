/* Small helpers over cJSON so screens read fields in one line. */
#ifndef SB_JSON_H
#define SB_JSON_H
#include "cJSON.h"
#include <stdlib.h>
static inline const char *js_str(const cJSON *o, const char *k, const char *def) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; return cJSON_IsString(v) && v->valuestring ? v->valuestring : def; }
static inline double js_num(const cJSON *o, const char *k, double def) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; return cJSON_IsNumber(v) ? v->valuedouble : def; }
static inline int js_bool(const cJSON *o, const char *k, int def) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def; }
static inline const cJSON *js_obj(const cJSON *o, const char *k) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; return cJSON_IsObject(v) ? v : NULL; }
static inline const cJSON *js_arr(const cJSON *o, const char *k) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; return cJSON_IsArray(v) ? v : NULL; }
/* a number that may arrive as a string ("8.5") */
static inline double js_numstr(const cJSON *o, const char *k, double def) {
  const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
  if (cJSON_IsNumber(v)) return v->valuedouble; if (cJSON_IsString(v) && v->valuestring) { char *e; double d = strtod(v->valuestring, &e); return e != v->valuestring ? d : def; } return def; }
#endif
