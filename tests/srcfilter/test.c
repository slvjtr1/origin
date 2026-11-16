#include "core/srcfilter.h"
#include <stdio.h>
#include <string.h>
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
  /* numbered from the series' first episode; the other ways an episode is written */
  EQ(src_episode_verdict_abs("One Piece S01E892 1080p", 21, 1, 892, NULL), 1); EQ(src_episode_verdict_abs("One Piece S01E893 1080p", 21, 1, 892, NULL), -1); EQ(src_episode_verdict_abs("Show S03E05", 3, 5, 45, NULL), 1);
  EQ(src_episode_verdict("Show Season 1 Episode 5 720p", 1, 5), 1); EQ(src_episode_verdict("Show Season 1 Episode 5 720p", 1, 6), -1); EQ(src_episode_verdict("Show S01E01-S01E10 1080p", 1, 7), 1);
  { int s1 = 0; EQ(src_episode_verdict_abs("Show S01E45", 3, 5, 0, &s1), -1); EQ(s1, 1); EQ(src_episode_verdict_abs("Show S11E24", 1, 1, 0, &s1), -1); EQ(s1, 0); }
  EQ(src_episode_loose_abs("Show - 045 [1080p].mkv", 3, 5, 45), 1); EQ(src_episode_loose_abs("Show - 046 [1080p].mkv", 3, 5, 45), 0); EQ(src_episode_loose_abs("Show/Season 3/05.mkv", 3, 5, 45), 1);
  /* the title a source names */
  { SrcWant w = { { "The Walking Dead" }, 1, 2010, 2022, 1, { "Fear the Walking Dead", "The Walking Dead: Dead City", "Talking Dead", "The Walking Dead" }, { 2015, 2023, 2011, 2010 }, 4 };
    EQ(src_title_verdict("The.Walking.Dead.S01E01.1080p.BluRay.x264.mkv", &w), 1); EQ(src_title_verdict("The Walking Dead (2010) Season 1 Complete", &w), 1); EQ(src_title_verdict("TWD.S01E01.720p.mkv", &w), 1);
    EQ(src_title_verdict("Fear.the.Walking.Dead.S01E01.1080p.mkv", &w), -2); EQ(src_title_verdict("The.Walking.Dead.Dead.City.S01E01.1080p.mkv", &w), -2); EQ(src_title_verdict("Talking.Dead.S01E01.mkv", &w), -2);
    EQ(src_title_verdict("The.Walking.Dead.Daryl.Dixon.S01E01.1080p.mkv", &w), -1); EQ(src_title_verdict("The Walking Dead World Beyond S01E01", &w), -1);
    EQ(src_title_verdict("The Walking Dead Complete Series\nFear.the.Walking.Dead.S01E01.mkv", &w), 1); /* (one line names it: the pack's file is the engine's to pick) */
    EQ(src_title_verdict("The Walking Dead US S01E01 720p", &w), 1); EQ(src_title_verdict("AMC The Walking Dead S01E01", &w), 1); EQ(src_title_verdict("\xf0\x9f\x91\xa4 12 \xf0\x9f\x92\xbe 1.2 GB", &w), 0);
    EQ(src_title_verdict("Los Muertos Vivientes S01E01 720p", &w), 0); EQ(src_title_verdict("S01E01 - Days Gone Bye.mkv", &w), 0); EQ(src_title_verdict("The Walking Dead / \xd0\xa5\xd0\xbe\xd0\xb4\xd1\x8f\xd1\x87\xd0\xb8\xd0\xb5 / Season 1", &w), 0);
    EQ(src_title_verdict("The Walking Dead \xd0\xa5\xd0\xbe\xd0\xb4\xd1\x8f\xd1\x87\xd0\xb8\xd0\xb5 S01E01", &w), 1); EQ(src_title_verdict("[Group] The Walking Dead - S01E01 [1080p]", &w), 1); EQ(src_title_verdict("www.site.org - The Walking Dead S01E01", &w), 1);
    EQ(src_title_verdict("The Walking Dead Universe Collection 1080p", &w), 0); }
  { SrcWant w = { { "Fear the Walking Dead" }, 1, 2015, 2023, 1, { "The Walking Dead", "Fear the Walking Dead" }, { 2010, 2015 }, 2 };
    EQ(src_title_verdict("The.Walking.Dead.S01E01.mkv", &w), -2); EQ(src_title_verdict("Fear.The.Walking.Dead.S01E01.mkv", &w), 1); EQ(src_title_verdict("FTWD.S01E01.mkv", &w), 1); }
  { SrcWant w = { { "Marvel's Daredevil", "Daredevil" }, 2, 2015, 2018, 1, { "Daredevil: Born Again" }, { 2025 }, 1 };
    EQ(src_title_verdict("Daredevil.S01E01.mkv", &w), 1); EQ(src_title_verdict("Marvels.Daredevil.S01E01.mkv", &w), 1); EQ(src_title_verdict("Daredevil.Born.Again.S01E01.mkv", &w), -2); }
  { SrcWant w = { { "Marvel's Jessica Jones" }, 1, 2015, 2019, 1, { "" }, { 0 }, 0 }; EQ(src_title_verdict("Jessica.Jones.S01E01.mkv", &w), 1); }
  { SrcWant w = { { "Star Wars: Andor" }, 1, 2022, 0, 1, { "" }, { 0 }, 0 }; EQ(src_title_verdict("Andor.S01E01.2160p.mkv", &w), 1); EQ(src_title_verdict("Star.Wars.Andor.S01E01.mkv", &w), 1); }
  { SrcWant w = { { "Andor" }, 1, 2022, 0, 1, { "" }, { 0 }, 0 }; EQ(src_title_verdict("Star.Wars.Andor.S01E01.mkv", &w), 1); }
  { SrcWant w = { { "Money Heist", "La casa de papel" }, 2, 2017, 2021, 1, { "Money Heist: Korea - Joint Economic Area" }, { 2022 }, 1 };
    EQ(src_title_verdict("La.Casa.de.Papel.S01E01.SPANISH.1080p", &w), 1); EQ(src_title_verdict("Money Heist AKA La Casa De Papel S01", &w), 1); EQ(src_title_verdict("Money.Heist.Korea.Joint.Economic.Area.S01E01", &w), -2); }
  { SrcWant w = { { "Shogun" }, 1, 2024, 0, 1, { "Shogun" }, { 1980 }, 1 }; EQ(src_title_verdict("Shogun.2024.S01E01.mkv", &w), 1); EQ(src_title_verdict("Shogun.1980.S01E01.mkv", &w), -2); EQ(src_title_verdict("Shogun.S01E01.mkv", &w), 1); }
  { SrcWant w = { { "Dune" }, 1, 2021, 0, 0, { "Dune: Part Two", "Dune" }, { 2024, 1984 }, 2 };
    EQ(src_title_verdict("Dune.2021.2160p.UHD.BluRay.mkv", &w), 1); EQ(src_title_verdict("Dune.1984.1080p.BluRay.mkv", &w), -2); EQ(src_title_verdict("Dune.Part.Two.2024.1080p.mkv", &w), -2); EQ(src_title_verdict("Dune Part 2 (2024) 1080p", &w), -2);
    EQ(src_title_verdict("Duna 2021 1080p Latino", &w), 0); EQ(src_title_verdict("Dune 1080p WEB-DL", &w), 1); EQ(src_title_verdict("Dune Collection 1984-2021 1080p", &w), 0); EQ(src_title_verdict("Some Other Film 2019 1080p", &w), -1); }
  { SrcWant w = { { "John Wick" }, 1, 2014, 0, 0, { "" }, { 0 }, 0 }; EQ(src_title_verdict("John.Wick.Chapter.2.2017.1080p.mkv", &w), -1); EQ(src_title_verdict("John Wick 2 2017 1080p", &w), -1); EQ(src_title_verdict("John Wick 1-4 1080p BluRay", &w), 0); EQ(src_title_verdict("John.Wick.2014.1080p.mkv", &w), 1); }
  { SrcWant w = { { "Wonder Woman 1984" }, 1, 2020, 0, 0, { "Wonder Woman" }, { 2017 }, 1 }; EQ(src_title_verdict("Wonder.Woman.1984.2020.IMAX.1080p.mkv", &w), 1); EQ(src_title_verdict("Wonder.Woman.1984.1080p.WEBRip.mkv", &w), 1); EQ(src_title_verdict("Wonder.Woman.2017.1080p.mkv", &w), -2); }
  { SrcWant w = { { "Blade Runner 2049" }, 1, 2017, 0, 0, { "Blade Runner" }, { 1982 }, 1 }; EQ(src_title_verdict("Blade.Runner.2049.2017.2160p.mkv", &w), 1); EQ(src_title_verdict("Blade.Runner.1982.The.Final.Cut.1080p.mkv", &w), -2); }
  { SrcWant w = { { "Am\xc3\xa9lie", "Le Fabuleux Destin d'Am\xc3\xa9lie Poulain" }, 2, 2001, 0, 0, { "" }, { 0 }, 0 }; EQ(src_title_verdict("Amelie.2001.1080p.BluRay.mkv", &w), 1); EQ(src_title_verdict("Le.Fabuleux.Destin.d.Amelie.Poulain.2001.mkv", &w), 1); }
  { SrcWant w = { { "1917" }, 1, 2019, 0, 0, { "" }, { 0 }, 0 }; EQ(src_title_verdict("1917.2019.1080p.BluRay.mkv", &w), 1); }
  { SrcWant w = { { "Mission: Impossible - Dead Reckoning Part One" }, 1, 2023, 0, 0, { "Mission: Impossible" }, { 1996 }, 1 }; EQ(src_title_verdict("Mission.Impossible.Dead.Reckoning.Part.1.2023.mkv", &w), 1); EQ(src_title_verdict("Mission.Impossible.Dead.Reckoning.2023.1080p.mkv", &w), 0); EQ(src_title_verdict("Mission.Impossible.1996.1080p.mkv", &w), -2); }
  EQ(src_is_cam("Film.2024.HDCAM.x264"), 1); EQ(src_is_cam("Film 2024 TS x264"), 1); EQ(src_is_cam("Film.2024.1080p.WEB-DL.mkv"), 0); EQ(src_is_cam("video.ts"), 0); EQ(src_is_cam("Cam 2018 1080p"), 0); EQ(src_is_cam("Film 2024 1080p TELESYNC"), 1);
  { char f[200]; EQ(strcmp(src_url_file("https://x.example/dl/abc/The.Show.S01E02.1080p.mkv?token=1", f, sizeof f), "The.Show.S01E02.1080p.mkv"), 0); EQ(src_url_file("https://x.example/play/abcdef", f, sizeof f)[0], 0); EQ(strcmp(src_url_file("https://x.example/a/My%20Film%202020.mp4", f, sizeof f), "My Film 2020.mp4"), 0); }
  printf(fails ? "%d failed\n" : "all passed\n", fails); return fails != 0;
}
