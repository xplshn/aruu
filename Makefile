CC       = cc -static
CFLAGS   = -std=c99 -Wall -Wextra -pedantic

BACKEND  ?= auto

CPPFLAGS ?= -Ishared -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
            -DFEATURE_NOFORK=0 -DFEATURE_NOEXEC=0

NINQU_LIB_SRC =\
        shared/libutil/ealloc.c\
        shared/libutil/eprintf.c\
        shared/libutil/strlcpy.c\
        shared/libutil/strlcat.c\
        shared/libutil/strtonum.c\
        shared/libutil/fshut.c\
        shared/libutil/mkdirp.c\
        shared/libutil/recurse_dir.c\
        shared/libutil/wexec.c

NINQU_SRC =\
        cmd/dev/ninqu/main.c\
        cmd/dev/ninqu/strlist.c\
        cmd/dev/ninqu/kv.c\
        cmd/dev/ninqu/sexpr.c\
        cmd/dev/ninqu/gate.c\
        cmd/dev/ninqu/registry.c\
        cmd/dev/ninqu/fs.c\
        cmd/dev/ninqu/exec.c\
        cmd/dev/ninqu/backend.c\
        cmd/dev/ninqu/parse.c\
        cmd/dev/ninqu/expand.c\
        cmd/dev/ninqu/schedule.c\
        cmd/dev/ninqu/query.c\
        cmd/dev/ninqu/resolve.c\
        cmd/dev/ninqu/ninja.c

NINQU ?= cmd/dev/ninqu/ninqu
# no $(filter -j%,$(MAKEFLAGS)) equivalent here (GNUmakefile's own
# trick): pass ninqu's own parallelism directly, "make JOBS=-j8 box"
JOBS ?=

# every real goal ninqu.rules exposes, listed once instead of
# GNUmakefile's own "%: FORCE" catch-all. a new top-level group needs
# a matching name added here too
.PHONY: all default clean box dev man install install-base uninstall \
        remove fmt fmt-all lib posix linux net xsi pseudo extra codegen

$(NINQU): $(NINQU_SRC) cmd/dev/ninqu/ninqu.h scripts/mk/config.set $(NINQU_LIB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(NINQU_SRC) $(NINQU_LIB_SRC)

scripts/mk/config.set: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh config

all default box dev man install install-base uninstall remove \
fmt fmt-all lib posix linux net xsi pseudo extra codegen: $(NINQU)
	$(NINQU) $(JOBS) $@

clean: $(NINQU)
	$(NINQU) clean
	rm -f $(NINQU) build.ninja
