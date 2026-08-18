CC       ?= cc -static
CFLAGS   ?= -std=c99 -Wall -Wextra -pedantic

BACKEND  ?= auto

CPPFLAGS ?= -Ishared -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
            -DFEATURE_NOFORK=0 -DFEATURE_NOEXEC=0
# NOEXEC cannot be on the bootstrap compile, but it can
# be on when one compiles aruu-box with ninqu included

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

# map make's -j to ninqu's -j flag
NINQU ?= cmd/dev/ninqu/ninqu
JOBS  ?= $(filter -j%,$(MAKEFLAGS))

# rebuilds config.set first so a build.cfg edit is picked up before
# ninqu compiles, ninqu also does this lazily on its own via
# (set-file), this just avoids the extra shell startup on every run
$(NINQU): $(NINQU_SRC) cmd/dev/ninqu/ninqu.h scripts/mk/config.set $(NINQU_LIB_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(NINQU_SRC) $(NINQU_LIB_SRC)

# FORCE has no rule, thus making any pattern
# that depends on it be always outdated
.PHONY: FORCE
FORCE:

# ninqu generates build.ninja if ninja/samu is available and execs that
# instead of running its own backend, but you can use make backend=internal
# to force the internal one to be used anyways
%: $(NINQU) FORCE
	$(NINQU) $(JOBS) $@

clean: $(NINQU)
	$(NINQU) clean
	rm -f $(NINQU) build.ninja

scripts/mk/config.set: build.cfg scripts/genconfig.sh
	sh scripts/genconfig.sh config

# stops the % pattern rule above from trying to rebuild these as if
# they were stale build targets
Makefile: ;
ninqu.rules: ;
cmd/dev/ninqu/ninqu.h: ;
cmd/dev/ninqu/main.c: ;
cmd/dev/ninqu/strlist.c: ;
cmd/dev/ninqu/kv.c: ;
cmd/dev/ninqu/sexpr.c: ;
cmd/dev/ninqu/gate.c: ;
cmd/dev/ninqu/registry.c: ;
cmd/dev/ninqu/fs.c: ;
cmd/dev/ninqu/exec.c: ;
cmd/dev/ninqu/backend.c: ;
cmd/dev/ninqu/parse.c: ;
cmd/dev/ninqu/expand.c: ;
cmd/dev/ninqu/schedule.c: ;
cmd/dev/ninqu/query.c: ;
cmd/dev/ninqu/resolve.c: ;
cmd/dev/ninqu/ninja.c: ;
build.cfg: ;
scripts/genconfig.sh: ;
