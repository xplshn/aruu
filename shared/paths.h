/* see LICENSE file for copyright and license details */
#ifndef ARUU_PATHS_H
#define ARUU_PATHS_H

/*
 * portable paths. override at compile time for non-fhs systems:
 * -daruu_path_etc="/config"
 *
 * os-specific paths (procfs, sysfs) are prefixed ARUU_LINUX_PATH_*
 * and only defined under __linux__. a util that reads /proc/modules
 * will fail to compile on BSD because ARUU_LINUX_PATH_PROC is not
 * defined there. this is intentional: the BSD code path must use
 * a different mechanism (sysctl, getmntinfo, etc)
 */

#ifndef ARUU_PATH_ETC
#define ARUU_PATH_ETC "/etc"
#endif

#ifndef ARUU_PATH_VAR
#define ARUU_PATH_VAR "/var"
#endif

#ifndef ARUU_PATH_DEV
#define ARUU_PATH_DEV "/dev"
#endif

#ifndef ARUU_PATH_RUN
#define ARUU_PATH_RUN "/run"
#endif

#ifndef ARUU_PATH_TMP
#define ARUU_PATH_TMP "/tmp"
#endif

#ifndef ARUU_PATH_BIN
#define ARUU_PATH_BIN "/bin"
#endif

#ifndef ARUU_PATH_SBIN
#define ARUU_PATH_SBIN "/sbin"
#endif

#ifndef ARUU_PATH_USR_BIN
#define ARUU_PATH_USR_BIN "/usr/bin"
#endif

#ifndef ARUU_PATH_USR_SBIN
#define ARUU_PATH_USR_SBIN "/usr/sbin"
#endif

#define ARUU_PATH_PASSWD     ARUU_PATH_ETC "/passwd"
#define ARUU_PATH_SHADOW     ARUU_PATH_ETC "/shadow"
#define ARUU_PATH_GROUP      ARUU_PATH_ETC "/group"
#define ARUU_PATH_DEVNULL    ARUU_PATH_DEV "/null"
#define ARUU_PATH_DEVURANDOM ARUU_PATH_DEV "/urandom"
#define ARUU_PATH_DEVRANDOM  ARUU_PATH_DEV "/random"
#define ARUU_PATH_DEVTTY     ARUU_PATH_DEV "/tty"
#define ARUU_PATH_DEVCONSOLE ARUU_PATH_DEV "/console"
#define ARUU_PATH_UTMP       ARUU_PATH_RUN "/utmp"
#define ARUU_PATH_WTMP       ARUU_PATH_VAR "/log/wtmp"
#define ARUU_PATH_BTMP       ARUU_PATH_VAR "/log/btmp"
#define ARUU_PATH_LASTLOG    ARUU_PATH_VAR "/log/lastlog"
#define ARUU_PATH_BSHELL     ARUU_PATH_BIN "/sh"

#ifndef ARUU_PATH_STDPATH
#define ARUU_PATH_STDPATH ARUU_PATH_BIN ":" ARUU_PATH_USR_BIN
#endif

#ifndef ARUU_PATH_DEFPATH
#define ARUU_PATH_DEFPATH ARUU_PATH_BIN ":" ARUU_PATH_USR_BIN
#endif

#if defined(__linux__)
#define ARUU_LINUX_PATH_PROC            "/proc"
#define ARUU_LINUX_PATH_SYS             "/sys"
#define ARUU_LINUX_PATH_PROC_MOUNTS     ARUU_LINUX_PATH_PROC "/mounts"
#define ARUU_LINUX_PATH_PROC_PARTITIONS ARUU_LINUX_PATH_PROC "/partitions"
#define ARUU_LINUX_PATH_PROC_MEMINFO    ARUU_LINUX_PATH_PROC "/meminfo"
#define ARUU_LINUX_PATH_PROC_LOADAVG    ARUU_LINUX_PATH_PROC "/loadavg"
#define ARUU_LINUX_PATH_PROC_CMDLINE    ARUU_LINUX_PATH_PROC "/cmdline"
#define ARUU_LINUX_PATH_PROC_VERSION    ARUU_LINUX_PATH_PROC "/version"
#define ARUU_LINUX_PATH_PROC_MODULES    ARUU_LINUX_PATH_PROC "/modules"
#define ARUU_LINUX_PATH_PROC_SYS        ARUU_LINUX_PATH_PROC "/sys"
#define ARUU_LINUX_PATH_SYS_MODULE      ARUU_LINUX_PATH_SYS "/module"
#define ARUU_LINUX_PATH_SYS_BLOCK       ARUU_LINUX_PATH_SYS "/block"
#define ARUU_LINUX_PATH_SYS_CLASS_NET   ARUU_LINUX_PATH_SYS "/class/net"
#define ARUU_LINUX_PATH_SYS_KERNEL      ARUU_LINUX_PATH_SYS "/kernel"
#define ARUU_LINUX_PATH_SYS_BUS_USB     ARUU_LINUX_PATH_SYS "/bus/usb/devices"
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
/* BSD systems do not have procfs or sysfs. utils that need this data
 * must use sysctl(3), getmntinfo(3), or kvm(3) instead */
#else
#error "paths.h: unsupported platform. define ARUU_LINUX_PATH_* or ARUU_BSD_PATH_* manually"
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define ARUU_BSD_PATH_MASTER_PASSWD ARUU_PATH_ETC "/master.passwd"
#define ARUU_BSD_PATH_PWD_MKDB      ARUU_PATH_USR_SBIN "/pwd_mkdb"
#endif

#endif
