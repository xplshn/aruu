package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// config holds strict key-value records from config.kv
type Config map[string]string

var configKeyPattern = regexp.MustCompile(`^[A-Z][A-Z0-9_]*$`)
var featureNamePattern = regexp.MustCompile(`^FEATURE_[A-Z][A-Z0-9_]*$`)

func parseConfigKv(path string) (Config, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	cfg := make(Config)
	sc := bufio.NewScanner(f)
	lineNo := 0
	for sc.Scan() {
		lineNo++
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		eq := strings.IndexByte(line, '=')
		if eq < 0 {
			return nil, fmt.Errorf("line %d: missing =", lineNo)
		}
		key := strings.TrimSpace(line[:eq])
		if !configKeyPattern.MatchString(key) {
			return nil, fmt.Errorf("line %d: invalid key %q", lineNo, key)
		}
		if strings.HasPrefix(key, "FEATURE") && !featureNamePattern.MatchString(key) {
			return nil, fmt.Errorf("line %d: invalid feature name %q", lineNo, key)
		}
		if _, ok := cfg[key]; ok {
			return nil, fmt.Errorf("line %d: duplicate key %q", lineNo, key)
		}

		raw := strings.TrimSpace(line[eq+1:])
		value, err := strconv.Unquote(raw)
		if err != nil {
			return nil, fmt.Errorf("line %d: invalid quoted value for %s: %w", lineNo, key, err)
		}
		cfg[key] = value
	}
	if err := sc.Err(); err != nil {
		return nil, err
	}

	return cfg, nil
}

func (cfg Config) isEnabled(key string) bool {
	return cfg[key] == "1"
}

// preprocessor condition stack for nested feature blocks
type ifFrame struct {
	active   bool
	seen     bool
	elseSeen bool
}

type ifStack struct {
	frames []ifFrame
}

func (s *ifStack) push(active, seen bool) {
	s.frames = append(s.frames, ifFrame{active: active, seen: seen})
}

func (s *ifStack) pop() {
	if len(s.frames) > 0 {
		s.frames = s.frames[:len(s.frames)-1]
	}
}

func (s *ifStack) parentActive() bool {
	for i := 0; i < len(s.frames)-1; i++ {
		if !s.frames[i].active {
			return false
		}
	}
	return true
}

func (s *ifStack) globallyActive() bool {
	for _, f := range s.frames {
		if !f.active {
			return false
		}
	}
	return true
}

func (s *ifStack) top() *ifFrame {
	if len(s.frames) == 0 {
		return nil
	}
	return &s.frames[len(s.frames)-1]
}

var conditionPattern = regexp.MustCompile(`^(?:(!?)defined\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)|(!?)([A-Za-z_][A-Za-z0-9_]*)(?:\s*(==|!=)\s*([01]))?)$`)

func validateConditionalName(name string) error {
	if strings.HasPrefix(name, "FEATURE") && !featureNamePattern.MatchString(name) {
		return fmt.Errorf("invalid feature name %q", name)
	}
	return nil
}

func evalCondition(expr string, cfg Config) (bool, error) {
	expr = strings.TrimSpace(expr)
	alternatives := strings.Split(expr, "||")
	result := false
	for _, alternative := range alternatives {
		terms := strings.Split(alternative, "&&")
		termResult := true
		for _, term := range terms {
			value, err := evalConditionAtom(term, cfg)
			if err != nil {
				return false, err
			}
			termResult = termResult && value
		}
		result = result || termResult
	}
	return result, nil
}

func evalConditionAtom(expr string, cfg Config) (bool, error) {
	match := conditionPattern.FindStringSubmatch(strings.TrimSpace(expr))
	if match == nil {
		return false, fmt.Errorf("unsupported preprocessor condition %q", expr)
	}

	not := match[1] != ""
	definedName := match[2]
	plainNot := match[3] != ""
	plainName := match[4]
	operator := match[5]
	value := match[6]

	if definedName != "" {
		if err := validateConditionalName(definedName); err != nil {
			return false, err
		}
		_, ok := cfg[definedName]
		return ok != not, nil
	}
	if err := validateConditionalName(plainName); err != nil {
		return false, err
	}
	if operator == "" {
		return cfg[plainName] == "1" != plainNot, nil
	}
	result := cfg[plainName] == value
	if operator == "!=" {
		result = !result
	}
	return result, nil
}

// section inference from file path
func inferSection(path string) int {
	parts := strings.Split(filepath.ToSlash(path), "/")
	if len(parts) < 3 {
		return 1
	}
	switch parts[1] {
	case "linux", "net", "xsi":
		return 8
	default:
		return 1
	}
}

func validateSourcePath(path string) error {
	if filepath.IsAbs(path) || filepath.Clean(path) != path || strings.Contains(path, "\\") {
		return fmt.Errorf("source path must be a clean relative path")
	}
	base := filepath.Base(path)
	if filepath.Ext(base) != ".c" || !manNamePattern.MatchString(strings.TrimSuffix(base, ".c")) {
		return fmt.Errorf("invalid generated path %q", path)
	}
	return nil
}

// a renderer turns a parsed page into one output format
type Renderer interface {
	Render() error
}

func newRenderer(format string, page *Page, w io.Writer) (Renderer, error) {
	switch strings.ToLower(format) {
	case "", "mdoc":
		return NewMdocRenderer(page, w), nil
	case "txt":
		return NewTxtRenderer(page, w), nil
	default:
		return nil, fmt.Errorf("unknown format %q (want mdoc or txt)", format)
	}
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}

func run(args []string, stdout, stderr io.Writer) int {
	flags := flag.NewFlagSet("mkman", flag.ContinueOnError)
	flags.SetOutput(stderr)
	configPath := flags.String("config", "scripts/mk/config.kv", "path to config.kv")
	sectionFlag := flags.Int("section", 0, "man section override (0 = infer from path)")
	dateFlag := flags.String("date", "", "date string for TH line (default: current month/year)")
	formatFlag := flags.String("fmt", "mdoc", "output format: mdoc or txt")
	outputPath := flags.String("out", "", "write output to this path instead of standard output")
	if err := flags.Parse(args); err != nil {
		return 1
	}

	if flags.NArg() != 1 {
		fmt.Fprintln(stderr, "usage: mkman [-config scripts/mk/config.kv] [-section N] [-fmt mdoc|txt] [-out file] file.c")
		return 1
	}
	cfile := flags.Arg(0)
	if err := validateSourcePath(cfile); err != nil {
		fmt.Fprintf(stderr, "mkman: %v\n", err)
		return 1
	}

	cfg, err := parseConfigKv(*configPath)
	if err != nil {
		fmt.Fprintf(stderr, "mkman: config: %v\n", err)
		return 1
	}

	section := *sectionFlag
	if section == 0 {
		section = inferSection(cfile)
	}
	if section != 1 && section != 8 {
		fmt.Fprintf(stderr, "mkman: invalid man section %d\n", section)
		return 1
	}

	date := *dateFlag
	if date == "" {
		date = time.Now().Format("January 2, 2006")
	}

	page, err := ParsePage(cfile, cfg, section, date)
	if err != nil {
		fmt.Fprintf(stderr, "mkman: scan: %v\n", err)
		return 1
	}
	if page == nil {
		fmt.Fprintf(stderr, "mkman: %s: no ?man comments found\n", cfile)
		return 0
	}

	var output bytes.Buffer
	renderer, err := newRenderer(*formatFlag, page, &output)
	if err != nil {
		fmt.Fprintf(stderr, "mkman: %v\n", err)
		return 1
	}
	if err := renderer.Render(); err != nil {
		fmt.Fprintf(stderr, "mkman: render: %v\n", err)
		return 1
	}

	outProvided := false
	flags.Visit(func(f *flag.Flag) {
		if f.Name == "out" {
			outProvided = true
		}
	})
	if outProvided {
		if strings.TrimSpace(*outputPath) == "" {
			fmt.Fprintln(stderr, "mkman: output path must not be empty")
			return 1
		}
		if err := os.WriteFile(*outputPath, output.Bytes(), 0666); err != nil {
			fmt.Fprintf(stderr, "mkman: write %s: %v\n", *outputPath, err)
			return 1
		}
		return 0
	}

	n, err := stdout.Write(output.Bytes())
	if err == nil && n != output.Len() {
		err = io.ErrShortWrite
	}
	if err != nil {
		fmt.Fprintf(stderr, "mkman: write stdout: %v\n", err)
		return 1
	}
	return 0
}
