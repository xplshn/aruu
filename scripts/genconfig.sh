#!/bin/sh
# genconfig.sh generates build configuration from build.cfg
#
# outputs four files:
#
# scripts/mk/config.set ninqu reads this via (set-file). flat
# KEY=VAL, unquoted. per-tool BUILD_CATEGORY_TOOL
# toggles get a lowercase alias here, since
# (gate ...) in ninqu.rules only ever checks
# the lowercase form
#
# scripts/mk/config.kv strictly quoted KEY="VAL" format, read by
# mkman through its -config flag. nothing in the ninqu build itself
# sources this file: genbox.sh takes its input on argv only
#
# shared/libutil/nofork_list.h
# wexec.c includes this at compile time
# generated from NOFORK_LIST in build.cfg
#
# scripts/mk/features.h every resolved FEATURE_* as a #define
# CPPFLAGS pulls this in with -include
#
# the codegen subcommands (getconf, bc, awk, sh) are also here so
# ninqu.rules can call (exec sh scripts/genconfig.sh <step>) for
# each one. they check mtimes internally and only regenerate when
# their input changed
#
set -e

target=${1:-config}

if [ -f build.cfg ]; then
        . ./build.cfg
else
        echo "genconfig: build.cfg not found" >&2
        exit 1
fi

newer_than() {
        [ -e "$2" ] || return 0
        local s t
        s=$(stat -c '%Y' "$1" 2>/dev/null || stat -f '%m' "$1" 2>/dev/null || echo 0)
        t=$(stat -c '%Y' "$2" 2>/dev/null || stat -f '%m' "$2" 2>/dev/null || echo 0)
        [ "$s" -gt "$t" ]
}

gen_getconf() {
        if newer_than scripts/getconf.sh cmd/posix/getconf.h; then
                printf "  GEN   cmd/posix/getconf.h\n"
                scripts/getconf.sh > cmd/posix/getconf.h || { rm -f cmd/posix/getconf.h; exit 1; }
        fi
}

gen_bc() {
        if newer_than cmd/posix/bc.y cmd/posix/bc.c; then
                printf "  GEN   cmd/posix/bc.c\n"
                CC=${CC:-cc}
                YACC=${YACC:-}
                if [ -z "$YACC" ]; then
                        if command -v yacc >/dev/null 2>&1; then
                                YACC="yacc"
                        elif command -v bison >/dev/null 2>&1; then
                                YACC="bison -y"
                        else
                                YACC="yacc"
                        fi
                fi
                if [ "$YACC" = "bison -y" ]; then
                        bison -o cmd/posix/bc.c cmd/posix/bc.y
                else
                        $YACC -o cmd/posix/bc.c cmd/posix/bc.y
                fi
        fi
}

gen_awk() {
        local dir="cmd/posix/awk"
        CC=${CC:-cc}
        YACC=${YACC:-}
        if [ -z "$YACC" ]; then
                if command -v yacc >/dev/null 2>&1; then
                        YACC="yacc"
                elif command -v bison >/dev/null 2>&1; then
                        YACC="bison -y"
                else
                        YACC="yacc"
                fi
        fi

        if newer_than "$dir/awkgram.y" "$dir/awkgram.tab.c" || newer_than "$dir/awkgram.y" "$dir/awkgram.tab.h"; then
                printf "  YACC  $dir/awkgram.tab.c\n"
                if [ "$YACC" = "bison -y" ]; then
                        bison -d -o "$dir/awkgram.tab.c" "$dir/awkgram.y"
                else
                        $YACC -d -o "$dir/awkgram.tab.c" "$dir/awkgram.y"
                        if [ ! -f "$dir/awkgram.tab.h" ]; then
                                if [ -f y.tab.h ]; then
                                        mv y.tab.h "$dir/awkgram.tab.h"
                                elif [ -f "$dir/y.tab.h" ]; then
                                        mv "$dir/y.tab.h" "$dir/awkgram.tab.h"
                                fi
                        fi
                fi
        fi

        if newer_than "$dir/maketab.c" "$dir/maketab" || newer_than "$dir/awkgram.tab.h" "$dir/maketab"; then
                printf "  CC    $dir/maketab\n"
                $CC -o "$dir/maketab" "$dir/maketab.c"
        fi

        if newer_than "$dir/maketab" "$dir/proctab.c" || newer_than "$dir/awkgram.tab.h" "$dir/proctab.c"; then
                printf "  GEN   $dir/proctab.c\n"
                "$dir/maketab" "$dir/awkgram.tab.h" > "$dir/proctab.c"
        fi
}

gen_sh() {
        local dir="cmd/posix/sh"
        CC=${CC:-cc}

        if newer_than "$dir/mknodes.c" "$dir/mknodes"; then
                printf "  CC    $dir/mknodes\n"
                $CC -o "$dir/mknodes" "$dir/mknodes.c"
        fi

        if newer_than "$dir/mksyntax.c" "$dir/mksyntax"; then
                printf "  CC    $dir/mksyntax\n"
                $CC -Ishared -I"$dir" -o "$dir/mksyntax" "$dir/mksyntax.c"
        fi

        if newer_than "$dir/mktokens" "$dir/token.h"; then
                printf "  GEN   $dir/token.h\n"
                (cd "$dir" && sh mktokens)
        fi

        if newer_than "$dir/mksyntax" "$dir/syntax.c" || newer_than "$dir/mksyntax" "$dir/syntax.h"; then
                printf "  GEN   $dir/syntax.c\n"
                (cd "$dir" && ./mksyntax)
        fi

        if newer_than "$dir/mknodes" "$dir/nodes.c" || newer_than "$dir/nodetypes" "$dir/nodes.c" || newer_than "$dir/nodes.c.pat" "$dir/nodes.c"; then
                printf "  GEN   $dir/nodes.c\n"
                (cd "$dir" && ./mknodes nodetypes nodes.c.pat)
        fi

        if newer_than "$dir/mkbuiltins" "$dir/builtins.c" || newer_than "$dir/builtins.def" "$dir/builtins.c" || newer_than "$dir/shell.h" "$dir/builtins.c" || [ ! -f "$dir/builtins.c" ]; then
                printf "  GEN   $dir/builtins.c\n"
                (cd "$dir" && sh mkbuiltins .)
        fi
}

clean() {
        printf "  CLEAN generated files\n"
        rm -f cmd/posix/getconf.h
        rm -f cmd/posix/bc.c cmd/posix/bc.h
        rm -f cmd/posix/awk/awkgram.tab.c cmd/posix/awk/awkgram.tab.h cmd/posix/awk/proctab.c cmd/posix/awk/maketab
        rm -f cmd/posix/sh/mknodes cmd/posix/sh/mksyntax cmd/posix/sh/token.h cmd/posix/sh/syntax.c cmd/posix/sh/syntax.h cmd/posix/sh/nodes.c cmd/posix/sh/nodes.h cmd/posix/sh/builtins.c cmd/posix/sh/builtins.h
        rm -f scripts/mk/config.set scripts/mk/config.kv scripts/mk/features.h shared/libutil/nofork_list.h
        rm -f scripts/mk/ninja-bin.cache
}

# config: generate config.set, config.kv, nofork_list.h
#
# config.set is what ninqu reads. config.kv is what mkbox and
# genbox.sh source. both contain the same data in different formats:
# config.kv is shell-quoted (KEY="VAL"), config.set is unquoted
# (KEY=VAL) since ninqu's (set-file) takes the rest of the line
# verbatim

config_check() {
        case "${cppflags_common}" in
                *-D_POSIX_C_SOURCE=200809L*) ;;
                *) printf 'genconfig: missing POSIX 2008 baseline\n' >&2; return 1 ;;
        esac
        case "${cppflags_common}" in
                *-D_DEFAULT_SOURCE*|*-D_GNU_SOURCE*|*-D_NETBSD_SOURCE*|*-D_BSD_SOURCE*|*-D_XOPEN_SOURCE*)
                        printf 'genconfig: non-POSIX feature-test macro leaked into CPPFLAGS\n' >&2
                        return 1
                        ;;
        esac
        for f in ${features}; do
                eval "val=\$$f"
                case "${val}" in
                        0|1) ;;
                        *) printf 'genconfig: %s must be 0 or 1, got %s\n' "${f}" "${val}" >&2; return 1 ;;
                esac
        done
}

config_resolve() {
        VERSION=${VERSION:-0}
        PREFIX=${PREFIX:-}
        MANPREFIX=${MANPREFIX:-/share/man}
        RANLIB=${RANLIB:-ranlib}
        AR=${AR:-ar}
        ARFLAGS=${ARFLAGS:-rc}
        CC=${CC:-cc}
        CFLAGS=${CFLAGS:--std=c99 -Wall -Wextra -pedantic}
        OBJCOPY=${OBJCOPY:-objcopy}
        LD=${LD:-ld}
        LDFLAGS_TLS=${LDFLAGS_TLS:-}
        LDLIBS_TLS=${LDLIBS_TLS:-}
        CPPFLAGS_TLS=${CPPFLAGS_TLS:-}
        NINJA_TRY=${NINJA_TRY:-samu,ninja}
        LDFLAGS="${LDFLAGS:-} ${LDFLAGS_TLS}"
        LDLIBS="${LDLIBS:-} ${LDLIBS_TLS}"

        for g in BUILD_POSIX BUILD_XSI BUILD_NET BUILD_PSEUDO BUILD_EXTRA BUILD_LINUX BUILD_DEV; do
                eval "${g}=\${${g}:-0}"
        done

        config_build_tools=""
        for category in posix xsi net pseudo extra linux dev; do
                [ -d "cmd/$category" ] || continue
                u_category=$(printf '%s' "$category" | tr '[:lower:]' '[:upper:]')
                tools=""
                for d in "cmd/$category"/*; do
                        [ -d "$d" ] && tools="$tools ${d##*/}"
                done
                for f in "cmd/$category"/*.c "cmd/$category"/*.y; do
                        [ -f "$f" ] || continue
                        b=${f##*/}
                        b=${b%.c}
                        b=${b%.y}
                        case "$b" in
                                maketab|mknodes|mksyntax|y.tab|*gram.tab) continue ;;
                        esac
                        tools="$tools $b"
                done
                tools=$(printf '%s\n' ${tools} | sort -u | grep -v '^$' || true)
                for b in ${tools}; do
                        u_tool=$(printf '%s' "$b" | tr 'a-z-' 'A-Z_')
                        var_name="BUILD_${u_category}_${u_tool}"
                        eval "has_override=\${${var_name}+yes}"
                        if [ "$has_override" = yes ]; then
                                eval "val=\${${var_name}}"
                        else
                                eval "val=\$BUILD_${u_category}"
                        fi
                        config_build_tools="${config_build_tools} ${var_name}=${val}"
                done
        done

        box_extra_libs=""
        [ "${BUILD_EXTRA_YAP:-0}" != 0 ] && box_extra_libs=-ltinfo
        config_build_tools="${config_build_tools} BOX_EXTRA_LIBS=${box_extra_libs}"
        features=$(set | grep '^FEATURE_[A-Za-z0-9_]*=' | cut -d= -f1 | sort)

        cppflags_common="-DPREFIX=\"${PREFIX}\" -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -include scripts/mk/features.h ${CPPFLAGS_TLS}"
        config_check
}

config_kv_quote() {
        escaped=$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')
        printf '"%s"' "$escaped"
}

config_kv() {
        config_resolve
        mkdir -p scripts/mk
        tmp="scripts/mk/config.kv.tmp.$$"
        {
                emit_kv() {
                        printf '%s=' "$1"
                        config_kv_quote "$2"
                        printf '\n'
                }

                emit_kv VERSION "${VERSION}"
                emit_kv PREFIX "${PREFIX}"
                emit_kv MANPREFIX "${MANPREFIX}"
                emit_kv RANLIB "${RANLIB}"
                emit_kv AR "${AR}"
                emit_kv ARFLAGS "${ARFLAGS}"
                emit_kv CC "${CC}"
                emit_kv CFLAGS "${CFLAGS}"
                emit_kv LDFLAGS "${LDFLAGS}"
                emit_kv LDLIBS "${LDLIBS}"
                emit_kv OBJCOPY "${OBJCOPY}"
                emit_kv LD "${LD}"
                emit_kv CPPFLAGS_TLS "${CPPFLAGS_TLS}"
                emit_kv LDFLAGS_TLS "${LDFLAGS_TLS}"
                emit_kv LDLIBS_TLS "${LDLIBS_TLS}"
                emit_kv NINJA_TRY "${NINJA_TRY}"

                for g in BUILD_POSIX BUILD_XSI BUILD_NET BUILD_PSEUDO BUILD_EXTRA BUILD_LINUX BUILD_DEV; do
                        eval "val=\$$g"
                        emit_kv "$g" "$val"
                done
                for entry in ${config_build_tools}; do
                        emit_kv "${entry%%=*}" "${entry#*=}"
                done
                for f in ${features}; do
                        eval "val=\$$f"
                        emit_kv "$f" "$val"
                done
                emit_kv CPPFLAGS "-Ishared ${cppflags_common}"
                emit_kv CPPFLAGS_LIBUTIL "${cppflags_common}"
        } > "${tmp}"
        mv "${tmp}" scripts/mk/config.kv
}

config_set() {
        config_resolve
        mkdir -p scripts/mk
        tmp="scripts/mk/config.set.tmp.$$"
        {
                emit_set() {
                        case "$2" in
                                *'
'*) printf 'genconfig: %s contains a newline\n' "$1" >&2; return 1 ;;
                        esac
                        printf '%s=%s\n' "$1" "$2"
                }

                emit_set VERSION "${VERSION}"
                emit_set PREFIX "${PREFIX}"
                emit_set MANPREFIX "${MANPREFIX}"
                emit_set RANLIB "${RANLIB}"
                emit_set AR "${AR}"
                emit_set ARFLAGS "${ARFLAGS}"
                emit_set CC "${CC}"
                emit_set CFLAGS "${CFLAGS}"
                emit_set LDFLAGS "${LDFLAGS}"
                emit_set LDLIBS "${LDLIBS}"
                emit_set OBJCOPY "${OBJCOPY}"
                emit_set LD "${LD}"
                emit_set CPPFLAGS_TLS "${CPPFLAGS_TLS}"
                emit_set LDFLAGS_TLS "${LDFLAGS_TLS}"
                emit_set LDLIBS_TLS "${LDLIBS_TLS}"
                emit_set NINJA_TRY "${NINJA_TRY}"

                for g in BUILD_POSIX BUILD_XSI BUILD_NET BUILD_PSEUDO BUILD_EXTRA BUILD_LINUX BUILD_DEV; do
                        eval "val=\$$g"
                        emit_set "$g" "$val"
                done
                for entry in ${config_build_tools}; do
                        entry_name="${entry%%=*}"
                        entry_value="${entry#*=}"
                        emit_set "${entry_name}" "${entry_value}"
                        entry_lower=$(printf '%s' "${entry_name}" | tr '[:upper:]' '[:lower:]')
                        emit_set "${entry_lower}" "${entry_value}"
                done
                for f in ${features}; do
                        eval "val=\$$f"
                        emit_set "$f" "$val"
                        feature_lower=$(printf '%s' "$f" | tr '[:upper:]' '[:lower:]')
                        emit_set "$feature_lower" "$val"
                done
                emit_set CPPFLAGS "-Ishared ${cppflags_common}"
                emit_set CPPFLAGS_LIBUTIL "${cppflags_common}"
        } > "${tmp}"
        mv "${tmp}" scripts/mk/config.set
}

features_h() {
        config_resolve
        mkdir -p scripts/mk
        tmp="scripts/mk/features.h.tmp.$$"
        {
                printf '/* generated by genconfig.sh */\n'
                for f in ${features}; do
                        eval "val=\$$f"
                        printf '#define %s %s\n' "$f" "$val"
                done
        } > "${tmp}"
        mv "${tmp}" scripts/mk/features.h
}

nofork_list_h() {
        config_resolve
        tmp="shared/libutil/nofork_list.h.tmp.$$"
        {
                printf '/* generated by genconfig.sh */\n'
                printf 'static const char *nofork_list[] = {\n'
                for app in ${NOFORK_LIST:-}; do
                        printf '  "%s",\n' "$app"
                done
                printf '  NULL\n'
                printf '};\n'
        } > "${tmp}"
        mv "${tmp}" shared/libutil/nofork_list.h
}

config() {
        config_set
        config_kv
        features_h
        nofork_list_h
}

case "$target" in
        getconf) gen_getconf ;;
        bc)      gen_bc ;;
        awk)     gen_awk ;;
        sh)      gen_sh ;;
        all)
                gen_getconf
                gen_bc
                gen_awk
                gen_sh
                ;;
        config)         config ;;
        config-set)     config_set ;;
        config-kv)      config_kv ;;
        features-h)     features_h ;;
        nofork-list-h)  nofork_list_h ;;
        config-check)   config_resolve ;;
        clean)          clean ;;
        *)
                printf "genconfig: unknown target: %s\n" "$target" >&2
                printf "usage: %s (config|config-set|config-kv|features-h|nofork-list-h|config-check|getconf|bc|awk|sh|all|clean)\n" "$0" >&2
                exit 1
                ;;
esac
