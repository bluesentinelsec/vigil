#ifndef VIGIL_INTERNAL_CLI_FRONTEND_H
#define VIGIL_INTERNAL_CLI_FRONTEND_H

#include <stddef.h>

#include "vigil/source.h"
#include "vigil/status.h"

int find_project_root(const char *start_path, char *out_buf, size_t buf_size);
int register_source_tree(vigil_source_registry_t *registry, const char *path, const char *project_root,
                         vigil_source_id_t *out_source_id, vigil_error_t *error);
int vigil_cli_run_test_command(int argc, char **argv);

#endif
