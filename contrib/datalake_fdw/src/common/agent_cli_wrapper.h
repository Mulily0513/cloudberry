#ifndef AGENT_CLI_WRAPPER_H
#define AGENT_CLI_WRAPPER_H

#include "postgres.h"
#include "utils/resowner.h"
#include "utils/memutils.h"
#include "src/components/agent_cli/c_interface/agent_c_api.h"

/* Agent CLI handle - same as existing structure */
typedef struct AgentCliHandle
{
    agent_cli_handle_t handle;
    agent_cli_status_t lastStatus;
    const char *lastErrorMessage;
    agent_cli_config_t *agentConfig;
    agent_cli_response_t *currentResponse;
    bool responseValid;
    bool isValid;
} AgentCliHandle;

/* Resource management - same as existing structure */
typedef struct AgentCliResource
{
    AgentCliHandle *agentClihandle;
    ResourceOwner resowner;
    struct AgentCliResource *next;
    struct AgentCliResource *prev;
} AgentCliResource;

/* Initialization and cleanup */
AgentCliHandle* agent_cli_wrapper_create(const char* server_url, const char* prefix, const char* namespace_name);
void agent_cli_wrapper_destroy(AgentCliHandle* handle);

/* Table operations */
void agent_cli_wrapper_create_table(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_load_table(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_table_exists(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_get_fragment(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_plan_file_groups(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_commit_file_groups(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_commit_append(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_commit_update(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_commit_rewrite(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_append_table(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_update_table(AgentCliHandle* handle, const char* table_name, const char* json);
void agent_cli_wrapper_drop_table(AgentCliHandle* handle, const char* table_name, const char* json);

void agent_cli_wrapper_get_statistics(AgentCliHandle* handle, const char* table_name, const char* json);

/* Catalog management operations */
void agent_cli_wrapper_create_catalog(AgentCliHandle* handle, const char* json);
void agent_cli_wrapper_list_catalogs(AgentCliHandle* handle, const char* json);
void agent_cli_wrapper_list_namespaces(AgentCliHandle* handle, const char* json);

/* Utility functions */
const char* agent_cli_wrapper_get_response(AgentCliHandle* handle);
bool agent_cli_wrapper_is_success(AgentCliHandle* handle);
void agent_cli_wrapper_check_error(AgentCliHandle* handle);
void agent_cli_wrapper_check_exec_error(AgentCliHandle* handle, const char *error_prefix);
void agent_cli_wrapper_check_exec_error_json(AgentCliHandle* handle, const char *error_prefix);
void agent_cli_wrapper_set_interrupt_callback(AgentCliHandle* handle, agent_cli_interrupt_callback_t callback);

/* Resource management functions */
AgentCliResource* agent_cli_wrapper_register_resource(AgentCliHandle* handle);
void agent_cli_wrapper_unregister_resource(AgentCliResource* resource);
void agent_cli_wrapper_resource_cleanup(ResourceReleasePhase phase, bool isCommit, bool isTopLevel, void *arg);

/* Global resource tracking - moved from FDW */
extern bool agent_cli_wrapper_resowner_callback_registered;
extern AgentCliResource *agent_cli_wrapper_open_resources;

#endif /* AGENT_CLI_WRAPPER_H */
