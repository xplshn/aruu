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

[ -f build.cfg ] || { printf 'build.cfg not found; see the repository README\n' >&2; exit 1; }
# shellcheck disable=SC1091
. ./build.cfg

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
CPPFLAGS="-Ishared -DPREFIX=\"$PREFIX\" -D_DEFAULT_SOURCE -D_GNU_SOURCE -D_NETBSD_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64$_feature_flags"
if [ "$FEATURE_USE_BEARSSL" = "1" ]; then
	if pkg-config --exists libbearssl 2>/dev/null; then
		CPPFLAGS="$CPPFLAGS $(pkg-config --cflags libbearssl)"
		TLS_LDLIBS="$TLS_LDLIBS $(pkg-config --libs libbearssl)"
	elif pkg-config --exists bearssl 2>/dev/null; then
		CPPFLAGS="$CPPFLAGS $(pkg-config --cflags bearssl)"
		TLS_LDLIBS="$TLS_LDLIBS $(pkg-config --libs bearssl)"
	else
		TLS_LDLIBS="$TLS_LDLIBS -LexternalRepos/BearSSL/build -lbearssl"
	fi
fi
if [ "$FEATURE_USE_LIBRESSL" = "1" ]; then
	if pkg-config --exists libtls 2>/dev/null; then
		CPPFLAGS="$CPPFLAGS $(pkg-config --cflags libtls)"
		TLS_LDLIBS="$TLS_LDLIBS $(pkg-config --libs libtls)"
	else
		TLS_LDLIBS="$TLS_LDLIBS -ltls"
	fi
fi
if [ "$FEATURE_USE_OPENSSL" = "1" ]; then
	if pkg-config --exists openssl 2>/dev/null; then
		CPPFLAGS="$CPPFLAGS $(pkg-config --cflags openssl)"
		TLS_LDLIBS="$TLS_LDLIBS $(pkg-config --libs openssl)"
	else
		TLS_LDLIBS="$TLS_LDLIBS -lssl -lcrypto"
	fi
fi
unset _feature_flags

CC=${CC:-cc}
AR=${AR:-ar}
ARFLAGS=${ARFLAGS:-rc}
RANLIB=${RANLIB:-ranlib}
PREFIX=${PREFIX:-/usr/local}
MANPREFIX=${MANPREFIX:-$PREFIX/share/man}

# shared headers; every object depends on these
HDR=$(echo shared/*.h)
LIB="shared/libredline/libredline.a shared/libutil/libutil.a shared/libutf/libutf.a"

# set by multi-file builders for generated headers, cleared after use
EXTRA_HDR=

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

# parallel job queue
#
# objs_for must not run in a subshell; enqueue writes _qn/_queue in the current shell

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

# compile and link primitives
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
		else                        libs="$libs $a"
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
#
# both directories enqueued before the single drain so they compile in parallel
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
	[ "$cat" = extra ] && cat=pseudo
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
	local cat="$1" src base var staged=""
	for src in cmd/"$cat"/*.c; do
		[ -f "$src" ] || continue
		base=${src##*/}; base=${base%.c}
		if [ "$cat" = posix ] && [ "$base" = diff ]; then
			continue
		fi
		var=$(cfgvar "$cat" "$base")
		cfg_enabled "$var" || continue
		compile_c "$src" "${src%.c}.o"
		staged="$staged ${src%.c}"
	done
	drain
	local bin
	for bin in $staged; do
		link_bin "$bin" "${bin}.o" -- $LIB
	done
}

# generated multi-file tools under cmd/posix
#
build_diff() {
	cfg_enabled BUILD_POSIX_DIFF || return 0
	local dir=cmd/posix/diff
	local objs=""
	EXTRA_HDR="$dir/diff.h $dir/pr.h $dir/xmalloc.h"
	compile_c "$dir/diff.c" "$dir/diff.o"
	compile_c "$dir/diffdir.c" "$dir/diffdir.o"
	compile_c "$dir/diffreg.c" "$dir/diffreg.o"
	compile_c "$dir/pr.c" "$dir/pr.o"
	compile_c "$dir/xmalloc.c" "$dir/xmalloc.o"
	EXTRA_HDR=
	objs="$dir/diff.o $dir/diffdir.o $dir/diffreg.o $dir/pr.o $dir/xmalloc.o"
	link_bin "$dir/diff" $objs -- $LIB -lm
}

build_patch() {
	cfg_enabled BUILD_POSIX_PATCH || return 0
	local dir=cmd/posix/patch
	EXTRA_HDR="$dir/common.h $dir/util.h $dir/pch.h $dir/inp.h $dir/backupfile.h $dir/pathnames.h"
	objs_for "$dir" "" "-I$dir"
	EXTRA_HDR=
	link_bin "$dir/patch" $_objs -- $LIB
}

build_diff3() {
	cfg_enabled BUILD_PSEUDO_DIFF3 || return 0
	local dir=cmd/extra/diff3
	EXTRA_HDR="cmd/posix/diff/xmalloc.h"
	compile_c "$dir/diff3.c" "$dir/diff3.o"
	EXTRA_HDR=
	link_bin "$dir/diff3" "$dir/diff3.o" cmd/posix/diff/xmalloc.o -- $LIB
}

build_awk() {
	cfg_enabled BUILD_POSIX_AWK || return 0
	local dir=cmd/posix/awk

	any_newer_than "$dir/awkgram.tab.c" "$dir/awkgram.y" && {
		if   command -v yacc  >/dev/null 2>&1; then
			printf '  YACC  %s/awkgram.tab.c\n' "$dir"
			yacc  -d -o "$dir/awkgram.tab.c" "$dir/awkgram.y"
			if [ ! -f "$dir/awkgram.tab.h" ]; then
				if [ -f y.tab.h ]; then
					mv y.tab.h "$dir/awkgram.tab.h"
				elif [ -f "$dir/y.tab.h" ]; then
					mv "$dir/y.tab.h" "$dir/awkgram.tab.h"
				fi
			fi
		elif command -v bison >/dev/null 2>&1; then
			printf '  BISON %s/awkgram.tab.c\n' "$dir"
			bison -d -o "$dir/awkgram.tab.c" "$dir/awkgram.y"
		else
			printf 'awk: no yacc/bison; using pre-generated awkgram.tab.c\n' >&2
		fi
	}

	any_newer_than "$dir/maketab" "$dir/maketab.c" "$dir/awkgram.tab.h" && {
		printf '  CC    %s/maketab\n' "$dir"
		eval "$CC $CFLAGS -o $dir/maketab $dir/maketab.c"
	}

	any_newer_than "$dir/proctab.c" "$dir/maketab" && {
		printf '  GEN   %s/proctab.c\n' "$dir"
		"$dir/maketab" "$dir/awkgram.tab.h" > "$dir/proctab.c"
	}

	EXTRA_HDR="$dir/awk.h $dir/awkgram.tab.h $dir/proto.h"
	objs_for "$dir" maketab.c "-I$dir"
	EXTRA_HDR=
	link_bin "$dir/awk" $_objs -- $LIB -lm
}

build_sh() {
	cfg_enabled BUILD_POSIX_SH || return 0
	local dir=cmd/posix/sh

	any_newer_than "$dir/mknodes" "$dir/mknodes.c" && {
		printf '  CC    %s/mknodes\n' "$dir"
		eval "$CC $CFLAGS -o $dir/mknodes $dir/mknodes.c"
	}

	any_newer_than "$dir/mksyntax" "$dir/mksyntax.c" && {
		printf '  CC    %s/mksyntax\n' "$dir"
		eval "$CC $CPPFLAGS -I$dir $CFLAGS -o $dir/mksyntax $dir/mksyntax.c"
	}

	any_newer_than "$dir/token.h" "$dir/mktokens" && {
		printf '  GEN   %s/token.h\n' "$dir"
		(cd "$dir" && sh mktokens)
	}

	any_newer_than "$dir/syntax.c" "$dir/mksyntax" && {
		printf '  GEN   %s/syntax.c\n' "$dir"
		(cd "$dir" && ./mksyntax)
	}

	any_newer_than "$dir/nodes.c" "$dir/mknodes" "$dir/nodetypes" "$dir/nodes.c.pat" && {
		printf '  GEN   %s/nodes.c\n' "$dir"
		(cd "$dir" && ./mknodes nodetypes nodes.c.pat)
	}

	any_newer_than "$dir/builtins.c" "$dir/mkbuiltins" "$dir/builtins.def" "$dir/shell.h" && {
		printf '  GEN   %s/builtins.c\n' "$dir"
		(cd "$dir" && sh mkbuiltins .)
	}

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

build_posix() {
	cfg_enabled BUILD_POSIX_GETCONF && [ ! -f cmd/posix/getconf.h ] && {
		printf '  GEN   cmd/posix/getconf.h\n'
		scripts/getconf.sh > cmd/posix/getconf.h || { rm -f cmd/posix/getconf.h; exit 1; }
	}
	build_simple_tools posix
	build_diff
	build_patch
	build_awk
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
	local var="$1" src="$2" base sec out_mdoc out_txt
	cfg_enabled "$var" || return 0
	[ -x scripts/mkman/mkman ] || { printf 'error: mkman not built\n' >&2; exit 1; }

	grep -qE '!man|\?man' "$src" || return 0

	base=${src##*/}; base=${base%.c}
	sec=$(man_section "$src")

	mkdir -p "man/man${sec}"
	out_mdoc="man/man${sec}/${base}.${sec}"
	out_txt="man/man${sec}/${base}.${sec}.txt"
	if any_newer_than "$out_mdoc" "$src" config.mk scripts/mkman/mkman; then
		printf '  MAN   %s\n' "$out_mdoc"
		scripts/mkman/mkman -fmt mdoc -config config.mk -section "$sec" "$src" > "$out_mdoc"
	fi
	if any_newer_than "$out_txt" "$src" config.mk scripts/mkman/mkman; then
		printf '  MAN   %s\n' "$out_txt"
		scripts/mkman/mkman -fmt txt -config config.mk -section "$sec" "$src" > "$out_txt"
	fi
}

build_man() {
	local dir cat src base
	if [ ! -x scripts/mkman/mkman ] || any_newer_than scripts/mkman/mkman scripts/mkman/main.go scripts/mkman/page.go scripts/mkman/parse.go scripts/mkman/mdoc.go; then
		printf '  GO    scripts/mkman/mkman\n'
		(cd scripts/mkman && go build -o mkman .)
	fi
	for dir in cmd/*; do
		[ -d "$dir" ] || continue
		cat=${dir##*/}
		for src in "$dir"/*.c; do
			[ -f "$src" ] || continue
			base=${src##*/}; base=${base%.c}
			build_man_for "$(cfgvar "$cat" "$base")" "$src"
		done
	done
	build_man_for BUILD_POSIX_DIFF cmd/posix/diff/diff.c
	build_man_for BUILD_POSIX_PATCH cmd/posix/patch/patch.c
	build_man_for BUILD_PSEUDO_DIFF3 cmd/extra/diff3/diff3.c
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

do_clean() {
	find shared cmd -name '*.o'                   -exec rm -f {} +
	find shared    -name '*.a'                    -exec rm -f {} +
	find cmd -type f ! -name '*.*' -perm -100     -exec rm -f {} +
	rm -f shared/libaruuelf.so
	rm -f cmd/posix/bc.c cmd/posix/getconf.h
	rm -f cmd/posix/patch/patch
	rm -f cmd/posix/awk/maketab cmd/posix/awk/proctab.c
	rm -f cmd/posix/awk/awkgram.tab.c cmd/posix/awk/awkgram.tab.h
	rm -f cmd/posix/sh/mknodes cmd/posix/sh/mksyntax
	rm -f cmd/posix/sh/token.h cmd/posix/sh/syntax.c cmd/posix/sh/syntax.h
	rm -f cmd/posix/sh/nodes.c cmd/posix/sh/nodes.h
	rm -f cmd/posix/sh/builtins.c cmd/posix/sh/builtins.h
	rm -rf man/man1 man/man8
	rm -rf aruu-box .box
	rm -f scripts/mkman/mkman
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
	build_diff3
		;;
	lib)             build_lib ;;
	posix)           build_lib; build_posix ;;
	dev)             build_lib; build_dev ;;
	make)            build_lib; build_make ;;
	linux|net|xsi|pseudo|extra)
	                 build_lib; build_simple_tools "$TARGET"; [ "$TARGET" = extra ] && build_diff3 ;;
	man)             build_man ;;
	install)         do_install ;;
	clean)           do_clean ;;
	*)
		printf 'usage: sh Makefile.sh [-jN] [-lN] [all|clean|install|man|lib|posix|linux|net|xsi|pseudo|extra|dev|make]\n' >&2
		exit 1
		;;
esac
