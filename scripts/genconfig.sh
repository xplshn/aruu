#!/bin/sh
# genconfig.sh - generate config.mk and config.kv from build.cfg

set -e

target=${1:-config}

# load build.cfg
if [ -f build.cfg ]; then
        . ./build.cfg
else
        echo "genconfig: build.cfg not found" >&2
        exit 1
fi

# helper to check if source is newer than target
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
  rm -f scripts/mk/config.mk scripts/mk/config.kv scripts/mk/rules.mk shared/libutil/nofork_list.h
}

# dynamic tool discovery function
list_tools() {
        local catg="$1" dirs="" files="" d f b
        [ -d "cmd/$catg" ] || return 0
        # find all subdirectories
        for d in "cmd/$catg"/*; do
                if [ -d "$d" ]; then
                        dirs="$dirs ${d##*/}"
                fi
        done
        # find all .c and .y files
        for f in "cmd/$catg"/*.c "cmd/$catg"/*.y; do
                [ -f "$f" ] || continue
                b=${f##*/}
                b=${b%.c}
                b=${b%.y}
                case "$b" in
                        # exclude intermediate files or helper generators
                        maketab|mknodes|mksyntax|y.tab|*gram.tab) continue ;;
                esac
                files="$files $b"
        done

        # output sorted unique list of tool names
        # shellcheck disable=SC2086
        printf '%s %s\n' $dirs $files | tr ' ' '\n' | sort -u | grep -v '^$'
}

write_subtarget() {
        group="$1"
        tool="$2"
        u_group=$(echo "$group" | tr '[:lower:]' '[:upper:]')
        u_tool=$(echo "$tool" | tr 'a-z-' 'A-Z_')
        var_name="BUILD_${u_group}_${u_tool}"

        eval "has_override=\${$var_name+yes}"
        eval "val=\${$var_name:-}"

        eval "group_val=\$BUILD_${u_group}"

        if [ "$has_override" = "yes" ]; then
                echo "${var_name} = ${val}" >> "${mk_file}"
                echo "${var_name}=\"${val}\"" >> "${kv_file}"
        else
                echo "${var_name} = \$(BUILD_${u_group})" >> "${mk_file}"
                echo "${var_name}=\"${group_val}\"" >> "${kv_file}"
        fi
}

gen_dir_rules() {
        local dir="$1" flags="$2" extra_files="$3" f obj files uniq_files
        [ -d "$dir" ] || return 0
        files=""
        for f in "$dir"/*.c; do
                if [ -f "$f" ]; then
                        files="$files $f"
                fi
        done
        for f in ${extra_files}; do
                files="$files $f"
        done
        uniq_files=$(echo "$files" | tr ' ' '\n' | sort -u | grep -v '^$')
        for f in $uniq_files; do
                obj="${f%.c}.o"
                echo "${obj}: ${f}" >> "${rules_file}"
                echo "	\$(CC) \$(CPPFLAGS) ${flags} \$(CFLAGS) -o \$@ -c ${f}" >> "${rules_file}"
        done
}

case "$target" in
        getconf) gen_getconf ;;
        bc)      gen_bc ;;
        awk)     gen_awk ;;
        sh)      gen_sh ;;
        clean)   clean ;;
        all)
                gen_getconf
                gen_bc
                gen_awk
                gen_sh
                ;;
        config)
                mkdir -p scripts/mk

                mk_file="scripts/mk/config.mk"
                kv_file="scripts/mk/config.kv"

                # clear existing files
                : > "${mk_file}"
                : > "${kv_file}"

                # write version prefix manprefix
                echo "VERSION = ${VERSION}" >> "${mk_file}"
                echo "VERSION=\"${VERSION}\"" >> "${kv_file}"

                echo "PREFIX = ${PREFIX}" >> "${mk_file}"
                echo "PREFIX=\"${PREFIX}\"" >> "${kv_file}"

                # default manprefix to prefix/share/man if empty
                man_pref="${MANPREFIX:-/usr/local/share/man}"
                echo "MANPREFIX = ${man_pref}" >> "${mk_file}"
                echo "MANPREFIX=\"${man_pref}\"" >> "${kv_file}"

                # toolchain defaults
                echo "RANLIB = ${RANLIB:-ranlib}" >> "${mk_file}"
                echo "RANLIB=\"${RANLIB:-ranlib}\"" >> "${kv_file}"

                echo "AR = ${AR:-ar}" >> "${mk_file}"
                echo "AR=\"${AR:-ar}\"" >> "${kv_file}"

                echo "ARFLAGS = ${ARFLAGS:-rc}" >> "${mk_file}"
                echo "ARFLAGS=\"${ARFLAGS:-rc}\"" >> "${kv_file}"

                # build toggles
                echo "" >> "${mk_file}"
                echo "# build toggles" >> "${mk_file}"
                for g in BUILD_POSIX BUILD_XSI BUILD_NET BUILD_PSEUDO BUILD_EXTRA BUILD_LINUX BUILD_DEV; do
                        eval "val=\${$g:-0}"
                        echo "$g = $val" >> "${mk_file}"
                        echo "$g=\"$val\"" >> "${kv_file}"
                done

                # write subtargets for each category dynamically
                for catg in posix xsi net pseudo extra linux dev; do
                        echo "" >> "${mk_file}"
                        echo "# ${catg} subtargets" >> "${mk_file}"
                        tools=$(list_tools "$catg")
                        for t in $tools; do
                                write_subtarget "$catg" "$t"
                        done

                        # build the enabled list using lowercase tools
                        enabled=""
                        for t in $tools; do
                                u_group=$(echo "$catg" | tr '[:lower:]' '[:upper:]')
                                u_tool=$(echo "$t" | tr 'a-z-' 'A-Z_')

                                # special cases for dev tools mapping to different variables
                                if [ "$catg" = "dev" ] && [ "$u_tool" = "AS" ]; then
                                        var_name="BUILD_DEV_CC"
                                else
                                        var_name="BUILD_${u_group}_${u_tool}"
                                fi

                                eval "has_override=\${$var_name+yes}"
                                eval "val=\${$var_name:-}"
                                if [ "$has_override" != "yes" ]; then
                                        if [ "$catg" = "dev" ] && [ "$u_tool" = "AS" ]; then
                                                eval "val=\$BUILD_DEV"
                                        else
                                                eval "val=\$BUILD_${u_group}"
                                        fi
                                fi

                                if [ "$val" = "1" ]; then
                                        # map to the correct path
                                        case "$catg/$t" in
                                                posix/awk)  path="cmd/posix/awk/awk" ;;
                                                posix/sh)   path="cmd/posix/sh/sh" ;;
                                                posix/make) path="cmd/posix/make/make" ;;
                                                extra/yap)  path="cmd/extra/yap/yap" ;;
                                                dev/ar)     path="cmd/dev/ar/ar" ;;
                                                dev/as)     path="cmd/dev/as/as" ;;
                                                dev/ld)     path="cmd/dev/ld/ld" ;;
                                                dev/cc)     path="cmd/dev/cc/cc" ;;
                                                *)          path="cmd/$catg/$t" ;;
                                        esac
                                        enabled="$enabled $path"
                                fi
                        done
                        u_catg=$(echo "$catg" | tr '[:lower:]' '[:upper:]')
                        # write the list of enabled binaries
                        echo "${u_catg}_BIN_CONFIG =${enabled}" >> "${mk_file}"
                done

                # write feature flags
                echo "" >> "${mk_file}"
                echo "# feature flags" >> "${mk_file}"
                features=$(set | grep '^FEATURE_[A-Za-z0-9_]*=' | cut -d= -f1 | sort)
                for f in $features; do
                        eval "val=\$$f"
                        echo "$f = $val" >> "${mk_file}"
                        echo "$f=\"$val\"" >> "${kv_file}"
                done

                # write TLS variables to config files
                echo "" >> "${mk_file}"
                echo "CPPFLAGS_TLS = ${CPPFLAGS_TLS}" >> "${mk_file}"
                echo "CPPFLAGS_TLS=\"${CPPFLAGS_TLS}\"" >> "${kv_file}"

                echo "LDFLAGS_TLS = ${LDFLAGS_TLS}" >> "${mk_file}"
                echo "LDFLAGS_TLS=\"${LDFLAGS_TLS}\"" >> "${kv_file}"

                echo "LDLIBS_TLS = ${LDLIBS_TLS}" >> "${mk_file}"
                echo "LDLIBS_TLS=\"${LDLIBS_TLS}\"" >> "${kv_file}"

                # write CPPFLAGS with all feature macros
                echo "" >> "${mk_file}"
                echo "CPPFLAGS =\\" >> "${mk_file}"
                echo "        -Ishared\\" >> "${mk_file}"
                echo "        -DPREFIX=\\\"\\\$(PREFIX)\\\"\\" >> "${mk_file}"
                echo "        -D_DEFAULT_SOURCE\\" >> "${mk_file}"
                echo "        -D_GNU_SOURCE\\" >> "${mk_file}"
                echo "        -D_NETBSD_SOURCE\\" >> "${mk_file}"
                echo "        -D_BSD_SOURCE\\" >> "${mk_file}"
                echo "        -D_XOPEN_SOURCE=700\\" >> "${mk_file}"
                echo "        -D_FILE_OFFSET_BITS=64\\" >> "${mk_file}"
                for f in $features; do
                        if [ "$f" = "FEATURE_USE_SSL" ]; then
                                continue
                        fi
                        echo "        -D$f=\$($f)\\" >> "${mk_file}"
                done
                echo "        \$(CPPFLAGS_TLS)" >> "${mk_file}"

                # write toolchain configuration
                echo "" >> "${mk_file}"
                echo "CC = ${CC:-cc}" >> "${mk_file}"
                echo "CFLAGS = ${CFLAGS:-}" >> "${mk_file}"
                echo "LDFLAGS = ${LDFLAGS:-} \$(LDFLAGS_TLS)" >> "${mk_file}"
                echo "LDLIBS = ${LDLIBS:-} \$(LDLIBS_TLS)" >> "${mk_file}"

                # write compilation rules for subdirectories that need special flags or nested structures
                rules_file="scripts/mk/rules.mk"
                echo "# autogenerated compilation rules" > "${rules_file}"

                echo "" >> "${rules_file}"
                gen_dir_rules "cmd/posix/awk" "-Icmd/posix/awk" "cmd/posix/awk/awkgram.tab.c cmd/posix/awk/proctab.c"
                gen_dir_rules "cmd/posix/sh" "-DSHELL -Icmd/posix/sh" "cmd/posix/sh/syntax.c cmd/posix/sh/nodes.c cmd/posix/sh/builtins.c"
                gen_dir_rules "cmd/extra/yap" "" ""
                gen_dir_rules "cmd/dev/ar" "-Icmd/dev/ar" ""
                gen_dir_rules "cmd/dev/xcutil" "-Icmd/dev/xcutil" ""
                gen_dir_rules "cmd/dev/ld" "-Icmd/dev/xcutil -Icmd/dev/ld" ""
                gen_dir_rules "cmd/dev/as" "-Icmd/dev/xcutil -Icmd/dev/as" ""
                gen_dir_rules "cmd/dev/as/arch/x64" "-Icmd/dev/xcutil -Icmd/dev/as -Icmd/dev/as/arch/x64" ""
                gen_dir_rules "cmd/dev/cc" "-Icmd/dev/cc" ""

                # generate nofork_list.h
                nofork_h="shared/libutil/nofork_list.h"
                echo "/* autogenerated by genconfig.sh */" > "${nofork_h}"
                echo "static const char *nofork_list[] = {" >> "${nofork_h}"
                for app in ${NOFORK_LIST}; do
                        echo "  \"${app}\"," >> "${nofork_h}"
                done
                echo "  NULL" >> "${nofork_h}"
                echo "};" >> "${nofork_h}"
                ;;
        *)
                printf "unknown target: %s\n" "$target" >&2
                exit 1
                ;;
esac
