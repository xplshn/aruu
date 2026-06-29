.POSIX:
include config.mk

.SUFFIXES:
.SUFFIXES: .y .o .c

HDR =\
	shared/arg.h\
	shared/compat.h\
	shared/config.h\
	shared/crypt.h\
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
	cmd/posix/diff/diff\
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
	cmd/posix/make/make

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
	cmd/extra/b3sum\
	cmd/extra/blkid\
	cmd/extra/lsblk\
	cmd/extra/fdisk\
	cmd/extra/sync\
	cmd/extra/diff3/diff3\
	cmd/dev/ar/ar\
	cmd/dev/as/as\
	cmd/dev/ld/ld\
	cmd/dev/cc/cc\
	cmd/pseudo/dmesg\
	cmd/pseudo/fallocate\
	cmd/pseudo/free\
	cmd/pseudo/pidof\
	cmd/pseudo/pwdx\
	cmd/pseudo/uptime

MAKEOBJ =\
	cmd/posix/make/defaults.o\
	cmd/posix/make/main.o\
	cmd/posix/make/parser.o\
	cmd/posix/make/posix.o\
	cmd/posix/make/rules.o

DIFFOBJ =\
	cmd/posix/diff/diff.o\
	cmd/posix/diff/diffdir.o\
	cmd/posix/diff/diffreg.o\
	cmd/posix/diff/pr.o\
	cmd/posix/diff/xmalloc.o

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
BIN_diff_1 = cmd/posix/diff/diff
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
BIN_b3sum_1 = cmd/extra/b3sum
BIN_blkid_1 = cmd/extra/blkid
BIN_lsblk_1 = cmd/extra/lsblk
BIN_fdisk_1 = cmd/extra/fdisk
BIN_sync_1 = cmd/extra/sync
BIN_diff3_1 = cmd/extra/diff3/diff3
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

POSIX_BIN = \
	$(BIN_basename_$(BUILD_POSIX_BASENAME)) \
	$(BIN_cal_$(BUILD_POSIX_CAL)) \
	$(BIN_cat_$(BUILD_POSIX_CAT)) \
	$(BIN_chgrp_$(BUILD_POSIX_CHGRP)) \
	$(BIN_chmod_$(BUILD_POSIX_CHMOD)) \
	$(BIN_chown_$(BUILD_POSIX_CHOWN)) \
	$(BIN_cksum_$(BUILD_POSIX_CKSUM)) \
	$(BIN_cmp_$(BUILD_POSIX_CMP)) \
	$(BIN_comm_$(BUILD_POSIX_COMM)) \
	$(BIN_cp_$(BUILD_POSIX_CP)) \
	$(BIN_cut_$(BUILD_POSIX_CUT)) \
	$(BIN_date_$(BUILD_POSIX_DATE)) \
	$(BIN_dd_$(BUILD_POSIX_DD)) \
	$(BIN_df_$(BUILD_POSIX_DF)) \
	$(BIN_diff_$(BUILD_POSIX_DIFF)) \
	$(BIN_dirname_$(BUILD_POSIX_DIRNAME)) \
	$(BIN_du_$(BUILD_POSIX_DU)) \
	$(BIN_echo_$(BUILD_POSIX_ECHO)) \
	$(BIN_ed_$(BUILD_POSIX_ED)) \
	$(BIN_env_$(BUILD_POSIX_ENV)) \
	$(BIN_expand_$(BUILD_POSIX_EXPAND)) \
	$(BIN_expr_$(BUILD_POSIX_EXPR)) \
	$(BIN_false_$(BUILD_POSIX_FALSE)) \
	$(BIN_find_$(BUILD_POSIX_FIND)) \
	$(BIN_fold_$(BUILD_POSIX_FOLD)) \
	$(BIN_getconf_$(BUILD_POSIX_GETCONF)) \
	$(BIN_grep_$(BUILD_POSIX_GREP)) \
	$(BIN_head_$(BUILD_POSIX_HEAD)) \
	$(BIN_id_$(BUILD_POSIX_ID)) \
	$(BIN_join_$(BUILD_POSIX_JOIN)) \
	$(BIN_kill_$(BUILD_POSIX_KILL)) \
	$(BIN_link_$(BUILD_POSIX_LINK)) \
	$(BIN_ln_$(BUILD_POSIX_LN)) \
	$(BIN_logger_$(BUILD_POSIX_LOGGER)) \
	$(BIN_logname_$(BUILD_POSIX_LOGNAME)) \
	$(BIN_ls_$(BUILD_POSIX_LS)) \
	$(BIN_mesg_$(BUILD_POSIX_MESG)) \
	$(BIN_mkdir_$(BUILD_POSIX_MKDIR)) \
	$(BIN_mkfifo_$(BUILD_POSIX_MKFIFO)) \
	$(BIN_mv_$(BUILD_POSIX_MV)) \
	$(BIN_nice_$(BUILD_POSIX_NICE)) \
	$(BIN_nl_$(BUILD_POSIX_NL)) \
	$(BIN_nohup_$(BUILD_POSIX_NOHUP)) \
	$(BIN_od_$(BUILD_POSIX_OD)) \
	$(BIN_paste_$(BUILD_POSIX_PASTE)) \
	$(BIN_pathchk_$(BUILD_POSIX_PATHCHK)) \
	$(BIN_printf_$(BUILD_POSIX_PRINTF)) \
	$(BIN_ps_$(BUILD_POSIX_PS)) \
	$(BIN_pwd_$(BUILD_POSIX_PWD)) \
	$(BIN_readlink_$(BUILD_POSIX_READLINK)) \
	$(BIN_renice_$(BUILD_POSIX_RENICE)) \
	$(BIN_rm_$(BUILD_POSIX_RM)) \
	$(BIN_rmdir_$(BUILD_POSIX_RMDIR)) \
	$(BIN_sed_$(BUILD_POSIX_SED)) \
	$(BIN_sleep_$(BUILD_POSIX_SLEEP)) \
	$(BIN_sort_$(BUILD_POSIX_SORT)) \
	$(BIN_split_$(BUILD_POSIX_SPLIT)) \
	$(BIN_tail_$(BUILD_POSIX_TAIL)) \
	$(BIN_tee_$(BUILD_POSIX_TEE)) \
	$(BIN_test_$(BUILD_POSIX_TEST)) \
	$(BIN_time_$(BUILD_POSIX_TIME)) \
	$(BIN_touch_$(BUILD_POSIX_TOUCH)) \
	$(BIN_tr_$(BUILD_POSIX_TR)) \
	$(BIN_true_$(BUILD_POSIX_TRUE)) \
	$(BIN_tsort_$(BUILD_POSIX_TSORT)) \
	$(BIN_tty_$(BUILD_POSIX_TTY)) \
	$(BIN_uname_$(BUILD_POSIX_UNAME)) \
	$(BIN_unexpand_$(BUILD_POSIX_UNEXPAND)) \
	$(BIN_uniq_$(BUILD_POSIX_UNIQ)) \
	$(BIN_unlink_$(BUILD_POSIX_UNLINK)) \
	$(BIN_uudecode_$(BUILD_POSIX_UUDECODE)) \
	$(BIN_uuencode_$(BUILD_POSIX_UUENCODE)) \
	$(BIN_wc_$(BUILD_POSIX_WC)) \
	$(BIN_who_$(BUILD_POSIX_WHO)) \
	$(BIN_xargs_$(BUILD_POSIX_XARGS)) \
	$(BIN_awk_$(BUILD_POSIX_AWK)) \
	$(BIN_sh_$(BUILD_POSIX_SH)) \
	$(BIN_pax_$(BUILD_POSIX_PAX)) \
	$(BIN_make_$(BUILD_POSIX_MAKE))

LINUX_BIN = \
	$(BIN_blkdiscard_$(BUILD_LINUX_BLKDISCARD)) \
	$(BIN_chvt_$(BUILD_LINUX_CHVT)) \
	$(BIN_ctrlaltdel_$(BUILD_LINUX_CTRLALTDEL)) \
	$(BIN_eject_$(BUILD_LINUX_EJECT)) \
	$(BIN_freeramdisk_$(BUILD_LINUX_FREERAMDISK)) \
	$(BIN_fsfreeze_$(BUILD_LINUX_FSFREEZE)) \
	$(BIN_hwclock_$(BUILD_LINUX_HWCLOCK)) \
	$(BIN_insmod_$(BUILD_LINUX_INSMOD)) \
	$(BIN_lsmod_$(BUILD_LINUX_LSMOD)) \
	$(BIN_modprobe_$(BUILD_LINUX_MODPROBE)) \
	$(BIN_depmod_$(BUILD_LINUX_DEPMOD)) \
	$(BIN_mkswap_$(BUILD_LINUX_MKSWAP)) \
	$(BIN_mount_$(BUILD_LINUX_MOUNT)) \
	$(BIN_mountpoint_$(BUILD_LINUX_MOUNTPOINT)) \
	$(BIN_pivot_root_$(BUILD_LINUX_PIVOT_ROOT)) \
	$(BIN_readahead_$(BUILD_LINUX_READAHEAD)) \
	$(BIN_rmmod_$(BUILD_LINUX_RMMOD)) \
	$(BIN_swaplabel_$(BUILD_LINUX_SWAPLABEL)) \
	$(BIN_swapoff_$(BUILD_LINUX_SWAPOFF)) \
	$(BIN_swapon_$(BUILD_LINUX_SWAPON)) \
	$(BIN_switch_root_$(BUILD_LINUX_SWITCH_ROOT)) \
	$(BIN_tunctl_$(BUILD_LINUX_TUNCTL)) \
	$(BIN_umount_$(BUILD_LINUX_UMOUNT)) \
	$(BIN_unshare_$(BUILD_LINUX_UNSHARE)) \
	$(BIN_vtallow_$(BUILD_LINUX_VTALLOW))

NET_BIN = \
	$(BIN_netcat_$(BUILD_NET_NETCAT)) \
	$(BIN_tftp_$(BUILD_NET_TFTP)) \
	$(BIN_wget_$(BUILD_NET_WGET)) \
	$(BIN_ping_$(BUILD_NET_PING)) \
	$(BIN_sdhcp_$(BUILD_NET_SDHCP)) \
	$(BIN_ifconfig_$(BUILD_NET_IFCONFIG)) \
	$(BIN_host_$(BUILD_NET_HOST)) \
	$(BIN_httpd_$(BUILD_NET_HTTPD)) \
	$(BIN_ip_$(BUILD_NET_IP))

XSI_BIN = \
	$(BIN_mknod_$(BUILD_XSI_MKNOD)) \
	$(BIN_passwd_$(BUILD_XSI_PASSWD)) \
	$(BIN_su_$(BUILD_XSI_SU))

PSEUDO_BIN = \
	$(BIN_chroot_$(BUILD_PSEUDO_CHROOT)) \
	$(BIN_clear_$(BUILD_PSEUDO_CLEAR)) \
	$(BIN_cols_$(BUILD_PSEUDO_COLS)) \
	$(BIN_cron_$(BUILD_PSEUDO_CRON)) \
	$(BIN_flock_$(BUILD_PSEUDO_FLOCK)) \
	$(BIN_getty_$(BUILD_PSEUDO_GETTY)) \
	$(BIN_halt_$(BUILD_PSEUDO_HALT)) \
	$(BIN_hostname_$(BUILD_PSEUDO_HOSTNAME)) \
	$(BIN_killall5_$(BUILD_PSEUDO_KILLALL5)) \
	$(BIN_last_$(BUILD_PSEUDO_LAST)) \
	$(BIN_lastlog_$(BUILD_PSEUDO_LASTLOG)) \
	$(BIN_login_$(BUILD_PSEUDO_LOGIN)) \
	$(BIN_md5sum_$(BUILD_PSEUDO_MD5SUM)) \
	$(BIN_mktemp_$(BUILD_PSEUDO_MKTEMP)) \
	$(BIN_nologin_$(BUILD_PSEUDO_NOLOGIN)) \
	$(BIN_pagesize_$(BUILD_PSEUDO_PAGESIZE)) \
	$(BIN_printenv_$(BUILD_PSEUDO_PRINTENV)) \
	$(BIN_respawn_$(BUILD_PSEUDO_RESPAWN)) \
	$(BIN_rev_$(BUILD_PSEUDO_REV)) \
	$(BIN_seq_$(BUILD_PSEUDO_SEQ)) \
	$(BIN_setsid_$(BUILD_PSEUDO_SETSID)) \
	$(BIN_sha1sum_$(BUILD_PSEUDO_SHA1SUM)) \
	$(BIN_sha224sum_$(BUILD_PSEUDO_SHA224SUM)) \
	$(BIN_sha256sum_$(BUILD_PSEUDO_SHA256SUM)) \
	$(BIN_sha384sum_$(BUILD_PSEUDO_SHA384SUM)) \
	$(BIN_sha512sum_$(BUILD_PSEUDO_SHA512SUM)) \
	$(BIN_sha512_224sum_$(BUILD_PSEUDO_SHA512_224SUM)) \
	$(BIN_sha512_256sum_$(BUILD_PSEUDO_SHA512_256SUM)) \
	$(BIN_sponge_$(BUILD_PSEUDO_SPONGE)) \
	$(BIN_stat_$(BUILD_PSEUDO_STAT)) \
	$(BIN_tar_$(BUILD_PSEUDO_TAR)) \
	$(BIN_truncate_$(BUILD_PSEUDO_TRUNCATE)) \
	$(BIN_watch_$(BUILD_PSEUDO_WATCH)) \
	$(BIN_which_$(BUILD_PSEUDO_WHICH)) \
	$(BIN_whoami_$(BUILD_PSEUDO_WHOAMI)) \
	$(BIN_xinstall_$(BUILD_PSEUDO_XINSTALL)) \
	$(BIN_yes_$(BUILD_PSEUDO_YES)) \
	$(BIN_base64_$(BUILD_PSEUDO_BASE64)) \
	$(BIN_b3sum_$(BUILD_PSEUDO_B3SUM)) \
	$(BIN_blkid_$(BUILD_PSEUDO_BLKID)) \
	$(BIN_lsblk_$(BUILD_PSEUDO_LSBLK)) \
	$(BIN_fdisk_$(BUILD_PSEUDO_FDISK)) \
	$(BIN_sync_$(BUILD_PSEUDO_SYNC)) \
	$(BIN_diff3_$(BUILD_PSEUDO_DIFF3)) \
	$(BIN_ar_$(BUILD_DEV_AR)) \
	$(BIN_as_$(BUILD_DEV_CC)) \
	$(BIN_ld_$(BUILD_DEV_LD)) \
	$(BIN_cc_$(BUILD_DEV_CC)) \
	$(BIN_dmesg_$(BUILD_PSEUDO_DMESG)) \
	$(BIN_fallocate_$(BUILD_PSEUDO_FALLOCATE)) \
	$(BIN_free_$(BUILD_PSEUDO_FREE)) \
	$(BIN_pidof_$(BUILD_PSEUDO_PIDOF)) \
	$(BIN_pwdx_$(BUILD_PSEUDO_PWDX)) \
	$(BIN_uptime_$(BUILD_PSEUDO_UPTIME))

OBJ = $(LIBUTFOBJ) $(LIBUTILOBJ) $(MAKEOBJ)

all: $(LIB) $(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN)

$(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL): $(LIB)

$(OBJ) $(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL): $(HDR)

.o:
	$(CC) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

.c:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

cmd/posix/bc.c: cmd/posix/bc.y
	$(YACC) -d -o $@ $<

cmd/posix/bc: cmd/posix/bc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ cmd/posix/bc.c $(LIB) $(LDLIBS)

$(MAKEOBJ): cmd/posix/make/make.h

cmd/posix/make/make: $(MAKEOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(MAKEOBJ) $(LIB) $(LDLIBS)

$(DIFFOBJ): cmd/posix/diff/diff.h cmd/posix/diff/pr.h cmd/posix/diff/xmalloc.h

cmd/posix/diff/diff: $(DIFFOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(DIFFOBJ) $(LIB) $(LDLIBS) -lm

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
	scripts/getconf.sh > $@ || { rm -f $@; exit 1; }

box: $(LIB)
	CC='$(CC)' CPPFLAGS='$(CPPFLAGS)' CFLAGS='$(CFLAGS)' \
	LDFLAGS='$(LDFLAGS)' LDLIBS='$(LDLIBS)' OBJCOPY='$(OBJCOPY)' \
	scripts/mkbox

.PHONY: man clean

scripts/mkman/mkman: scripts/mkman/main.go scripts/mkman/page.go scripts/mkman/parse.go scripts/mkman/mdoc.go
	cd scripts/mkman && go build -o mkman .

man: scripts/mkman/mkman
	@for src in $(POSIX_BIN_ALL:=.c) $(PSEUDO_BIN_ALL:=.c); do \
		if [ -f "$$src" ] && grep -qE '!man|\?man' "$$src"; then \
			base=$$(basename $$src .c); \
			mkdir -p man/man1; \
			scripts/mkman/mkman -fmt mdoc -config config.mk -section 1 "$$src" > "man/man1/$$base.1"; \
			scripts/mkman/mkman -fmt txt -config config.mk -section 1 "$$src" > "man/man1/$$base.1.txt"; \
		fi; \
	done
	@for src in $(LINUX_BIN_ALL:=.c) $(NET_BIN_ALL:=.c) $(XSI_BIN_ALL:=.c); do \
		if [ -f "$$src" ] && grep -qE '!man|\?man' "$$src"; then \
			base=$$(basename $$src .c); \
			mkdir -p man/man8; \
			scripts/mkman/mkman -fmt mdoc -config config.mk -section 8 "$$src" > "man/man8/$$base.8"; \
			scripts/mkman/mkman -fmt txt -config config.mk -section 8 "$$src" > "man/man8/$$base.8.txt"; \
		fi; \
	done

clean:
	rm -f shared/libutf/*.o shared/libutil/*.o shared/libredline/*.o
	rm -f cmd/posix/*.o cmd/posix/diff/*.o cmd/posix/make/*.o cmd/posix/awk/*.o cmd/posix/sh/*.o
	rm -f cmd/linux/*.o cmd/net/*.o cmd/xsi/*.o cmd/pseudo/*.o
	rm -f cmd/extra/*.o cmd/extra/diff3/*.o cmd/dev/ar/*.o cmd/dev/ld/*.o cmd/dev/cc/*.o cmd/dev/as/*.o cmd/dev/xcutil/*.o
	rm -f $(POSIX_BIN_ALL) $(LINUX_BIN_ALL) $(NET_BIN_ALL) $(XSI_BIN_ALL) $(PSEUDO_BIN_ALL) $(LIB)
	rm -f cmd/posix/make/make cmd/posix/getconf.h cmd/posix/bc.c
	rm -f cmd/posix/awk/awk cmd/posix/awk/maketab cmd/posix/awk/awkgram.tab.c cmd/posix/awk/awkgram.tab.h cmd/posix/awk/proctab.c
	rm -f cmd/posix/sh/sh cmd/posix/sh/mknodes cmd/posix/sh/mksyntax
	rm -f cmd/posix/sh/syntax.c cmd/posix/sh/syntax.h cmd/posix/sh/nodes.c cmd/posix/sh/nodes.h cmd/posix/sh/builtins.c cmd/posix/sh/builtins.h cmd/posix/sh/token.h
	rm -f cmd/dev/cc/cc1 cmd/dev/cc/cpp cmd/dev/as/as cmd/dev/ld/ld cmd/dev/ar/ar shared/libaruuelf.so
	rm -f cmd/dev/config.h cmd/dev/cc/config.h cmd/dev/version.h
	rm -rf aruu-box .box man/man1 man/man8 scripts/mkman/mkman

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

cmd/posix/awk/awkgram.tab.c cmd/posix/awk/awkgram.tab.h: cmd/posix/awk/awkgram.y
	$(YACC) -d -o cmd/posix/awk/awkgram.tab.c cmd/posix/awk/awkgram.y
	@if [ ! -f cmd/posix/awk/awkgram.tab.h ]; then \
		if [ -f y.tab.h ]; then mv y.tab.h cmd/posix/awk/awkgram.tab.h; \
		elif [ -f cmd/posix/awk/y.tab.h ]; then mv cmd/posix/awk/y.tab.h cmd/posix/awk/awkgram.tab.h; fi; \
	fi

cmd/posix/awk/maketab: cmd/posix/awk/maketab.c cmd/posix/awk/awkgram.tab.h
	$(CC) $(CFLAGS) -o $@ cmd/posix/awk/maketab.c

cmd/posix/awk/proctab.c: cmd/posix/awk/maketab
	cmd/posix/awk/maketab cmd/posix/awk/awkgram.tab.h > $@

$(AWKOBJ): cmd/posix/awk/awk.h cmd/posix/awk/awkgram.tab.h cmd/posix/awk/proto.h

cmd/posix/awk/%.o: cmd/posix/awk/%.c
	$(CC) $(CPPFLAGS) -Icmd/posix/awk $(CFLAGS) -o $@ -c $<

cmd/posix/awk/awk: $(AWKOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(AWKOBJ) $(LIB) $(LDLIBS) -lm

cmd/posix/sh/mknodes: cmd/posix/sh/mknodes.c
	$(CC) $(CFLAGS) -o $@ cmd/posix/sh/mknodes.c

cmd/posix/sh/mksyntax: cmd/posix/sh/mksyntax.c
	$(CC) $(CPPFLAGS) -Icmd/posix/sh $(CFLAGS) -o $@ cmd/posix/sh/mksyntax.c

cmd/posix/sh/syntax.c cmd/posix/sh/syntax.h: cmd/posix/sh/mksyntax
	cd cmd/posix/sh && ./mksyntax

cmd/posix/sh/nodes.c cmd/posix/sh/nodes.h: cmd/posix/sh/mknodes cmd/posix/sh/nodetypes cmd/posix/sh/nodes.c.pat
	cd cmd/posix/sh && ./mknodes nodetypes nodes.c.pat

cmd/posix/sh/builtins.c cmd/posix/sh/builtins.h: cmd/posix/sh/mkbuiltins cmd/posix/sh/builtins.def cmd/posix/sh/shell.h
	cd cmd/posix/sh && sh mkbuiltins .

cmd/posix/sh/token.h: cmd/posix/sh/mktokens
	cd cmd/posix/sh && sh mktokens

$(SHOBJ): $(SH_GENHDRS)

cmd/posix/sh/%.o: cmd/posix/sh/%.c
	$(CC) $(CPPFLAGS) -DSHELL -Icmd/posix/sh $(CFLAGS) -o $@ -c $<

cmd/posix/sh/sh: $(SHOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(SHOBJ) $(LIB) $(LDLIBS)

cmd/extra/diff3/diff3: cmd/extra/diff3/diff3.o cmd/posix/diff/xmalloc.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/extra/diff3/diff3.o cmd/posix/diff/xmalloc.o $(LIB) $(LDLIBS)

cmd/net/wget: cmd/net/wget.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/net/wget.o $(LIB) $(LDLIBS) $(LDLIBS_TLS)

cmd/dev/ar/ar: cmd/dev/ar/ar.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/dev/ar/ar.o $(LIB) $(LDLIBS)

cmd/dev/ar/%.o: cmd/dev/ar/%.c
	$(CC) $(CPPFLAGS) -Icmd/dev/ar $(CFLAGS) -o $@ -c $<

cmd/dev/xcutil/%.o: cmd/dev/xcutil/%.c
	$(CC) -Icmd/dev/xcutil $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

LD_OBJ =\
	cmd/dev/xcutil/util.o\
	cmd/dev/xcutil/table.o\
	cmd/dev/xcutil/elfutil.o\
	cmd/dev/xcutil/archive.o\
	cmd/dev/ld/ld.o\
	cmd/dev/ld/elfobj.o

cmd/dev/ld/%.o: cmd/dev/ld/%.c
	$(CC) -Icmd/dev/xcutil -Icmd/dev/ld $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

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

# headers shared across the assembler sources. listing them as prerequisites
# of every .o stops stale objects when a header changes (inst.h, parse_asm.h),
# which otherwise produced silently-wrong builds
AS_HDRS =\
	cmd/dev/as/parse_asm.h\
	cmd/dev/as/ir_asm.h\
	cmd/dev/as/as_util.h\
	cmd/dev/as/asm_code.h\
	cmd/dev/as/arch/x64/inst.h

cmd/dev/as/%.o: cmd/dev/as/%.c $(AS_HDRS) cmd/dev/config.h
	$(CC) -Icmd/dev/xcutil -Icmd/dev/as $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

cmd/dev/as/arch/x64/%.o: cmd/dev/as/arch/x64/%.c $(AS_HDRS) cmd/dev/config.h
	$(CC) -Icmd/dev/xcutil -Icmd/dev/as -Icmd/dev/as/arch/x64 $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

cmd/dev/as/as: $(AS_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(AS_OBJ) $(LIB) $(LDLIBS)

cmd/dev/cc/driver.o: cmd/dev/cc/driver.c cmd/dev/cc/config.h
	$(CC) -Icmd/dev/cc $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

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

cmd/dev/cc/%.o: cmd/dev/cc/%.c
	$(CC) -Icmd/dev/cc $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

cmd/dev/cc/cc1: $(CC1_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(CC1_OBJ) $(LIB) $(LDLIBS)

cmd/dev/cc/cpp: cmd/dev/cc/cpp.o $(CPP_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ cmd/dev/cc/cpp.o $(CPP_OBJ) $(LIB) $(LDLIBS)

cmd/dev/config.h cmd/dev/cc/config.h cmd/dev/version.h: cmd/dev/configure
	sh cmd/dev/configure

$(AS_OBJ) $(LD_OBJ) $(CC1_OBJ) $(CPP_OBJ) cmd/dev/ar/ar.o cmd/dev/cc/cpp.o: cmd/dev/config.h
