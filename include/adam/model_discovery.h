/* AdamAI - GPL-3.0-only */
#ifndef ADAM_MODEL_DISCOVERY_H
#define ADAM_MODEL_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>

/* Resolve an untagged Ollama model name (e.g. adam1) or wildcard (adam1:*)
 * against /api/tags. Prefers the highest numeric tag, including adam1:1.0.
 * Names already carrying an explicit version are returned unchanged.
 */
bool adam_model_select_from_tags(const char *tags_json, const char *requested,
                                 char *result, size_t result_cap);
bool adam_model_resolve(const char *ollama_endpoint, const char *requested,
                        char *result, size_t result_cap,
                        char *error, size_t error_cap);
#endif
