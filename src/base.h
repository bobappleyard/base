#include <stdint.h>
#include <stdbool.h>
#include "cc.h"

/* Value types */

struct base_process_t;

typedef enum {

    BASE_NULL,
    BASE_BOOL,
    BASE_INT,
    BASE_FLT,
    BASE_USER_FUNC,
    BASE_BUILTIN_FUNC,
    BASE_VAR,
    BASE_VEC,
    BASE_BUF,
    BASE_STR,
    BASE_FWD

} base_kind_t;

typedef struct base_value_t *base_value_t;

typedef uintptr_t base_uint_t;
typedef intptr_t base_int_t;

typedef struct {

    base_kind_t kind;
    char        data[];

} base_object_t;

typedef struct {

    base_kind_t kind;
    base_uint_t count;
    uint8_t     chars[];

} base_string_t;

typedef struct {

    base_kind_t   kind;
    base_string_t name;
    bool          exported;
    base_value_t  value;

} base_var_t;

typedef void (base_func_impl_t)(struct base_process_t *p, base_value_t data, base_uint_t argc);

typedef struct {

    base_kind_t  kind;
    base_uint_t  count;
    base_value_t items[];

} base_vector_t;

#define BASE_NEW(process, data) _Generic((data),                                               \
    base_user_func_t    : base_new_object((process), &(data), sizeof(data), BASE_USER_FUNC),   \
    base_builtin_func_t : base_new_object((process), &(data), sizeof(data), BASE_BUILTIN_FUNC) \
)

#define BASE_UNPACK(into, from) _Generic(*(into),                                   \
    base_vector_t*       : base_unpack_reference((into), (from), BASE_VEC),         \
    base_user_func_t*    : base_unpack_reference((into), (from), BASE_USER_FUNC),   \
    base_builtin_func_t* : base_unpack_reference((into), (from), BASE_BUILTIN_FUNC) \
)

base_kind_t base_value_kind(base_value_t val);
bool base_is_value_reference(base_value_t val);
bool base_value_as_bool(base_value_t val);

bool base_unpack_reference(void *into, base_value_t from, base_kind_t tag);

int base_value_as_int(base_value_t val);
base_value_t base_int_as_value(int val);

/* Execution types */

typedef struct {

    base_value_t            items;
    base_uint_t             toplevel;
    map(char*, base_var_t*) exports;

} base_package_t;

typedef void (base_op_impl_t)(struct base_process_t *, base_uint_t);

typedef struct {

    base_op_impl_t *code;
    uintptr_t       arg;

} base_op_t;

typedef struct {

    base_kind_t     kind;
    base_uint_t     argc;
    base_op_t       code;

} base_builtin_func_t;

typedef struct {

    base_kind_t     kind;
    base_package_t *pkg;
    base_uint_t     argc;
    base_uint_t     varc;
    base_uint_t     codec;
    base_op_t       code[];

} base_user_func_t;

typedef struct {

    void (*visit_object)(base_collect_ctx_t *ctx, base_value_t object);
    void (*call_object)(struct base_process_t *process, base_uint_t argc);

} base_typeinfo_t;

typedef struct base_process_t {

    base_value_t                value;
    base_value_t               *frame;
    base_value_t               *prev_frame;
    base_op_t                  *code;
    base_user_func_t           *scope;

    base_value_t               *stack;
    char                       *alloc_arena;
    base_uint_t                 alloc_cur;
    base_uint_t                 alloc_end;

    map(char*, base_package_t) *packages;
    map(char*, base_value_t)   *builtins;

} base_process_t;

void base_process_init(base_process_t *process, base_uint_t arena_size);
void base_process_exec(base_process_t *process, base_value_t toplevel);
void base_error(base_process_t *process, char *message);

bool base_parse_pkg(base_process_t *process, 
                    base_package_t *into,
                    uint8_t *from, base_uint_t size);

base_value_t base_func_new(base_process_t *process, base_func_impl_t *code, base_value_t data);
base_value_t base_user_func_new(base_process_t *process, 
                                base_uint_t argc, base_uint_t varc, base_package_t *pkg,
                                vec(base_op_t) *code);
base_value_t base_builtin_func_new(base_process_t *process, base_uint_t argc, base_op_impl_t *code);

base_value_t base_vector_new(struct base_process_t *process, base_uint_t count);
uint32_t base_vector_count(base_value_t vec);
base_value_t base_vector_get(base_value_t vec, base_uint_t idx);
void base_vector_set(base_value_t vec, base_uint_t idx, base_value_t val);
base_value_t base_new_object(base_process_t *process, void *data, base_uint_t size, base_kind_t kind);

typedef enum {

    BASE_PKG_INVALID,
    BASE_PKG_NULL,
    BASE_PKG_TRUE,
    BASE_PKG_FALSE,
    BASE_PKG_INT,
    BASE_PKG_FLT,
    BASE_PKG_STR,
    BASE_PKG_BIF,
    BASE_PKG_FUNC,
    BASE_PKG_TOP,
    BASE_PKG_VAR

} base_pkg_item_id_t;

typedef enum {

    BASE_BC_NOP,
    BASE_BC_PKG,
    BASE_BC_ARG,
    BASE_BC_GET,
    BASE_BC_SET,
    BASE_BC_JMP,
    BASE_BC_BCH,
    BASE_BC_CAL,
    BASE_BC_RET,
    BASE_BC_RCL
    
} base_bytecode_t;

