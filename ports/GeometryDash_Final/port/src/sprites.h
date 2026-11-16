#ifndef SPRITES_H
#define SPRITES_H

int sprites_load(void);
void sprites_forget(void);
int spr_find(const char *name);
int spr_w(int id);
int spr_h(int id);

#endif