
#ifndef __NOUVEAU_SWITCH_PUBLIC_H__
#define __NOUVEAU_SWITCH_PUBLIC_H__

struct pipe_screen;
struct pipe_resource;

struct pipe_screen *nouveau_switch_screen_create(void);
int nouveau_switch_resource_get_syncpoint(struct pipe_resource *resource, unsigned int *out_threshold);

#endif
