/* Android TV's own services, through the Java side (OriginBridge.java). Only on Android. */
#ifndef SB_ANDROID_H
#define SB_ANDROID_H
/* one call into OriginBridge.cmd(name, a, b, n), any thread:
   "install" a b (an update: the APK's address and SHA-256), "open" a (a page in the TV's browser, when it has one),
   "exit" (the app closes: Home's "press again to exit") */
void android_cmd(const char *name, const char *a, const char *b, int n);
const char *android_update_status(void); /* the update's progress line from Java ("" none) */
#endif
