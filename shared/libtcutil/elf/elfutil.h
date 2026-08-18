/* ELF header and program header output */

#ifndef ARUU_TCUTIL_ELFUTIL_H
#define ARUU_TCUTIL_ELFUTIL_H

#include <stdio.h> /* FILE */

#ifdef __APPLE__
#include "../../../include/elf.h"
#else
#include <elf.h>
#endif

void out_elf_header(FILE *fp, uintptr_t entry, int phnum, int shnum, int flags, uintptr_t shoff);
void out_program_header(
    FILE     *fp,
    int       type,
    uintptr_t offset,
    uintptr_t vaddr,
    size_t    filesz,
    size_t    memsz,
    int       flags,
    size_t    align
);

#endif /* ARUU_TCUTIL_ELFUTIL_H */
