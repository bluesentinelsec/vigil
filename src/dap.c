#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/dap.h"
#include "vigil/json.h"
#include "vigil/jsonrpc.h"
#include "vigil/type.h"

/* ── Internal state ──────────────────────────────────────────────── */

struct vigil_dap_server
{
    vigil_jsonrpc_transport_t transport;
    vigil_allocator_t allocator;
    vigil_vm_t *vm;
    const vigil_source_registry_t *sources;
    vigil_debugger_t *debugger;
    vigil_object_t *program;
    vigil_source_id_t source_id;
    int running;
    int seq;
};

/* ── JSON helpers ────────────────────────────────────────────────── */

static const vigil_allocator_t *ap(vigil_dap_server_t *s)
{
    return &s->allocator;
}

static vigil_status_t jset_str(vigil_json_value_t *o, const char *k, const char *v, const vigil_allocator_t *a,
                               vigil_error_t *e)
{
    vigil_json_value_t *val = NULL;
    vigil_status_t s = vigil_json_string_new(a, v, strlen(v), &val, e);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(o, k, strlen(k), val, e);
}

static vigil_status_t jset_int(vigil_json_value_t *o, const char *k, int64_t v, const vigil_allocator_t *a,
                               vigil_error_t *e)
{
    vigil_json_value_t *val = NULL;
    vigil_status_t s = vigil_json_number_new(a, (double)v, &val, e);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(o, k, strlen(k), val, e);
}

static vigil_status_t jset_bool(vigil_json_value_t *o, const char *k, int v, const vigil_allocator_t *a,
                                vigil_error_t *e)
{
    vigil_json_value_t *val = NULL;
    vigil_status_t s = vigil_json_bool_new(a, v, &val, e);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_json_object_set(o, k, strlen(k), val, e);
}

static const char *jget_str(const vigil_json_value_t *o, const char *k)
{
    const vigil_json_value_t *v = vigil_json_object_get(o, k);
    return v ? vigil_json_string_value(v) : "";
}

static int jget_int(const vigil_json_value_t *o, const char *k)
{
    const vigil_json_value_t *v = vigil_json_object_get(o, k);
    return v ? (int)vigil_json_number_value(v) : 0;
}

/* ── Response / event builders ───────────────────────────────────── */

static vigil_status_t send_response(vigil_dap_server_t *s, int req_seq, const char *cmd, vigil_json_value_t *body,
                                    vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *msg = NULL;
    vigil_status_t st = vigil_json_object_new(a, &msg, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_json_free(&body);
        return st;
    }
    jset_int(msg, "seq", ++s->seq, a, error);
    jset_str(msg, "type", "response", a, error);
    jset_int(msg, "request_seq", req_seq, a, error);
    jset_bool(msg, "success", 1, a, error);
    jset_str(msg, "command", cmd, a, error);
    if (body != NULL)
    {
        vigil_json_object_set(msg, "body", 4, body, error);
    }
    st = vigil_jsonrpc_write(&s->transport, msg, error);
    vigil_json_free(&msg);
    return st;
}

static vigil_status_t send_event(vigil_dap_server_t *s, const char *event, vigil_json_value_t *body,
                                 vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *msg = NULL;
    vigil_status_t st = vigil_json_object_new(a, &msg, error);
    if (st != VIGIL_STATUS_OK)
    {
        vigil_json_free(&body);
        return st;
    }
    jset_int(msg, "seq", ++s->seq, a, error);
    jset_str(msg, "type", "event", a, error);
    jset_str(msg, "event", event, a, error);
    if (body != NULL)
    {
        vigil_json_object_set(msg, "body", 4, body, error);
    }
    st = vigil_jsonrpc_write(&s->transport, msg, error);
    vigil_json_free(&msg);
    return st;
}

/* ── DAP request handlers ────────────────────────────────────────── */

static vigil_status_t handle_initialize(vigil_dap_server_t *s, int req_seq, vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *body = NULL;
    vigil_json_object_new(a, &body, error);
    jset_bool(body, "supportsConfigurationDoneRequest", 1, a, error);
    jset_bool(body, "supportsSingleThreadExecutionRequests", 1, a, error);
    vigil_status_t st = send_response(s, req_seq, "initialize", body, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    return send_event(s, "initialized", NULL, error);
}

static vigil_status_t handle_launch(vigil_dap_server_t *s, int req_seq, vigil_error_t *error)
{
    return send_response(s, req_seq, "launch", NULL, error);
}

static vigil_status_t handle_threads(vigil_dap_server_t *s, int req_seq, vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *body = NULL, *threads = NULL, *thread = NULL;
    vigil_json_object_new(a, &body, error);
    vigil_json_array_new(a, &threads, error);
    vigil_json_object_new(a, &thread, error);
    jset_int(thread, "id", 1, a, error);
    jset_str(thread, "name", "main", a, error);
    vigil_json_array_push(threads, thread, error);
    vigil_json_object_set(body, "threads", 7, threads, error);
    return send_response(s, req_seq, "threads", body, error);
}

static vigil_status_t handle_set_breakpoints(vigil_dap_server_t *s, int req_seq, const vigil_json_value_t *args,
                                             vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);

    /* Clear existing breakpoints — DAP sends the full set each time. */
    vigil_debugger_clear_all_breakpoints(s->debugger);

    vigil_json_value_t *body = NULL, *bps_arr = NULL;
    vigil_json_object_new(a, &body, error);
    vigil_json_array_new(a, &bps_arr, error);

    const vigil_json_value_t *bps = args ? vigil_json_object_get(args, "breakpoints") : NULL;
    size_t count = bps ? vigil_json_array_count(bps) : 0;

    /* Use the program's source_id. */
    vigil_source_id_t source_id = s->source_id;

    for (size_t i = 0; i < count; i++)
    {
        const vigil_json_value_t *bp = vigil_json_array_get(bps, i);
        int line = jget_int(bp, "line");
        size_t bp_id = 0;
        vigil_debugger_set_breakpoint(s->debugger, source_id, (uint32_t)line, &bp_id, error);

        vigil_json_value_t *bp_resp = NULL;
        vigil_json_object_new(a, &bp_resp, error);
        jset_bool(bp_resp, "verified", 1, a, error);
        jset_int(bp_resp, "line", line, a, error);
        vigil_json_array_push(bps_arr, bp_resp, error);
    }

    vigil_json_object_set(body, "breakpoints", 11, bps_arr, error);
    return send_response(s, req_seq, "setBreakpoints", body, error);
}

static vigil_status_t handle_stack_trace(vigil_dap_server_t *s, int req_seq, vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *body = NULL, *frames = NULL;
    vigil_json_object_new(a, &body, error);
    vigil_json_array_new(a, &frames, error);

    size_t count = vigil_debugger_frame_count(s->debugger);
    for (size_t i = 0; i < count; i++)
    {
        const char *name = NULL;
        size_t name_len = 0;
        vigil_source_id_t src = 0;
        uint32_t line = 0, col = 0;
        vigil_debugger_frame_info(s->debugger, i, &name, &name_len, &src, &line, &col);

        vigil_json_value_t *frame = NULL;
        vigil_json_object_new(a, &frame, error);
        jset_int(frame, "id", (int64_t)i, a, error);

        char fname[256];
        const char *display_name = name ? name : "<unknown>";
        size_t display_len = name ? name_len : 9U;
        size_t copy = display_len < sizeof(fname) - 1 ? display_len : sizeof(fname) - 1;
        memcpy(fname, display_name, copy);
        fname[copy] = '\0';
        jset_str(frame, "name", fname, a, error);
        jset_int(frame, "line", line, a, error);
        jset_int(frame, "column", col, a, error);

        /* Source object. */
        vigil_json_value_t *source = NULL;
        vigil_json_object_new(a, &source, error);
        jset_str(source, "name", "main.vigil", a, error);
        jset_int(source, "sourceReference", 0, a, error);
        vigil_json_object_set(frame, "source", 6, source, error);

        vigil_json_array_push(frames, frame, error);
    }

    vigil_json_object_set(body, "stackFrames", 11, frames, error);
    jset_int(body, "totalFrames", (int64_t)count, a, error);
    return send_response(s, req_seq, "stackTrace", body, error);
}

static vigil_status_t handle_scopes(vigil_dap_server_t *s, int req_seq, int frame_id, vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *body = NULL, *scopes = NULL, *scope = NULL;
    vigil_json_object_new(a, &body, error);
    vigil_json_array_new(a, &scopes, error);
    vigil_json_object_new(a, &scope, error);
    jset_str(scope, "name", "Locals", a, error);
    jset_str(scope, "presentationHint", "locals", a, error);
    /* variablesReference encodes frame_id + 1 (0 means no variables). */
    jset_int(scope, "variablesReference", frame_id + 1, a, error);
    jset_bool(scope, "expensive", 0, a, error);
    vigil_json_array_push(scopes, scope, error);
    vigil_json_object_set(body, "scopes", 6, scopes, error);
    return send_response(s, req_seq, "scopes", body, error);
}

static vigil_status_t handle_variables(vigil_dap_server_t *s, int req_seq, int var_ref, vigil_error_t *error)
{
    const vigil_allocator_t *a = ap(s);
    vigil_json_value_t *body = NULL, *vars = NULL;
    vigil_json_object_new(a, &body, error);
    vigil_json_array_new(a, &vars, error);

    if (var_ref > 0)
    {
        size_t frame_idx = (size_t)(var_ref - 1);
        const char *names[32];
        size_t name_lens[32];
        vigil_value_t values[32];
        size_t count = vigil_debugger_frame_locals(s->debugger, frame_idx, names, name_lens, values, 32);

        for (size_t i = 0; i < count; i++)
        {
            vigil_json_value_t *var = NULL;
            vigil_json_object_new(a, &var, error);

            char vname[128];
            size_t copy = name_lens[i] < sizeof(vname) - 1 ? name_lens[i] : sizeof(vname) - 1;
            memcpy(vname, names[i], copy);
            vname[copy] = '\0';
            jset_str(var, "name", vname, a, error);

            /* Format value as string. */
            char vbuf[64];
            switch (vigil_value_kind(&values[i]))
            {
            case VIGIL_VALUE_INT:
                snprintf(vbuf, sizeof(vbuf), "%" PRId64, vigil_value_as_int(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "i32", a, error);
                break;
            case VIGIL_VALUE_UINT:
                snprintf(vbuf, sizeof(vbuf), "%" PRIu64, vigil_value_as_uint(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "u32", a, error);
                break;
            case VIGIL_VALUE_FLOAT:
                snprintf(vbuf, sizeof(vbuf), "%g", vigil_value_as_float(&values[i]));
                jset_str(var, "value", vbuf, a, error);
                jset_str(var, "type", "f64", a, error);
                break;
            case VIGIL_VALUE_BOOL:
                jset_str(var, "value", vigil_value_as_bool(&values[i]) ? "true" : "false", a, error);
                jset_str(var, "type", "bool", a, error);
                break;
            default:
                jset_str(var, "value", "<object>", a, error);
                jset_str(var, "type", "object", a, error);
                break;
            }
            jset_int(var, "variablesReference", 0, a, error);
            vigil_json_array_push(vars, var, error);
            vigil_value_release(&values[i]);
        }
    }

    vigil_json_object_set(body, "variables", 9, vars, error);
    return send_response(s, req_seq, "variables", body, error);
}

/* ── Debug callback (runs while VM is paused) ────────────────────── */

static vigil_debug_action_t dap_on_stop(vigil_debugger_t *debugger, vigil_debug_stop_reason_t reason, void *userdata)
{
    vigil_dap_server_t *s = (vigil_dap_server_t *)userdata;
    vigil_error_t error;
    memset(&error, 0, sizeof(error));
    const vigil_allocator_t *a = ap(s);

    /* Send "stopped" event. */
    vigil_json_value_t *body = NULL;
    vigil_json_object_new(a, &body, &error);
    const char *reason_str = "breakpoint";
    if (reason == VIGIL_DEBUG_STOP_STEP)
        reason_str = "step";
    else if (reason == VIGIL_DEBUG_STOP_ENTRY)
        reason_str = "entry";
    jset_str(body, "reason", reason_str, a, &error);
    jset_int(body, "threadId", 1, a, &error);
    send_event(s, "stopped", body, &error);

    /* Process DAP requests while paused. */
    for (;;)
    {
        vigil_json_value_t *msg = NULL;
        if (vigil_jsonrpc_read(&s->transport, &msg, &error) != VIGIL_STATUS_OK)
        {
            s->running = 0;
            return VIGIL_DEBUG_CONTINUE;
        }

        const char *cmd = jget_str(msg, "command");
        int req_seq = jget_int(msg, "seq");
        const vigil_json_value_t *args = vigil_json_object_get(msg, "arguments");

        if (strcmp(cmd, "continue") == 0)
        {
            vigil_debugger_continue(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            vigil_json_free(&msg);
            return VIGIL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "next") == 0)
        {
            vigil_debugger_step_over(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            vigil_json_free(&msg);
            return VIGIL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stepIn") == 0)
        {
            vigil_debugger_step_into(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            vigil_json_free(&msg);
            return VIGIL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stepOut") == 0)
        {
            vigil_debugger_step_out(debugger);
            send_response(s, req_seq, cmd, NULL, &error);
            vigil_json_free(&msg);
            return VIGIL_DEBUG_CONTINUE;
        }
        if (strcmp(cmd, "stackTrace") == 0)
        {
            handle_stack_trace(s, req_seq, &error);
        }
        else if (strcmp(cmd, "scopes") == 0)
        {
            handle_scopes(s, req_seq, args ? jget_int(args, "frameId") : 0, &error);
        }
        else if (strcmp(cmd, "variables") == 0)
        {
            handle_variables(s, req_seq, args ? jget_int(args, "variablesReference") : 0, &error);
        }
        else if (strcmp(cmd, "threads") == 0)
        {
            handle_threads(s, req_seq, &error);
        }
        else if (strcmp(cmd, "disconnect") == 0)
        {
            send_response(s, req_seq, cmd, NULL, &error);
            s->running = 0;
            vigil_json_free(&msg);
            return VIGIL_DEBUG_CONTINUE;
        }
        else
        {
            /* Unknown request while paused — respond OK. */
            send_response(s, req_seq, cmd, NULL, &error);
        }
        vigil_json_free(&msg);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

vigil_status_t vigil_dap_server_create(vigil_dap_server_t **out, FILE *in, FILE *out_stream,
                                       const vigil_allocator_t *allocator, vigil_error_t *error)
{
    if (out == NULL || in == NULL || out_stream == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "dap: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_allocator_t a =
        (allocator != NULL && vigil_allocator_is_valid(allocator)) ? *allocator : vigil_default_allocator();

    vigil_dap_server_t *s = (vigil_dap_server_t *)a.allocate(a.user_data, sizeof(*s));
    if (s == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "dap: allocation failed");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    memset(s, 0, sizeof(*s));
    s->allocator = a;
    vigil_jsonrpc_transport_init(&s->transport, in, out_stream, &s->allocator);
    *out = s;
    return VIGIL_STATUS_OK;
}

void vigil_dap_server_destroy(vigil_dap_server_t **server)
{
    if (server == NULL || *server == NULL)
        return;
    vigil_dap_server_t *s = *server;
    if (s->debugger != NULL)
    {
        vigil_debugger_detach(s->debugger);
        vigil_debugger_destroy(&s->debugger);
    }
    vigil_allocator_t a = s->allocator;
    a.deallocate(a.user_data, s);
    *server = NULL;
}

vigil_status_t vigil_dap_server_set_runtime(vigil_dap_server_t *server, vigil_vm_t *vm,
                                            const vigil_source_registry_t *sources, vigil_error_t *error)
{
    if (server == NULL || vm == NULL || sources == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "dap: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    server->vm = vm;
    server->sources = sources;
    return vigil_debugger_create(&server->debugger, vm, sources, error);
}

void vigil_dap_server_set_program(vigil_dap_server_t *server, vigil_object_t *function, vigil_source_id_t source_id)
{
    if (server != NULL)
    {
        server->program = function;
        server->source_id = source_id;
    }
}

vigil_status_t vigil_dap_server_run(vigil_dap_server_t *server, vigil_error_t *error)
{
    if (server == NULL || server->debugger == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "dap: server not configured");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_debugger_set_callback(server->debugger, dap_on_stop, server);
    vigil_debugger_attach(server->debugger);
    server->running = 1;
    server->seq = 0;

    /* Main message loop — process requests until launch, then run VM. */
    while (server->running)
    {
        vigil_json_value_t *msg = NULL;
        vigil_status_t st = vigil_jsonrpc_read(&server->transport, &msg, error);
        if (st != VIGIL_STATUS_OK)
            break;

        const char *cmd = jget_str(msg, "command");
        int req_seq = jget_int(msg, "seq");
        const vigil_json_value_t *args = vigil_json_object_get(msg, "arguments");

        if (strcmp(cmd, "initialize") == 0)
        {
            handle_initialize(server, req_seq, error);
        }
        else if (strcmp(cmd, "launch") == 0)
        {
            handle_launch(server, req_seq, error);

            /* Execute the program — the debug callback handles
               all interaction while the VM is running. */
            if (server->program != NULL)
            {
                vigil_value_t result;
                vigil_value_init_nil(&result);
                vigil_error_t exec_error;
                memset(&exec_error, 0, sizeof(exec_error));
                vigil_vm_execute_function(server->vm, server->program, &result, &exec_error);
                vigil_value_release(&result);

                /* Send terminated + exited events. */
                send_event(server, "terminated", NULL, error);
                vigil_json_value_t *exit_body = NULL;
                vigil_json_object_new(ap(server), &exit_body, error);
                jset_int(exit_body, "exitCode", 0, ap(server), error);
                send_event(server, "exited", exit_body, error);
            }
        }
        else if (strcmp(cmd, "setBreakpoints") == 0)
        {
            handle_set_breakpoints(server, req_seq, args, error);
        }
        else if (strcmp(cmd, "configurationDone") == 0)
        {
            send_response(server, req_seq, cmd, NULL, error);
        }
        else if (strcmp(cmd, "threads") == 0)
        {
            handle_threads(server, req_seq, error);
        }
        else if (strcmp(cmd, "disconnect") == 0)
        {
            send_response(server, req_seq, cmd, NULL, error);
            server->running = 0;
        }
        else
        {
            /* Unknown — respond OK. */
            send_response(server, req_seq, cmd, NULL, error);
        }
        vigil_json_free(&msg);
    }

    vigil_debugger_detach(server->debugger);
    return VIGIL_STATUS_OK;
}
