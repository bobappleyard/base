#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "base.h"

typedef struct {
    uint8_t     *data;
    base_uint_t  start, end;
} pkg_buffer_t;

typedef struct {
    base_process_t *process;
    base_package_t *pkg;
    base_uint_t     item_count;
    base_value_t   *items;
} pkg_context_t;

typedef struct {
    base_process_t *process;
    base_value_t    func;
    base_uint_t     argc, varc, pkgc;
    base_package_t *pkg;
} bytecode_ctx_t;

void base_error(base_process_t *process, char *message) {
    printf("%s\n", message);
    process->code = NULL;
}

static bool check_header(pkg_buffer_t *buf);
static bool read_value(pkg_context_t *ctx, int i, pkg_buffer_t *from);
static bool read_uint(base_uint_t *into, pkg_buffer_t *from);
static bool buffer_slice(pkg_buffer_t *into, pkg_buffer_t *from, base_uint_t size);
static bool read_string(char **into, pkg_buffer_t *from);

bool base_parse_pkg(base_process_t *process, base_package_t *into, uint8_t *from, size_t size) {
    #define error(msg) printf("%s\n", msg); return false

    pkg_buffer_t buf = {
        .data  = from,
        .start = 0,
        .end   = size
    };
    if (!check_header(&buf)) { error("invalid package header"); }

    base_uint_t item_count;
    if (!read_uint(&item_count, &buf)) { error("unable to read item count"); }

    base_vector_t *items = malloc(sizeof(base_vector_t)  + item_count * sizeof(base_value_t));
    items->kind = BASE_VEC;
    items->count = item_count;
    
    into->items = (base_value_t) items;
    pkg_context_t ctx = {
        .process    = process,
        .pkg        = into,
        .item_count = item_count,
        .items      = items->items
    };

    for (int i = 0; i < item_count; i++) {
        if (!read_value(&ctx, i, &buf)) { error("unable to read item"); }
    }

    printf("finished reading package\n");
    return true;

    #undef error
}

/* Opcodes */

static void op_halt(base_process_t *process, base_uint_t arg) {
    process->code = NULL;
}

static void op_pkg_get(base_process_t *process, base_uint_t arg) {
    process->value = base_vector_get(process->scope->pkg->items, arg);
}

static void op_arg_get(base_process_t *process, base_uint_t arg) {
    process->value = process->prev_frame[arg + 3];
}

static void op_local_get(base_process_t *process, base_uint_t arg) {
    process->value = process->frame[arg];
}

static void op_local_set(base_process_t *process, base_uint_t arg) {
    process->frame[arg] = process->value;
}

static void op_jump(base_process_t *process, base_uint_t arg) {
    process->code += arg;
}

static void op_branch(base_process_t *process, base_uint_t arg) {
    if (!base_value_as_bool(process->value)) {
        process->code += arg;
    }
}

static void op_call(base_process_t *process, base_uint_t arg) {
    if (process->frame < process->stack + 3) {
        base_error(process, "stack overflow");
        return;
    }
    if (base_value_kind(process->value) != BASE_FUNC) {
        base_error(process, "trying to call non-function");
        return;
    }

    process->frame     -= 3;
    process->frame[0]   = (base_value_t) process->code;
    process->frame[1]   = (base_value_t) process->prev_frame;
    process->frame[2]   = (base_value_t) process->scope;
    process->prev_frame = process->frame;

    base_func_t *func;
    BASE_UNPACK(&func, process->value);
    func->code(process, func->data, arg);
}

static void op_return(base_process_t *process, base_uint_t arg) {
    process->frame      = process->prev_frame;
    process->code       = (base_op_t *) process->frame[0];
    process->prev_frame = (base_value_t *) process->frame[1];
    process->scope    = (base_user_func_t *) process->frame[2];
    process->frame     += 3;
}

static void op_return_call(base_process_t *process, base_uint_t arg) {

}

/* Package parsing */

static bool check_header(pkg_buffer_t *buf) {
    buf->start += 8;
    return true;
}

static base_op_t op(void (*code)(base_process_t *, base_uint_t), base_uint_t arg) {
    return (base_op_t) { .code = code, .arg = arg };
}

static bool read_bytecode(bytecode_ctx_t *ctx, pkg_buffer_t *from) {
    vec(base_op_t)        ops;          
    map(base_uint_t, int) forward_refs; 
    init(&ops);
    init(&forward_refs);

    bool success = true;
    int i = 0;
    base_uint_t start = from->start;

    while(from->start < from->end) {
        base_uint_t arg;
        #define error(msg) printf("%s\n", msg); goto fail
        #define needs_arg() if (!read_uint(&arg, from)) { error("unable to read arg"); }

        int *ref = get(&forward_refs, from->start - start);
        if (ref != NULL) {
            get(&ops, *ref)->arg = i - *ref - 1;
            erase(&forward_refs, from->start - start);
        }

        switch (from->data[from->start++]) {
        case BASE_BC_NOP:
            break;

        case BASE_BC_PKG:
            needs_arg();
            printf("pkg %ld\n", arg);
            if (arg >= ctx->pkgc) { error("pkg reference out of bounds"); }
            push(&ops, op(&op_pkg_get, arg));
            break;

        case BASE_BC_ARG:
            needs_arg();
            printf("arg %ld\n", arg);
            if (arg >= ctx->argc) { error("arg reference out of bounds"); }
            push(&ops, op(&op_arg_get, arg));
            break;

        case BASE_BC_GET:
            needs_arg();
            printf("get %ld\n", arg);
            if (arg >= ctx->varc) { error("local reference out of bounds"); }
            push(&ops, op(&op_local_get, arg));
            break;

        case BASE_BC_SET:
            needs_arg();
            printf("set %ld\n", arg);
            if (arg >= ctx->varc) { error("local reference out of bounds"); }
            push(&ops, op(&op_local_set, arg));
            break;

        case BASE_BC_JMP:
            needs_arg();
            printf("jmp %ld\n", arg);
            insert(&forward_refs, arg, i);
            push(&ops, op(&op_jump, arg));
            break;

        case BASE_BC_BCH:
            needs_arg();
            printf("bch %ld\n", arg);
            insert(&forward_refs, arg, i);
            push(&ops, op(&op_branch, arg));
            break;

        case BASE_BC_CAL:
            needs_arg();
            printf("cal %ld\n", arg);
            if (arg >= ctx->varc) { error("arg count out of bounds"); }
            push(&ops, op(&op_pkg_get, arg));
            break;

       case BASE_BC_RET:
            printf("ret\n");
            push(&ops, op(&op_return, 0));
            break;

        case BASE_BC_RCL:
            needs_arg();
            printf("rcl %ld\n", arg);
            if (arg >= ctx->varc) { error("arg count out of bounds"); }
            push(&ops, op(&op_return_call, arg));
            break;
        }

        i++;

        #undef needs_arg
        #undef error
    }

    ctx->func = base_user_func_new(ctx->process, ctx->argc, ctx->varc, ctx->pkg, &ops);

    goto cleanup;

fail:
    success = false;

cleanup:
    cleanup(&ops);
    cleanup(&forward_refs);

    return success;
}

static bool read_value(pkg_context_t *ctx, int i, pkg_buffer_t *from) {
    #define error(msg) printf("%s\n", msg); return false
    
    if (from->start >= from->end) { error("unexpected eof"); }

    uint8_t id = from->data[from->start++];
    switch (id) {
    case BASE_PKG_NULL:
    case BASE_PKG_TRUE:
    case BASE_PKG_FALSE:
        break;

    case BASE_PKG_INT:
        printf("reading int\n");
        base_uint_t value;
        if (!read_uint(&value, from)) { error("failed to read integer"); }
        ctx->items[i] = base_int_as_value(value);
        return true;

    case BASE_PKG_FLT:
    case BASE_PKG_STR:
    case BASE_PKG_BIF:
        char *name;
        if (!read_string(&name, from)) { error("failed to read builtin name"); }
        base_value_t *builtin = get(ctx->process->builtins, name);
        if (builtin == NULL) { error("unknown builtin"); }
        ctx->items[i] = *builtin;
        return true;

    case BASE_PKG_TOP:
        ctx->pkg->toplevel = i;
    case BASE_PKG_FUNC:
        printf("reading func\n");
        base_uint_t argc, varc, bufc;
        if (!read_uint(&argc, from)) { error("failed to read arg count"); }
        if (!read_uint(&varc, from)) { error("failed to read var count"); }
        if (!read_uint(&bufc, from)) { error("failed to read buffer size"); }
        
        pkg_buffer_t buf;
        if (!buffer_slice(&buf, from, bufc)) { error("unexpected eof"); }

        bytecode_ctx_t inner = {
            .process = ctx->process,
            .pkg     = ctx->pkg,
            .argc    = argc,
            .varc    = varc,
            .pkgc    = ctx->item_count
        };
        if (!read_bytecode(&inner, &buf)) { error("failed to read bytecode"); }

        ctx->items[i] = inner.func;
        return true;

    case BASE_PKG_VAR:
    }

    printf("unsupported package item type %d\n", id);
    return false;

    #undef error
}

static bool buffer_slice(pkg_buffer_t *into, pkg_buffer_t *from, base_uint_t size) {
    if (from->start + size > from->end) {
        return false;
    }
    into->data = from->data;
    into->start = from->start;
    into->end = from->start + size;
    from->start = from->start + size;
    return true;
}

static bool read_string(char **into, pkg_buffer_t *from) {
    base_uint_t size;
    if (!read_uint(&size, from)) {
        return false;
    }
    pkg_buffer_t buf;
    if (!buffer_slice(&buf, from, size)) {
        return false;
    }
    char *str = malloc(size+1);
    str[size] = 0;
    memcpy(str, buf.data + buf.start, size);
    *into = str;
    return true;
}

static bool read_uint(base_uint_t *into, pkg_buffer_t *from) {
    base_uint_t n = from->end - from->start;
    base_uint_t res = 0;

    for (int i = 0; i < n; i++) {
        uint8_t b = from->data[i + from->start];
        uint8_t masked = b & 0x7f;
        res |= ((base_uint_t) b) << 7 * i;
        if (b == masked) {
            from->start += i + 1;
            *into = res;
            return true;
        }
    }

    return false;
}

void base_process_exec(base_process_t *process, base_value_t toplevel) {
    static base_op_t boot[] = {
        { .code = &op_local_get, .arg = 0 },
        { .code = &op_call,      .arg = 0 },
        { .code = &op_halt                }
    }; 
    
    process->stack      = malloc(1024 * sizeof(base_value_t));
    process->frame      = process->stack + 1023;
    process->frame[0]   = toplevel;
    process->prev_frame = NULL;
    process->scope    = NULL;
    process->code       = boot;

    while (process->code != NULL) {
        base_op_t *op = process->code++;
        op->code(process, op->arg);
    }
}
