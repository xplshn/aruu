package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestParseConfigKv(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.kv")
	content := "NAME=\"a \\\"quoted\\\" path\\\\name\"\nEMPTY=\"\"\nFEATURE_A=\"1\"\n"
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	cfg, err := parseConfigKv(path)
	if err != nil {
		t.Fatal(err)
	}
	if cfg["NAME"] != "a \"quoted\" path\\name" || cfg["EMPTY"] != "" {
		t.Fatalf("unexpected config: %#v", cfg)
	}
}

func TestParseConfigKvRejectsMalformedRecords(t *testing.T) {
	tests := []struct {
		name    string
		content string
	}{
		{"missing separator", "NAME\n"},
		{"invalid key", "name=\"x\"\n"},
		{"unquoted value", "NAME=x\n"},
		{"duplicate key", "NAME=\"x\"\nNAME=\"y\"\n"},
		{"invalid feature name", "FEATUREbad=\"1\"\n"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			path := filepath.Join(t.TempDir(), "config.kv")
			if err := os.WriteFile(path, []byte(test.content), 0600); err != nil {
				t.Fatal(err)
			}
			if _, err := parseConfigKv(path); err == nil {
				t.Fatal("parseConfigKv accepted malformed input")
			}
		})
	}
}

func TestEvalConditionCompatibility(t *testing.T) {
	cfg := Config{"FEATURE_A": "1", "FEATURE_B": "0"}
	for _, test := range []struct {
		expr string
		want bool
	}{
		{"FEATURE_A", true},
		{"!FEATURE_B", true},
		{"FEATURE_B == 0", true},
		{"defined(FEATURE_A)", true},
		{"!defined(FEATURE_C)", true},
		{"defined(_DEFAULT_SOURCE)", false},
		{"FEATURE_A && FEATURE_B", false},
		{"FEATURE_A || FEATURE_B", true},
	} {
		got, err := evalCondition(test.expr, cfg)
		if err != nil {
			t.Fatalf("evalCondition(%q): %v", test.expr, err)
		}
		if got != test.want {
			t.Errorf("evalCondition(%q) = %v, want %v", test.expr, got, test.want)
		}
	}
}

func TestParsePageRejectionClasses(t *testing.T) {
	tests := []struct {
		name    string
		content string
	}{
		{
			name:    "malformed command metadata",
			content: "// ?man cmd:\n",
		},
		{
			name:    "unsupported conditional",
			content: "// ?man cmd: summary\n#if (FEATURE_A)\n// ?man -a: use a\n#endif\n",
		},
		{
			name:    "invalid feature name",
			content: "// ?man cmd: summary\n#ifdef FEATUREbad\n// ?man -a: use a\n#endif\n",
		},
		{
			name:    "duplicate option",
			content: "// ?man cmd: summary\n// ?man -a: first\n// ?man -a: second\n",
		},
		{
			name:    "empty type suffix",
			content: "// ?man cmd: summary\n// ?man -a:: use a\n",
		},
		{
			name:    "invalid type suffix",
			content: "// ?man cmd: summary\n// ?man -a:bad/type: use a\n",
		},
		{
			name:    "malformed group",
			content: "// ?man cmd: summary\n// ?man -a{1bad}: use a\n// ?man -b: use b\n",
		},
		{
			name:    "misplaced group",
			content: "// ?man cmd: summary\n// ?man -a{x}y: use a\n// ?man -b: use b\n",
		},
		{
			name:    "singleton group",
			content: "// ?man cmd: summary\n// ?man -a{choice}: use a\n",
		},
		{
			name:    "mixed required group",
			content: "// ?man cmd: summary\n// ?man -a{choice}!: use a\n// ?man -b{choice}: use b\n",
		},
		{
			name:    "unclosed synopsis group",
			content: "// ?man cmd: summary\n// ?man synopsis: [-a\n",
		},
		{
			name:    "mismatched synopsis group",
			content: "// ?man cmd: summary\n// ?man synopsis: [-a}\n",
		},
		{
			name:    "empty synopsis group",
			content: "// ?man cmd: summary\n// ?man synopsis: []\n",
		},
		{
			name:    "misplaced synopsis pipe",
			content: "// ?man cmd: summary\n// ?man synopsis: -a || -b\n",
		},
		{
			name:    "synopsis without colon",
			content: "// ?man cmd: summary\n// ?man synopsis\n",
		},
		{
			name:    "empty arguments",
			content: "// ?man cmd: summary\n// ?man arguments:\n",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if _, err := parseSource(t, test.content, Config{"FEATURE_A": "1"}); err == nil {
				t.Fatal("ParsePage accepted malformed metadata")
			}
		})
	}
}

func TestParseDirectiveComments(t *testing.T) {
	content := "// ?man cmd: summary\n" +
		"#ifndef O_SEARCH /* not present on some bsds */\n" +
		"// ?man -a: use a\n" +
		"#endif /* O_SEARCH */\n" +
		"#ifdef FEATURE_A /* gated\n" +
		"                    feature\n" +
		"                    still */\n" +
		"// ?man -b: use b\n" +
		"#endif\n" +
		"#if defined(FEATURE_A) /* a // b */\n" +
		"// ?man -c: use c\n" +
		"#endif\n"
	page, err := parseSource(t, content, Config{"FEATURE_A": "1"})
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Options) != 3 {
		t.Fatalf("got %d options, want 3", len(page.Options))
	}
}

func TestParseOptionCompatibility(t *testing.T) {
	for _, line := range []string{
		"-a: use a",
		"-a:str: use a",
		"-?: use question",
		"--color:when: use color",
		"-a{mode}!: use mode a",
		"-a:str{mode}!: use mode a",
	} {
		if _, err := parseOption(line); err != nil {
			t.Errorf("parseOption(%q): %v", line, err)
		}
	}
}

func TestParseSynopsisCompatibility(t *testing.T) {
	for _, raw := range []string{
		"[-a] file ...",
		"(-f | -u) mountpoint",
		"| -l num",
		"+format | mmddHHMM[[CC]yy]",
		"[-aBbdipTtw] [-c|-e|-f]",
		"[x | t | -x | -t] [file ...]",
	} {
		if _, err := parseSynopsis(raw, false); err != nil {
			t.Errorf("parseSynopsis(%q): %v", raw, err)
		}
	}
}

func TestExplicitEmptySynopsisCompatibility(t *testing.T) {
	page, err := parseSource(t, "// ?man sync: flush disk cache\n// ?man synopsis:\n// ?man flush pending writes\n", Config{})
	if err != nil {
		t.Fatal(err)
	}
	if len(page.Synopsis) != 0 {
		t.Fatalf("empty explicit synopsis produced %d forms", len(page.Synopsis))
	}
}

func TestValidateSourcePath(t *testing.T) {
	for _, path := range []string{"cmd/posix/cat.c", "cmd/nac/main.c", "ports/pkgtools/dtr/main.c"} {
		if err := validateSourcePath(path); err != nil {
			t.Errorf("validateSourcePath(%q): %v", path, err)
		}
	}
	for _, path := range []string{"cmd/posix/../cat.c", "cmd/posix/cat"} {
		if err := validateSourcePath(path); err == nil {
			t.Errorf("validateSourcePath accepted %q", path)
		}
	}
}

type failingWriter struct{}

func (failingWriter) Write([]byte) (int, error) {
	return 0, errors.New("write failed")
}

type shortWriter struct{}

func (shortWriter) Write(p []byte) (int, error) {
	return len(p) - 1, nil
}

func TestRenderWriteFailure(t *testing.T) {
	page := &Page{Name: "cat", Summary: "concatenate files", Section: 1, Date: "January 2, 2006"}
	for _, writer := range []io.Writer{failingWriter{}, shortWriter{}} {
		if err := NewMdocRenderer(page, writer).Render(); err == nil {
			t.Fatal("mdoc renderer accepted a failed write")
		}
		if err := NewTxtRenderer(page, writer).Render(); err == nil {
			t.Fatal("text renderer accepted a failed write")
		}
	}
}

func TestRunRejectionsAndOutput(t *testing.T) {
	dir := t.TempDir()
	sourceDir := filepath.Join(dir, "cmd", "posix")
	if err := os.MkdirAll(sourceDir, 0700); err != nil {
		t.Fatal(err)
	}
	source := filepath.Join(sourceDir, "bad.c")
	if err := os.WriteFile(source, []byte("// ?man bad:\n"), 0600); err != nil {
		t.Fatal(err)
	}
	config := filepath.Join(dir, "config.kv")
	if err := os.WriteFile(config, []byte("FEATURE_A=\"1\"\n"), 0600); err != nil {
		t.Fatal(err)
	}

	oldwd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := os.Chdir(oldwd); err != nil {
			t.Errorf("restore working directory: %v", err)
		}
	})

	validSource := "// ?man bad: reject bad input\n// ?man synopsis: [file ...]\n"
	for _, test := range []struct {
		name   string
		source string
		args   []string
		want   string
	}{
		{
			name:   "malformed metadata",
			source: "// ?man bad:\n",
			args:   []string{"-config", config, "cmd/posix/bad.c"},
			want:   "malformed command metadata",
		},
		{
			name:   "empty output path",
			source: validSource,
			args:   []string{"-config", config, "-out", "", "cmd/posix/bad.c"},
			want:   "output path must not be empty",
		},
		{
			name:   "output write failure",
			source: validSource,
			args:   []string{"-config", config, "-out", ".", "cmd/posix/bad.c"},
			want:   "write .",
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			if err := os.WriteFile(source, []byte(test.source), 0600); err != nil {
				t.Fatal(err)
			}
			var stdout bytes.Buffer
			var stderr bytes.Buffer
			if status := run(test.args, &stdout, &stderr); status == 0 {
				t.Fatal("run accepted malformed input")
			}
			if !strings.Contains(stderr.String(), test.want) {
				t.Fatalf("stderr %q does not contain %q", stderr.String(), test.want)
			}
		})
	}

	if err := os.WriteFile(source, []byte("// ?man bad: reject bad input\n// ?man synopsis: [file ...]\n"), 0600); err != nil {
		t.Fatal(err)
	}
	out := filepath.Join(dir, "bad.1")
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	if status := run([]string{"-config", config, "-out", out, "cmd/posix/bad.c"}, &stdout, &stderr); status != 0 {
		t.Fatalf("run failed: %s", stderr.String())
	}
	if stdout.Len() != 0 {
		t.Fatalf("run wrote to standard output: %q", stdout.String())
	}
	written, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Contains(written, []byte(".Dt BAD 1\n")) {
		t.Fatalf("unexpected output:\n%s", written)
	}

	var shortOutput shortWriter
	stderr.Reset()
	if status := run([]string{"-config", config, "cmd/posix/bad.c"}, &shortOutput, &stderr); status == 0 {
		t.Fatal("run accepted a short standard-output write")
	}
	if !strings.Contains(stderr.String(), "write stdout: short write") {
		t.Fatalf("unclear standard-output write error: %s", stderr.String())
	}
}

func TestRenderCompatibility(t *testing.T) {
	page, err := parseSource(t, `// ?man cat: concatenate files
// ?man arguments: [-abt] [file ...]
// ?man concatenate FILE to standard output
// ?man -a: answer all
// ?man -b:str: ignore expanded tabs
`, Config{})
	if err != nil {
		t.Fatal(err)
	}
	page.Date = "January 2, 2006"

	var mdoc bytes.Buffer
	if err := NewMdocRenderer(page, &mdoc).Render(); err != nil {
		t.Fatal(err)
	}
	wantMdoc := ".Dd January 2, 2006\n.Dt CAT 1\n.Os aruu\n.Sh NAME\n.Nm cat\n.Nd concatenate files\n.Sh SYNOPSIS\n.Nm cat\n.Op Fl a\n.Op Fl b Ar str\n.Op Fl a Fl b Fl t\n.Op Ar file ...\n.Sh DESCRIPTION\n.Pp\n.No concatenate FILE to standard output\n.Sh OPTIONS\n.Bl -tag -width Ds\n.It Fl a\n.Pp\n.No answer all\n.It Fl b Ar str\n.Pp\n.No ignore expanded tabs\n.El\n"
	if mdoc.String() != wantMdoc {
		t.Fatalf("mdoc output changed:\n%s", mdoc.String())
	}

	var text bytes.Buffer
	if err := NewTxtRenderer(page, &text).Render(); err != nil {
		t.Fatal(err)
	}
	wantText := "NAME\n       cat - concatenate files\n\nSYNOPSIS\n       cat [-a] [-b str] [-a -b -t] [file ...]\n\nDESCRIPTION\n\n       concatenate FILE to standard output\n\nOPTIONS\n       -a\n\n               answer all\n\n       -b str\n\n               ignore expanded tabs\n"
	if text.String() != wantText {
		t.Fatalf("text output changed:\n%s", text.String())
	}
}

func parseSource(t *testing.T, content string, cfg Config) (*Page, error) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "source.c")
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	page, err := ParsePage(path, cfg, 1, "January 2, 2006")
	if err != nil {
		return nil, err
	}
	if page == nil {
		return nil, fmt.Errorf("ParsePage returned no page")
	}
	return page, nil
}
