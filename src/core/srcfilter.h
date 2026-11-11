#pragma once
/* Clean sources: what a source's own text says about it (see srcfilter.c) */
enum { SRC_OK = 0, SRC_HARMFUL, SRC_TINY, SRC_NOT_IT };
int src_vid_episode(const char *vid, int *season, int *episode); /* "tt0773262:1:1" -> 1, 1 (0 for a film, or an id that is not numbered so) */
int src_episode_verdict(const char *text, int season, int episode); /* 1 names this episode, -1 names another (or another season), 0 does not say */
int src_episode_loose(const char *path, int season, int episode); /* a pack's file numbered without a season ("05 - Title.mkv") */
int src_is_video(const char *name);
int src_is_harmful(const char *name); /* a program, a script, an archive, a shortcut */
int src_junk(const char *name, const char *title, const char *filename, double size, int series);
