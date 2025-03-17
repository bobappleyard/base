#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "base.h"

static uintptr_t int_val(base_value_t val) {
    union {
        void *p;
        uintptr_t i;
    } v;
    v.p = val;
    return v.i;
}

base_kind_t base_value_kind(base_value_t val) {
    switch (int_val(val) & 3) {
    case 1:
    case 3:
        return BASE_INT;
    case 2:
        return BASE_FLT;
    }
    return ((base_object_t *) val)->kind;
}

bool base_is_value_reference(base_value_t val) {
    return false;
}

bool base_value_as_bool(base_value_t val) {
    return false;
}

int base_value_as_int(base_value_t val) {
    return int_val(val) >> 2;
}

base_value_t base_int_as_value(int val) {
    return (base_value_t) ((uintptr_t) val << 2 | 1);
}

uint32_t base_vector_count(base_value_t vec) {
    base_vector_t *v;
    if (!BASE_UNPACK(&v, vec)) {
        return 0;
    }
    return v->count;
}

base_value_t base_vector_get(base_value_t vec, base_uint_t idx) {
    base_vector_t *v;
    if (!BASE_UNPACK(&v, vec)) {
        return NULL;
    }
    return v->items[idx];
}

void base_vector_set(base_value_t vec, base_uint_t idx, base_value_t val) {
    base_vector_t *v;
    if (!BASE_UNPACK(&v, vec)) {
        return;
    }
    v->items[idx] = val;
}

bool base_unpack_reference(void *into, base_value_t from, base_kind_t tag) {
    if (base_value_kind(from) != tag) {
        return false;
    }
    *((void **) into) = from;
    return true;
}

static void collect(base_process_t *process) {
    printf("out of memory\n");
    exit(1);
}

static void *alloc_memory(base_process_t *process, base_uint_t size) {
    if (process->alloc_cur + size > process->alloc_end) {
        collect(process);
    }

    char *object = process->alloc_arena + process->alloc_cur;
    process->alloc_cur += size;

    memset(object, 0, size);

    return object;
}

base_value_t base_new_object(base_process_t *process, void *data, base_uint_t size, base_kind_t kind) {
    base_object_t *object = alloc_memory(process, size + sizeof(base_object_t));
    object->kind = kind;
    memcpy(object->data, data, size);
    return (union {base_object_t *o; base_value_t v;}) {.o = object}.v;
}

void base_process_init(base_process_t *process, base_uint_t arena_size) {
    process->alloc_arena = malloc(arena_size);
    process->alloc_cur = 0;
    process->alloc_end = arena_size;
    process->builtins = malloc(sizeof(*process->builtins));
    init(process->builtins);
    insert(process->builtins, "hi", base_int_as_value(5));
}

base_value_t object_to_value(void *object) {
    union {
        void *o;
        base_value_t v;
    } cast;
    cast.o = object;
    return cast.v;
}

base_value_t base_func_new(base_process_t *process, base_func_impl_t *code, base_value_t data) {
    base_func_t *func = alloc_memory(process, sizeof(base_func_t));

    func->kind = BASE_FUNC;
    func->code = code;
    func->data = data;
    
    return object_to_value(func);
}

static void user_func_wrapper(base_process_t *process, base_value_t data, base_uint_t argc) {
    base_user_func_t *scope = NULL;
    BASE_UNPACK(&scope, data);

    if (argc != scope->argc) {
        base_error(process, "wrong number of arguments");
        return;
    }
    
    process->frame  -= scope->varc;
    process->code    = scope->code;
    process->scope   = scope;
}

base_value_t base_user_func_new(base_process_t *process, 
                                base_uint_t argc, base_uint_t varc, base_package_t *pkg,
                                vec(base_op_t) *code) {
    
    base_uint_t size = sizeof(base_user_func_t) + size(code) * sizeof(base_op_t);
    base_user_func_t *func = alloc_memory(process, size);
    func->kind  = BASE_USER_FUNC;
    func->argc  = argc;
    func->varc  = varc;
    func->pkg   = pkg;
    func->codec = size(code);

    base_op_t *cur = func->code;
    for_each(code, op) {
        *cur++ = *op;
    }

    return base_func_new(process, &user_func_wrapper, object_to_value(func));
}

static void builtin_func_wrapper(base_process_t *process, base_value_t data, base_uint_t argc) {
    base_builtin_func_t *scope = NULL;
    BASE_UNPACK(&scope, data);

    if (argc != scope->argc) {
        base_error(process, "wrong number of arguments");
        return;
    }

    process->code  = &scope->code;
}

base_value_t base_builtin_func_new(base_process_t *process, base_uint_t argc, base_op_impl_t *code) {

    base_builtin_func_t *func = alloc_memory(process, sizeof(base_builtin_func_t));
    func->kind      = BASE_BUILTIN_FUNC;
    func->argc      = argc;
    func->code.code = code;

    return base_func_new(process, &builtin_func_wrapper, object_to_value(func));
}
