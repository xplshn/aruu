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
        # Find all subdirectories
        for d in "cmd/$catg"/*; do
                if [ -d "$d" ]; then
                        dirs="$dirs ${d##*/}"
                fi
        done
        # Find all .c and .y files
        for f in "cmd/$catg"/*.c "cmd/$catg"/*.y; do
                [ -f "$f" ] || continue
                b=${f##*/}
                b=${b%.c}
                b=${b%.y}
                case "$b" in
                        # Exclude intermediate files or helper generators
                        maketab|mknodes|mksyntax|y.tab|*gram.tab) continue ;;
                esac
                files="$files $b"
        done

        # Output sorted unique list of uppercase names
        # shellcheck disable=SC2086
        printf '%s %s\n' $dirs $files | tr ' ' '\n' | sort -u | tr 'a-z-' 'A-Z_' | grep -v '^$'
}

write_subtarget() {
        group="$1"
        tool="$2"
        u_group=$(echo "$group" | tr '[:lower:]' '[:upper:]')
        var_name="BUILD_${u_group}_${tool}"

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
