#include "core/subclean.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
/* a line as the app shows it: markup out, sound descriptions out, clutter out */
static const char *line(const char *in) { static char b[2048]; snprintf(b, sizeof b, "%s", in); sub_strip_tags(b); sub_strip_sdh(b); sub_clean(b); return b; }
#define IS(in, want) do { const char *g_ = line(in); if (strcmp(g_, want)) { printf("FAIL line %d: \"%s\" -> \"%s\", want \"%s\"\n", __LINE__, in, g_, want); fails++; } } while (0)
#define EQ(a, b) do { int a_ = (a), b_ = (b); if (a_ != b_) { printf("FAIL line %d: %s = %d, want %d\n", __LINE__, #a, a_, b_); fails++; } } while (0)
int main(void) {
  /* a name between marks stays, whatever the marks */
  IS("يا (ريك)", "يا ريك"); IS("يا \"ريك\"", "يا ريك"); IS("يا «ريك»", "يا ريك"); IS("يا [ريك]", "يا ريك"); IS("يا {ريك}", "يا ريك"); IS("يا <ريك>", "يا ريك"); IS("يا <<ريك>>", "يا ريك");
  IS("(ريك)، أين (كارل)؟", "ريك، أين كارل؟"); IS("ـ (ريك)!\nـ ماذا؟", "ـ ريك!\nـ ماذا؟"); IS("ذهبنا إلى (أتلانتا) مع (داريل ديكسون)", "ذهبنا إلى أتلانتا مع داريل ديكسون");
  IS("<i>يا (ريك)</i>", "يا ريك"); IS("{\\an8}يا (ريك)", "يا ريك"); IS("<font color=\"#ffff00\">يا (ريك)</font>", "يا ريك");
  /* sounds still go */
  IS("(موسيقى)", ""); IS("[يضحك] لا أصدق", "لا أصدق"); IS("(بالإسبانية) مرحباً", "مرحباً"); IS("[صوت إطلاق نار]", ""); IS("(صراخ)\nاهربوا", "اهربوا");
  IS("[coughs] Hello", "Hello"); IS("(GUNSHOTS)", ""); IS("RICK: Get down!", "Get down!"); IS("- JOHN: Hi\n- MARY: Hello", "- Hi\n- Hello"); IS("my friend(s) came", "my friend s came");
  /* markup goes, the words beside it never do */
  IS("<i>Hello</i> there", "Hello there"); IS("5 < 6 and so on", "5 6 and so on"); IS("a <b>bold</b> <u>move</u>", "a bold move"); IS("Tom &amp; Jerry", "Tom Jerry"); IS("R&amp;B", "R&B"); IS("one<br>two", "one\ntwo");
  IS("it&#39;s", "it's"); IS("don&apos;t", "don't"); IS("wait&nbsp;here", "wait here"); IS("one\\Ntwo", "one\ntwo"); IS("<c.yellow>Hi</c> <00:00:01.000>there", "Hi there"); IS("{y:i}Hello", "Hello");
  IS("x <unknown thing", "x unknown thing"); IS("{ a note", "a note"); IS("<v Rick>Carl!", "Carl!");
  /* the kept marks */
  IS("What?! No...", "What?! No..."); IS("- Yes.\n- No.", "- Yes.\n- No."); IS("♪ la la ♪", "la la"); IS("♪ DRAMATIC MUSIC ♪", ""); IS("10:30 or 50%", "10:30 or 50%"); IS("f**k", "f**k");
  /* adverts and credits */
  EQ(sub_is_ad("Subtitles by explosiveskull"), 1); EQ(sub_is_ad("www.OpenSubtitles.org"), 1); EQ(sub_is_ad("Support us and become VIP member"), 1); EQ(sub_is_ad("Downloaded from YTS.MX"), 1);
  EQ(sub_is_ad("Visit example-site.com today"), 1); EQ(sub_is_ad("ترجمة: أحمد"), 1); EQ(sub_is_ad("تابعونا على تويتر"), 1); EQ(sub_is_ad("@some_handle"), 1); EQ(sub_is_ad("قناتنا على التلجرام"), 1);
  EQ(sub_is_ad("I need a translation, now."), 0); EQ(sub_is_ad("Wait... come here."), 0); EQ(sub_is_ad("He works at the U.N. now."), 0); EQ(sub_is_ad("هذه ترجمة سيئة للرسالة"), 0); EQ(sub_is_ad("Mr. Co. is here"), 0); EQ(sub_is_ad("e.g. this"), 0); EQ(sub_is_ad("Email me at home"), 0);
  /* old code pages: Arabic told from Western by its bytes */
  { static const unsigned char ar[] = { 0xe3, 0xd1, 0xcd, 0xc8, 0xc7, 0x20, 0xed, 0xc7, 0x20, 0xd1, 0xed, 0xdf, 0xa1, 0x20, 0xdf, 0xed, 0xdd, 0x20, 0xcd, 0xc7, 0xe1, 0xdf, 0xbf }, fr[] = { 0x4c, 0x65, 0x20, 0x63, 0x61, 0x66, 0xe9, 0x20, 0x65, 0x73, 0x74, 0x20, 0x64, 0xe9, 0x6a, 0xe0, 0x20, 0x70, 0x72, 0xea, 0x74, 0x2c, 0x20, 0x46, 0x72, 0x61, 0x6e, 0xe7, 0x6f, 0x69, 0x73, 0x2e }; char o[400];
    EQ(sub_utf8_ok(ar, sizeof ar), 0); EQ(sub_bytes_arabic(ar, sizeof ar), 1); EQ(sub_bytes_arabic(fr, sizeof fr), 0);
    sub_bytes_utf8(ar, sizeof ar, -1, o); EQ(strcmp(o, "مرحبا يا ريك، كيف حالك؟"), 0); sub_bytes_utf8(fr, sizeof fr, -1, o); EQ(strcmp(o, "Le café est déjà prêt, François."), 0);
    EQ(sub_utf8_ok((const unsigned char *)"يا", 4), 1); }
  EQ(sub_is_ad("Wait.me too"), 0); EQ(sub_is_ad("Listen.to me now"), 0); EQ(sub_is_ad("watch at films.to"), 1); EQ(sub_is_ad("example.me/join"), 1);
  /* nothing crashes, nothing grows: random bytes */
  srand(7); for (int k = 0; k < 200000; k++) { char b[96], c[96]; int n = rand() % 90; static const char *bits[] = { "<", ">", "{", "}", "(", ")", "[", "]", "&", "#", ";", "\\", "N", "i", "/", " ", "\n", "ي", "ا", "ر", "♪", "A", "b", ":", ".", "\xff", "\xd9", "&amp", "<i", "{\\" };
    int o = 0; while (o < n) { const char *x = bits[rand() % 30]; int l = (int)strlen(x); if (o + l > 90) break; memcpy(b + o, x, l); o += l; } b[o] = 0; memcpy(c, b, o + 1);
    size_t a = strlen(b); sub_strip_tags(b); sub_strip_sdh(b); sub_clean(b); sub_is_ad(c); if (strlen(b) > a) { printf("FAIL: grew\n"); fails++; break; } }
  printf(fails ? "%d failed\n" : "all passed\n", fails); return fails != 0;
}
