-include scripts/mk/config.mk

.SUFFIXES: .y .o .c
HDR =\
	shared/arg.h\
	shared/compat.h\
	shared/config.h\
	shared/crypt.h\
	shared/wexec.h\
	shared/fs.h\
	shared/md5.h\
	shared/queue.h\
	shared/sha1.h\
	shared/sha224.h\
	shared/sha256.h\
	shared/sha384.h\
	shared/sha512.h\
	shared/sha512-224.h\
	shared/sha512-256.h\
	shared/text.h\
	shared/utf.h\
	shared/util.h\
	shared/passwd.h\
	shared/paths.h\
	shared/reboot.h\
	shared/rtc.h\
	shared/proc.h\
	shared/tls.h

LIBUTFOBJ =\
	shared/libutf/fgetrune.o\
	shared/libutf/fputrune.o\
	shared/libutf/isalnumrune.o\
	shared/libutf/isalpharune.o\
	shared/libutf/isblankrune.o\
	shared/libutf/iscntrlrune.o\
	shared/libutf/isdigitrune.o\
	shared/libutf/isgraphrune.o\
	shared/libutf/isprintrune.o\
	shared/libutf/ispunctrune.o\
	shared/libutf/isspacerune.o\
	shared/libutf/istitlerune.o\
	shared/libutf/isxdigitrune.o\
	shared/libutf/lowerrune.o\
	shared/libutf/rune.o\
	shared/libutf/runetype.o\
	shared/libutf/upperrune.o\
	shared/libutf/utf.o\
	shared/libutf/utftorunestr.o

LIBUTILOBJ =\
	shared/libutil/concat.o\
	shared/libutil/cp.o\
	shared/libutil/crypt.o\
	shared/libutil/confirm.o\
	shared/libutil/diffutil.o\
	shared/libutil/wexec.o\
	shared/libutil/ealloc.o\
	shared/libutil/enmasse.o\
	shared/libutil/eprintf.o\
	shared/libutil/eregcomp.o\
	shared/libutil/estrtod.o\
	shared/libutil/fnck.o\
	shared/libutil/fshut.o\
	shared/libutil/getlines.o\
	shared/libutil/human.o\
	shared/libutil/linecmp.o\
	shared/libutil/md5.o\
	shared/libutil/memmem.o\
	shared/libutil/mkdirp.o\
	shared/libutil/mode.o\
	shared/libutil/parseoffset.o\
	shared/libutil/putword.o\
	shared/libutil/reallocarray.o\
	shared/libutil/recurse.o\
	shared/libutil/rm.o\
	shared/libutil/sha1.o\
	shared/libutil/sha224.o\
	shared/libutil/sha256.o\
	shared/libutil/sha384.o\
	shared/libutil/sha512.o\
	shared/libutil/sha512-224.o\
	shared/libutil/sha512-256.o\
	shared/libutil/strcasestr.o\
	shared/libutil/strlcat.o\
	shared/libutil/strlcpy.o\
	shared/libutil/strsep.o\
	shared/libutil/strnsubst.o\
	shared/libutil/strtonum.o\
	shared/libutil/writeall.o\
	shared/libutil/unescape.o\
	shared/libutil/agetcwd.o\
	shared/libutil/agetline.o\
	shared/libutil/apathmax.o\
	shared/libutil/estrtol.o\
	shared/libutil/estrtoul.o\
	shared/libutil/explicit_bzero.o\
	shared/libutil/passwd.o\
	shared/libutil/proc.o\
	shared/libutil/tty.o\
	shared/libutil/fconcat.o\
	shared/libutil/recurse_dir.o\
	shared/libutil/sig.o\
	shared/libutil/net.o\
	shared/libutil/sysinfo.o\
	shared/libutil/tls.o\
	shared/libutil/diskutil.o

LIBREDLINEOBJ =\
	shared/libredline/redline.o

LIB = shared/libredline/libredline.a shared/libutil/libutil.a shared/libutf/libutf.a

POSIX_BIN_ALL =\
	cmd/posix/basename\
	cmd/posix/cal\
	cmd/posix/cat\
	cmd/posix/chgrp\
	cmd/posix/chmod\
	cmd/posix/chown\
	cmd/posix/cksum\
	cmd/posix/cmp\
	cmd/posix/comm\
	cmd/posix/cp\
	cmd/posix/cut\
	cmd/posix/date\
	cmd/posix/dd\
	cmd/posix/df\
	cmd/posix/diff\
	cmd/posix/patch\
	cmd/posix/dirname\
	cmd/posix/du\
	cmd/posix/echo\
	cmd/posix/ed\
	cmd/posix/env\
	cmd/posix/expand\
	cmd/posix/expr\
	cmd/posix/false\
	cmd/posix/find\
	cmd/posix/fold\
	cmd/posix/getconf\
	cmd/posix/grep\
	cmd/posix/head\
	cmd/posix/id\
	cmd/posix/join\
	cmd/posix/kill\
	cmd/posix/link\
	cmd/posix/ln\
	cmd/posix/logger\
	cmd/posix/logname\
	cmd/posix/ls\
	cmd/posix/mesg\
	cmd/posix/mkdir\
	cmd/posix/mkfifo\
	cmd/posix/mv\
	cmd/posix/nice\
	cmd/posix/nl\
	cmd/posix/nohup\
	cmd/posix/od\
	cmd/posix/paste\
	cmd/posix/pathchk\
	cmd/posix/printf\
	cmd/posix/ps\
	cmd/posix/pwd\
	cmd/posix/readlink\
	cmd/posix/renice\
	cmd/posix/rm\
	cmd/posix/rmdir\
	cmd/posix/sed\
	cmd/posix/sleep\
	cmd/posix/sort\
	cmd/posix/split\
	cmd/posix/tail\
	cmd/posix/tee\
	cmd/posix/test\
	cmd/posix/time\
	cmd/posix/touch\
	cmd/posix/tr\
	cmd/posix/true\
	cmd/posix/tsort\
	cmd/posix/tty\
	cmd/posix/uname\
	cmd/posix/unexpand\
	cmd/posix/uniq\
	cmd/posix/unlink\
	cmd/posix/uudecode\
	cmd/posix/uuencode\
	cmd/posix/wc\
	cmd/posix/who\
	cmd/posix/xargs\
	cmd/posix/awk/awk\
	cmd/posix/sh/sh\
	cmd/posix/pax\
	cmd/posix/make/make\
	cmd/posix/bc

LINUX_BIN_ALL =\
	cmd/linux/blkdiscard\
	cmd/linux/chvt\
	cmd/linux/ctrlaltdel\
	cmd/linux/eject\
	cmd/linux/freeramdisk\
	cmd/linux/fsfreeze\
	cmd/linux/hwclock\
	cmd/linux/insmod\
	cmd/linux/lsmod\
	cmd/linux/lsusb\
	cmd/linux/modprobe\
	cmd/linux/depmod\
	cmd/linux/mkswap\
	cmd/linux/mount\
	cmd/linux/mountpoint\
	cmd/linux/pivot_root\
	cmd/linux/readahead\
	cmd/linux/rmmod\
	cmd/linux/swaplabel\
	cmd/linux/swapoff\
	cmd/linux/swapon\
	cmd/linux/switch_root\
	cmd/linux/sysctl\
	cmd/linux/tunctl\
	cmd/linux/umount\
	cmd/linux/unshare\
	cmd/linux/vtallow

NET_BIN_ALL =\
	cmd/net/netcat\
	cmd/net/tftp\
	cmd/net/wget\
	cmd/net/ping\
	cmd/net/sdhcp\
	cmd/net/ifconfig\
	cmd/net/host\
	cmd/net/httpd\
	cmd/net/ip

XSI_BIN_ALL =\
	cmd/xsi/mknod\
	cmd/xsi/passwd\
	cmd/xsi/su

PSEUDO_BIN_ALL =\
	cmd/pseudo/chroot\
	cmd/pseudo/clear\
	cmd/pseudo/cols\
	cmd/pseudo/cron\
	cmd/pseudo/flock\
	cmd/pseudo/getty\
	cmd/pseudo/halt\
	cmd/pseudo/hostname\
	cmd/pseudo/killall5\
	cmd/pseudo/last\
	cmd/pseudo/lastlog\
	cmd/pseudo/login\
	cmd/pseudo/md5sum\
	cmd/pseudo/mktemp\
	cmd/pseudo/nologin\
	cmd/pseudo/pagesize\
	cmd/pseudo/printenv\
	cmd/pseudo/respawn\
	cmd/pseudo/rev\
	cmd/pseudo/seq\
	cmd/pseudo/setsid\
	cmd/pseudo/sha1sum\
	cmd/pseudo/sha224sum\
	cmd/pseudo/sha256sum\
	cmd/pseudo/sha384sum\
	cmd/pseudo/sha512sum\
	cmd/pseudo/sha512-224sum\
	cmd/pseudo/sha512-256sum\
	cmd/pseudo/sponge\
	cmd/pseudo/stat\
	cmd/pseudo/tar\
	cmd/pseudo/truncate\
	cmd/pseudo/watch\
	cmd/pseudo/which\
	cmd/pseudo/whoami\
	cmd/pseudo/xinstall\
	cmd/pseudo/yes\
	cmd/pseudo/base64\
	cmd/pseudo/mkpasswd\
	cmd/extra/b3sum\
	cmd/extra/blkid\
	cmd/extra/lsblk\
	cmd/extra/fdisk\
	cmd/extra/sync\
	cmd/extra/yap/yap\
	cmd/dev/ar/ar\
	cmd/dev/as/as\
	cmd/dev/ld/ld\
	cmd/dev/cc/cc\
	cmd/pseudo/dmesg\
	cmd/pseudo/fallocate\
	cmd/pseudo/free\
	cmd/pseudo/pidof\
	cmd/pseudo/pwdx\
	cmd/pseudo/uptime\
	cmd/pseudo/diff3

MAKEOBJ =\
	cmd/posix/make/defaults.o\
	cmd/posix/make/main.o\
	cmd/posix/make/parser.o\
	cmd/posix/make/posix.o\
	cmd/posix/make/rules.o



BIN_basename_1 = cmd/posix/basename
BIN_cal_1 = cmd/posix/cal
BIN_cat_1 = cmd/posix/cat
BIN_chgrp_1 = cmd/posix/chgrp
BIN_chmod_1 = cmd/posix/chmod
BIN_chown_1 = cmd/posix/chown
BIN_cksum_1 = cmd/posix/cksum
BIN_cmp_1 = cmd/posix/cmp
BIN_comm_1 = cmd/posix/comm
BIN_cp_1 = cmd/posix/cp
BIN_cut_1 = cmd/posix/cut
BIN_date_1 = cmd/posix/date
BIN_dd_1 = cmd/posix/dd
BIN_df_1 = cmd/posix/df
BIN_diff_1 = cmd/posix/diff
BIN_patch_1 = cmd/posix/patch
BIN_dirname_1 = cmd/posix/dirname
BIN_du_1 = cmd/posix/du
BIN_echo_1 = cmd/posix/echo
BIN_ed_1 = cmd/posix/ed
BIN_env_1 = cmd/posix/env
BIN_expand_1 = cmd/posix/expand
BIN_expr_1 = cmd/posix/expr
BIN_false_1 = cmd/posix/false
BIN_find_1 = cmd/posix/find
BIN_fold_1 = cmd/posix/fold
BIN_getconf_1 = cmd/posix/getconf
BIN_grep_1 = cmd/posix/grep
BIN_head_1 = cmd/posix/head
BIN_id_1 = cmd/posix/id
BIN_join_1 = cmd/posix/join
BIN_kill_1 = cmd/posix/kill
BIN_link_1 = cmd/posix/link
BIN_ln_1 = cmd/posix/ln
BIN_logger_1 = cmd/posix/logger
BIN_logname_1 = cmd/posix/logname
BIN_ls_1 = cmd/posix/ls
BIN_mesg_1 = cmd/posix/mesg
BIN_mkdir_1 = cmd/posix/mkdir
BIN_mkfifo_1 = cmd/posix/mkfifo
BIN_mv_1 = cmd/posix/mv
BIN_nice_1 = cmd/posix/nice
BIN_nl_1 = cmd/posix/nl
BIN_nohup_1 = cmd/posix/nohup
BIN_od_1 = cmd/posix/od
BIN_paste_1 = cmd/posix/paste
BIN_pathchk_1 = cmd/posix/pathchk
BIN_printf_1 = cmd/posix/printf
BIN_ps_1 = cmd/posix/ps
BIN_pwd_1 = cmd/posix/pwd
BIN_readlink_1 = cmd/posix/readlink
BIN_renice_1 = cmd/posix/renice
BIN_rm_1 = cmd/posix/rm
BIN_rmdir_1 = cmd/posix/rmdir
BIN_sed_1 = cmd/posix/sed
BIN_sleep_1 = cmd/posix/sleep
BIN_sort_1 = cmd/posix/sort
BIN_split_1 = cmd/posix/split
BIN_tail_1 = cmd/posix/tail
BIN_tee_1 = cmd/posix/tee
BIN_test_1 = cmd/posix/test
BIN_time_1 = cmd/posix/time
BIN_touch_1 = cmd/posix/touch
BIN_tr_1 = cmd/posix/tr
BIN_true_1 = cmd/posix/true
BIN_tsort_1 = cmd/posix/tsort
BIN_tty_1 = cmd/posix/tty
BIN_uname_1 = cmd/posix/uname
BIN_unexpand_1 = cmd/posix/unexpand
BIN_uniq_1 = cmd/posix/uniq
BIN_unlink_1 = cmd/posix/unlink
BIN_uudecode_1 = cmd/posix/uudecode
BIN_uuencode_1 = cmd/posix/uuencode
BIN_wc_1 = cmd/posix/wc
BIN_who_1 = cmd/posix/who
BIN_xargs_1 = cmd/posix/xargs
BIN_awk_1 = cmd/posix/awk/awk
BIN_sh_1 = cmd/posix/sh/sh
BIN_pax_1 = cmd/posix/pax
BIN_make_1 = cmd/posix/make/make

BIN_blkdiscard_1 = cmd/linux/blkdiscard
BIN_chvt_1 = cmd/linux/chvt
BIN_ctrlaltdel_1 = cmd/linux/ctrlaltdel
BIN_eject_1 = cmd/linux/eject
BIN_freeramdisk_1 = cmd/linux/freeramdisk
BIN_fsfreeze_1 = cmd/linux/fsfreeze
BIN_hwclock_1 = cmd/linux/hwclock
BIN_insmod_1 = cmd/linux/insmod
BIN_lsmod_1 = cmd/linux/lsmod
BIN_lsusb_1 = cmd/linux/lsusb
BIN_modprobe_1 = cmd/linux/modprobe
BIN_depmod_1 = cmd/linux/depmod
BIN_mkswap_1 = cmd/linux/mkswap
BIN_mount_1 = cmd/linux/mount
BIN_mountpoint_1 = cmd/linux/mountpoint
BIN_pivot_root_1 = cmd/linux/pivot_root
BIN_readahead_1 = cmd/linux/readahead
BIN_rmmod_1 = cmd/linux/rmmod
BIN_swaplabel_1 = cmd/linux/swaplabel
BIN_swapoff_1 = cmd/linux/swapoff
BIN_swapon_1 = cmd/linux/swapon
BIN_switch_root_1 = cmd/linux/switch_root
BIN_sysctl_1 = cmd/linux/sysctl
BIN_tunctl_1 = cmd/linux/tunctl
BIN_umount_1 = cmd/linux/umount
BIN_unshare_1 = cmd/linux/unshare
BIN_vtallow_1 = cmd/linux/vtallow

BIN_netcat_1 = cmd/net/netcat
BIN_tftp_1 = cmd/net/tftp
BIN_wget_1 = cmd/net/wget
BIN_ping_1 = cmd/net/ping
BIN_sdhcp_1 = cmd/net/sdhcp
BIN_ifconfig_1 = cmd/net/ifconfig
BIN_host_1 = cmd/net/host
BIN_httpd_1 = cmd/net/httpd
BIN_ip_1 = cmd/net/ip

BIN_mknod_1 = cmd/xsi/mknod
BIN_passwd_1 = cmd/xsi/passwd
BIN_su_1 = cmd/xsi/su

BIN_chroot_1 = cmd/pseudo/chroot
BIN_clear_1 = cmd/pseudo/clear
BIN_cols_1 = cmd/pseudo/cols
BIN_cron_1 = cmd/pseudo/cron
BIN_flock_1 = cmd/pseudo/flock
BIN_getty_1 = cmd/pseudo/getty
BIN_halt_1 = cmd/pseudo/halt
BIN_hostname_1 = cmd/pseudo/hostname
BIN_killall5_1 = cmd/pseudo/killall5
BIN_last_1 = cmd/pseudo/last
BIN_lastlog_1 = cmd/pseudo/lastlog
BIN_login_1 = cmd/pseudo/login
BIN_md5sum_1 = cmd/pseudo/md5sum
BIN_mktemp_1 = cmd/pseudo/mktemp
BIN_nologin_1 = cmd/pseudo/nologin
BIN_pagesize_1 = cmd/pseudo/pagesize
BIN_printenv_1 = cmd/pseudo/printenv
BIN_respawn_1 = cmd/pseudo/respawn
BIN_rev_1 = cmd/pseudo/rev
BIN_seq_1 = cmd/pseudo/seq
BIN_setsid_1 = cmd/pseudo/setsid
BIN_sha1sum_1 = cmd/pseudo/sha1sum
BIN_sha224sum_1 = cmd/pseudo/sha224sum
BIN_sha256sum_1 = cmd/pseudo/sha256sum
BIN_sha384sum_1 = cmd/pseudo/sha384sum
BIN_sha512sum_1 = cmd/pseudo/sha512sum
BIN_sha512_224sum_1 = cmd/pseudo/sha512-224sum
BIN_sha512_256sum_1 = cmd/pseudo/sha512-256sum
BIN_sponge_1 = cmd/pseudo/sponge
BIN_stat_1 = cmd/pseudo/stat
BIN_tar_1 = cmd/pseudo/tar
BIN_truncate_1 = cmd/pseudo/truncate
BIN_watch_1 = cmd/pseudo/watch
BIN_which_1 = cmd/pseudo/which
BIN_whoami_1 = cmd/pseudo/whoami
BIN_xinstall_1 = cmd/pseudo/xinstall
BIN_yes_1 = cmd/pseudo/yes
BIN_base64_1 = cmd/pseudo/base64
BIN_mkpasswd_1 = cmd/pseudo/mkpasswd
BIN_bc_1 = cmd/posix/bc
BIN_b3sum_1 = cmd/extra/b3sum
BIN_blkid_1 = cmd/extra/blkid
BIN_lsblk_1 = cmd/extra/lsblk
BIN_fdisk_1 = cmd/extra/fdisk
BIN_sync_1 = cmd/extra/sync
BIN_yap_1 = cmd/extra/yap/yap
BIN_diff3_1 = cmd/pseudo/diff3
BIN_ar_1 = cmd/dev/ar/ar
BIN_as_1 = cmd/dev/as/as
BIN_ld_1 = cmd/dev/ld/ld
BIN_cc_1 = cmd/dev/cc/cc
BIN_dmesg_1 = cmd/pseudo/dmesg
BIN_fallocate_1 = cmd/pseudo/fallocate
BIN_free_1 = cmd/pseudo/free
BIN_pidof_1 = cmd/pseudo/pidof
BIN_pwdx_1 = cmd/pseudo/pwdx
BIN_uptime_1 = cmd/pseudo/uptime

POSIX_BIN = $(POSIX_BIN_CONFIG)

LINUX_BIN = $(LINUX_BIN_CONFIG)

NET_BIN = $(NET_BIN_CONFIG)

XSI_BIN = $(XSI_BIN_CONFIG)

PSEUDO_BIN = $(PSEUDO_BIN_CONFIG) $(EXTRA_BIN_CONFIG) $(DEV_BIN_CONFIG)


OBJ = $(LIBUTFOBJ) $(LIBUTILOBJ) $(MAKEOBJ)

all: $(LIB) $(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN)

-include deps.mk
-include scripts/mk/rules.mk

$(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL): $(LIB)

$(OBJ) $(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL): $(HDR)

.o:
	$(CC) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

.c:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

cmd/posix/bc.c: cmd/posix/bc.y
	YACC='$(YACC)' sh scripts/genconfig.sh bc

cmd/posix/bc: cmd/posix/bc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ cmd/posix/bc.c $(LIB) $(LDLIBS)

$(MAKEOBJ): cmd/posix/make/make.h

cmd/posix/make/make: $(MAKEOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(MAKEOBJ) $(LIB) $(LDLIBS)

cmd/posix/diff: cmd/posix/diff.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ cmd/posix/diff.c $(LIB) $(LDLIBS) -lm

shared/libutf/libutf.a: $(LIBUTFOBJ)
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $@

shared/libredline/libredline.a: $(LIBREDLINEOBJ)
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $@

shared/libutil/libutil.a: $(LIBUTILOBJ)
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $@

cmd/posix/getconf: cmd/posix/getconf.h

cmd/posix/getconf.h:
	sh scripts/genconfig.sh getconf

box: $(LIB)
	CC='$(CC)' CPPFLAGS='$(CPPFLAGS)' CFLAGS='$(CFLAGS)' \
	LDFLAGS='$(LDFLAGS)' LDLIBS='$(LDLIBS)' OBJCOPY='$(OBJCOPY)' \
	scripts/mkbox

.PHONY: man clean install regen help

scripts/mkman/mkman: scripts/mkman/main.go scripts/mkman/page.go scripts/mkman/parse.go scripts/mkman/mdoc.go
	cd scripts/mkman && go build -o mkman .

man: scripts/mkman/mkman
	find cmd -type f -name '*.c' | while read -r src; do \
		if [ -f "$$src" ] && grep -qE '!man|\?man' "$$src"; then \
			sub=$${src#cmd/}; \
			cat=$${sub%%/*}; \
			base=$${src##*/}; \
			base=$${base%.c}; \
			name=$${sub#*/}; \
			if [ "$${name}" != "$${base}.c" ]; then \
				name=$${name%%/*}; \
			else \
				name=$$base; \
			fi; \
			u_group=$$(echo "$$cat" | tr 'a-z' 'A-Z'); \
			u_tool=$$(echo "$$name" | tr 'a-z-' 'A-Z_'); \
			var_name="BUILD_$${u_group}_$${u_tool}"; \
			val=""; \
			if [ -f scripts/mk/config.kv ]; then \
				val=$$(grep "^$${var_name}=" scripts/mk/config.kv | cut -d= -f2 | tr -d '"'); \
			fi; \
			if [ "$$val" != "1" ]; then \
				continue; \
			fi; \
			case "$$cat" in \
				linux|net|xsi) sec=8 ;; \
				*)             sec=1 ;; \
			esac; \
			mkdir -p "man/man$${sec}"; \
			scripts/mkman/mkman -fmt mdoc -config scripts/mk/config.kv -section "$${sec}" "$$src" > "man/man$${sec}/$$name.$${sec}"; \
			scripts/mkman/mkman -fmt txt -config scripts/mk/config.kv -section "$${sec}" "$$src" > "man/man$${sec}/$$name.$${sec}.txt"; \
		fi; \
	done

regen: scripts/mk/config.mk cmd/posix/getconf.h cmd/posix/bc.c cmd/posix/awk/awkgram.tab.c cmd/posix/awk/proctab.c cmd/posix/sh/token.h cmd/posix/sh/syntax.c cmd/posix/sh/nodes.c cmd/posix/sh/builtins.c

help:
	@echo "Available targets:"
	@echo "  all       - Build all enabled utilities"
	@echo "  clean     - Clean build artifacts"
	@echo "  install   - Install binaries and man pages"
	@echo "  box       - Build the multicall binary (aruu-box)"
	@echo "  man       - Generate manual pages"
	@echo "  regen     - Regenerate build system files and config"
	@echo "  help      - Show this help message"

install: all man
	@mkdir -p $(PREFIX)/bin $(MANPREFIX)/man1 $(MANPREFIX)/man8
	@find cmd -type f ! -name '*.*' -perm -100 | while read -r bin; do \
		printf "  INSTALL %s/bin/%s\n" "$(PREFIX)" "$${bin##*/}"; \
		cp "$$bin" "$(PREFIX)/bin/$${bin##*/}"; \
		chmod 755 "$(PREFIX)/bin/$${bin##*/}"; \
	done
	@find man/man1 man/man8 -type f -name '*.[18]' 2>/dev/null | while read -r f; do \
		sec=$${f%/*}; sec=$${sec##*/}; \
		cp "$$f" "$(MANPREFIX)/$${sec}/$${f##*/}"; \
	done

clean:
	@printf "  CLEAN object files\n"
	@find shared cmd -name "*.o" -exec rm -f {} +
	@printf "  CLEAN static libraries\n"
	@rm -f $(LIB)
	@printf "  CLEAN compiled binaries\n"
	@rm -f $(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL)
	@sh scripts/genconfig.sh clean
	@printf "  CLEAN build tools\n"
	@rm -f scripts/mkman/mkman
	@rm -f scripts/mk/config.kv
	@rm -f scripts/mk/config.mk
	@rm -f scripts/mk/rules.mk

	@printf "  CLEAN dev artifacts\n"
	@rm -f cmd/dev/cc/cc1 cmd/dev/cc/cpp cmd/dev/as/as cmd/dev/ld/ld cmd/dev/ar/ar shared/libaruuelf.so
	@rm -f cmd/dev/config.h cmd/dev/cc/config.h cmd/dev/version.h
	@printf "  CLEAN misc\n"
	@rm -rf aruu-box .box man/man1 man/man8
	@printf "  CLEAN done\n"
AWKOBJ =\
	cmd/posix/awk/b.o\
	cmd/posix/awk/main.o\
	cmd/posix/awk/parse.o\
	cmd/posix/awk/proctab.o\
	cmd/posix/awk/tran.o\
	cmd/posix/awk/lib.o\
	cmd/posix/awk/run.o\
	cmd/posix/awk/lex.o\
	cmd/posix/awk/math.o\
	cmd/posix/awk/awkgram.tab.o

SH_GENHDRS =\
	cmd/posix/sh/syntax.h\
	cmd/posix/sh/nodes.h\
	cmd/posix/sh/builtins.h\
	cmd/posix/sh/token.h

SHOBJ =\
	cmd/posix/sh/alias.o\
	cmd/posix/sh/arith_yacc.o\
	cmd/posix/sh/arith_yylex.o\
	cmd/posix/sh/cd.o\
	cmd/posix/sh/echo.o\
	cmd/posix/sh/error.o\
	cmd/posix/sh/easter.o\
	cmd/posix/sh/eval.o\
	cmd/posix/sh/exec.o\
	cmd/posix/sh/expand.o\
	cmd/posix/sh/lineedit.o\
	cmd/posix/sh/input.o\
	cmd/posix/sh/jobs.o\
	cmd/posix/sh/kill.o\
	cmd/posix/sh/mail.o\
	cmd/posix/sh/main.o\
	cmd/posix/sh/memalloc.o\
	cmd/posix/sh/miscbltin.o\
	cmd/posix/sh/mystring.o\
	cmd/posix/sh/options.o\
	cmd/posix/sh/output.o\
	cmd/posix/sh/parser.o\
	cmd/posix/sh/printf.o\
	cmd/posix/sh/redir.o\
	cmd/posix/sh/show.o\
	cmd/posix/sh/test.o\
	cmd/posix/sh/trap.o\
	cmd/posix/sh/var.o\
	cmd/posix/sh/builtins.o\
	cmd/posix/sh/nodes.o\
	cmd/posix/sh/syntax.o

YAPOBJ =\
	cmd/extra/yap/ansi.o\
	cmd/extra/yap/commands.o\
	cmd/extra/yap/display.o\
	cmd/extra/yap/getcomm.o\
	cmd/extra/yap/getline.o\
	cmd/extra/yap/help.o\
	cmd/extra/yap/keys.o\
	cmd/extra/yap/machine.o\
	cmd/extra/yap/main.o\
	cmd/extra/yap/options.o\
	cmd/extra/yap/output.o\
	cmd/extra/yap/pattern.o\
	cmd/extra/yap/process.o\
	cmd/extra/yap/prompt.o\
	cmd/extra/yap/term.o

cmd/posix/awk/awkgram.tab.c: cmd/posix/awk/awkgram.y cmd/posix/awk/maketab.c
	CC='$(CC)' CFLAGS='$(CFLAGS)' YACC='$(YACC)' sh scripts/genconfig.sh awk

cmd/posix/awk/awkgram.tab.h: cmd/posix/awk/awkgram.tab.c ;
cmd/posix/awk/maketab: cmd/posix/awk/awkgram.tab.c ;
cmd/posix/awk/proctab.c: cmd/posix/awk/awkgram.tab.c ;

$(AWKOBJ): cmd/posix/awk/awk.h cmd/posix/awk/awkgram.tab.h cmd/posix/awk/proto.h

cmd/posix/awk/awk: $(AWKOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(AWKOBJ) $(LIB) $(LDLIBS) -lm

cmd/posix/sh/builtins.c: cmd/posix/sh/mknodes.c cmd/posix/sh/mksyntax.c cmd/posix/sh/nodetypes cmd/posix/sh/nodes.c.pat cmd/posix/sh/mkbuiltins cmd/posix/sh/builtins.def cmd/posix/sh/shell.h cmd/posix/sh/mktokens
	CC='$(CC)' CFLAGS='$(CFLAGS)' CPPFLAGS='$(CPPFLAGS)' sh scripts/genconfig.sh sh

cmd/posix/sh/syntax.c: cmd/posix/sh/builtins.c ;
cmd/posix/sh/syntax.h: cmd/posix/sh/builtins.c ;
cmd/posix/sh/nodes.c: cmd/posix/sh/builtins.c ;
cmd/posix/sh/nodes.h: cmd/posix/sh/builtins.c ;
cmd/posix/sh/builtins.h: cmd/posix/sh/builtins.c ;
cmd/posix/sh/token.h: cmd/posix/sh/builtins.c ;
cmd/posix/sh/mknodes: cmd/posix/sh/builtins.c ;
cmd/posix/sh/mksyntax: cmd/posix/sh/builtins.c ;

$(SHOBJ): $(SH_GENHDRS)

cmd/posix/sh/sh: $(SHOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(SHOBJ) $(LIB) $(LDLIBS)

cmd/extra/yap/yap: $(YAPOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(YAPOBJ) $(LIB) $(LDLIBS) -ltinfo

cmd/extra/diff3/diff3: cmd/extra/diff3/diff3.o cmd/posix/diff/xmalloc.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/extra/diff3/diff3.o cmd/posix/diff/xmalloc.o $(LIB) $(LDLIBS)

cmd/net/wget: cmd/net/wget.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/net/wget.o $(LIB) $(LDLIBS) $(LDLIBS_TLS)

cmd/dev/ar/ar: cmd/dev/ar/ar.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/dev/ar/ar.o $(LIB) $(LDLIBS)

LD_OBJ =\
	cmd/dev/xcutil/util.o\
	cmd/dev/xcutil/table.o\
	cmd/dev/xcutil/elfutil.o\
	cmd/dev/xcutil/archive.o\
	cmd/dev/ld/ld.o\
	cmd/dev/ld/elfobj.o

cmd/dev/ld/ld: $(LD_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(LD_OBJ) $(LIB) $(LDLIBS)

AS_OBJ =\
	cmd/dev/xcutil/util.o\
	cmd/dev/xcutil/table.o\
	cmd/dev/xcutil/elfutil.o\
	cmd/dev/xcutil/archive.o\
	cmd/dev/as/as.o\
	cmd/dev/as/as_util.o\
	cmd/dev/as/emit_elf.o\
	cmd/dev/as/emit_macho.o\
	cmd/dev/as/ir_asm.o\
	cmd/dev/as/parse_asm.o\
	cmd/dev/as/arch/x64/asm_code.o\
	cmd/dev/as/arch/x64/ir_asm_x64.o\
	cmd/dev/as/arch/x64/parse_x64.o

AS_HDRS =\
	cmd/dev/as/parse_asm.h\
	cmd/dev/as/ir_asm.h\
	cmd/dev/as/as_util.h\
	cmd/dev/as/asm_code.h\
	cmd/dev/as/arch/x64/inst.h

cmd/dev/as/as: $(AS_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(AS_OBJ) $(LIB) $(LDLIBS)

cmd/dev/cc/driver.o: cmd/dev/cc/config.h

cmd/dev/cc/cc: cmd/dev/cc/driver.o cmd/dev/cc/util.o $(LIB) cmd/dev/cc/cc1 cmd/dev/cc/cpp cmd/dev/as/as cmd/dev/ld/ld
	$(CC) $(LDFLAGS) -o $@ cmd/dev/cc/driver.o cmd/dev/cc/util.o $(LIB) $(LDLIBS)

CC1_OBJ =\
	cmd/dev/cc/attr.o\
	cmd/dev/cc/decl.o\
	cmd/dev/cc/eval.o\
	cmd/dev/cc/expr.o\
	cmd/dev/cc/init.o\
	cmd/dev/cc/cc1.o\
	cmd/dev/cc/map.o\
	cmd/dev/cc/pp.o\
	cmd/dev/cc/qbe.o\
	cmd/dev/cc/scan.o\
	cmd/dev/cc/scope.o\
	cmd/dev/cc/stmt.o\
	cmd/dev/cc/targ.o\
	cmd/dev/cc/token.o\
	cmd/dev/cc/tree.o\
	cmd/dev/cc/type.o\
	cmd/dev/cc/utf.o\
	cmd/dev/cc/util.o

CPP_OBJ =\
	cmd/dev/cc/attr.o\
	cmd/dev/cc/decl.o\
	cmd/dev/cc/eval.o\
	cmd/dev/cc/expr.o\
	cmd/dev/cc/init.o\
	cmd/dev/cc/map.o\
	cmd/dev/cc/pp.o\
	cmd/dev/cc/qbe.o\
	cmd/dev/cc/scan.o\
	cmd/dev/cc/scope.o\
	cmd/dev/cc/stmt.o\
	cmd/dev/cc/targ.o\
	cmd/dev/cc/token.o\
	cmd/dev/cc/tree.o\
	cmd/dev/cc/type.o\
	cmd/dev/cc/utf.o\
	cmd/dev/cc/util.o

cmd/dev/cc/cc1: $(CC1_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(CC1_OBJ) $(LIB) $(LDLIBS)

cmd/dev/cc/cpp: cmd/dev/cc/cpp.o $(CPP_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/dev/cc/cpp.o $(CPP_OBJ) $(LIB) $(LDLIBS)

cmd/dev/config.h: cmd/dev/configure
	sh cmd/dev/configure

cmd/dev/cc/config.h: cmd/dev/config.h ;
cmd/dev/version.h: cmd/dev/config.h ;

$(AS_OBJ) $(LD_OBJ) $(CC1_OBJ) $(CPP_OBJ) cmd/dev/ar/ar.o cmd/dev/cc/cpp.o: cmd/dev/config.h

scripts/mk/config.mk: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh

scripts/mk/config.kv: scripts/mk/config.mk ;
scripts/mk/rules.mk: scripts/mk/config.mk ;

Makefile: ;
