CC       = cc -static
CFLAGS   = -std=c99 -Wall -Wextra -pedantic

# ninqu has to exist before any ninqu.rules build (box, dev, ...) can
# start, so it compiles with BOOTSTRAP_CC. plain $(CC) could resolve to
# an installed aruu cc that cant yet build the tool it is needed for.
# defaults to $(CC), override it alone to pick a bootstrap compiler
BOOTSTRAP_CC ?= $(CC)

BACKEND  ?= auto

CPPFLAGS ?= -Ishared -D_POSIX_C_SOURCE=200809L \
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
# no $(filter -j%,$(MAKEFLAGS)) trick here, pass the parallelism
# directly: make JOBS=-j8 box
JOBS ?=

# every goal ninqu.rules exposes, since there is no %: FORCE catch-all
# here. a new top-level group needs a name added too
.PHONY: all default clean box dev man install install-base uninstall \
        remove fmt fmt-all lib posix linux net xsi pseudo extra codegen test

$(NINQU): $(NINQU_SRC) cmd/dev/ninqu/ninqu.h scripts/mk/config.set scripts/mk/features.h shared/libutil/nofork_list.h $(NINQU_LIB_SRC)
	$(BOOTSTRAP_CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(NINQU_SRC) $(NINQU_LIB_SRC)

scripts/mk/config.set: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh config-set

scripts/mk/features.h: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh features-h

shared/libutil/nofork_list.h: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh nofork-list-h

all default box dev man install install-base uninstall remove \
        fmt fmt-all lib posix linux net xsi pseudo extra codegen test: $(NINQU)
	$(NINQU) $(JOBS) $@

clean: $(NINQU)
	$(NINQU) clean
	rm -f $(NINQU) build.ninja
