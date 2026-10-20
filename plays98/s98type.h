#ifndef _S98TYPE_H
#define _S98TYPE_H

struct PluginInitAPI_t;
OCP_INTERNAL int s98_type_init (struct PluginInitAPI_t *API);

struct PluginCloseAPI_t;
OCP_INTERNAL void s98_type_done (struct PluginCloseAPI_t *API);

extern OCP_INTERNAL const struct cpifaceplayerstruct s98Player;

#endif
