#ifndef _GSFTYPE_H
#define _GSFTYPE_H 1

struct PluginInitAPI_t;
OCP_INTERNAL int gsf_type_init (struct PluginInitAPI_t *API);

struct PluginCloseAPI_t;
OCP_INTERNAL void gsf_type_done (struct PluginCloseAPI_t *API);

#endif
