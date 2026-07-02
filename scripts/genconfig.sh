#!/bin/sh
# genconfig.sh - generate config.mk and config.kv from build.cfg

set -e

# load build.cfg
if [ -f build.cfg ]; then
        . ./build.cfg
else
        echo "genconfig: build.cfg not found" >&2
        exit 1
fi

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

# dynamic tool discovery function
list_tools() {
        local catg="$1" dirs="" files="" d f b
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

gen_dir_rules() {
        local dir="$1" flags="$2" extra_files="$3" f obj files uniq_files
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
