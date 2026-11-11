/* QR codes drawn on screen (sign-in with the phone, the phone keyboard, the settings' links). */
#ifndef SB_QR_H
#define SB_QR_H
/* a white rounded card of `size` with the code inside; 0 when the text cannot be encoded */
int qr_draw(const char *text, float x, float y, float size, float radius, float pad);
#endif
