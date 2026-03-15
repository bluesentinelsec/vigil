#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/dap.h"
#include "basl/json.h"
#include "basl/jsonrpc.h"
#include "basl/type.h"
#include "internal/basl_internal.h"

/* ── Internal state ──────────────────────────────────────────────── */

struct basl_dap_server {
    basl_jsonrpc_transport_t transport;
    basl_allocator_t allocator;
    basl_vm_t *vm;
    const basl_source_registry_t *sources;
    basl_debugger_t *debugger;
    basl_object_t *program;
    basl_source_id_t source_id;
    int running;
    int seq;
};

/* ── JSON helpers ────────────────────────────────────────────────── */

static const basl_allocator_t *ap(basl_dap_server_t *s) {
    return &s->allocator;
}

static basl_status_t jset_str(
    basl_json_value_t *o, const char *k, const char *v,
    const basl_allocator_t *a, basl_error_t *e
) {
    basl_json_value_t *val = NULL;
    basl_status_t s = basl_json_string_new(a, v, strlen(v), &val, e);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(o, k, strlen(k), val, e);
}

static basl_status_t jset_int(
    basl_json_value_t *o, const char *k, int64_t v,
    const basl_allocator_t *a, basl_error_t *e
) {
    basl_json_value_t *val = NULL;
    basl_status_t s = basl_json_number_new(a, (double)v, &val, e);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(o, k, strlen(k), val, e);
}

static basl_status_t jset_bool(
    basl_json_value_t *o, const char *k, int v,
    const basl_allocator_t *a, basl_error_t *e
) {
    basl_json_value_t *val = NULL;
    basl_status_t s = basl_json_bool_new(a, v, &val, e);
    if (s != BASL_STATUS_OK) return s;
    return basl_json_object_set(o, k, strlen(k), val, e);
}

static const char *jget_str(const basl_json_value_t *o, const char *k) {
    const basl_json_value_t *v = basl_json_object_get(o, k);
    return v ? basl_json_string_value(v) : "";
}

static int jget_int(const basl_json_value_t *o, const char *k) {
    const basl_json_value_t *v = basl_json_object_get(o, k);
    return v ? (int)basl_json_number_value(v) : 0;
}

/* ── Response / event builders ───────────────────────────────────── */

static basl_status_t send_response(
    basl_dap_server_t *s, int req_seq, const char *cmd,
    basl_json_value_t *body, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *msg = NULL;
    basl_status_t st = basl_json_object_new(a, &msg, error);
    if (st != BASL_STATUS_OK) { basl_json_free(&body); return st; }
    jset_int(msg, "seq", ++s->seq, a, error);
    jset_str(msg, "type", "response", a, error);
    jset_int(msg, "request_seq", req_seq, a, error);
    jset_bool(msg, "success", 1, a, error);
    jset_str(msg, "command", cmd, a, error);
    if (body != NULL) {
        basl_json_object_set(msg, "body", 4, body, error);
    }
    st = basl_jsonrpc_write(&s->transport, msg, error);
    basl_json_free(&msg);
    return st;
}

static basl_status_t send_event(
    basl_dap_server_t *s, const char *event,
    basl_json_value_t *body, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *msg = NULL;
    basl_status_t st = basl_json_object_new(a, &msg, error);
    if (st != BASL_STATUS_OK) { basl_json_free(&body); return st; }
    jset_int(msg, "seq", ++s->seq, a, error);
    jset_str(msg, "type", "event", a, error);
    jset_str(msg, "event", event, a, error);
    if (body != NULL) {
        basl_json_object_set(msg, "body", 4, body, error);
    }
    st = basl_jsonrpc_write(&s->transport, msg, error);
    basl_json_free(&msg);
    return st;
}

/* ── DAP request handlers ────────────────────────────────────────── */

static basl_status_t handle_initialize(
    basl_dap_server_t *s, int req_seq, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *body = NULL;
    basl_json_object_new(a, &body, error);
    jset_bool(body, "supportsConfigurationDoneRequest", 1, a, error);
    jset_bool(body, "supportsSingleThreadExecutionRequests", 1, a, error);
    basl_status_t st = send_response(s, req_seq, "initialize", body, error);
    if (st != BASL_STATUS_OK) return st;
    return send_event(s, "initialized", NULL, error);
}

static basl_status_t handle_launch(
    basl_dap_server_t *s, int req_seq, basl_error_t *error
) {
    return send_response(s, req_seq, "launch", NULL, error);
}

static basl_status_t handle_threads(
    basl_dap_server_t *s, int req_seq, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *body = NULL, *threads = NULL, *thread = NULL;
    basl_json_object_new(a, &body, error);
    basl_json_array_new(a, &threads, error);
    basl_json_object_new(a, &thread, error);
    jset_int(thread, "id", 1, a, error);
    jset_str(thread, "name", "main", a, error);
    basl_json_array_push(threads, thread, error);
    basl_json_object_set(body, "threads", 7, threads, error);
    return send_response(s, req_seq, "threads", body, error);
}

static basl_status_t handle_set_breakpoints(
    basl_dap_server_t *s, int req_seq,
    const basl_json_value_t *args, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);

    /* Clear existing breakpoints — DAP sends the full set each time. */
    basl_debugger_clear_all_breakpoints(s->debugger);

    basl_json_value_t *body = NULL, *bps_arr = NULL;
    basl_json_object_new(a, &body, error);
    basl_json_array_new(a, &bps_arr, error);

    const basl_json_value_t *bps = args ? basl_json_object_get(args, "breakpoints") : NULL;
    size_t count = bps ? basl_json_array_count(bps) : 0;

    /* Use the program's source_id. */
    basl_source_id_t source_id = s->source_id;

    for (size_t i = 0; i < count; i++) {
        const basl_json_value_t *bp = basl_json_array_get(bps, i);
        int line = jget_int(bp, "line");
        size_t bp_id = 0;
        basl_debugger_set_breakpoint(s->debugger, source_id, (uint32_t)line, &bp_id, error);

        basl_json_value_t *bp_resp = NULL;
        basl_json_object_new(a, &bp_resp, error);
        jset_bool(bp_resp, "verified", 1, a, error);
        jset_int(bp_resp, "line", line, a, error);
        basl_json_array_push(bps_arr, bp_resp, error);
    }

    basl_json_object_set(body, "breakpoints", 11, bps_arr, error);
    return send_response(s, req_seq, "setBreakpoints", body, error);
}

static basl_status_t handle_stack_trace(
    basl_dap_server_t *s, int req_seq, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *body = NULL, *frames = NULL;
    basl_json_object_new(a, &body, error);
    basl_json_array_new(a, &frames, error);

    size_t count = basl_debugger_frame_count(s->debugger);
    for (size_t i = 0; i < count; i++) {
        const char *name = NULL;
        size_t name_len = 0;
        basl_source_id_t src = 0;
        uint32_t line = 0, col = 0;
        basl_debugger_frame_info(s->debugger, i, &name, &name_len, &src, &line, &col);

        basl_json_value_t *frame = NULL;
        basl_json_object_new(a, &frame, error);
        jset_int(frame, "id", (int64_t)i, a, error);

        char fname[256];
        size_t copy = name_len < sizeof(fname) - 1 ? name_len : sizeof(fname) - 1;
        memcpy(fname, name ? name : "<unknown>", copy);
        fname[copy] = '\0';
        jset_str(frame, "name", fname, a, error);
        jset_int(frame, "line", line, a, error);
        jset_int(frame, "column", col, a, error);

        /* Source object. */
        basl_json_value_t *source = NULL;
        basl_json_object_new(a, &source, error);
        jset_str(source, "name", "main.basl", a, error);
        jset_int(source, "sourceReference", 0, a, error);
        basl_json_object_set(frame, "source", 6, source, error);

        basl_json_array_push(frames, frame, error);
    }

    basl_json_object_set(body, "stackFrames", 11, frames, error);
    jset_int(body, "totalFrames", (int64_t)count, a, error);
    return send_response(s, req_seq, "stackTrace", body, error);
}

static basl_status_t handle_scopes(
    basl_dap_server_t *s, int req_seq, int frame_id, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *body = NULL, *scopes = NULL, *scope = NULL;
    basl_json_object_new(a, &body, error);
    basl_json_array_new(a, &scopes, error);
    basl_json_object_new(a, &scope, error);
    jset_str(scope, "name", "Locals", a, error);
    jset_str(scope, "presentationHint", "locals", a, error);
    /* variablesReference encodes frame_id + 1 (0 means no variables). */
    jset_int(scope, "variablesReference", frame_id + 1, a, error);
    jset_bool(scope, "expensive", 0, a, error);
    basl_json_array_push(scopes, scope, error);
    basl_json_object_set(body, "scopes", 6, scopes, error);
    return send_response(s, req_seq, "scopes", body, error);
}

static basl_status_t handle_variables(
    basl_dap_server_t *s, int req_seq, int var_ref, basl_error_t *error
) {
    const basl_allocator_t *a = ap(s);
    basl_json_value_t *body = NULL, *vars = NULL;
    basl_json_object_new(a, &body, error);
    basl_json_array_new(a, &vars, error);

    if (var_ref > 0) {
        size_t frame_idx = (size_t)(var_ref - 1);
        const char *names[32];
        size_t name_lens[32];
        basl_value_t values[32];
        size_t count = basl_debugger_frame_locals(
            s->debugger, frame_idx, names, name_lens, values, 32);

        for (size_t i = 0; i < count; i++) {
            basl_json_value_t *var = NULL;
            basl_json_object_new(a, &var, error);

            char vname[128];
            size_t copy = name_lens[i] < sizeof(vname) - 1 ? name_lens[i] : sizeof(vname) - 1;
            memcpy(vname, names[i], copy);
            vname[copy] = '\0';
            jset_str(var, "name", vname, a, error);

            /* Format value as string. */
            char vbuf[64];
            switch (basl_value_kind(&values[i])) {
            case BASL_VALUE_INT:
                snprintf(vbuf, sizeof(vbuf), "%" PRId64, basl_value_as_int(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "i32", a, error);
                break;
            case BASL_VALUE_UINT:
                snprintf(vbuf, sizeof(vbuf), "%" PRIu64, basl_value_as_uint(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "u32", a, error);
                break;
            case BASL_VALUE_FLOAT:
                snprintf(vbuf, sizeof(vbuf), "%g", basl_value_as_float(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "f64", a, error);
                break;
            case BASL_VALUE_BOOL:
                jset_str(var, "value", basl_value_as_bool(&values[i]) ? "true" : "false", a, error);
                jset_str(var, "type", "bool", a, error);
                break;
            default:
                jset_str(var, "value", "<object>", a, error);
                jset_str(var, "type", "object", a, error);
                break;
            }
            jset_int(var, "variablesReference", 0, a, error);
            basl_json_array_push(vars, var, error);
            basl_value_release(&values[i]);
        }
    }

    basl_json_object_set(body, "variables", 9, vars, error);
    return send_response(s, req_seq, "variables", body, error);
}

/* ── Debug callback (runs while VM is paused) ────────────────────── */

static basl_debug_action_t dap_on_stop(
    basl_debugger_t *debugger,
    basl_debug_stop_reason_t reason,
    void *userdata
) {
    basl_dap_server_t *s = (basl_dap_server_t *)userdata;
    basl_error_t error;
    memset(&error, 0, sizeof(error));
    const basl_allocator_t *a = ap(s);

    /* Send "stopped" event. */
    basl_json_value_t *body = NULL;
    basl_json_object_new(a, &body, &error);
    const char *reason_str = "breakpoint";
    if (reason == BASL_DEBUG_STOP_STEP) reason_str = "step";
    else if (reason == BASL_DEBUG_STOP_ENTRY) reason_str = "entry";
    jset_str(body, "reason", reason_str, a, &error);
    jset_int(body, "threadId", 1, a, &error);
    send_event(s, "stopped", body, &error);

    /* Process DAP requests while paused. */
    for (;;) {
        basl_json_value_t *msg = NULL;
        if (basl_jsonrpc_read(&s->transport, &msg, &error) != BASL_STATUS_OK) {
            s->running = 0;
            return BASL_DEBUG_CONTINUE;
        }

        const char *cmd = jget_str(msg, "command");
        int req_seq = jget_int(msg, "seq");
        const basl_json_value_t *args = basl_json_object_get(msg, "arguments");

        if (strcmp(cmd, "continue") == 0) {
            basl_debugger_continue(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            basl_json_free(&msg);
            return BASL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "next") == 0) {
            basl_debugger_step_over(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            basl_json_free(&msg);
            return BASL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stepIn") == 0) {
            basl_debugger_step_into(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            basl_json_free(&msg);
            return BASL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stepOut") == 0) {
            basl_debugger_step_out(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            basl_json_free(&msg);
            return BASL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stackTrace") == 0) {
            handle_stack_trace(s, req_seq, &error);
        } else if (strcmp(cmd, "scopes") == 0) {
            handle_scopes(s, req_seq, args ? jget_int(args, "frameId") : 0, &error);
        } else if (strcmp(cmd, "variables") == 0) {
            handle_variables(s, req_seq, args ? jget_int(args, "variablesReference") : 0, &error);
        } else if (strcmp(cmd, "threads") == 0) {
            handle_threads(s, req_seq, &error);
        } else if (strcmp(cmd, "disconnect") == 0) {
            send_response(s, req_seq, cmd, NULL, &error);
            s->running = 0;
            basl_json_free(&msg);
            return BASL_DEBUG_CONTINUE;
        } else {
            /* Unknown request while paused — respond OK. */
            send_response(s, req_seq, cmd, NULL, &error);
        }
        basl_json_free(&msg);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

basl_status_t basl_dap_server_create(
    basl_dap_server_t **out,
    FILE *in,
    FILE *out_stream,
    const basl_allocator_t *allocator,
    basl_error_t *error
) {
    if (out == NULL || in == NULL || out_stream == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "dap: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_allocator_t a = (allocator != NULL && basl_allocator_is_valid(allocator))
                             ? *allocator
                             : basl_default_allocator();

    basl_dap_server_t *s = (basl_dap_server_t *)a.allocate(a.user_data, sizeof(*s));
    if (s == NULL) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY,
                               "dap: allocation failed");
        return BASL_STATUS_OUT_OF_MEMORY;
    }
    memset(s, 0, sizeof(*s));
    s->allocator = a;
    basl_jsonrpc_transport_init(&s->transport, in, out_stream, &s->allocator);
    *out = s;
    return BASL_STATUS_OK;
}

void basl_dap_server_destroy(basl_dap_server_t **server) {
    if (server == NULL || *server == NULL) return;
    basl_dap_server_t *s = *server;
    if (s->debugger != NULL) {
        basl_debugger_detach(s->debugger);
        basl_debugger_destroy(&s->debugger);
    }
    basl_allocator_t a = s->allocator;
    a.deallocate(a.user_data, s);
    *server = NULL;
}

basl_status_t basl_dap_server_set_runtime(
    basl_dap_server_t *server,
    basl_vm_t *vm,
    const basl_source_registry_t *sources,
    basl_error_t *error
) {
    if (server == NULL || vm == NULL || sources == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "dap: invalid arguments");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    server->vm = vm;
    server->sources = sources;
    return basl_debugger_create(&server->debugger, vm, sources, error);
}

void basl_dap_server_set_program(
    basl_dap_server_t *server,
    basl_object_t *function,
    basl_source_id_t source_id
) {
    if (server != NULL) {
        server->program = function;
        server->source_id = source_id;
    }
}

basl_status_t basl_dap_server_run(
    basl_dap_server_t *server,
    basl_error_t *error
) {
    if (server == NULL || server->debugger == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "dap: server not configured");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_debugger_set_callback(server->debugger, dap_on_stop, server);
    basl_debugger_attach(server->debugger);
    server->running = 1;
    server->seq = 0;

    /* Main message loop — process requests until launch, then run VM. */
    while (server->running) {
        basl_json_value_t *msg = NULL;
        basl_status_t st = basl_jsonrpc_read(&server->transport, &msg, error);
        if (st != BASL_STATUS_OK) break;

        const char *cmd = jget_str(msg, "command");
        int req_seq = jget_int(msg, "seq");
        const basl_json_value_t *args = basl_json_object_get(msg, "arguments");

        if (strcmp(cmd, "initialize") == 0) {
            handle_initialize(server, req_seq, error);
        } else if (strcmp(cmd, "launch") == 0) {
            handle_launch(server, req_seq, error);

            /* Execute the program — the debug callback handles
               all interaction while the VM is running. */
            if (server->program != NULL) {
                basl_value_t result;
                basl_value_init_nil(&result);
                basl_error_t exec_error;
                memset(&exec_error, 0, sizeof(exec_error));
                basl_vm_execute_function(server->vm, server->program,
                                         &result, &exec_error);
                basl_value_release(&result);

                /* Send terminated + exited events. */
                send_event(server, "terminated", NULL, error);
                basl_json_value_t *exit_body = NULL;
                basl_json_object_new(ap(server), &exit_body, error);
                jset_int(exit_body, "exitCode", 0, ap(server), error);
                send_event(server, "exited", exit_body, error);
            }
        } else if (strcmp(cmd, "setBreakpoints") == 0) {
            handle_set_breakpoints(server, req_seq, args, error);
        } else if (strcmp(cmd, "configurationDone") == 0) {
            send_response(server, req_seq, cmd, NULL, error);
        } else if (strcmp(cmd, "threads") == 0) {
            handle_threads(server, req_seq, error);
        } else if (strcmp(cmd, "disconnect") == 0) {
            send_response(server, req_seq, cmd, NULL, error);
            server->running = 0;
        } else {
            /* Unknown — respond OK. */
            send_response(server, req_seq, cmd, NULL, error);
        }
        basl_json_free(&msg);
    }

    basl_debugger_detach(server->debugger);
    return BASL_STATUS_OK;
}
