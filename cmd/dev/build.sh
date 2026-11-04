#!/bin/sh
# build dev toolchain directly without the shared aruu library
set -e

DEVDIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DEVDIR/../.." && pwd)"
OUT="${DEVDIR}/bin"
mkdir -p "$OUT"

CC=${CC:-cc}
WARN="-Wno-discarded-qualifiers -Wno-unused-function -Wno-unused-variable \
      -Wno-incompatible-pointer-types -Wno-int-to-pointer-cast \
      -Wno-pointer-to-int-cast -Wno-implicit-function-declaration \
      -Wno-parentheses -Wno-int-conversion -Wno-sign-compare"
CFLAGS="$WARN -std=c11 -D_POSIX_C_SOURCE=200809L \
  -I${ROOT}/shared/libtcutil -I${ROOT}/shared/libtcutil/elf -I${ROOT}/shared/libtcutil/ar \
  -I${DEVDIR} -I${DEVDIR}/as -I${DEVDIR}/as/arch/x64 \
  -I${DEVDIR}/ld -I${DEVDIR}/cc"

# generate config if missing
if [ ! -f "${DEVDIR}/config.h" ]; then
  sh "${DEVDIR}/configure"
fi

TCU="${ROOT}/shared/libtcutil/tcutil.c ${ROOT}/shared/libtcutil/table.c \
      ${ROOT}/shared/libtcutil/elf/elfutil.c ${ROOT}/shared/libtcutil/ar/archive.c"

# assembler
echo "building as..."
$CC $CFLAGS -o "$OUT/as" \
  ${DEVDIR}/as/as.c ${DEVDIR}/as/as_util.c ${DEVDIR}/as/parse_asm.c \
  ${DEVDIR}/as/ir_asm.c ${DEVDIR}/as/arch/x64/asm_code.c \
  ${DEVDIR}/as/arch/x64/parse_x64.c ${DEVDIR}/as/arch/x64/reloc_x64.c \
  ${DEVDIR}/as/arch/x64/backend.c ${DEVDIR}/as/emit_elf.c $TCU

# linker
echo "building ld..."
$CC $CFLAGS -I${DEVDIR}/ld/arch/x64 -I${DEVDIR}/ld/arch/x86 \
  -I${DEVDIR}/ld/arch/arm -I${DEVDIR}/ld/arch/aarch64 -I${DEVDIR}/ld/arch/riscv64 \
  -o "$OUT/ld" \
  ${DEVDIR}/ld/ld.c ${DEVDIR}/ld/arch.c \
  ${DEVDIR}/ld/arch/x64/reloc_x64.c ${DEVDIR}/ld/arch/x86/reloc_x86.c \
  ${DEVDIR}/ld/arch/arm/reloc_arm.c ${DEVDIR}/ld/arch/aarch64/reloc_aarch64.c \
  ${DEVDIR}/ld/arch/riscv64/reloc_riscv64.c $TCU

# shared cc objects (everything except entry points)
CC_COMMON="${DEVDIR}/cc/attr.c ${DEVDIR}/cc/decl.c \
  ${DEVDIR}/cc/eval.c ${DEVDIR}/cc/expr.c ${DEVDIR}/cc/init.c \
  ${DEVDIR}/cc/map.c ${DEVDIR}/cc/pp.c ${DEVDIR}/cc/scan.c \
  ${DEVDIR}/cc/scope.c ${DEVDIR}/cc/stmt.c ${DEVDIR}/cc/targ.c \
  ${DEVDIR}/cc/token.c ${DEVDIR}/cc/tree.c ${DEVDIR}/cc/type.c \
  ${DEVDIR}/cc/utf.c ${DEVDIR}/cc/util.c ${DEVDIR}/cc/qbe.c"

# cc1 (cproc)
echo "building cc1..."
$CC $CFLAGS -o "$OUT/cc1" \
  ${DEVDIR}/cc/cc1.c $CC_COMMON

# preprocessor
echo "building cpp..."
$CC $CFLAGS -o "$OUT/cpp" \
  ${DEVDIR}/cc/cpp.c $CC_COMMON

# driver
echo "building cc..."
$CC $CFLAGS -o "$OUT/cc" ${DEVDIR}/cc/driver.c ${DEVDIR}/cc/util.c

echo "done. binaries in $OUT"
