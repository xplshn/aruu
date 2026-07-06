#!/bin/sh
# genconfig.sh generates build configuration from build.cfg
#
# outputs four files:
#
#   scripts/mk/config.set   ninqu reads this via (set-file). flat
#                           KEY=VAL, unquoted. per-tool BUILD_CATEGORY_TOOL
#                           toggles get a lowercase alias here, since
#                           (gate ...) in ninqu.rules only ever checks
#                           the lowercase form
#
#   scripts/mk/config.kv    shell-quoted KEY="VAL" format. sourced by
#                           read via the -config flag by scripts/mkman.
#                           nothing in the ninqu build itself sources
#                           this file: genbox.sh takes its input on argv
#                           only, see the BOX section of ninqu.rules
#
#   shared/libutil/nofork_list.h
#                           wexec.c includes this at compile time.
#                           generated from NOFORK_LIST in build.cfg
#
#   scripts/mk/features.h   every resolved FEATURE_* as a #define
#                           CPPFLAGS pulls this in with -include
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
}

# config: generate config.set, config.kv, nofork_list.h
#
# config.set is what ninqu reads. config.kv is what mkbox and
# genbox.sh source. both contain the same data in different formats:
# config.kv is shell-quoted (KEY="VAL"), config.set is unquoted
# (KEY=VAL) since ninqu's (set-file) takes the rest of the line
# verbatim

config() {
        mkdir -p scripts/mk

        kv_file="scripts/mk/config.kv"
        set_file="scripts/mk/config.set"

        : > "${kv_file}"
        : > "${set_file}"

        emit() {
                local k="$1" v="$2"
                echo "${k}=\"${v}\"" >> "${kv_file}"
                echo "${k}=${v}" >> "${set_file}"
        }

        emit VERSION   "${VERSION}"
        emit PREFIX    "${PREFIX}"
        emit MANPREFIX "${MANPREFIX:-/usr/local/share/man}"
        emit RANLIB    "${RANLIB:-ranlib}"
        emit AR        "${AR:-ar}"
        emit ARFLAGS   "${ARFLAGS:-rc}"
        emit CC        "${CC:-cc}"
        emit CFLAGS    "${CFLAGS:--std=c99 -Wall -Wextra -pedantic}"
        emit LDFLAGS   "${LDFLAGS:-} ${LDFLAGS_TLS:-}"
        emit LDLIBS    "${LDLIBS:-} ${LDLIBS_TLS:-}"
        emit OBJCOPY   "${OBJCOPY:-objcopy}"
        emit LD        "${LD:-ld}"

        emit CPPFLAGS_TLS "${CPPFLAGS_TLS:-}"
        emit LDFLAGS_TLS  "${LDFLAGS_TLS:-}"
        emit LDLIBS_TLS   "${LDLIBS_TLS:-}"

        # these seven are only consulted per-tool below (build_CATEGORY_TOOL,
        # build_CATEGORY_$(BASESTEM)), the bare category toggle has no gate
        # anywhere in ninqu.rules, (group CATEGORY ...) already does that
        # job. config.kv still gets the uppercase copy since genbox.sh
        # and mkbox source it directly
        for g in BUILD_POSIX BUILD_XSI BUILD_NET BUILD_PSEUDO BUILD_EXTRA BUILD_LINUX BUILD_DEV; do
                eval "val=\${$g:-0}"
                echo "${g}=\"${val}\"" >> "${kv_file}"
        done

        # per-tool toggles: scan cmd/<category>/ for tools. a tool is
        # either a .c/.y file directly in the category dir (perfile)
        # or a subdirectory (multi-file tool). emit BUILD_<CATEGORY>_<TOOL>
        # plus lowercase alias for each
        for category in posix xsi net pseudo extra linux dev; do
                [ -d "cmd/$category" ] || continue
                u_category=$(echo "$category" | tr '[:lower:]' '[:upper:]')

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

                tools=$(echo "$tools" | tr ' ' '\n' | sort -u | grep -v '^$' || true)

                for b in $tools; do
                        u_tool=$(echo "$b" | tr 'a-z-' 'A-Z_')
                        var_name="BUILD_${u_category}_${u_tool}"

                        #if [ "$category" = "dev" ] && [ "$u_tool" = "AS" ]; then
                        #        var_name="BUILD_DEV_CC"
                        #fi

                        eval "has_override=\${$var_name+yes}"
                        if [ "$has_override" = "yes" ]; then
                                eval "val=\${$var_name}"
                        else
                                eval "val=\$BUILD_${u_category}"
                        fi

                        echo "${var_name}=\"${val}\"" >> "${kv_file}"
                        lk=$(echo "$var_name" | tr '[:upper:]' '[:lower:]')
                        echo "${lk}=${val}" >> "${set_file}"
                done
        done

        # box's own link line has no per-tool composition the way a
        # standalone PERFILE binary's EXTRA_LIBS map does (box links
        # every enabled tool's object into one binary in one shot), so
        # a tool that needs a library nothing else in the box needs
        # (yap needs terminfo) can only get it by ninqu.rules reading
        # a value genconfig.sh already resolved, same as every other
        # build_* toggle. this is still data, not policy: which
        # library to add for which tool is decided here, not what to
        # do with that decision
        box_extra_libs=""
        [ "${yap_val:-0}" != "0" ] && box_extra_libs="-ltinfo"
        emit BOX_EXTRA_LIBS "${box_extra_libs}"

        features=$(set | grep '^FEATURE_[A-Za-z0-9_]*=' | cut -d= -f1 | sort)
        for f in $features; do
                eval "val=\$$f"
                emit "$f" "$val"
        done

        # every resolved FEATURE_* becomes a #define in one generated
        # header. config.h's own #ifndef guards still apply to anything
        # not here (there is nothing not here), and any file gets the
        # real values just by being compiled with -include, whether or
        # not it bothers to #include "config.h" itself.
        features_h="scripts/mk/features.h"
        {
                echo "/* autogenerated by genconfig.sh, do not edit */"
                for f in $features; do
                        eval "fval=\$$f"
                        echo "#define ${f} ${fval}"
                done
        } > "${features_h}"

        cppflags_common="-DPREFIX=\"${PREFIX}\" -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_NETBSD_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64 -include ${features_h} ${CPPFLAGS_TLS:-}"
        cppflags_set="-Ishared ${cppflags_common}"
        echo "CPPFLAGS=\"${cppflags_set}\"" >> "${kv_file}"
        echo "CPPFLAGS=${cppflags_set}" >> "${set_file}"

        # shared/tls.h would conflict with the system's tls.h
        # if we passed -Ishared to build libutil
        echo "CPPFLAGS_LIBUTIL=\"${cppflags_common}\"" >> "${kv_file}"
        echo "CPPFLAGS_LIBUTIL=${cppflags_common}" >> "${set_file}"

        nofork_h="shared/libutil/nofork_list.h"
        echo "/* autogenerated by genconfig.sh */" > "${nofork_h}"
        echo "static const char *nofork_list[] = {" >> "${nofork_h}"
        for app in ${NOFORK_LIST}; do
                echo "  \"${app}\"," >> "${nofork_h}"
        done
        echo "  NULL" >> "${nofork_h}"
        echo "};" >> "${nofork_h}"
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
        config)  config ;;
        clean)   clean ;;
        *)
                printf "genconfig: unknown target: %s\n" "$target" >&2
                printf "usage: %s (config|getconf|bc|awk|sh|all|clean)\n" "$0" >&2
                exit 1
                ;;
esac
