/* libass built inside the app: FreeType, HarfBuzz and FriBidi are the app's own, no system font provider (no
   fontconfig: fonts come from the app's bundle and the file's attachments), plain C (no assembly) */
#define CONFIG_ASM 0
#define CONFIG_LARGE_TILES 0
#define CONFIG_SOURCEVERSION "0.17.5"
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRDUP 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRNDUP 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1
