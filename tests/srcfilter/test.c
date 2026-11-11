#include "core/srcfilter.h"
#include <stdio.h>
static int fails;
#define EQ(a, b) do { int a_ = (a), b_ = (b); if (a_ != b_) { printf("FAIL line %d: %s = %d, want %d\n", __LINE__, #a, a_, b_); fails++; } } while (0)
int main(void) {
  int s, e;
  EQ(src_vid_episode("tt0773262:1:1", &s, &e), 1); EQ(s, 1); EQ(e, 1);
  EQ(src_vid_episode("tt0773262", &s, &e), 0); EQ(src_vid_episode("kitsu:123:5", &s, &e), 0); EQ(src_vid_episode("tt1:0:3", &s, &e), 0);
  EQ(src_episode_verdict("Dexter.S01E01.1080p.BluRay.x264.mkv", 1, 1), 1);
  EQ(src_episode_verdict("Dexter.S02E01.1080p.BluRay.x264.mkv", 1, 1), -1);
  EQ(src_episode_verdict("Dexter S01-S08 Complete 1080p\nDexter.S02E01.mkv", 1, 1), -1);
  EQ(src_episode_verdict("Dexter S01-S08 Complete 1080p\nDexter.S01E01.mkv", 1, 1), 1);
  EQ(src_episode_verdict("Dexter Season 2 Complete 1080p", 1, 1), -1);
  EQ(src_episode_verdict("Dexter Seasons 1-8 Complete", 1, 1), 0);
  EQ(src_episode_verdict("Dexter S01 1080p BluRay", 1, 4), 0);
  EQ(src_episode_verdict("Dexter S02 1080p BluRay", 1, 4), -1);
  EQ(src_episode_verdict("Dexter (2006) 1080p 1920x1080 x264", 1, 1), 0);
  EQ(src_episode_verdict("Show 1x05 HDTV", 1, 5), 1); EQ(src_episode_verdict("Show 1x05 HDTV", 1, 6), -1);
  EQ(src_episode_verdict("Show S01E01-E03 1080p", 1, 2), 1); EQ(src_episode_verdict("Show S01E01E02 1080p", 1, 2), 1);
  EQ(src_episode_verdict("Show S01E01-03 1080p", 1, 3), 1); EQ(src_episode_verdict("Show.S01E02-720p.mkv", 1, 5), -1);
  EQ(src_episode_verdict("Show.S01E02-10bit.mkv", 1, 9), -1); EQ(src_episode_verdict("Show S01 E05 720p", 1, 5), 1);
  EQ(src_episode_verdict("Se7en 1995 1080p", 1, 1), 0); EQ(src_episode_verdict("The Film 2024 2160p HDR", 2, 3), 0);
  EQ(src_episode_loose("Dexter/Season 1/05 - Love American Style.mkv", 1, 5), 1);
  EQ(src_episode_loose("Dexter/Season 2/05 - The Dark Defender.mkv", 1, 5), 0);
  EQ(src_episode_loose("Show - 05 [1080p].mkv", 1, 5), 1); EQ(src_episode_loose("Show/Episode 12.mkv", 1, 12), 1); EQ(src_episode_loose("Show/ep03.mkv", 1, 4), 0);
  EQ(src_is_video("a/b/film.MKV"), 1); EQ(src_is_video("film.exe"), 0); EQ(src_is_harmful("Film.2024.1080p.mkv.exe"), 1); EQ(src_is_harmful("film.mkv"), 0);
  EQ(src_junk("Torrentio\n4k", "Film 2024 2160p\nFilm.2024.2160p.mkv", "Film.2024.2160p.mkv", 20e9, 0), SRC_OK);
  EQ(src_junk("X", "Film 2024\nFilm.2024.1080p.exe", "", 0, 0), SRC_HARMFUL);
  EQ(src_junk("X", "Film 2024", "Film.2024.zip", 0, 0), SRC_HARMFUL);
  EQ(src_junk("X", "Film 2024 password in comments", "", 0, 0), SRC_HARMFUL);
  EQ(src_junk("X", "Film 2024 www.site.com", "", 0, 0), SRC_OK);
  EQ(src_junk("X", "Film 2024", "Film.2024.mkv", 9e6, 0), SRC_TINY); EQ(src_junk("X", "Show S01E01", "Show.S01E01.mkv", 90e6, 1), SRC_OK);
  EQ(src_junk("X", "Film 2024", "film-sample.mkv", 0, 0), SRC_NOT_IT); EQ(src_junk("X", "Sample Size 2024", "Sample.Size.2024.mkv", 0, 0), SRC_OK); EQ(src_junk("X", "", "Trailer.Park.Boys.S01E01.mkv", 0, 1), SRC_OK); EQ(src_junk("X", "", "Film.2024.Trailer.mp4", 0, 0), SRC_NOT_IT);
  printf(fails ? "%d failed\n" : "all passed\n", fails); return fails != 0;
}
