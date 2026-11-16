#pragma once
#include <stddef.h>
/* Clean sources: what a source's own text says about it (see srcfilter.c) */
enum { SRC_OK = 0, SRC_HARMFUL, SRC_TINY, SRC_NOT_IT };
int src_vid_episode(const char *vid, int *season, int *episode); /* "tt0773262:1:1" -> 1, 1 (0 for a film, or an id that is not numbered so) */
int src_episode_verdict(const char *text, int season, int episode); /* 1 names this episode, -1 names another (or another season), 0 does not say */
int src_episode_loose(const char *path, int season, int episode); /* a pack's file numbered without a season ("05 - Title.mkv") */
/* the same two, also told the episode's number counted from the series' first one (0 unknown): "S01E45" and "Show - 045"
   then name the 5th of season 3. *s1 (may be NULL): the text numbers its episodes in season 1 only */
int src_episode_verdict_abs(const char *text, int season, int episode, int absEp, int *s1);
int src_episode_loose_abs(const char *path, int season, int episode, int absEp);
int src_is_video(const char *name);
int src_is_harmful(const char *name); /* a program, a script, an archive, a shortcut */
int src_junk(const char *name, const char *title, const char *filename, double size, int series);
/* ---- Super clean ---- */
/* the title opened: its names (the catalog's, its own language's), its year(s), and the other real titles that share
   its words (a spin-off, a sequel, a remake), each with its year */
typedef struct { char name[4][160]; int nname, year, yearEnd, series; char other[24][120]; int oyear[24], nother; } SrcWant;
int src_title_verdict(const char *text, const SrcWant *w); /* 1 names it, -1 reads as another title, -2 names another real title, 0 does not say */
int src_is_cam(const char *text);                           /* filmed in a cinema */
const char *src_url_file(const char *url, char *out, size_t cap); /* the video file a direct address ends with ("" none) */
