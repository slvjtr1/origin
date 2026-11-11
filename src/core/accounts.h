/* Accounts kept on this TV: every Stremio account signed in here is remembered (its sign-in key stays in the app's own
   data, as the account in use always did), so switching is one press: the account in use goes, the other signs in with
   its own library and addons, as a sign-in does. Settings are the TV's, shared by all. */
#ifndef SB_ACCOUNTS_H
#define SB_ACCOUNTS_H
#define ACCOUNTS_MAX 6
int         accounts_n(void);
const char *accounts_email(int i);
int         accounts_is_current(int i);
unsigned    accounts_serial(void);      /* bumps when the list changes */
void        accounts_switch(int i);     /* to account i */
void        accounts_add(void);         /* to the sign-in, the account in use kept */
void        accounts_logout(void);      /* signs the account in use out and forgets it here; another kept one takes over */
int         accounts_back(void);        /* Back on the sign-in: 1 when a kept account was signed in again */
void        accounts_tick(void);        /* per frame: the account in use joins the list once known */
#endif
