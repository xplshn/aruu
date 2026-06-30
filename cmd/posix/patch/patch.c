/*
 * $OpenBSD: patch.c,v 1.45 2007/04/18 21:52:24 sobrado Exp $
 * $DragonFly: src/usr.bin/patch/patch.c,v 1.10 2008/08/10 23:39:56 joerg Exp $
 * $NetBSD: patch.c,v 1.35 2024/07/12 15:48:39 manu Exp $
 */

/*
 * patch - a program to apply diffs to original files
 * 
 * Copyright 1986, Larry Wall
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this condition and the following disclaimer.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * -C option added in 1998, original code by Marc Espie, based on FreeBSD
 * behaviour
 */

/* ?man patch: apply a diff file to an original
synopsis: [-bCcEeflNnRstuv] [-B backup-prefix] [-D symbol] [-d directory] [-F max-fuzz] [-i patchfile] [-o out-file] [-p strip-count] [-r rej-name] [-V t | nil | never | none] [-x number] [-z backup-ext] [--backup-if-mismatch] [--no-backup-if-mismatch] [--posix] [origfile [patchfile]]
patch will take a patch file containing any of the four forms of difference listing produced by the `diff(1)` program and apply those differences to an original file, producing a patched version. If `patchfile` is omitted, or is a hyphen, the patch will be read from the standard input.

patch will attempt to determine the type of the diff listing, unless over-ruled by a `-c`, `-e`, `-n`, or `-u` option. Context diffs (old-style, new-style, and unified) and normal diffs are applied directly by the patch program itself, whereas ed diffs are simply fed to the `ed(1)` editor via a pipe.

If the `patchfile` contains more than one patch, patch will try to apply each of them as if they came from separate patch files. This means, among other things, that it is assumed that the name of the file to patch must be determined for each diff listing, and that the garbage before each diff listing will be examined for interesting things such as file names and revision level (see the section on Filename Determination below).

## OPTIONS
### -B backup-prefix, --prefix backup-prefix
Causes the next argument to be interpreted as a prefix to the backup file name. If this argument is specified, any argument to `-z` will be ignored.

### -b, --backup
Save a backup copy of the file before it is modified. By default the original file is saved with a backup extension of `.orig` unless the file already has a numbered backup, in which case a numbered backup is made. This is equivalent to specifying `-V existing`. This option is currently the default, unless `--posix` is specified.

### --backup-if-mismatch
Create a backup file if the patch doesn't apply cleanly. This option only makes sense when `--backup` is disabled, i.e. when in `--posix` mode.

### -C, --check
Checks that the patch would apply cleanly, but does not modify anything.

### -c, --context
Forces patch to interpret the patch file as a context diff.

### -D symbol, --ifdef symbol
Causes patch to use the `#ifdef...#endif` construct to mark changes. The argument following will be used as the differentiating symbol. Note that, unlike the C compiler, there must be a space between the `-D` and the argument.

### -d directory, --directory directory
Causes patch to interpret the next argument as a directory, and change the working directory to it before doing anything else.

### -E, --remove-empty-files
Causes patch to remove output files that are empty after the patches have been applied. This option is useful when applying patches that create or remove files.

### -e, --ed
Forces patch to interpret the patch file as an `ed(1)` script.

### -F max-fuzz, --fuzz max-fuzz
Sets the maximum fuzz factor. This option only applies to context diffs, and causes patch to ignore up to that many lines in looking for places to install a hunk. Note that a larger fuzz factor increases the odds of a faulty patch. The default fuzz factor is 2, and it may not be set to more than the number of lines of context in the context diff, ordinarily 3.

### -f, --force
Forces patch to assume that the user knows exactly what he or she is doing, and to not ask any questions. It assumes the following: skip patches for which a file to patch can't be found; patch files even though they have the wrong version for the `Prereq:` line in the patch; and assume that patches are not reversed even if they look like they are. This option does not suppress commentary; use `-s` for that.

### -i patchfile, --input patchfile
Causes the next argument to be interpreted as the input file name (i.e., a patchfile). This option may be specified multiple times.

### -l, --ignore-whitespace
Causes the pattern matching to be done loosely, in case the tabs and spaces have been munged in your input file. Any sequence of whitespace in the pattern line will match any sequence in the input file. Normal characters must still match exactly. Each line of the context must still match a line in the input file.

### -N, --forward
Causes patch to ignore patches that it thinks are reversed or already applied. See also `-R`.

### -n, --normal
Forces patch to interpret the patch file as a normal diff.

### --no-backup-if-mismatch
Turn off `--backup-if-mismatch`. This option exists mostly for compatibility with GNU patch.

### -o out-file, --output out-file
Causes the next argument to be interpreted as the output file name.

### -p strip-count, --strip strip-count
Sets the pathname strip count, which controls how pathnames found in the patch file are treated, in case you keep your files in a different directory than the person who sent out the patch. The strip count specifies how many slashes are to be stripped from the front of the pathname. (Any intervening directory names also go away.) For example, supposing the file name in the patch file was `/u/howard/src/blurfl/blurfl.c`:

Setting `-p0` gives the entire pathname unmodified.

`-p1` gives

`u/howard/src/blurfl/blurfl.c`

without the leading slash.

`-p4` gives

`blurfl/blurfl.c`

Not specifying `-p` at all just gives you `blurfl.c`, unless all of the directories in the leading path (`u/howard/src/blurfl`) exist and that path is relative, in which case you get the entire pathname unmodified. Whatever you end up with is looked for either in the current directory, or the directory specified by the `-d` option.

### -R, --reverse
Tells patch that this patch was created with the old and new files swapped. (Yes, I'm afraid that does happen occasionally, human nature being what it is.) patch will attempt to swap each hunk around before applying it. Rejects will come out in the swapped format. The `-R` option will not work with ed diff scripts because there is too little information to reconstruct the reverse operation.

If the first hunk of a patch fails, patch will reverse the hunk to see if it can be applied that way. If it can, you will be asked if you want to have the `-R` option set. If it can't, the patch will continue to be applied normally. (Note: this method cannot detect a reversed patch if it is a normal diff and if the first command is an append (i.e., it should have been a delete) since appends always succeed, due to the fact that a null context will match anywhere. Luckily, most patches add or change lines rather than delete them, so most reversed normal diffs will begin with a delete, which will fail, triggering the heuristic.)

### -r rej-name, --reject-file rej-name
Causes the next argument to be interpreted as the reject file name.

### -s, --quiet, --silent
Makes patch do its work silently, unless an error occurs.

### -t, --batch
Similar to `-f`, in that it suppresses questions, but makes some different assumptions: skip patches for which a file to patch can't be found (the same as `-f`); skip patches for which the file has the wrong version for the `Prereq:` line in the patch; and assume that patches are reversed if they look like they are.

### -u, --unified
Forces patch to interpret the patch file as a unified context diff (a unidiff).

### -V t | nil | never | none, --version-control t | nil | never | none
Causes the next argument to be interpreted as a method for creating backup file names. The type of backups made can also be given in the `PATCH_VERSION_CONTROL` or `VERSION_CONTROL` environment variables, which are overridden by this option. The `-B` option overrides this option, causing the prefix to always be used for making backup file names. The values of the `PATCH_VERSION_CONTROL` and `VERSION_CONTROL` environment variables and the argument to the `-V` option are like the GNU Emacs "version-control" variable; they also recognize synonyms that are more descriptive. The valid values are (unique abbreviations are accepted):

`t`, numbered
: Always make numbered backups.

`nil`, existing
: Make numbered backups of files that already have them, simple backups of the others.

`never`, simple
: Always make simple backups.

`none`
: No backups are created.

### -v, --version
Causes patch to print out its revision header and patch level.

### -x number, --debug number
Sets internal debugging flags, and is of interest only to patchers.

### -z backup-ext, --suffix backup-ext
Causes the next argument to be interpreted as the backup extension, to be used in place of `.orig`.

### --posix
Enables strict POSIX.1-2004 conformance, specifically:

Backup files are not created unless the `-b` option is specified.

If unspecified, the file name used is the first of the old, new and index files that exists.

## Patch Application
patch will try to skip any leading garbage, apply the diff, and then skip any trailing garbage. Thus you could feed an article or message containing a diff listing to patch, and it should work. If the entire diff is indented by a consistent amount, this will be taken into account.

With context diffs, and to a lesser extent with normal diffs, patch can detect when the line numbers mentioned in the patch are incorrect, and will attempt to find the correct place to apply each hunk of the patch. As a first guess, it takes the line number mentioned for the hunk, plus or minus any offset used in applying the previous hunk. If that is not the correct place, patch will scan both forwards and backwards for a set of lines matching the context given in the hunk. First patch looks for a place where all lines of the context match. If no such place is found, and it's a context diff, and the maximum fuzz factor is set to 1 or more, then another scan takes place ignoring the first and last line of context. If that fails, and the maximum fuzz factor is set to 2 or more, the first two and last two lines of context are ignored, and another scan is made. (The default maximum fuzz factor is 2.)

If patch cannot find a place to install that hunk of the patch, it will put the hunk out to a reject file, which normally is the name of the output file plus `.rej`. (Note that the rejected hunk will come out in context diff form whether the input patch was a context diff or a normal diff. If the input was a normal diff, many of the contexts will simply be null.) The line numbers on the hunks in the reject file may be different than in the patch file: they reflect the approximate location patch thinks the failed hunks belong in the new file rather than the old one.

As each hunk is completed, you will be told whether the hunk succeeded or failed, and which line (in the new file) patch thought the hunk should go on. If this is different from the line number specified in the diff, you will be told the offset. A single large offset MAY be an indication that a hunk was installed in the wrong place. You will also be told if a fuzz factor was used to make the match, in which case you should also be slightly suspicious.

## Filename Determination
If no original file is specified on the command line, patch will try to figure out from the leading garbage what the name of the file to edit is. When checking a prospective file name, pathname components are stripped as specified by the `-p` option and the file's existence and writability are checked relative to the current working directory (or the directory specified by the `-d` option).

If the diff is a context or unified diff, patch is able to determine the old and new file names from the diff header. For context diffs, the "old" file is specified in the line beginning with "***" and the "new" file is specified in the line beginning with "---". For a unified diff, the "old" file is specified in the line beginning with "---" and the "new" file is specified in the line beginning with "+++". If there is an `Index:` line in the leading garbage (regardless of the diff type), patch will use the file name from that line as the "index" file.

patch will choose the file name by performing the following steps, with the first match used:

If patch is operating in strict POSIX.1-2004 mode, the first of the "old", "new" and "index" file names that exist is used. Otherwise, patch will examine either the "old" and "new" file names or, for a non-context diff, the "index" file name, and choose the file name with the fewest path components, the shortest basename, and the shortest total file name length (in that order).

If no file exists, patch checks for the existence of the files in an RCS directory using the criteria specified above. If found, patch will attempt to get or check out the file.

If no suitable file was found to patch, the patch file is a context or unified diff, and the old file was zero length, the new file name is created and used.

If the file name still cannot be determined, patch will prompt the user for the file name to use.

Additionally, if the leading garbage contains a `Prereq:` line, patch will take the first word from the prerequisites line (normally a version number) and check the input file to see if that word can be found. If not, patch will ask for confirmation before proceeding.

The upshot of all this is that you should be able to say, while in a news interface, the following:

`| patch -d /usr/src/local/blurfl`

and patch a file in the blurfl directory directly from the article containing the patch.

## Backup Files
By default, the patched version is put in place of the original, with the original file backed up to the same name with the extension `.orig`, or as specified by the `-B`, `-V`, or `-z` options. The extension used for making backup files may also be specified in the `SIMPLE_BACKUP_SUFFIX` environment variable, which is overridden by the options above.

If the backup file is a symbolic or hard link to the original file, patch creates a new backup file name by changing the first lowercase letter in the last component of the file's name into uppercase. If there are no more lowercase letters in the name, it removes the first character from the name. It repeats this process until it comes up with a backup file that does not already exist or is not linked to the original file.

You may also specify where you want the output to go with the `-o` option; if that file already exists, it is backed up first.

## Notes For Patch Senders
There are several things you should bear in mind if you are going to be sending out patches:

First, you can save people a lot of grief by keeping a `patchlevel.h` file which is patched to increment the patch level as the first diff in the patch file you send out. If you put a `Prereq:` line in with the patch, it won't let them apply patches out of order without some warning.

Second, make sure you've specified the file names right, either in a context diff header, or with an `Index:` line. If you are patching something in a subdirectory, be sure to tell the patch user to specify a `-p` option as needed.

Third, you can create a file by sending out a diff that compares a null file to the file you want to create. This will only work if the file you want to create doesn't exist already in the target directory.

Fourth, take care not to send out reversed patches, since it makes people wonder whether they already applied the patch.

Fifth, while you may be able to get away with putting 582 diff listings into one file, it is probably wiser to group related patches into separate files in case something goes haywire.

## ENVIRONMENT
`POSIXLY_CORRECT`
: When set, patch behaves as if the `--posix` option has been specified.

`SIMPLE_BACKUP_SUFFIX`
: Extension to use for backup file names instead of `.orig`.

`TMPDIR`
: Directory to put temporary files in; default is `/tmp`.

`PATCH_VERSION_CONTROL`
: Selects when numbered backup files are made.

`VERSION_CONTROL`
: Same as `PATCH_VERSION_CONTROL`.

## FILES
`$TMPDIR/patch*`
: patch temporary files

`/dev/tty`
: used to read input when patch prompts the user

## DIAGNOSTICS
Too many to list here, but generally indicative that patch couldn't parse your patch file.

The message "Hmm..." indicates that there is unprocessed text in the patch file and that patch is attempting to intuit whether there is a patch in that text and, if so, what kind of patch it is.

The patch utility exits with one of the following values:

`0`
: Successful completion.

`1`
: One or more lines were written to a reject file.

`>1`
: An error occurred.

When applying a set of patches in a loop it behooves you to check this exit status so you don't apply a later patch to a partially patched file.

## SEE ALSO
`diff(1)`

## STANDARDS
The patch utility is compliant with the POSIX.1-2004 specification (except as detailed above for the `--posix` option), though the presence of patch itself is optional.

The flags `-C`, `-E`, `-f`, `-s`, `-t`, `-u`, `-v`, `-B`, `-F`, `-V`, `-x`, and `-z` and `--posix` are extensions to that specification.

## AUTHORS
Larry Wall with many other contributors.

## CAVEATS
patch cannot tell if the line numbers are off in an ed script, and can only detect bad line numbers in a normal diff when it finds a "change" or a "delete" command. A context diff using fuzz factor 3 may have the same problem. Until a suitable interactive interface is added, you should probably do a context diff in these cases to see if the changes made sense. Of course, compiling without errors is a pretty good indication that the patch worked, but not always.

patch usually produces the correct results, even when it has to do a lot of guessing. However, the results are guaranteed to be correct only when the patch is applied to exactly the same version of the file that the patch was generated from.

## BUGS
Could be smarter about partial matches, excessively deviant offsets and swapped code, but that would take an extra pass.

Check patch mode (`-C`) will fail if you try to check several patches in succession that build on each other. The entire patch code would have to be restructured to keep temporary files around so that it can handle this situation.

If code has been duplicated (for instance with `#ifdef OLDCODE ... #else ... #endif`), patch is incapable of patching both versions, and, if it works at all, will likely patch the wrong one, and tell you that it succeeded to boot.

If you apply a patch you've already applied, patch will think it is a reversed patch, and offer to un-apply the patch. This could be construed as a feature.
*/

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"
#include "util.h"
#include "pch.h"
#include "inp.h"
#include "backupfile.h"
#include "pathnames.h"

mode_t		filemode = 0644;

char		*buf;			/* general purpose buffer */
size_t		bufsz;			/* general purpose buffer size */

bool		using_plan_a = true;	/* try to keep everything in memory */
bool		out_of_mem = false;	/* ran out of memory in plan a */

#define MAXFILEC 2

char		*filearg[MAXFILEC];
bool		ok_to_create_file = false;
char		*outname = NULL;
char		*origprae = NULL;
char		*TMPOUTNAME;
char		*TMPINNAME;
char		*TMPREJNAME;
char		*TMPPATNAME;
bool		toutkeep = false;
bool		trejkeep = false;
bool		warn_on_invalid_line;
bool		last_line_missing_eol;

#ifdef DEBUGGING
int		debug = 0;
#endif

bool		force = false;
bool		batch = false;
bool		verbose = true;
bool		reverse = false;
bool		noreverse = false;
bool		skip_rest_of_patch = false;
int		strippath = 957;
bool		canonicalize = false;
bool		check_only = false;
int		diff_type = 0;
char		*revision = NULL;	/* prerequisite revision, if any */
LINENUM		input_lines = 0;	/* how long is input file in lines */
int		posix = 0;		/* strict POSIX mode? */
int		backup_if_mismatch = -1;/* create backup file when patch doesn't apply cleanly */

static void	reinitialize_almost_everything(void);
static void	get_some_switches(void);
static void	reset_getopt_state(void);
static LINENUM	locate_hunk(LINENUM);
static void	abort_context_hunk(void);
static void	rej_line(int, LINENUM);
static void	abort_hunk(void);
static void	apply_hunk(LINENUM);
static void	init_output(const char *);
static void	init_reject(const char *);
static void	copy_till(LINENUM, bool);
static bool	spew_output(void);
static void	dump_line(LINENUM, bool);
static bool	patch_match(LINENUM, LINENUM, LINENUM);
static bool	similar(const char *, const char *, ssize_t);
__dead static void	usage(void);

/* true if -E was specified on command line.  */
static bool	remove_empty_files = false;

/* true if -R was specified on command line.  */
static bool	reverse_flag_specified = false;

/* buffer holding the name of the rejected patch file. */
static char	rejname[PATH_MAX];

/* buffer for stderr */
static char	serrbuf[BUFSIZ];

/* how many input lines have been irretractibly output */
static LINENUM	last_frozen_line = 0;

static int	Argc;		/* guess */
static char	**Argv;
static int	Argc_last;	/* for restarting plan_b */
static char	**Argv_last;

static FILE	*ofp = NULL;	/* output file pointer */
static FILE	*rejfp = NULL;	/* reject file pointer */

static int	filec = 0;	/* how many file arguments? */
static LINENUM	last_offset = 0;
static LINENUM	maxfuzz = 2;

/* patch using ifdef, ifndef, etc. */
static bool		do_defines = false;
/* #ifdef xyzzy */
static char		if_defined[128];
/* #ifndef xyzzy */
static char		not_defined[128];
/* #else */
static const char	else_defined[] = "#else\n";
/* #endif xyzzy */
static char		end_defined[128];


/* Apply a set of diffs as appropriate. */

int
main(int argc, char *argv[])
{
	int	error = 0, hunk, failed, i, fd;
	LINENUM	where = 0, newwhere, fuzz, mymaxfuzz;
	const	char *tmpdir;
	char	*v;

	bufsz = INITLINELEN;
	if ((buf = malloc(bufsz)) == NULL)
		pfatal("allocating input buffer");
	buf[0] = '\0';

	setbuf(stderr, serrbuf);
	for (i = 0; i < MAXFILEC; i++)
		filearg[i] = NULL;

	/* Cons up the names of the temporary files.  */
	if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
		tmpdir = _PATH_TMP;
	for (i = strlen(tmpdir) - 1; i > 0 && tmpdir[i] == '/'; i--)
		;
	i++;
	if (asprintf(&TMPOUTNAME, "%.*s/patchoXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPOUTNAME)) < 0)
		pfatal("can't create %s", TMPOUTNAME);
	close(fd);

	if (asprintf(&TMPINNAME, "%.*s/patchiXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPINNAME)) < 0)
		pfatal("can't create %s", TMPINNAME);
	close(fd);

	if (asprintf(&TMPREJNAME, "%.*s/patchrXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPREJNAME)) < 0)
		pfatal("can't create %s", TMPREJNAME);
	close(fd);

	if (asprintf(&TMPPATNAME, "%.*s/patchpXXXXXXXXXX", i, tmpdir) == -1)
		fatal("cannot allocate memory");
	if ((fd = mkstemp(TMPPATNAME)) < 0)
		pfatal("can't create %s", TMPPATNAME);
	close(fd);

	v = getenv("SIMPLE_BACKUP_SUFFIX");
	if (v)
		simple_backup_suffix = v;
	else
		simple_backup_suffix = ORIGEXT;

	if ((v = getenv("PATCH_VERSION_CONTROL")) == NULL)
		v = getenv("VERSION_CONTROL");
	if (v != NULL)
		backup_type = get_version(v);

	/* parse switches */
	Argc = argc;
	Argv = argv;
	get_some_switches();

	if (backup_type == undefined)
		backup_type = posix ? none : numbered_existing;

	/* make sure we clean up /tmp in case of disaster */
	set_signals(0);

	for (open_patch_file(filearg[1]); there_is_another_patch();
	    reinitialize_almost_everything()) {
		/* for each patch in patch file */

		warn_on_invalid_line = true;

		if (outname == NULL)
			outname = savestr(filearg[0]);

		/* for ed script just up and do it and exit */
		if (diff_type == ED_DIFF) {
			do_ed_script();
			continue;
		}
		/* initialize the patched file */
		if (!skip_rest_of_patch)
			init_output(TMPOUTNAME);

		/* initialize reject file */
		init_reject(TMPREJNAME);

		/* find out where all the lines are */
		if (!skip_rest_of_patch)
			scan_input(filearg[0]);

		/* from here on, open no standard i/o files, because malloc */
		/* might misfire and we can't catch it easily */

		/* apply each hunk of patch */
		hunk = 0;
		failed = 0;
		out_of_mem = false;
		while (another_hunk()) {
			hunk++;
			fuzz = 0;
			mymaxfuzz = pch_context();
			if (maxfuzz < mymaxfuzz)
				mymaxfuzz = maxfuzz;
			if (!skip_rest_of_patch) {
				do {
					where = locate_hunk(fuzz);
					if (hunk == 1 && where == 0 && !force) {
						/* dwim for reversed patch? */
						if (!pch_swap()) {
							if (fuzz == 0)
								say("Not enough memory to try swapped hunk!  Assuming unswapped.\n");
							continue;
						}
						reverse = !reverse;
						/* try again */
						where = locate_hunk(fuzz);
						if (where == 0) {
							/* didn't find it swapped */
							if (!pch_swap())
								/* put it back to normal */
								fatal("lost hunk on alloc error!\n");
							reverse = !reverse;
						} else if (noreverse) {
							if (!pch_swap())
								/* put it back to normal */
								fatal("lost hunk on alloc error!\n");
							reverse = !reverse;
							say("Ignoring previously applied (or reversed) patch.\n");
							skip_rest_of_patch = true;
						} else if (batch) {
							if (verbose)
								say("%seversed (or %spreviously applied) patch detected!  %s -R.",
								    reverse ? "R" : "Unr",
								    reverse ? "" : "not ",
								    reverse ? "Assuming" : "Ignoring");
						} else {
							ask("%seversed (or %spreviously applied) patch detected!  %s -R? [y] ",
							    reverse ? "R" : "Unr",
							    reverse ? "" : "not ",
							    reverse ? "Assume" : "Ignore");
							if (*buf == 'n') {
								ask("Apply anyway? [n] ");
								if (*buf != 'y')
									skip_rest_of_patch = true;
								where = 0;
								reverse = !reverse;
								if (!pch_swap())
									/* put it back to normal */
									fatal("lost hunk on alloc error!\n");
							}
						}
					}
				} while (!skip_rest_of_patch && where == 0 &&
				    ++fuzz <= mymaxfuzz);

				if (skip_rest_of_patch) {	/* just got decided */
					if (ferror(ofp) || fclose(ofp)) {
						say("Error writing %s\n",
						    TMPOUTNAME);
						error = 1;
					}
					ofp = NULL;
				}
			}
			newwhere = pch_newfirst() + last_offset;
			if (skip_rest_of_patch) {
				abort_hunk();
				failed++;
				if (verbose)
					say("Hunk #%d ignored at %ld.\n",
					    hunk, newwhere);
			} else if (where == 0) {
				abort_hunk();
				failed++;
				if (verbose)
					say("Hunk #%d failed at %ld.\n",
					    hunk, newwhere);
			} else {
				apply_hunk(where);
				if (verbose) {
					say("Hunk #%d succeeded at %ld",
					    hunk, newwhere);
					if (fuzz != 0)
						say(" with fuzz %ld", fuzz);
					if (last_offset)
						say(" (offset %ld line%s)",
						    last_offset,
						    last_offset == 1L ? "" : "s");
					say(".\n");
				}
			}
		}

		if (out_of_mem && using_plan_a) {
			Argc = Argc_last;
			Argv = Argv_last;
			say("\n\nRan out of memory using Plan A--trying again...\n\n");
			if (ofp)
				fclose(ofp);
			ofp = NULL;
			if (rejfp)
				fclose(rejfp);
			rejfp = NULL;
			continue;
		}
		if (hunk == 0)
			fatal("Internal error: hunk should not be 0\n");

		/* finish spewing out the new file */
		if (!skip_rest_of_patch && !spew_output()) {
			say("Can't write %s\n", TMPOUTNAME);
			error = 1;
		}

		/* and put the output where desired */
		ignore_signals();
		if (!skip_rest_of_patch) {
			struct stat	statbuf;
			char	*realout = outname;

			if (!check_only) {
				/* handle --backup-if-mismatch */
				enum backup_type saved = backup_type;
				if (failed > 0 && backup_if_mismatch > 0 && backup_type == none)
					backup_type = simple;
				if (move_file(TMPOUTNAME, outname) < 0) {
					toutkeep = true;
					realout = TMPOUTNAME;
					chmod(TMPOUTNAME, filemode);
				} else
					chmod(outname, filemode);
				backup_type = saved;

				if (remove_empty_files &&
				    stat(realout, &statbuf) == 0 &&
				    statbuf.st_size == 0) {
					if (verbose)
						say("Removing %s (empty after patching).\n",
						    realout);
					unlink(realout);
				}
			}
		}
		if (ferror(rejfp) || fclose(rejfp)) {
			say("Error writing %s\n", rejname);
			error = 1;
		}
		rejfp = NULL;
		if (failed) {
			error = 1;
			if (*rejname == '\0') {
				if (strlcpy(rejname, outname,
				    sizeof(rejname)) >= sizeof(rejname))
					fatal("filename %s is too long\n", outname);
				if (strlcat(rejname, REJEXT,
				    sizeof(rejname)) >= sizeof(rejname))
					fatal("filename %s is too long\n", outname);
			}
			if (skip_rest_of_patch) {
				say("%d out of %d hunks ignored--saving rejects to %s\n",
				    failed, hunk, rejname);
			} else {
				say("%d out of %d hunks FAILED -- saving rejects to %s\n",
				    failed, hunk, rejname);
			}
			if (!check_only && move_file(TMPREJNAME, rejname) < 0)
				trejkeep = true;
		}
		set_signals(1);
	}
	my_exit(error);
	/* NOTREACHED */
}

static void
reset_getopt_state(void)
{
#if defined(__GLIBC__) || defined(__linux__)
	optind = 0;
#else
	optreset = 1;
	optind = 1;
#endif
}

/* Prepare to find the next patch to do in the patch file. */

static void
reinitialize_almost_everything(void)
{
	re_patch();
	re_input();

	input_lines = 0;
	last_frozen_line = 0;

	filec = 0;
	if (!out_of_mem) {
		free(filearg[0]);
		filearg[0] = NULL;
	}

	free(outname);
	outname = NULL;

	last_offset = 0;
	diff_type = 0;

	free(revision);
	revision = NULL;

	reverse = reverse_flag_specified;
	skip_rest_of_patch = false;

	get_some_switches();
}

/* Process switches and filenames. */

static void
get_some_switches(void)
{
	const char *options = "b::B:cCd:D:eEfF:i:lnNo:p:r:RstuvV:x:z:";
	static struct option longopts[] = {
		{"backup",		no_argument,		0,	'b'},
		{"backup-if-mismatch",	no_argument,		&backup_if_mismatch, 1},
		{"batch",		no_argument,		0,	't'},
		{"check",		no_argument,		0,	'C'},
		{"context",		no_argument,		0,	'c'},
		{"debug",		required_argument,	0,	'x'},
		{"directory",		required_argument,	0,	'd'},
		{"ed",			no_argument,		0,	'e'},
		{"force",		no_argument,		0,	'f'},
		{"forward",		no_argument,		0,	'N'},
		{"fuzz",		required_argument,	0,	'F'},
		{"ifdef",		required_argument,	0,	'D'},
		{"input",		required_argument,	0,	'i'},
		{"ignore-whitespace",	no_argument,		0,	'l'},
		{"no-backup-if-mismatch",	no_argument,	&backup_if_mismatch, 0},
		{"normal",		no_argument,		0,	'n'},
		{"output",		required_argument,	0,	'o'},
		{"prefix",		required_argument,	0,	'B'},
		{"quiet",		no_argument,		0,	's'},
		{"reject-file",		required_argument,	0,	'r'},
		{"remove-empty-files",	no_argument,		0,	'E'},
		{"reverse",		no_argument,		0,	'R'},
		{"silent",		no_argument,		0,	's'},
		{"strip",		required_argument,	0,	'p'},
		{"suffix",		required_argument,	0,	'z'},
		{"unified",		no_argument,		0,	'u'},
		{"version",		no_argument,		0,	'v'},
		{"version-control",	required_argument,	0,	'V'},
		{"posix",		no_argument,		&posix,	1},
		{NULL,			0,			0,	0}
	};
	int ch;

	rejname[0] = '\0';
	Argc_last = Argc;
	Argv_last = Argv;
	if (!Argc)
		return;
	reset_getopt_state();
	while ((ch = getopt_long(Argc, Argv, options, longopts, NULL)) != -1) {
		switch (ch) {
		case 'b':
			if (backup_type == undefined)
				backup_type = numbered_existing;
			if (optarg == NULL)
				break;
			if (verbose)
				say("Warning, the ``-b suffix'' option has been"
				    " obsoleted by the -z option.\n");
			/* FALLTHROUGH */
		case 'z':
			/* must directly follow 'b' case for backwards compat */
			simple_backup_suffix = savestr(optarg);
			break;
		case 'B':
			origprae = savestr(optarg);
			break;
		case 'c':
			diff_type = CONTEXT_DIFF;
			break;
		case 'C':
			check_only = true;
			break;
		case 'd':
			if (chdir(optarg) < 0)
				pfatal("can't cd to %s", optarg);
			break;
		case 'D':
			do_defines = true;
			if (!isalpha((unsigned char)*optarg) && *optarg != '_')
				fatal("argument to -D is not an identifier\n");
			snprintf(if_defined, sizeof if_defined,
			    "#ifdef %s\n", optarg);
			snprintf(not_defined, sizeof not_defined,
			    "#ifndef %s\n", optarg);
			snprintf(end_defined, sizeof end_defined,
			    "#endif /* %s */\n", optarg);
			break;
		case 'e':
			diff_type = ED_DIFF;
			break;
		case 'E':
			remove_empty_files = true;
			break;
		case 'f':
			force = true;
			break;
		case 'F':
			maxfuzz = atoi(optarg);
			break;
		case 'i':
			if (++filec == MAXFILEC)
				fatal("too many file arguments\n");
			filearg[filec] = savestr(optarg);
			break;
		case 'l':
			canonicalize = true;
			break;
		case 'n':
			diff_type = NORMAL_DIFF;
			break;
		case 'N':
			noreverse = true;
			break;
		case 'o':
			outname = savestr(optarg);
			break;
		case 'p':
			strippath = atoi(optarg);
			break;
		case 'r':
			if (strlcpy(rejname, optarg,
			    sizeof(rejname)) >= sizeof(rejname))
				fatal("argument for -r is too long\n");
			break;
		case 'R':
			reverse = true;
			reverse_flag_specified = true;
			break;
		case 's':
			verbose = false;
			break;
		case 't':
			batch = true;
			break;
		case 'u':
			diff_type = UNI_DIFF;
			break;
		case 'v':
			version();
			break;
		case 'V':
			backup_type = get_version(optarg);
			break;
#ifdef DEBUGGING
		case 'x':
			debug = atoi(optarg);
			break;
#endif
		default:
			if (ch != '\0')
				usage();
			break;
		}
	}
	Argc -= optind;
	Argv += optind;

	if (Argc > 0) {
		filearg[0] = savestr(*Argv++);
		Argc--;
		while (Argc > 0) {
			if (++filec == MAXFILEC)
				fatal("too many file arguments\n");
			filearg[filec] = savestr(*Argv++);
			Argc--;
		}
	}

	if (getenv("POSIXLY_CORRECT") != NULL)
		posix = 1;

	if (backup_if_mismatch == -1) {
		backup_if_mismatch = posix ? 0 : 1;
	}
}

static void
usage(void)
{
	fprintf(stderr,
"usage: patch [-bCcEeflNnRstuv] [-B backup-prefix] [-D symbol] [-d directory]\n"
"             [-F max-fuzz] [-i patchfile] [-o out-file] [-p strip-count]\n"
"             [-r rej-name] [-V t | nil | never] [-x number] [-z backup-ext]\n"
"             [--backup-if-mismatch] [--no-backup-if-mismatch] [--posix]\n"
"             [origfile [patchfile]]\n"
"       patch <patchfile\n");
	my_exit(EXIT_FAILURE);
}

/*
 * Attempt to find the right place to apply this hunk of patch.
 */
static LINENUM
locate_hunk(LINENUM fuzz)
{
	LINENUM	first_guess = pch_first() + last_offset;
	LINENUM	offset;
	LINENUM	pat_lines = pch_ptrn_lines();
	LINENUM	max_pos_offset = input_lines - first_guess - pat_lines + 1;
	LINENUM	max_neg_offset = first_guess - last_frozen_line - 1 + pch_context();

	if (pat_lines == 0) {		/* null range matches always */
		if (verbose && fuzz == 0 && (diff_type == CONTEXT_DIFF
		    || diff_type == NEW_CONTEXT_DIFF
		    || diff_type == UNI_DIFF)) {
			say("Empty context always matches.\n");
		}
		return (first_guess);
	}
	if (max_neg_offset >= first_guess)	/* do not try lines < 0 */
		max_neg_offset = first_guess - 1;
	if (first_guess <= input_lines && patch_match(first_guess, 0, fuzz))
		return first_guess;
	for (offset = 1; ; offset++) {
		bool	check_after = (offset <= max_pos_offset);
		bool	check_before = (offset <= max_neg_offset);

		if (check_after && patch_match(first_guess, offset, fuzz)) {
#ifdef DEBUGGING
			if (debug & 1)
				say("Offset changing from %ld to %ld\n",
				    last_offset, offset);
#endif
			last_offset = offset;
			return first_guess + offset;
		} else if (check_before && patch_match(first_guess, -offset, fuzz)) {
#ifdef DEBUGGING
			if (debug & 1)
				say("Offset changing from %ld to %ld\n",
				    last_offset, -offset);
#endif
			last_offset = -offset;
			return first_guess - offset;
		} else if (!check_before && !check_after)
			return 0;
	}
}

/* We did not find the pattern, dump out the hunk so they can handle it. */

static void
abort_context_hunk(void)
{
	LINENUM	i;
	const LINENUM	pat_end = pch_end();
	/*
	 * add in last_offset to guess the same as the previous successful
	 * hunk
	 */
	const LINENUM	oldfirst = pch_first() + last_offset;
	const LINENUM	newfirst = pch_newfirst() + last_offset;
	const LINENUM	oldlast = oldfirst + pch_ptrn_lines() - 1;
	const LINENUM	newlast = newfirst + pch_repl_lines() - 1;
	const char	*stars = (diff_type >= NEW_CONTEXT_DIFF ? " ****" : "");
	const char	*minuses = (diff_type >= NEW_CONTEXT_DIFF ? " ----" : " -----");

	fprintf(rejfp, "***************\n");
	for (i = 0; i <= pat_end; i++) {
		switch (pch_char(i)) {
		case '*':
			if (oldlast < oldfirst)
				fprintf(rejfp, "*** 0%s\n", stars);
			else if (oldlast == oldfirst)
				fprintf(rejfp, "*** %ld%s\n", oldfirst, stars);
			else
				fprintf(rejfp, "*** %ld,%ld%s\n", oldfirst,
				    oldlast, stars);
			break;
		case '=':
			if (newlast < newfirst)
				fprintf(rejfp, "--- 0%s\n", minuses);
			else if (newlast == newfirst)
				fprintf(rejfp, "--- %ld%s\n", newfirst, minuses);
			else
				fprintf(rejfp, "--- %ld,%ld%s\n", newfirst,
				    newlast, minuses);
			break;
		case '\n':
			fprintf(rejfp, "%s", pfetch(i));
			break;
		case ' ':
		case '-':
		case '+':
		case '!':
			fprintf(rejfp, "%c %s", pch_char(i), pfetch(i));
			break;
		default:
			fatal("fatal internal error in abort_context_hunk\n");
		}
	}
}

static void
rej_line(int ch, LINENUM i)
{
	size_t len;
	const char *line = pfetch(i);

	len = strlen(line);

	fprintf(rejfp, "%c%s", ch, line);
	if (len == 0 || line[len-1] != '\n')
		fprintf(rejfp, "\n\\ No newline at end of file\n");
}

static void
abort_hunk(void)
{
	LINENUM		i, j, split;
	int		ch1, ch2;
	const LINENUM	pat_end = pch_end();
	const LINENUM	oldfirst = pch_first() + last_offset;
	const LINENUM	newfirst = pch_newfirst() + last_offset;

	if (diff_type != UNI_DIFF) {
		abort_context_hunk();
		return;
	}
	split = -1;
	for (i = 0; i <= pat_end; i++) {
		if (pch_char(i) == '=') {
			split = i;
			break;
		}
	}
	if (split == -1) {
		fprintf(rejfp, "malformed hunk: no split found\n");
		return;
	}
	i = 0;
	j = split + 1;
	fprintf(rejfp, "@@ -%ld,%ld +%ld,%ld @@\n",
	    pch_ptrn_lines() ? oldfirst : 0,
	    pch_ptrn_lines(), newfirst, pch_repl_lines());
	while (i < split || j <= pat_end) {
		ch1 = i < split ? pch_char(i) : -1;
		ch2 = j <= pat_end ? pch_char(j) : -1;
		if (ch1 == '-') {
			rej_line('-', i);
			i++;
		} else if (ch1 == ' ' && ch2 == ' ') {
			rej_line(' ', i);
			i++;
			j++;
		} else if (ch1 == '!' && ch2 == '!') {
			while (i < split && ch1 == '!') {
				rej_line('-', i);
				i++;
				ch1 = i < split ? pch_char(i) : -1;
			}
			while (j <= pat_end && ch2 == '!') {
				rej_line('+', j);
				j++;
				ch2 = j <= pat_end ? pch_char(j) : -1;
			}
		} else if (ch1 == '*') {
			i++;
		} else if (ch2 == '+' || ch2 == ' ') {
			rej_line(ch2, j);
			j++;
		} else {
			fprintf(rejfp, "internal error on (%ld %ld %ld)\n",
			    i, split, j);
			rej_line(ch1, i);
			rej_line(ch2, j);
			return;
		}
	}
}

/* We found where to apply it (we hope), so do it. */

static void
apply_hunk(LINENUM where)
{
	LINENUM		old = 1;
	const LINENUM	lastline = pch_ptrn_lines();
	LINENUM		new = lastline + 1;
#define OUTSIDE 0
#define IN_IFNDEF 1
#define IN_IFDEF 2
#define IN_ELSE 3
	int		def_state = OUTSIDE;
	const LINENUM	pat_end = pch_end();

	where--;
	while (pch_char(new) == '=' || pch_char(new) == '\n')
		new++;

	while (old <= lastline) {
		if (pch_char(old) == '-') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				if (def_state == OUTSIDE) {
					fputs(not_defined, ofp);
					def_state = IN_IFNDEF;
				} else if (def_state == IN_IFDEF) {
					fputs(else_defined, ofp);
					def_state = IN_ELSE;
				}
				fputs(pfetch(old), ofp);
			}
			last_frozen_line++;
			old++;
		} else if (new > pat_end) {
			break;
		} else if (pch_char(new) == '+') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				if (def_state == IN_IFNDEF) {
					fputs(else_defined, ofp);
					def_state = IN_ELSE;
				} else if (def_state == OUTSIDE) {
					fputs(if_defined, ofp);
					def_state = IN_IFDEF;
				}
			}
			fputs(pfetch(new), ofp);
			new++;
		} else if (pch_char(new) != pch_char(old)) {
			say("Out-of-sync patch, lines %ld,%ld--mangled text or line numbers, maybe?\n",
			    pch_hunk_beg() + old,
			    pch_hunk_beg() + new);
#ifdef DEBUGGING
			say("oldchar = '%c', newchar = '%c'\n",
			    pch_char(old), pch_char(new));
#endif
			my_exit(2);
		} else if (pch_char(new) == '!') {
			copy_till(where + old - 1, false);
			if (do_defines) {
				fputs(not_defined, ofp);
				def_state = IN_IFNDEF;
			}
			while (pch_char(old) == '!') {
				if (do_defines) {
					fputs(pfetch(old), ofp);
				}
				last_frozen_line++;
				old++;
			}
			if (do_defines) {
				fputs(else_defined, ofp);
				def_state = IN_ELSE;
			}
			while (pch_char(new) == '!') {
				fputs(pfetch(new), ofp);
				new++;
			}
		} else {
			if (pch_char(new) != ' ')
				fatal("Internal error: expected ' '\n");
			old++;
			new++;
			if (do_defines && def_state != OUTSIDE) {
				fputs(end_defined, ofp);
				def_state = OUTSIDE;
			}
		}
	}
	if (new <= pat_end && pch_char(new) == '+') {
		copy_till(where + old - 1, false);
		if (do_defines) {
			if (def_state == OUTSIDE) {
				fputs(if_defined, ofp);
				def_state = IN_IFDEF;
			} else if (def_state == IN_IFNDEF) {
				fputs(else_defined, ofp);
				def_state = IN_ELSE;
			}
		}
		while (new <= pat_end && pch_char(new) == '+') {
			fputs(pfetch(new), ofp);
			new++;
		}
	}
	if (do_defines && def_state != OUTSIDE) {
		fputs(end_defined, ofp);
	}
}

/*
 * Open the new file.
 */
static void
init_output(const char *name)
{
	ofp = fopen(name, "w");
	if (ofp == NULL)
		pfatal("can't create %s", name);
}

/*
 * Open a file to put hunks we can't locate.
 */
static void
init_reject(const char *name)
{
	rejfp = fopen(name, "w");
	if (rejfp == NULL)
		pfatal("can't create %s", name);
}

/*
 * Copy input file to output, up to wherever hunk is to be applied.
 * If endoffile is true, treat the last line specially since it may
 * lack a newline.
 */
static void
copy_till(LINENUM lastline, bool endoffile)
{
	if (last_frozen_line > lastline)
		fatal("misordered hunks! output would be garbled\n");
	while (last_frozen_line < lastline) {
		if (++last_frozen_line == lastline && endoffile)
			dump_line(last_frozen_line, !last_line_missing_eol);
		else
			dump_line(last_frozen_line, true);
	}
}

/*
 * Finish copying the input file to the output file.
 */
static bool
spew_output(void)
{
	int rv;

#ifdef DEBUGGING
	if (debug & 256)
		say("il=%ld lfl=%ld\n", input_lines, last_frozen_line);
#endif
	if (input_lines)
		copy_till(input_lines, true);	/* dump remainder of file */
	rv = ferror(ofp) == 0 && fclose(ofp) == 0;
	ofp = NULL;
	return rv;
}

/*
 * Copy one line from input to output.
 */
static void
dump_line(LINENUM line, bool write_newline)
{
	char	*s;

	s = ifetch(line, 0);
	if (s == NULL)
		return;
	/* Note: string is not NUL terminated. */
	for (; *s != '\n'; s++)
		putc(*s, ofp);
	if (write_newline)
		putc('\n', ofp);
}

/*
 * Does the patch pattern match at line base+offset?
 */
static bool
patch_match(LINENUM base, LINENUM offset, LINENUM fuzz)
{
	LINENUM		pline = 1 + fuzz;
	LINENUM		iline;
	LINENUM		pat_lines = pch_ptrn_lines() - fuzz;
	const char	*ilineptr;
	const char	*plineptr;
	ssize_t		plinelen;

	for (iline = base + offset + fuzz; pline <= pat_lines; pline++, iline++) {
		ilineptr = ifetch(iline, offset >= 0);
		if (ilineptr == NULL)
			return false;
		plineptr = pfetch(pline);
		plinelen = pch_line_len(pline);
		if (canonicalize) {
			if (!similar(ilineptr, plineptr, plinelen))
				return false;
		} else if (strnNE(ilineptr, plineptr, plinelen))
			return false;
		if (iline == input_lines) {
			/*
			 * We are looking at the last line of the file.
			 * If the file has no eol, the patch line should
			 * not have one either and vice-versa. Note that
			 * plinelen > 0.
			 */
			if (last_line_missing_eol) {
				if (plineptr[plinelen - 1] == '\n')
					return false;
			} else {
				if (plineptr[plinelen - 1] != '\n')
					return false;
			}
		}
	}
	return true;
}

/*
 * Do two lines match with canonicalized white space?
 */
static bool
similar(const char *a, const char *b, ssize_t len)
{
	while (len) {
		if (isspace((unsigned char)*b)) {	/* whitespace (or \n) to match? */
			if (!isspace((unsigned char)*a))	/* no corresponding whitespace? */
				return false;
			while (len && isspace((unsigned char)*b) && *b != '\n')
				b++, len--;	/* skip pattern whitespace */
			while (isspace((unsigned char)*a) && *a != '\n')
				a++;	/* skip target whitespace */
			if (*a == '\n' || *b == '\n')
				return (*a == *b);	/* should end in sync */
		} else if (*a++ != *b++)	/* match non-whitespace chars */
			return false;
		else
			len--;	/* probably not necessary */
	}
	return true;		/* actually, this is not reached */
	/* since there is always a \n */
}
