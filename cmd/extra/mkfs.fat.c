/*	$OpenBSD: newfs_msdos.c,v 1.30 2025/09/17 16:07:57 deraadt Exp $	*/

/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(__OpenBSD__) && !defined(ARUU_FORCE_POSIX)
#undef _POSIX_C_SOURCE
#endif

#include "diskutil.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#if defined(__OpenBSD__) && !defined(ARUU_FORCE_POSIX)
#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#elif defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <linux/hdreg.h>
#include <sys/ioctl.h>
#endif

#ifndef howmany
#define howmany(x, y) (((x) + (y) - 1) / (y))
#endif
#ifndef powerof2
#define powerof2(x) ((x) && !((x) & ((x) - 1)))
#endif

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

#define MAXU16	  0xffff	/* maximum unsigned 16-bit quantity */
#define BPN	  4		/* bits per nibble */
#define NPB	  2		/* nibbles per byte */

#define DOSMAGIC  0xaa55	/* DOS magic number */
#define MINBPS	  512		/* minimum bytes per sector */
#define MAXSPC	  128		/* maximum sectors per cluster */
#define MAXNFT	  16		/* maximum number of FATs */
#define DEFBLK	  4096		/* default block size */
#define DEFBLK16  2048		/* default block size FAT16 */
#define DEFRDE	  512		/* default root directory entries */
#define RESFTE	  2		/* reserved FAT entries */
#define MINCLS12  1		/* minimum FAT12 clusters */
#define MINCLS16  0xff5		/* minimum FAT16 clusters */
#define MINCLS32  0xfff5	/* minimum FAT32 clusters */
#define MAXCLS12  0xff4		/* maximum FAT12 clusters */
#define MAXCLS16  0xfff4	/* maximum FAT16 clusters */
#define MAXCLS32  0xffffff4	/* maximum FAT32 clusters */

#define mincls(fat)  ((fat) == 12 ? MINCLS12 :	\
		      (fat) == 16 ? MINCLS16 :	\
				    MINCLS32)

#define maxcls(fat)  ((fat) == 12 ? MAXCLS12 :	\
		      (fat) == 16 ? MAXCLS16 :	\
				    MAXCLS32)

#define mk1(p, x)				\
    (p) = (uint8_t)(x)

#define mk2(p, x)				\
    (p)[0] = (uint8_t)(x),			\
    (p)[1] = (uint8_t)((x) >> 010)

#define mk4(p, x)				\
    (p)[0] = (uint8_t)(x),			\
    (p)[1] = (uint8_t)((x) >> 010),		\
    (p)[2] = (uint8_t)((x) >> 020),		\
    (p)[3] = (uint8_t)((x) >> 030)

#define argto1(arg, lo, msg)  argtou(arg, lo, 0xff, msg)
#define argto2(arg, lo, msg)  argtou(arg, lo, 0xffff, msg)
#define argto4(arg, lo, msg)  argtou(arg, lo, 0xffffffff, msg)
#define argtox(arg, lo, msg)  argtou(arg, lo, UINT_MAX, msg)

struct bs {
    uint8_t jmp[3];		/* bootstrap entry point */
    uint8_t oem[8];		/* OEM name and version */
};

struct bsbpb {
    uint8_t bps[2];		/* bytes per sector */
    uint8_t spc;		/* sectors per cluster */
    uint8_t res[2];		/* reserved sectors */
    uint8_t nft;		/* number of FATs */
    uint8_t rde[2];		/* root directory entries */
    uint8_t sec[2];		/* total sectors */
    uint8_t mid;		/* media descriptor */
    uint8_t spf[2];		/* sectors per FAT */
    uint8_t spt[2];		/* sectors per track */
    uint8_t hds[2];		/* drive heads */
    uint8_t hid[4];		/* hidden sectors */
    uint8_t bsec[4];		/* big total sectors */
};

struct bsxbpb {
    uint8_t bspf[4];		/* big sectors per FAT */
    uint8_t xflg[2];		/* FAT control flags */
    uint8_t vers[2];		/* file system version */
    uint8_t rdcl[4];		/* root directory start cluster */
    uint8_t infs[2];		/* file system info sector */
    uint8_t bkbs[2];		/* backup boot sector */
    uint8_t rsvd[12];		/* reserved */
};

struct bsx {
    uint8_t drv;		/* drive number */
    uint8_t rsvd;		/* reserved */
    uint8_t sig;		/* extended boot signature */
    uint8_t volid[4];		/* volume ID number */
    uint8_t label[11];		/* volume label */
    uint8_t type[8];		/* file system type */
};

struct de {
    uint8_t namext[11];	/* name and extension */
    uint8_t attr;		/* attributes */
    uint8_t rsvd[10];		/* reserved */
    uint8_t time[2];		/* creation time */
    uint8_t date[2];		/* creation date */
    uint8_t clus[2];		/* starting cluster */
    uint8_t size[4];		/* size */
};

struct bpb {
    unsigned int bps;			/* bytes per sector */
    unsigned int spc;			/* sectors per cluster */
    unsigned int res;			/* reserved sectors */
    unsigned int nft;			/* number of FATs */
    unsigned int rde;			/* root directory entries */
    unsigned int sec;			/* total sectors */
    unsigned int mid;			/* media descriptor */
    unsigned int spf;			/* sectors per FAT */
    unsigned int spt;			/* sectors per track */
    unsigned int hds;			/* drive heads */
    unsigned int hid;			/* hidden sectors */
    unsigned int bsec;			/* big total sectors */
    unsigned int bspf;			/* big sectors per FAT */
    unsigned int rdcl; 		/* root directory start cluster */
    unsigned int infs; 		/* file system info sector */
    unsigned int bkbs; 		/* backup boot sector */
};

static struct {
    const char *name;
    struct bpb bpb;
} stdfmt[] = {
    {"160",  {512, 1, 1, 2,  64,  320, 0xfe, 1,  8, 1, 0, 0, 0, 0, 0, 0}},
    {"180",  {512, 1, 1, 2,  64,  360, 0xfc, 2,  9, 1, 0, 0, 0, 0, 0, 0}},
    {"320",  {512, 2, 1, 2, 112,  640, 0xff, 1,  8, 2, 0, 0, 0, 0, 0, 0}},
    {"360",  {512, 2, 1, 2, 112,  720, 0xfd, 2,  9, 2, 0, 0, 0, 0, 0, 0}},
    {"720",  {512, 2, 1, 2, 112, 1440, 0xf9, 3,  9, 2, 0, 0, 0, 0, 0, 0}},
    {"1200", {512, 1, 1, 2, 224, 2400, 0xf9, 7, 15, 2, 0, 0, 0, 0, 0, 0}},
    {"1440", {512, 1, 1, 2, 224, 2880, 0xf0, 9, 18, 2, 0, 0, 0, 0, 0, 0}},
    {"2880", {512, 2, 1, 2, 240, 5760, 0xf0, 9, 36, 2, 0, 0, 0, 0, 0, 0}}
};

static uint8_t bootcode[] = {
    0xfa,			/* cli		    */
    0x31, 0xc0, 		/* xor	   ax,ax    */
    0x8e, 0xd0, 		/* mov	   ss,ax    */
    0xbc, 0x00, 0x7c,		/* mov	   sp,7c00h */
    0xfb,			/* sti		    */
    0x8e, 0xd8, 		/* mov	   ds,ax    */
    0xe8, 0x00, 0x00,		/* call    $ + 3    */
    0x5e,			/* pop	   si	    */
    0x83, 0xc6, 0x19,		/* add	   si,+19h  */
    0xbb, 0x07, 0x00,		/* mov	   bx,0007h */
    0xfc,			/* cld		    */
    0xac,			/* lodsb	    */
    0x84, 0xc0, 		/* test    al,al    */
    0x74, 0x06, 		/* jz	   $ + 8    */
    0xb4, 0x0e, 		/* mov	   ah,0eh   */
    0xcd, 0x10, 		/* int	   10h	    */
    0xeb, 0xf5, 		/* jmp	   $ - 9    */
    0x30, 0xe4, 		/* xor	   ah,ah    */
    0xcd, 0x16, 		/* int	   16h	    */
    0xcd, 0x19, 		/* int	   19h	    */
    0x0d, 0x0a,
    'N', 'o', 'n', '-', 's', 'y', 's', 't',
    'e', 'm', ' ', 'd', 'i', 's', 'k',
    0x0d, 0x0a,
    'P', 'r', 'e', 's', 's', ' ', 'a', 'n',
    'y', ' ', 'k', 'e', 'y', ' ', 't', 'o',
    ' ', 'r', 'e', 'b', 'o', 'o', 't',
    0x0d, 0x0a,
    0
};

static void gettarget(int, const char *, int, struct bpb *);
static void getstdfmt(const char *, struct bpb *);
static void print_bpb(struct bpb *);
static unsigned int argtou(const char *, unsigned int, unsigned int, const char *);
static int oklabel(const char *);
static void mklabel(uint8_t *, const char *);
static void setstr(uint8_t *, const char *, size_t);
static void __attribute__((noreturn)) usage(void);

/*
 * Construct a FAT12, FAT16, or FAT32 file system.
 */
int
main(int argc, char *argv[])
{
    const char *opt_B = NULL, *opt_L = NULL, *opt_O = NULL, *opt_f = NULL;
    unsigned int opt_F = 0, opt_I = 0, opt_S = 0, opt_a = 0, opt_b = 0, opt_c = 0, opt_e = 0;
    unsigned int opt_h = 0, opt_i = 0, opt_k = 0, opt_m = 0, opt_n = 0, opt_o = 0, opt_r = 0;
    unsigned int opt_s = 0, opt_u = 0;
    int opt_N = 0, Iflag = 0, mflag = 0, oflag = 0;
    char buf[PATH_MAX];
    struct stat sb;
    struct timeval tv;
    struct bpb bpb;
    struct tm *tm;
    struct bs *bs;
    struct bsbpb *bsbpb;
    struct bsxbpb *bsxbpb;
    struct bsx *bsx;
    struct de *de;
    uint8_t *img;
    const char *bname, *fname, *arg;
    ssize_t n;
    time_t now;
    unsigned int fat, bss, rds, cls, dir, lsn, x, x1, x2;
    uint64_t capacity, bits;
    int fd, fd1, flags;

#if defined(__OpenBSD__) && !defined(ARUU_FORCE_POSIX)
    if (pledge("stdio rpath wpath disklabel", NULL) == -1)
	eprintf("pledge:");
#endif

    ARGBEGIN {
	case 'N':
	    opt_N = 1;
	    break;
	case 'B':
	    opt_B = EARGF(usage());
	    break;
	case 'F':
	    arg = EARGF(usage());
	    opt_F = strtonum(arg, 1, INT_MAX, NULL);
	    if (!(opt_F == 12 || opt_F == 16 || opt_F == 32))
		eprintf("%s: bad FAT type\n", arg);
	    break;
	case 'I':
	    opt_I = argto4(EARGF(usage()), 0, "volume ID");
	    Iflag = 1;
	    break;
	case 'n':
	case 'L':
	    arg = EARGF(usage());
	    if (!oklabel(arg))
		eprintf("%s: bad volume label\n", arg);
	    opt_L = arg;
	    break;
	case 'O':
	    arg = EARGF(usage());
	    if (strlen(arg) > 8)
		eprintf("%s: bad OEM string\n", arg);
	    opt_O = arg;
	    break;
	case 'S':
	    opt_S = argto2(EARGF(usage()), 1, "bytes/sector");
	    break;
	case 'a':
	    opt_a = argto4(EARGF(usage()), 1, "sectors/FAT");
	    break;
	case 'b':
	    opt_b = argtox(EARGF(usage()), 1, "block size");
	    opt_c = 0;
	    break;
	case 'c':
	    opt_c = argto1(EARGF(usage()), 1, "sectors/cluster");
	    opt_b = 0;
	    break;
	case 'e':
	    opt_e = argto2(EARGF(usage()), 1, "directory entries");
	    break;
	case 'f':
	    opt_f = EARGF(usage());
	    break;
	case 'h':
	    opt_h = argto2(EARGF(usage()), 1, "drive heads");
	    break;
	case 'i':
	    opt_i = argto2(EARGF(usage()), 1, "info sector");
	    break;
	case 'k':
	    opt_k = argto2(EARGF(usage()), 1, "backup sector");
	    break;
	case 'm':
	    opt_m = argto1(EARGF(usage()), 0, "media descriptor");
	    mflag = 1;
	    break;
	case 'A':
	    opt_n = argto1(EARGF(usage()), 1, "number of FATs");
	    break;
	case 'o':
	    opt_o = argto4(EARGF(usage()), 0, "hidden sectors");
	    oflag = 1;
	    break;
	case 'q':			/* Compat with newfs -q */
	    break;
	case 'r':
	    opt_r = argto2(EARGF(usage()), 1, "reserved sectors");
	    break;
	case 's':
	    opt_s = argto4(EARGF(usage()), 1, "file system size");
	    break;
	case 't':			/* Compat with newfs -t */
	    (void)EARGF(usage());
	    break;
	case 'u':
	    opt_u = argto2(EARGF(usage()), 1, "sectors/track");
	    break;
	default:
	    usage();
    } ARGEND
    if (argc != 1)
	usage();
    fname = argv[0];
    flags = opt_N ? O_RDONLY : O_RDWR;
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
    /* The kernel refuses a writable claim on a mounted block device. */
    if (!opt_N)
	flags |= O_EXCL;
#endif
    fd = open(fname, flags | O_NONBLOCK);
    if (fd < 0)
	eprintf("%s:", fname);
    memset(&bpb, 0, sizeof(bpb));
    if (opt_f) {
	getstdfmt(opt_f, &bpb);
	bpb.bsec = bpb.sec;
	bpb.sec = 0;
	bpb.bspf = bpb.spf;
	bpb.spf = 0;
    }
    if (opt_h)
	bpb.hds = opt_h;
    if (opt_u)
	bpb.spt = opt_u;
    if (opt_S)
	bpb.bps = opt_S;
    if (opt_s)
	bpb.bsec = opt_s;
    gettarget(fd, fname, opt_N, &bpb);
    if (oflag)
	bpb.hid = opt_o;
    if (!powerof2(bpb.bps))
	eprintf("bytes/sector (%u) is not a power of 2\n", bpb.bps);
    if (bpb.bps < MINBPS)
	eprintf("bytes/sector (%u) is too small; minimum is %u\n",
	     bpb.bps, MINBPS);
    if (!(fat = opt_F)) {
	if (opt_f)
	    fat = 12;
	else if (!opt_e && (opt_i || opt_k))
	    fat = 32;
    }
    if ((fat == 32 && opt_e) || (fat != 32 && (opt_i || opt_k)))
	eprintf("-%c is not a legal FAT%s option\n",
	     fat == 32 ? 'e' : opt_i ? 'i' : 'k',
	     fat == 32 ? "32" : "12/16");
    if (opt_f && fat == 32)
	bpb.rde = 0;
    if (opt_b) {
	if (!powerof2(opt_b))
	    eprintf("block size (%u) is not a power of 2\n", opt_b);
	if (opt_b < bpb.bps)
	    eprintf("block size (%u) is too small; minimum is %u\n",
		 opt_b, bpb.bps);
	if (opt_b > bpb.bps * MAXSPC)
	    eprintf("block size (%u) is too large; maximum is %u\n",
		 opt_b, bpb.bps * MAXSPC);
	bpb.spc = opt_b / bpb.bps;
    }
    if (opt_c) {
	if (!powerof2(opt_c))
	    eprintf("sectors/cluster (%u) is not a power of 2\n", opt_c);
	bpb.spc = opt_c;
    }
    if (opt_r)
	bpb.res = opt_r;
    if (opt_n) {
	if (opt_n > MAXNFT)
	    eprintf("number of FATs (%u) is too large; maximum is %u\n",
		 opt_n, MAXNFT);
	bpb.nft = opt_n;
    }
    if (opt_e)
	bpb.rde = opt_e;
    if (mflag) {
	if (opt_m < 0xf0)
	    eprintf("illegal media descriptor (%#x)\n", opt_m);
	bpb.mid = opt_m;
    }
    if (opt_a)
	bpb.bspf = opt_a;
    if (opt_i)
	bpb.infs = opt_i;
    if (opt_k)
	bpb.bkbs = opt_k;
    bss = 1;
    bname = NULL;
    fd1 = -1;
    if (opt_B) {
	bname = opt_B;
	if (!strchr(bname, '/')) {
	    if (snprintf(buf, sizeof buf, "/boot/%s", bname) >= (int)sizeof buf)
		eprintf("bootstrap path too long\n");
	    bname = buf;
	}
	if ((fd1 = open(bname, O_RDONLY)) == -1 || fstat(fd1, &sb))
	    eprintf("%s:", bname);
	if (!S_ISREG(sb.st_mode) || sb.st_size % bpb.bps ||
	    sb.st_size < bpb.bps || sb.st_size > bpb.bps * MAXU16)
	    eprintf("%s: inappropriate file type or format\n", bname);
	bss = sb.st_size / bpb.bps;
    }
    if (!bpb.nft)
	bpb.nft = 2;
    if (!fat) {
	if (bpb.bsec < (bpb.res ? bpb.res : bss) +
	    howmany((RESFTE + (bpb.spc ? MINCLS16 : MAXCLS12 + 1)) *
		    ((bpb.spc ? 16 : 12) / BPN), bpb.bps * NPB) *
	    bpb.nft +
	    howmany(bpb.rde ? bpb.rde : DEFRDE,
		    bpb.bps / sizeof(struct de)) +
	    (bpb.spc ? MINCLS16 : MAXCLS12 + 1) *
	    (bpb.spc ? bpb.spc : howmany(DEFBLK, bpb.bps)))
	    fat = 12;
	else if (bpb.rde || bpb.bsec <
		 (bpb.res ? bpb.res : bss) +
		 howmany((RESFTE + MAXCLS16) * 2, bpb.bps) * bpb.nft +
		 howmany(DEFRDE, bpb.bps / sizeof(struct de)) +
		 (MAXCLS16 + 1) *
		 (bpb.spc ? bpb.spc : howmany(8192, bpb.bps)))
	    fat = 16;
	else
	    fat = 32;
    }
    x = bss;
    if (fat == 32) {
	if (!bpb.infs) {
	    if (x == MAXU16 || x == bpb.bkbs)
		eprintf("no room for info sector\n");
	    bpb.infs = x;
	}
	if (bpb.infs != MAXU16 && bpb.infs < bss)
	    eprintf("info sector would overwrite bootstrap\n");
	if (bpb.infs != MAXU16 && x <= bpb.infs)
	    x = bpb.infs + 1;
	if (!bpb.bkbs) {
	    if (x == MAXU16)
		eprintf("no room for backup sector\n");
	    bpb.bkbs = x;
	} else if (bpb.bkbs != MAXU16 && bpb.bkbs == bpb.infs)
	    eprintf("backup sector would overwrite info sector\n");
	if (bpb.bkbs != MAXU16) {
	    if (bpb.bkbs < bss || (bpb.infs != MAXU16 &&
		bpb.infs >= bpb.bkbs && bpb.infs < bpb.bkbs + bss))
		eprintf("overlapping backup bootstrap\n");
	    x = MAXIMUM(x, bpb.bkbs + bss);
	    if (bpb.infs != MAXU16)
		x = MAXIMUM(x, bpb.bkbs + bpb.infs + 1);
	    if (x > MAXU16)
		eprintf("backup exceeds reserved area\n");
	}
    }
    if (!bpb.res)
	bpb.res = fat == 32 ? MAXIMUM(x, MAXIMUM(16384 / bpb.bps, 4)) : x;
    else if (bpb.res < x)
	eprintf("too few reserved sectors\n");
    if (fat != 32) {
	if (!bpb.rde)
	    bpb.rde = MAXIMUM(DEFRDE, bpb.bps / sizeof(struct de));
	if (bpb.rde % (bpb.bps / sizeof(struct de)))
	    eprintf("root directory must occupy whole sectors\n");
    }
    rds = howmany(bpb.rde, bpb.bps / sizeof(struct de));
    if (!bpb.spc) {
	unsigned int minfat = howmany((RESFTE + mincls(fat)) * (fat / BPN),
				      bpb.bps * NPB);
	uint64_t metadata = bpb.res + rds +
	    (uint64_t)bpb.nft * (bpb.bspf ? bpb.bspf : minfat);

	/* A small EFI partition may need clusters below the default 4 KiB. */
	bpb.spc = howmany(fat == 16 ? DEFBLK16 : DEFBLK, bpb.bps);
	while (bpb.spc > 1 && metadata + (uint64_t)mincls(fat) * bpb.spc > bpb.bsec)
	    bpb.spc >>= 1;
	while (bpb.spc < MAXSPC &&
	     bpb.res +
	     howmany((RESFTE + maxcls(fat)) * (fat / BPN),
		     bpb.bps * NPB) * bpb.nft +
	     rds +
	     (uint64_t)(maxcls(fat) + 1) * bpb.spc <= bpb.bsec)
	    bpb.spc <<= 1;
    }
    if (fat != 32 && bpb.bspf > MAXU16)
	eprintf("too many sectors/FAT for FAT12/16\n");
    x1 = bpb.res + rds;
    if (x1 >= bpb.bsec)
	eprintf("metadata exceeds filesystem size\n");
    /* The FAT must cover the data clusters plus its two reserved entries.
     * Each sector added to a FAT also removes one sector per copy from data. */
    bits = ((uint64_t)bpb.bsec - x1 + RESFTE * bpb.spc) * fat;
    x = bpb.spc * bpb.bps * 8 + bpb.nft * fat;
    x2 = howmany(bits, x);
    x = howmany((uint64_t)(RESFTE + maxcls(fat)) * fat, bpb.bps * 8);
    if (!bpb.bspf)
	bpb.bspf = MINIMUM(x2, x);
    if (x1 + (uint64_t)bpb.bspf * bpb.nft >= bpb.bsec)
	eprintf("metadata exceeds filesystem size\n");
    x1 += bpb.bspf * bpb.nft;
    cls = (bpb.bsec - x1) / bpb.spc;
    capacity = (uint64_t)bpb.bspf * bpb.bps * 8 / fat - RESFTE;
    capacity = MINIMUM(capacity, maxcls(fat));
    if (cls > capacity) {
	cls = capacity;
	bpb.bsec = x1 + cls * bpb.spc;
	weprintf("warning: FAT limits filesystem to %u sectors\n", bpb.bsec);
    }
    if (cls < mincls(fat))
	eprintf("%u clusters too few for FAT%u, need %u\n", cls, fat, mincls(fat));
    printf("%s: %u sector%s in %u FAT%u cluster%s "
	   "(%u bytes/cluster)\n", fname, cls * bpb.spc,
	   cls * bpb.spc == 1 ? "" : "s", cls, fat,
	   cls == 1 ? "" : "s", bpb.bps * bpb.spc);
    if (!bpb.mid)
	bpb.mid = !bpb.hid ? 0xf0 : 0xf8;
    if (fat == 32)
	bpb.rdcl = RESFTE;
    if ((uint64_t)bpb.hid + bpb.bsec <= MAXU16) {
	bpb.sec = bpb.bsec;
	bpb.bsec = 0;
    }
    if (fat != 32) {
	bpb.spf = bpb.bspf;
	bpb.bspf = 0;
    }
    print_bpb(&bpb);
    if (!opt_N) {
	if (gettimeofday(&tv, NULL) < 0)
	    eprintf("gettimeofday:");
	now = tv.tv_sec;
	if ((tm = localtime(&now)) == NULL)
		eprintf("Invalid time\n");

	img = emalloc(bpb.bps);
	dir = bpb.res + (bpb.spf ? bpb.spf : bpb.bspf) * bpb.nft;
	for (lsn = 0; lsn < dir + (fat == 32 ? bpb.spc : rds); lsn++) {
	    x = lsn;
	    if (opt_B &&
		fat == 32 && bpb.bkbs != MAXU16 &&
		bss <= bpb.bkbs && x >= bpb.bkbs) {
		x -= bpb.bkbs;
		if (!x && lseek(fd1, 0, SEEK_SET))
		    eprintf("%s:", bname);
	    }
	    if (opt_B && x < bss) {
		if ((n = read(fd1, img, bpb.bps)) == -1)
		    eprintf("%s:", bname);
		if (n != bpb.bps)
		    eprintf("%s: can't read sector %u\n", bname, x);
	    } else
		memset(img, 0, bpb.bps);
	    if (!lsn ||
	      (fat == 32 && bpb.bkbs != MAXU16 && lsn == bpb.bkbs)) {
		x1 = sizeof(struct bs);
		bsbpb = (struct bsbpb *)(img + x1);
		mk2(bsbpb->bps, bpb.bps);
		mk1(bsbpb->spc, bpb.spc);
		mk2(bsbpb->res, bpb.res);
		mk1(bsbpb->nft, bpb.nft);
		mk2(bsbpb->rde, bpb.rde);
		mk2(bsbpb->sec, bpb.sec);
		mk1(bsbpb->mid, bpb.mid);
		mk2(bsbpb->spf, bpb.spf);
		mk2(bsbpb->spt, bpb.spt);
		mk2(bsbpb->hds, bpb.hds);
		mk4(bsbpb->hid, bpb.hid);
		mk4(bsbpb->bsec, bpb.bsec);
		x1 += sizeof(struct bsbpb);
		if (fat == 32) {
		    bsxbpb = (struct bsxbpb *)(img + x1);
		    mk4(bsxbpb->bspf, bpb.bspf);
		    mk2(bsxbpb->xflg, 0);
		    mk2(bsxbpb->vers, 0);
		    mk4(bsxbpb->rdcl, bpb.rdcl);
		    mk2(bsxbpb->infs, bpb.infs);
		    mk2(bsxbpb->bkbs, bpb.bkbs);
		    x1 += sizeof(struct bsxbpb);
		}
		bsx = (struct bsx *)(img + x1);
		mk1(bsx->sig, 0x29);
		if (Iflag)
		    x = opt_I;
		else
		    x = (((unsigned int)(1 + tm->tm_mon) << 8 |
			  (unsigned int)tm->tm_mday) +
			 ((unsigned int)tm->tm_sec << 8 |
			  (unsigned int)(tv.tv_usec / 10))) << 16 |
			((unsigned int)(1900 + tm->tm_year) +
			 ((unsigned int)tm->tm_hour << 8 |
			  (unsigned int)tm->tm_min));
		mk4(bsx->volid, x);
		mklabel(bsx->label, opt_L ? opt_L : "NO NAME");
		setstr(bsx->type, fat == 12 ? "FAT12" : fat == 16 ? "FAT16" : "FAT32",
		       sizeof(bsx->type));
		if (!opt_B) {
		    x1 += sizeof(struct bsx);
		    bs = (struct bs *)img;
		    mk1(bs->jmp[0], 0xeb);
		    mk1(bs->jmp[1], x1 - 2);
		    mk1(bs->jmp[2], 0x90);
		    setstr(bs->oem, opt_O ? opt_O : "BSD  4.4",
			   sizeof(bs->oem));
		    memcpy(img + x1, bootcode, sizeof(bootcode));
		    mk2(img + MINBPS - 2, DOSMAGIC);
		}
	    } else if (fat == 32 && bpb.infs != MAXU16 &&
		       (lsn == bpb.infs ||
			(bpb.bkbs != MAXU16 &&
			 lsn == bpb.bkbs + bpb.infs))) {
		mk4(img, 0x41615252);
		mk4(img + MINBPS - 28, 0x61417272);
		mk4(img + MINBPS - 24, cls - 1);
		mk4(img + MINBPS - 20, bpb.rdcl + 1);
		mk2(img + MINBPS - 2, DOSMAGIC);
	    } else if (lsn >= bpb.res && lsn < dir &&
		       !((lsn - bpb.res) %
			 (bpb.spf ? bpb.spf : bpb.bspf))) {
		mk1(img[0], bpb.mid);
		for (x = 1; x < fat * (fat == 32 ? 3 : 2) / 8; x++)
		    mk1(img[x], fat == 32 && x % 4 == 3 ? 0x0f : 0xff);
	    } else if (lsn == dir && opt_L) {
		de = (struct de *)img;
		mklabel(de->namext, opt_L);
		mk1(de->attr, 0x08);
		x = (unsigned int)tm->tm_hour << 11 |
		    (unsigned int)tm->tm_min << 5 |
		    (unsigned int)tm->tm_sec >> 1;
		mk2(de->time, x);
		x = (unsigned int)(tm->tm_year - 80) << 9 |
		    (unsigned int)(tm->tm_mon + 1) << 5 |
		    (unsigned int)tm->tm_mday;
		mk2(de->date, x);
	    }
	    if ((n = writeall(fd, img, bpb.bps)) == -1)
		eprintf("%s:", fname);
	    if (n != bpb.bps)
		eprintf("%s: can't write sector %u\n", fname, lsn);
	}
	free(img);
	if (fsync(fd) < 0)
	    eprintf("%s:", fname);
    }
    if (fd1 >= 0)
	close(fd1);
    if (close(fd) < 0)
	eprintf("%s:", fname);
    return fshut(stdout, "<stdout>");
}

/* Fill the BPB from the opened partition or the length of an existing image. */
static void
gettarget(int fd, const char *path, int dry, struct bpb *bpb)
{
    struct stat st;
    struct BlockDev dev = { .fd = fd };
    uint64_t sectors, start = 0;

    if (fstat(fd, &st) < 0)
	eprintf("%s:", path);
    if (S_ISREG(st.st_mode)) {
	dev.size = st.st_size;
	dev.sec_size = MINBPS;
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
    } else if (S_ISBLK(st.st_mode)) {
	struct hd_geometry geo;

	blockdev_sys_size(&dev);
	if (ioctl(fd, HDIO_GETGEO, &geo) == 0) {
	    start = (uint64_t)geo.start * 512;
	    if (!bpb->spt)
		bpb->spt = geo.sectors;
	    if (!bpb->hds)
		bpb->hds = geo.heads;
	}
#elif defined(__FreeBSD__) && !defined(ARUU_FORCE_POSIX)
    } else if (S_ISCHR(st.st_mode)) {
	blockdev_sys_size(&dev);
#elif defined(__OpenBSD__) && !defined(ARUU_FORCE_POSIX)
    } else if (S_ISCHR(st.st_mode)) {
	struct disklabel dl;
	struct statfs *mp;
	size_t part = DISKPART(st.st_rdev);
	int n;

	if (ioctl(fd, DIOCGDINFO, &dl) < 0)
	    eprintf("%s: disklabel:", path);
	if (part >= dl.d_npartitions)
	    eprintf("%s: partition unavailable\n", path);
	if (!dry) {
	    struct stat raw;
	    char name[PATH_MAX];

	    if (!(n = getmntinfo(&mp, MNT_NOWAIT)))
		eprintf("getmntinfo:");
	    for (; n--; mp++) {
		if (strncmp(mp->f_mntfromname, "/dev/", 5))
		    continue;
		if (snprintf(name, sizeof name, "/dev/r%s", mp->f_mntfromname + 5) >= (int)sizeof name)
		    eprintf("mounted device name too long\n");
		if (stat(name, &raw) == 0 && S_ISCHR(raw.st_mode) &&
		    (raw.st_rdev == st.st_rdev || (part == RAW_PART &&
		    DISKLABELDEV(raw.st_rdev) == DISKLABELDEV(st.st_rdev))))
		    eprintf("%s is mounted on %s\n", path, mp->f_mntonname);
	    }
	}
	dev.sec_size = dl.d_secsize;
	dev.size = DL_GETPSIZE(&dl.d_partitions[part]) * (uint64_t)dl.d_secsize;
	start = DL_GETPOFFSET(&dl.d_partitions[part]) * (uint64_t)dl.d_secsize;
	if (!bpb->spt && dl.d_nsectors <= 63)
	    bpb->spt = dl.d_nsectors;
	if (!bpb->hds && dl.d_ntracks <= MAXU16)
	    bpb->hds = dl.d_ntracks;
#endif
    } else {
	eprintf("%s: unsupported file type\n", path);
    }
    (void)dry;
    if (!dev.sec_size || !dev.size)
	eprintf("%s: cannot determine a nonzero target size\n", path);
    if (!bpb->bps)
	bpb->bps = dev.sec_size;
    if (bpb->bps < dev.sec_size || bpb->bps > MAXU16)
	eprintf("%s: invalid sector size %u\n", path, bpb->bps);
    sectors = dev.size / bpb->bps;
    if (start / bpb->bps <= UINT32_MAX)
	bpb->hid = start / bpb->bps;
    if (!bpb->mid && !S_ISREG(st.st_mode))
	bpb->mid = 0xf8;
    if (!bpb->bsec) {
	if (sectors > UINT32_MAX)
	    eprintf("%s: too many sectors for FAT\n", path);
	bpb->bsec = sectors;
    }
    if (!bpb->bsec || bpb->bsec > sectors)
	eprintf("%s: filesystem does not fit on target\n", path);
    if (!bpb->spt)
	bpb->spt = 63;
    if (!bpb->hds)
	bpb->hds = 255;
}

/*
 * Get a standard format.
 */
static void
getstdfmt(const char *fmt, struct bpb *bpb)
{
    unsigned int x, i;

    x = sizeof(stdfmt) / sizeof(stdfmt[0]);
    for (i = 0; i < x && strcmp(fmt, stdfmt[i].name); i++)
	continue;
    if (i == x)
	eprintf("%s: unknown standard format\n", fmt);
    *bpb = stdfmt[i].bpb;
}

/*
 * Print out BPB values.
 */
static void
print_bpb(struct bpb *bpb)
{
    printf("bps=%u spc=%u res=%u nft=%u", bpb->bps, bpb->spc, bpb->res,
	   bpb->nft);
    if (bpb->rde)
	printf(" rde=%u", bpb->rde);
    if (bpb->sec)
	printf(" sec=%u", bpb->sec);
    printf(" mid=%#x", bpb->mid);
    if (bpb->spf)
	printf(" spf=%u", bpb->spf);
    printf(" spt=%u hds=%u hid=%u", bpb->spt, bpb->hds, bpb->hid);
    if (bpb->bsec)
	printf(" bsec=%u", bpb->bsec);
    if (!bpb->spf) {
	printf(" bspf=%u rdcl=%u", bpb->bspf, bpb->rdcl);
	printf(" infs=");
	printf(bpb->infs == MAXU16 ? "%#x" : "%u", bpb->infs);
	printf(" bkbs=");
	printf(bpb->bkbs == MAXU16 ? "%#x" : "%u", bpb->bkbs);
    }
    printf("\n");
}

/*
 * Convert and check a numeric option argument.
 */
static unsigned int
argtou(const char *arg, unsigned int lo, unsigned int hi, const char *msg)
{
    char *s;
    unsigned long x;

    errno = 0;
    x = strtoul(arg, &s, 0);
    if (errno || !*arg || *arg == '-' || isspace((unsigned char)*arg) || *s || x < lo || x > hi)
	eprintf("%s: bad %s\n", arg, msg);
    return x;
}

/*
 * Check a volume label.
 */
static int
oklabel(const char *src)
{
    int c = 0, i;

    for (i = 0; i <= 11; i++) {
	c = (unsigned char)*src++;
	if (c < ' ' + !i || strchr("\"*+,./:;<=>?[\\]|", c))
	    break;
    }
    return i && !c;
}

/*
 * Make a volume label.
 */
static void
mklabel(uint8_t *dest, const char *src)
{
    int c, i;

    for (i = 0; i < 11; i++) {
	c = *src ? toupper((unsigned char)*src++) : ' ';
	*dest++ = !i && c == 0xe5 ? 5 : c;
    }
}

/*
 * Copy string, padding with spaces.
 */
static void
setstr(uint8_t *dest, const char *src, size_t len)
{
    while (len--)
	*dest++ = *src ? *src++ : ' ';
}

/*
 * Print usage message.
 */
static void __attribute__((noreturn))
usage(void)
{
	eprintf("usage: %s "
	    "[-N] [-a FAT-size] [-B boot] [-b block-size]\n"
	    "\t[-c cluster-size] [-e dirents] [-F FAT-type] [-f format]\n"
	    "\t[-h heads] [-I volid] [-i info] [-k backup] [-L label]\n"
	    "\t[-m media] [-n label] [-A FATs] [-O OEM] [-o hidden] [-r reserved]\n"
	    "\t[-S sector-size] [-s total] [-u track-size] device-or-image\n",
	    argv0);
}
