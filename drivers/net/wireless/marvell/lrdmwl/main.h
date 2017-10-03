#ifndef _MAIN_H_
#define _MAIN_H_

/* WMM Turbo mode */
extern int wmm_turbo;

int mwl_add_card(void *, struct mwl_if_ops *);
void mwl_wl_deinit(struct mwl_priv *);
void mwl_set_caps(struct mwl_priv *priv);
extern void timer_routine(unsigned long data);

#endif
