#include <stdio.h>
#include <stdlib.h>
#include "base.h"

static bool load_program(base_process_t *process, base_package_t *into, char *path) {
    #define error(msg) printf("%s\n", msg); goto fail

    bool success = true;
    FILE *prog = NULL;
    uint8_t *src = NULL;

    prog = fopen(path, "r");
    if (prog == NULL) { error("missing program"); }
    fseek(prog, 0L, SEEK_END);
    size_t size = ftell(prog);
    fseek(prog, 0L, SEEK_SET);
    src = malloc(size);
    if (fread(src, 1L, size, prog) != size) { error("failed to read program"); }
    if (!base_parse_pkg(process, into, src, size)) { error("failed to read package"); }

    goto cleanup;

fail:
    success = false;

cleanup:
    if (src != NULL) { free(src); }
    if (prog != NULL) { fclose(prog); }
    return success;

    #undef error
}

int main(int argc, char **argv) {
    base_process_t process;
    base_package_t prog;

    base_process_init(&process, 1024*1024);

    if (!load_program(&process, &prog, argv[1])) {
        printf("failed to load program\n");
        return 1;
    }

    printf("executing...\n");
    base_process_exec(&process, base_vector_get(prog.items, prog.toplevel));

    printf("result: %p\n", process.value);
}

