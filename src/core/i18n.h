/* UI strings: the English text is the key, the dictionaries in assets/i18n translate it. {name}
   placeholders are filled by T1/T2. Arabic turns the layout right-to-left. */
#ifndef SB_I18N_H
#define SB_I18N_H
int  i18n_init(const char *assetDir, const char *lang);  /* lang: "auto", "en", "ar", "fr"... */
const char *i18n_lang(void);
const char *i18n_tv_lang(void);  /* the TV's own UI language, two letters ("fr") */
int  i18n_rtl(void);
unsigned i18n_gen(void);         /* bumps with every language change: strings kept by screens are made again */
const char *T(const char *en);
const char *i18n_key_of(const char *translated); /* the English key of a translated string, or NULL (rare uses only: it searches) */
/* fills {k1} -> v1 (and {k2} -> v2); returns a rotating static buffer */
const char *T1(const char *en, const char *k1, const char *v1);
const char *T2(const char *en, const char *k1, const char *v1, const char *k2, const char *v2);
const char *i18n_lang_name(const char *code);  /* the language's own name for the picker */
int  i18n_lang_count(void);
const char *i18n_lang_code(int i);
#endif
