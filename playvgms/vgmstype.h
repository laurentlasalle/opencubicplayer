#ifndef _VGMSTYPE_H
#define _VGMSTYPE_H 1

struct PluginInitAPI_t;
OCP_INTERNAL int vgms_type_init (struct PluginInitAPI_t *API);

struct PluginCloseAPI_t;
OCP_INTERNAL void vgms_type_done (struct PluginCloseAPI_t *API);

#endif
