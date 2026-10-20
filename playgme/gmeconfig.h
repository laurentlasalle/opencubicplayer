#ifndef _GMECONFIG_H
#define _GMECONFIG_H 1

struct PluginInitAPI_t;
struct PluginCloseAPI_t;

OCP_INTERNAL int gme_config_init (struct PluginInitAPI_t *API);
OCP_INTERNAL void gme_config_done (struct PluginCloseAPI_t *API);

#endif
