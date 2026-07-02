#!/bin/sh
# drives the build without needing a working make to bootstrap
#
set -e

# per-run work directory, cleaned up on exit
#
WORKDIR="${TMPDIR:-/tmp}/aruu.$$"
mkdir -p "$WORKDIR"
trap 'rm -rf "$WORKDIR"' EXIT INT TERM HUP

JOBS=1
LOAD=0
TARGET=all

while [ $# -gt 0 ]; do
        case "$1" in
                -j)  shift; JOBS="${1:?-j requires a number}" ;;
                -j*) JOBS="${1#-j}" ;;
                -l)  shift; LOAD="${1:?-l requires a number}" ;;
                -l*) LOAD="${1#-l}" ;;
                -*)  printf 'unknown flag: %s\n' "$1" >&2; exit 1 ;;
                *)   TARGET="$1" ;;
        esac
        shift
done

# timestamp comparison
#
mtime() {
        stat -c '%Y' "$1" 2>/dev/null || stat -f '%m' "$1" 2>/dev/null || echo 0
}
newer_than() {
        local s t
        [ -e "$2" ] || return 0
        s=$(mtime "$1"); t=$(mtime "$2")
        [ "$s" -gt "$t" ]
}
any_newer_than() {
        local target="$1"; shift
        for src do
                newer_than "$src" "$target" && return 0
        done
        return 1
}

[ -f build.cfg ] || { printf 'build.cfg not found; see the repository README\n' >&2; exit 1; }

# generate config files if missing or build.cfg is newer
if [ ! -f scripts/mk/config.kv ] || newer_than build.cfg scripts/mk/config.kv; then
        sh scripts/genconfig.sh
fi

# shellcheck disable=SC1091
. ./build.cfg

# load config.kv if it exists to override and provide detected TLS settings
if [ -f scripts/mk/config.kv ]; then
        # shellcheck disable=SC1091
        . ./scripts/mk/config.kv
fi

# cppflags derived from build.cfg; any feature_* added there is picked up automatically
_feature_flags=""
while IFS= read -r _line; do
        _trimmed=$(printf '%s' "$_line" | tr -d ' \t')
        case "$_trimmed" in
                FEATURE_*=*)
                        _key=${_trimmed%%=*}
                        eval "_val=\$$_key"
                        _feature_flags="$_feature_flags -D${_key}=${_val}"
                        ;;
        esac
done <<EOF
$(tr ';' '\n' < build.cfg)
EOF
unset _line _key _val _trimmed

CPPFLAGS="-Ishared -DPREFIX=\\\"\$PREFIX\\\" -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_NETBSD_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64$_feature_flags $CPPFLAGS_TLS"
LDFLAGS="${LDFLAGS:-} $LDFLAGS_TLS"
TLS_LDLIBS="$LDLIBS_TLS"

CC=${CC:-cc}
AR=${AR:-ar}
ARFLAGS=${ARFLAGS:-rc}
RANLIB=${RANLIB:-ranlib}
PREFIX=${PREFIX:-/usr/local}
MANPREFIX=${MANPREFIX:-$PREFIX/share/man}

# shared headers, most objects depend on these
HDR=$(echo shared/*.h)
LIB="shared/libredline/libredline.a shared/libutil/libutil.a shared/libutf/libutf.a"

# set by multi-file builders for generated headers, cleared after use
EXTRA_HDR=

# parallel job queue
# objs_for must not run in a subshell, enqueue writes _qn/_queue in the current shell
#
_qn=0
_queue=""
_objs=""
# wraps arg in single quotes, escaping embedded ones, for safe eval
shquote() {
        printf "'"
        printf '%s' "$1" | sed "s/'/'\\\\''/g"
        printf "'"
}
enqueue() {
        local desc="$1" cmd="$2"
        _qn=$((_qn + 1))
        local s="$WORKDIR/j${_qn}.sh"
        {
                printf '#!/bin/sh\n'
                printf 'printf "%%s\\n" %s\n' "$(shquote "$desc")"
                printf 'eval %s\n'            "$(shquote "$cmd")"
        } > "$s"
        _queue="$_queue $_qn"
}
# silently skipped on systems without /proc/loadavg
_wait_for_load() {
        [ "$LOAD" -eq 0 ] && return 0
        local avg
        while true; do
                avg=$(awk '{printf "%d", int($1 + 0.5)}' /proc/loadavg 2>/dev/null) || return 0
                [ "$avg" -le "$LOAD" ] && return 0
                sleep 1
        done
}
drain() {
        [ -z "$_queue" ] && return 0
        local n
        _wait_for_load
        if [ "$JOBS" -le 1 ]; then
                for n in $_queue; do
                        sh "$WORKDIR/j${n}.sh" || { printf 'error: build step failed\n' >&2; exit 1; }
                done
        else
                # xargs -P (POSIX 2024), -n 1 so each path becomes its own sh invocation
                for n in $_queue; do
                        printf '%s\n' "$WORKDIR/j${n}.sh"
                done | xargs -P "$JOBS" -n 1 sh || { printf 'error: build step failed\n' >&2; exit 1; }
        fi
        _queue=""
}

# compile and link routines
#
compile_c() {
        local src="$1" obj="$2"; shift 2
        # shellcheck disable=SC2086
        any_newer_than "$obj" "$src" $HDR $EXTRA_HDR || return 0
        enqueue "  CC  $obj" "$CC $* $CPPFLAGS $CFLAGS -o $obj -c $src"
}
link_bin() {
        local bin="$1" objs="" libs="" sep=0; shift
        for a do
                if   [ "$a" = "--" ]; then sep=1
                elif [ "$sep" = 0  ]; then objs="$objs $a"
                else                       libs="$libs $a"
                fi
        done
        drain
        # shellcheck disable=SC2086
        any_newer_than "$bin" $objs || return 0
        printf '  LD  %s\n' "$bin"
        # shellcheck disable=SC2086
        if [ "${bin##*/}" = "wget" ]; then
                eval "$CC $LDFLAGS -o $bin $objs $libs $LDLIBS $TLS_LDLIBS"
        else
                eval "$CC $LDFLAGS -o $bin $objs $libs $LDLIBS"
        fi
}
# writes to _objs rather than stdout so the caller avoids a subshell that
# would discard enqueued jobs
objs_for() {
        local dir="$1" skip=" $2 " flags="$3" src base
        _objs=""
        for src in "$dir"/*.c; do
                [ -f "$src" ] || continue
                base=${src##*/}
                case "$skip" in *" $base "*) continue ;; esac
                compile_c "$src" "${src%.c}.o" $flags
                _objs="$_objs ${src%.c}.o"
        done
}

# static libraries
# both directories enqueued before the single drain so they compile in parallel
#
build_lib() {
        local utf_objs util_objs redline_objs
        objs_for shared/libutf "" ""
        utf_objs="$_objs"
        objs_for shared/libutil "" ""
        util_objs="$_objs"
        objs_for shared/libredline "" ""
        redline_objs="$_objs"
        drain
        # shellcheck disable=SC2086
        any_newer_than shared/libutf/libutf.a $utf_objs && {
                printf '  AR  shared/libutf/libutf.a\n'
                $AR $ARFLAGS shared/libutf/libutf.a $utf_objs
                $RANLIB shared/libutf/libutf.a
        }
        # shellcheck disable=SC2086
        any_newer_than shared/libutil/libutil.a $util_objs && {
                printf '  AR  shared/libutil/libutil.a\n'
                $AR $ARFLAGS shared/libutil/libutil.a $util_objs
                $RANLIB shared/libutil/libutil.a
        }
        # shellcheck disable=SC2086
        any_newer_than shared/libredline/libredline.a $redline_objs && {
                printf '  AR  shared/libredline/libredline.a\n'
                $AR $ARFLAGS shared/libredline/libredline.a $redline_objs
                $RANLIB shared/libredline/libredline.a
        }
        return 0
}

# per-tool category builder
#
cfgvar() {
        local cat="$1" base="$2"
        printf 'BUILD_%s_%s' "$cat" "$base" | tr 'a-z-' 'A-Z_'
}

# individual flag overrides group flag
cfg_enabled() {
        local _var="$1" _v
        while [ -n "$_var" ]; do
                eval "_v=\${${_var}-UNSET}"
                if [ "$_v" = "1" ]; then
                        return 0
                elif [ "$_v" = "0" ]; then
                        return 1
                fi
                case "$_var" in
                        *_[!_]*) _var=$(printf '%s' "$_var" | sed 's/_[^_]*$//') ;;
                        *)       break ;;
                esac
        done
        return 1
}

build_simple_tools() {
        local cat="$1" src base var
        for src in cmd/"$cat"/*.c; do
                [ -f "$src" ] || continue
                base=${src##*/}; base=${base%.c}
                var=$(cfgvar "$cat" "$base")
                cfg_enabled "$var" || continue
                local bin="cmd/$cat/$base"
                # shellcheck disable=SC2086
                any_newer_than "$bin" "$src" $HDR $EXTRA_HDR $LIB || continue
                if [ "$base" = "diff" ]; then
                        enqueue "  CC  $bin" "$CC $src $CPPFLAGS $CFLAGS $LDFLAGS -o $bin $LIB -lm $LDLIBS"
                else
                        enqueue "  CC  $bin" "$CC $src $CPPFLAGS $CFLAGS $LDFLAGS -o $bin $LIB $LDLIBS"
                fi
        done
        drain
}

build_awk() {
        cfg_enabled BUILD_POSIX_AWK || return 0
        local dir=cmd/posix/awk
        CC="$CC" CFLAGS="$CFLAGS" YACC="$YACC" sh scripts/genconfig.sh awk
        EXTRA_HDR="$dir/awk.h $dir/awkgram.tab.h $dir/proto.h"
        objs_for "$dir" maketab.c "-I$dir"
        EXTRA_HDR=
        link_bin "$dir/awk" $_objs -- $LIB -lm
}

build_sh() {
        cfg_enabled BUILD_POSIX_SH || return 0
        local dir=cmd/posix/sh
        CC="$CC" CFLAGS="$CFLAGS" CPPFLAGS="$CPPFLAGS" sh scripts/genconfig.sh sh
        EXTRA_HDR="$dir/syntax.h $dir/nodes.h $dir/builtins.h $dir/token.h"
        objs_for "$dir" "mknodes.c mksyntax.c" "-DSHELL -I$dir"
        EXTRA_HDR=
        link_bin "$dir/sh" $_objs -- $LIB
}

build_make() {
        cfg_enabled BUILD_POSIX_MAKE || return 0
        local dir=cmd/posix/make
        EXTRA_HDR="$dir/make.h"
        objs_for "$dir" "" ""
        EXTRA_HDR=
        link_bin "$dir/make" $_objs -- $LIB
}

build_bc() {
        cfg_enabled BUILD_POSIX_BC || return 0
        local src=cmd/posix/bc.c
        YACC="$YACC" sh scripts/genconfig.sh bc
        # shellcheck disable=SC2086
        any_newer_than cmd/posix/bc "$src" $HDR $EXTRA_HDR $LIB || return 0
        printf '  CC  cmd/posix/bc\n'
        eval "$CC $src $CPPFLAGS $CFLAGS $LDFLAGS -o cmd/posix/bc $LIB $LDLIBS"
}

build_posix() {
        cfg_enabled BUILD_POSIX_GETCONF && sh scripts/genconfig.sh getconf
        build_simple_tools posix
        build_awk
        build_bc
        build_sh
        build_make
}

# multi-file dev toolchain
#
build_ar() {
        cfg_enabled BUILD_DEV_AR || return 0
        local dir=cmd/dev/ar
        objs_for "$dir" tinyar.c "-I$dir"
        link_bin "$dir/ar" $_objs -- $LIB
}

build_as() {
        cfg_enabled BUILD_DEV_AS || return 0
        local dir="cmd/dev/as"
        local flags="-Icmd/dev/xcutil -I$dir -Ishared"
        local objs=""
        objs_for "cmd/dev/xcutil" "" "$flags"
        objs="$objs $_objs"
        objs_for "$dir" "" "$flags"
        objs="$objs $_objs"
        objs_for "$dir/arch/x64" "" "-Icmd/dev/xcutil -I$dir -I$dir/arch/x64 -Ishared"
        objs="$objs $_objs"
        link_bin "$dir/as" $objs -- $LIB
}

build_ld() {
        cfg_enabled BUILD_DEV_LD || return 0
        local dir="cmd/dev/ld"
        local flags="-Icmd/dev/xcutil -I$dir"
        local objs=""
        objs_for "cmd/dev/xcutil" "" "$flags"
        objs="$objs $_objs"
        objs_for "$dir" "" "$flags"
        objs="$objs $_objs"
        link_bin "$dir/ld" $objs -- $LIB
}

build_cc() {
        cfg_enabled BUILD_DEV_CC || return 0
        local dir="cmd/dev/cc"
        local cflags="-I$dir"
        local common

        # compiled once, linked into both cc1 and cpp
        objs_for "$dir" "driver.c cc1.c cpp.c" "$cflags"
        common="$_objs"
        drain

        compile_c "$dir/cc1.c"    "$dir/cc1.o"    $cflags
        compile_c "$dir/cpp.c"    "$dir/cpp.o"    $cflags
        compile_c "$dir/driver.c" "$dir/driver.o" $cflags
        drain

        link_bin "$dir/cc1" "$dir/cc1.o"    $common -- $LIB
        link_bin "$dir/cpp" "$dir/cpp.o"    $common -- $LIB
        link_bin "$dir/cc"  "$dir/driver.o" "$dir/util.o" -- $LIB
}

build_dev() {
        [ -f cmd/dev/configure ] && sh cmd/dev/configure
        build_ar
        build_as
        build_ld
        build_cc
}

# manual page generation
#
man_section() {
        case "$1" in
                */linux/*|*/net/*|*/xsi/*) printf '8\n' ;;
                *)                         printf '1\n' ;;
        esac
}

build_man_for() {
        local var="$1" src="$2" name="$3" sec out_mdoc out_txt
        cfg_enabled "$var" || return 0
        [ -x scripts/mkman/mkman ] || { printf 'error: mkman not built\n' >&2; exit 1; }

        grep -qE '!man|\?man' "$src" || return 0

        sec=$(man_section "$src")

        mkdir -p "man/man${sec}"
        out_mdoc="man/man${sec}/${name}.${sec}"
        out_txt="man/man${sec}/${name}.${sec}.txt"
        if any_newer_than "$out_mdoc" "$src" scripts/mk/config.kv scripts/mkman/mkman; then
                printf '  MAN   %s\n' "$out_mdoc"
                scripts/mkman/mkman -fmt mdoc -config scripts/mk/config.kv -section "$sec" "$src" > "$out_mdoc"
        fi
        if any_newer_than "$out_txt" "$src" scripts/mk/config.kv scripts/mkman/mkman; then
                printf '  MAN   %s\n' "$out_txt"
                scripts/mkman/mkman -fmt txt -config scripts/mk/config.kv -section "$sec" "$src" > "$out_txt"
        fi
}

build_man() {
        local src cat base sub name var
        if [ ! -x scripts/mkman/mkman ] || any_newer_than scripts/mkman/mkman scripts/mkman/main.go scripts/mkman/page.go scripts/mkman/parse.go scripts/mkman/mdoc.go; then
                printf '  GO    scripts/mkman/mkman\n'
                (cd scripts/mkman && go build -o mkman .)
        fi
        find cmd -type f -name '*.c' | while read -r src; do
                sub=${src#cmd/}
                cat=${sub%%/*}
                base=${src##*/}
                base=${base%.c}
                name=${sub#*/}
                if [ "${name}" != "${base}.c" ]; then
                        name=${name%%/*}
                else
                        name=$base
                fi
                var=$(cfgvar "$cat" "$name")
                build_man_for "$var" "$src" "$name"
        done
}

# install and clean
#
do_install() {
        local bin f sec
        mkdir -p "$PREFIX/bin" "$MANPREFIX/man1" "$MANPREFIX/man8"
        find cmd -type f ! -name '*.*' -perm -100 | while read -r bin; do
                printf '  INSTALL %s/bin/%s\n' "$PREFIX" "${bin##*/}"
                cp "$bin" "$PREFIX/bin/${bin##*/}"
                chmod 755 "$PREFIX/bin/${bin##*/}"
        done
        find man/man1 man/man8 -type f -name '*.[18]' 2>/dev/null | while read -r f; do
                sec=${f%/*}; sec=${sec##*/}
                cp "$f" "$MANPREFIX/${sec}/${f##*/}"
        done
}

do_regen() {
        # 1. regenerate config
        sh scripts/genconfig.sh

        # 2. re-read the new config.kv
        if [ -f scripts/mk/config.kv ]; then
                # shellcheck disable=SC1091
                . scripts/mk/config.kv
        fi

        # 3. clean and generate files using scripts/genconfig.sh
        CC="$CC" CFLAGS="$CFLAGS" CPPFLAGS="$CPPFLAGS" YACC="$YACC" sh scripts/genconfig.sh clean
        CC="$CC" CFLAGS="$CFLAGS" CPPFLAGS="$CPPFLAGS" YACC="$YACC" sh scripts/genconfig.sh all
}

do_clean() {
        printf '  CLEAN object files\n'
        find shared cmd -name '*.o' -exec rm -f {} +
        printf '  CLEAN static libraries\n'
        find shared -name '*.a' -exec rm -f {} +
        printf '  CLEAN compiled binaries\n'
        find cmd -type f ! -name '*.*' -perm -100 -exec rm -f {} +
        sh scripts/genconfig.sh clean
        rm -f scripts/mk/config.mk scripts/mk/config.kv scripts/mk/rules.mk shared/libutil/nofork_list.h
        printf '  CLEAN build tools\n'
        rm -f scripts/mkman/mkman
        printf '  CLEAN dev artifacts\n'
        rm -f cmd/dev/cc/cc1 cmd/dev/cc/cpp cmd/dev/as/as cmd/dev/ld/ld cmd/dev/ar/ar
        rm -f shared/libaruuelf.so
        rm -f cmd/dev/config.h cmd/dev/cc/config.h cmd/dev/version.h
        printf '  CLEAN misc\n'
        rm -rf man/man1 man/man8
        rm -rf aruu-box .box
        printf '  CLEAN done\n'
}

case "$TARGET" in
        all)
                build_lib
                build_posix
                build_dev
        build_simple_tools linux
        build_simple_tools net
        build_simple_tools xsi
        build_simple_tools pseudo
        build_simple_tools extra
                ;;
        lib)             build_lib ;;
        posix)           build_lib; build_posix ;;
        dev)             build_lib; build_dev ;;
        make)            build_lib; build_make ;;
        linux|net|xsi|pseudo|extra)
                         build_lib; build_simple_tools "$TARGET" ;;
        regen)           do_regen ;;
        clean)           do_clean ;;
        man)             build_man ;;
        install)
                build_lib
                build_posix
                build_dev
                build_simple_tools linux
                build_simple_tools net
                build_simple_tools xsi
                build_simple_tools pseudo
                build_simple_tools extra
                build_man
                do_install
                ;;
        help)
                printf 'usage: sh Makefile.sh [-jN] [-lN] [all|clean|install|man|lib|posix|linux|net|xsi|pseudo|extra|dev|make|regen|help]\n'
                printf '\nTargets:\n'
                printf '  all       Build all enabled utilities\n'
                printf '  clean     Clean built objects and binaries\n'
                printf '  install   Install binaries and man pages\n'
                printf '  man       Generate man pages\n'
                printf '  lib       Build core libraries\n'
                printf '  posix     Build POSIX utilities\n'
                printf '  linux     Build Linux utilities\n'
                printf '  net       Build Network utilities\n'
                printf '  xsi       Build XSI utilities\n'
                printf '  pseudo    Build Pseudo utilities\n'
                printf '  extra     Build Extra utilities\n'
                printf '  dev       Build Dev toolchain\n'
                printf '  make      Build POSIX make\n'
                printf '  regen     Regenerate configuration and all generated source files\n'
                printf '  help      Show this help message\n'
                ;;
        *)
                printf 'usage: sh Makefile.sh [-jN] [-lN] [all|clean|install|man|lib|posix|linux|net|xsi|pseudo|extra|dev|make|regen|help]\n' >&2
                exit 1
                ;;
esac
