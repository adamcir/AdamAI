/* AdamAI - GPL-3.0-only
 * Backend-independent registry of local coding-agent tools.
 */
#ifndef ADAM_TOOL_REGISTRY_H
#define ADAM_TOOL_REGISTRY_H
#include "adam/fs_tool.h"
#include <json-c/json.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>

typedef bool (*AdamToolApproval)(const char *description, void *userdata);
typedef struct {
    AdamFs *fs;
    const atomic_bool *cancelled;
    AdamToolApproval approve;
    void *approval_userdata;
    bool allow_processes;
    bool allow_network;
} AdamToolContext;

/* Schemas returned here are owned by caller and may be added to Ollama tools. */
struct json_object *adam_tools_schemas(void);
bool adam_tools_known(const char *name);
bool adam_tools_is_network(const char *name);
/* Return caller-owned HTTPS Nominatim URL for a plain UTF-8 place query. */
char *adam_geo_search_url(const char *query);
/* Returns true only on success. Caller frees output; failure explains why. */
bool adam_tools_execute(const char *name, struct json_object *args,
                        AdamToolContext *ctx, char **output,
                        char *error, size_t error_cap);
#endif
