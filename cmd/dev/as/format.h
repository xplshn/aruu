/* format dispatch: selects the output object format at compile time
 * a new format adds its emit function below and its source file
 * alongside emit_elf.c / emit_macho.c */

#ifndef AS_FORMAT_H
#define AS_FORMAT_H

#include "../config.h"

struct Table;
struct Vector;

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
int emit_macho_obj(
    const char *ofn, struct Vector *sections, struct Table *label_table, struct Vector *unresolved
);
#define EMIT_OBJ emit_macho_obj
#else
int emit_elf_obj(
    const char *ofn, struct Vector *sections, struct Table *label_table, struct Vector *unresolved
);
#define EMIT_OBJ emit_elf_obj
#endif

#endif /* AS_FORMAT_H */
