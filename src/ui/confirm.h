/* A question before something that can't be undone: a card in the middle of the screen with Cancel and a red OK.
   Cancel has the focus first; Back cancels too. onOk runs only on OK. */
#ifndef SB_CONFIRM_H
#define SB_CONFIRM_H
void confirm_show(const char *title, const char *body, const char *ok, void (*onOk)(void *), void *ud);
int  confirm_open(void);
#endif
