package main

import (
	"bufio"
	"fmt"
	"os"
	"regexp"
	"slices"
	"strings"
)

var xrefPattern = regexp.MustCompile(`^\s*([A-Za-z0-9][A-Za-z0-9+_.-]*)\((\d+[A-Za-z]*)\)\s*$`)
var groupPattern = regexp.MustCompile(`\{([A-Za-z0-9_-]+)\}`)

func parseNameSummary(line string) (string, string, bool) {
	line = strings.TrimSpace(line)
	if idx := strings.Index(line, ":"); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+1:]), true
	}
	if idx := strings.Index(line, " \\- "); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+4:]), true
	}
	if idx := strings.Index(line, " - "); idx >= 0 {
		return strings.TrimSpace(line[:idx]), strings.TrimSpace(line[idx+3:]), true
	}
	return "", "", false
}

func extractManLines(path string, cfg Config) ([]string, error) {
	contentBytes, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var lines []string
	stack := &ifStack{}
	sc := bufio.NewScanner(strings.NewReader(string(contentBytes)))
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	inBlockComment := false

	for sc.Scan() {
		line := sc.Text()
		trimmed := strings.TrimSpace(line)

		if inBlockComment {
			if strings.Contains(trimmed, "*/") {
				inBlockComment = false
				idx := strings.Index(trimmed, "*/")
				before := strings.TrimSpace(trimmed[:idx])
				if strings.HasPrefix(before, "* ") {
					before = before[2:]
				} else if before == "*" {
					before = ""
				}
				if before != "" {
					lines = append(lines, stripManPrefix(before))
				}
				continue
			}

			stripped := trimmed
			if strings.HasPrefix(stripped, "* ") {
				stripped = stripped[2:]
			} else if stripped == "*" {
				stripped = ""
			}
			lines = append(lines, stripManPrefix(stripped))
			continue
		}

		if strings.HasPrefix(trimmed, "#") {
			directive := trimmed[1:]
			if ci := strings.Index(directive, "// "); ci >= 0 {
				directive = directive[:ci]
			}
			directive = strings.TrimSpace(directive)

			switch {
			case strings.HasPrefix(directive, "ifdef "):
				key := strings.TrimSpace(directive[6:])
				_, defined := cfg[key]
				pa := stack.parentActive()
				stack.push(pa && defined, defined)
			case strings.HasPrefix(directive, "ifndef "):
				key := strings.TrimSpace(directive[7:])
				_, defined := cfg[key]
				pa := stack.parentActive()
				stack.push(pa && !defined, !defined)
			case strings.HasPrefix(directive, "if "):
				expr := strings.TrimSpace(directive[3:])
				result := evalCondition(expr, cfg)
				pa := stack.parentActive()
				stack.push(pa && result, result)
			case directive == "else":
				if top := stack.top(); top != nil {
					pa := stack.parentActive()
					top.active = pa && !top.seen
					top.seen = true
				}
			case strings.HasPrefix(directive, "elif "):
				if top := stack.top(); top != nil {
					expr := strings.TrimSpace(directive[5:])
					result := evalCondition(expr, cfg)
					pa := stack.parentActive()
					top.active = pa && !top.seen && result
					if result {
						top.seen = true
					}
				}
			case directive == "endif":
				stack.pop()
			}
			continue
		}

		if !stack.globallyActive() {
			continue
		}

		if strings.Contains(trimmed, "/* ?man") {
			startIdx := strings.Index(trimmed, "/* ?man")
			rest := strings.TrimSpace(trimmed[startIdx+7:])
			if strings.HasSuffix(rest, " */") {
				lines = append(lines, strings.TrimSpace(rest[:len(rest)-2]))
			} else {
				lines = append(lines, rest)
				inBlockComment = true
			}
			continue
		}

		if idx := strings.Index(trimmed, "// ?man"); idx >= 0 {
			lines = append(lines, strings.TrimSpace(trimmed[idx+7:]))
		}
	}

	if err := sc.Err(); err != nil {
		return nil, err
	}
	return lines, nil
}

func stripManPrefix(line string) string {
	if strings.HasPrefix(line, "// ?man ") {
		return line[8:]
	}
	if strings.HasPrefix(line, "// ?man") {
		return line[7:]
	}
	return line
}

func ParsePage(path string, cfg Config, section int, date string) (*Page, error) {
	lines, err := extractManLines(path, cfg)
	if err != nil {
		return nil, err
	}

	page := &Page{Section: section, Date: date}

	var descriptionLines []string
	var sections []rawSection
	var currentSection *rawSection
	var sawContent bool
	var fallbackArgs string
	var rawOptions []rawOption
	var currentOption *rawOption
	optionIndex := make(map[string]int)

	for _, raw := range lines {
		line := strings.TrimSpace(raw)
		if line == "" {
			if currentSection != nil {
				currentSection.Lines = append(currentSection.Lines, "")
			} else if currentOption != nil {
				currentOption.Lines = append(currentOption.Lines, "")
			} else if sawContent {
				descriptionLines = append(descriptionLines, "")
			}
			continue
		}

		if page.Name == "" && !strings.HasPrefix(line, "-") && !strings.HasPrefix(line, "#") {
			if name, summary, ok := parseNameSummary(line); ok {
				page.Name = name
				page.Summary = summary
				sawContent = true
				continue
			}
		}

		lower := strings.ToLower(line)
		switch {
		case strings.HasPrefix(lower, "synopsis:"):
			currentOption = nil
			form := strings.TrimSpace(line[len("synopsis:"):])
			if form != "" {
				page.Synopsis = append(page.Synopsis, parseSynopsis(form, true))
			}
			sawContent = true
			continue
		case strings.HasPrefix(lower, "arguments:"):
			currentOption = nil
			fallbackArgs = strings.TrimSpace(line[len("arguments:"):])
			sawContent = true
			continue
		case strings.HasPrefix(line, "## "):
			currentOption = nil
			title := strings.TrimSpace(line[3:])
			sections = append(sections, rawSection{Title: title})
			currentSection = &sections[len(sections)-1]
			sawContent = true
			continue
		}

		if currentSection == nil {
			if opt, ok := parseOption(line); ok {
				rawOptions = append(rawOptions, rawOption{
					Option: opt,
					Lines:  []string{opt.Desc},
				})
				currentOption = &rawOptions[len(rawOptions)-1]
			} else if isCodeLabel(line) || isCodeLabelPrefix(line) {
				continue
			} else if currentOption != nil {
				currentOption.Lines = append(currentOption.Lines, line)
			} else {
				descriptionLines = append(descriptionLines, line)
			}
			sawContent = true
			continue
		}

		currentSection.Lines = append(currentSection.Lines, line)
		sawContent = true
	}

	if !sawContent || page.Name == "" {
		return nil, nil
	}

	// explicit synopsis forms are authoritative. for simple pages we can produce
	// a synopsis from the parsed option specs plus the arguments line
	for _, raw := range rawOptions {
		raw.Option.Body = parseBlocks(raw.Lines, "")
		addOption(page, optionIndex, raw.Option)
	}

	if len(page.Synopsis) == 0 && fallbackArgs != "" {
		page.Synopsis = append(page.Synopsis, synthesizeSynopsis(page.Options, fallbackArgs))
	}

	page.Description = parseBlocks(descriptionLines, "")
	for _, section := range sections {
		page.Sections = append(page.Sections, Section{
			Title:  section.Title,
			Blocks: parseBlocks(section.Lines, section.Title),
		})
	}
	normalizePageInlines(page)

	return page, nil
}

type rawSection struct {
	Title string
	Lines []string
}

type rawOption struct {
	Option Option
	Lines  []string
}

func parseOption(line string) (Option, bool) {
	if !strings.HasPrefix(line, "-") {
		return Option{}, false
	}

	parts := strings.Split(line, ":")
	if len(parts) < 2 {
		return Option{}, false
	}

	specParts := []string{strings.TrimSpace(parts[0])}
	i := 1
	for i < len(parts)-1 {
		part := strings.TrimSpace(parts[i])
		if !looksLikeOptionArg(part) {
			break
		}
		specParts = append(specParts, part)
		i++
	}

	desc := strings.TrimSpace(strings.Join(parts[i:], ":"))
	flag, typ, group, required := splitOptionSpec(strings.Join(specParts, ":"))
	if flag == "" {
		return Option{}, false
	}

	rawSpec := flag
	if typ != "" {
		rawSpec += " " + typ
	}
	spec := normalizeOptionSpec(rawSpec)
	desc = stripRepeatedSpec(spec, desc)
	if spec == "" || desc == "" {
		return Option{}, false
	}

	return Option{
		Spec:     parseSynopsis(spec, false).Items,
		Desc:     desc,
		Key:      synopsisKey(spec) + "\x00" + group,
		Group:    group,
		Required: required,
	}, true
}

// splitoptionspec peels the required marker (!) and the mutual exclusion
// group marker ({name}) off a raw option spec, leaving the bare flag and
// its optional colon-delimited type suffix
func splitOptionSpec(raw string) (flag, typ, group string, required bool) {
	raw = strings.TrimSpace(raw)
	if strings.HasSuffix(raw, "!") {
		required = true
		raw = raw[:len(raw)-1]
	}
	if m := groupPattern.FindStringSubmatchIndex(raw); m != nil {
		group = raw[m[2]:m[3]]
		raw = raw[:m[0]] + raw[m[1]:]
	}
	if idx := strings.Index(raw, ":"); idx >= 0 {
		flag = raw[:idx]
		typ = raw[idx+1:]
	} else {
		flag = raw
	}
	return flag, typ, group, required
}

func looksLikeOptionArg(s string) bool {
	if s == "" {
		return false
	}
	return !strings.ContainsAny(s, " \t")
}

func normalizeOptionSpec(spec string) string {
	spec = strings.TrimSpace(spec)
	if spec == "" {
		return ""
	}

	parts := strings.Split(spec, ":")
	if len(parts) == 1 {
		return strings.Join(strings.Fields(spec), " ")
	}

	normalized := []string{strings.TrimSpace(parts[0])}
	for _, part := range parts[1:] {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		normalized = append(normalized, part)
	}
	return strings.Join(normalized, " ")
}

func stripRepeatedSpec(spec, desc string) string {
	spec = strings.TrimSpace(spec)
	desc = strings.TrimSpace(desc)
	candidates := []string{
		spec + ":",
		strings.ReplaceAll(spec, " ", ":") + ":",
	}
	for _, candidate := range candidates {
		if strings.HasPrefix(desc, candidate) {
			return strings.TrimSpace(desc[len(candidate):])
		}
	}
	return desc
}

func addOption(page *Page, index map[string]int, opt Option) {
	if idx, ok := index[opt.Key]; ok {
		if betterOptionDesc(opt.Desc, page.Options[idx].Desc) {
			page.Options[idx].Desc = opt.Desc
			page.Options[idx].Spec = opt.Spec
			page.Options[idx].Body = opt.Body
		}
		return
	}

	primary := optionPrimaryFlag(opt)
	if primary != "" {
		for idx, existing := range page.Options {
			if optionPrimaryFlag(existing) != primary {
				continue
			}
			if betterOptionDesc(opt.Desc, existing.Desc) {
				page.Options[idx].Desc = opt.Desc
				page.Options[idx].Spec = opt.Spec
				page.Options[idx].Body = opt.Body
				delete(index, existing.Key)
				index[opt.Key] = idx
			}
			return
		}
	}

	index[opt.Key] = len(page.Options)
	page.Options = append(page.Options, opt)
}

func betterOptionDesc(newDesc, oldDesc string) bool {
	return optionDescScore(newDesc) > optionDescScore(oldDesc)
}

func optionDescScore(desc string) int {
	desc = strings.TrimSpace(desc)
	score := len(desc)
	lower := strings.ToLower(desc)
	if strings.HasPrefix(lower, "specify ") && strings.HasSuffix(lower, " option") {
		score -= 1000
	}
	return score
}

func optionPrimaryFlag(opt Option) string {
	for _, item := range opt.Spec {
		if item.Kind == SynFlag {
			return item.Text
		}
	}
	return ""
}

func synopsisKey(spec string) string {
	return strings.Join(strings.Fields(spec), " ")
}

func isCodeLabel(line string) bool {
	if !strings.HasSuffix(line, ":") {
		return false
	}
	for _, r := range line[:len(line)-1] {
		if (r < 'A' || r > 'Z') && (r < '0' || r > '9') && r != '_' {
			return false
		}
	}
	return len(line) > 1
}

func isCodeLabelPrefix(line string) bool {
	idx := strings.IndexByte(line, ':')
	if idx <= 0 {
		return false
	}
	return isCodeLabel(line[:idx+1])
}

func parseBlocks(lines []string, title string) []Block {
	lines = trimBlankLines(lines)
	if len(lines) == 0 {
		return nil
	}

	if blocks, ok := parseSubsections(lines, title); ok {
		return blocks
	}

	if items, ok := parseHeadingItems(lines); ok {
		return []Block{{Kind: BlockTaggedList, Items: items}}
	}

	if items, ok := parseSequentialTaggedItems(lines); ok {
		return []Block{{Kind: BlockTaggedList, Items: items}}
	}

	paragraphs := splitParagraphs(lines)
	var blocks []Block

	for _, paragraph := range paragraphs {
		if len(paragraph) == 0 {
			continue
		}

		if isSeeAlsoTitle(title) {
			if refs, ok := parseXRefs(strings.Join(paragraph, " ")); ok {
				slices.SortFunc(refs, func(a, b XRef) int {
					if a.Section != b.Section {
						return strings.Compare(a.Section, b.Section)
					}
					return strings.Compare(strings.ToLower(a.Name), strings.ToLower(b.Name))
				})
				blocks = append(blocks, Block{Kind: BlockSeeAlso, Refs: refs})
				continue
			}
		}

		if item, ok := parseTaggedItem(paragraph); ok {
			if len(blocks) != 0 && blocks[len(blocks)-1].Kind == BlockTaggedList {
				blocks[len(blocks)-1].Items = append(blocks[len(blocks)-1].Items, item)
			} else {
				blocks = append(blocks, Block{Kind: BlockTaggedList, Items: []Item{item}})
			}
			continue
		}

		blocks = append(blocks, Block{
			Kind:    BlockParagraph,
			Inlines: parseInlines(strings.Join(paragraph, " ")),
		})
	}

	return blocks
}

func parseSubsections(lines []string, title string) ([]Block, bool) {
	hasHeading := false
	for _, line := range lines {
		if strings.HasPrefix(strings.TrimSpace(line), "### ") {
			hasHeading = true
			break
		}
	}
	if !hasHeading {
		return nil, false
	}

	var blocks []Block
	var preamble []string
	for len(lines) != 0 && !strings.HasPrefix(strings.TrimSpace(lines[0]), "### ") {
		preamble = append(preamble, lines[0])
		lines = lines[1:]
	}
	if parsed := parseBlocks(preamble, title); len(parsed) != 0 {
		blocks = append(blocks, parsed...)
	}

	for len(lines) != 0 {
		head := strings.TrimSpace(lines[0])
		if !strings.HasPrefix(head, "### ") {
			return nil, false
		}
		subtitle := strings.TrimSpace(head[4:])
		lines = lines[1:]
		var body []string
		for len(lines) != 0 && !strings.HasPrefix(strings.TrimSpace(lines[0]), "### ") {
			body = append(body, lines[0])
			lines = lines[1:]
		}
		blocks = append(blocks, Block{
			Kind:  BlockSubsection,
			Title: subtitle,
			Blocks: parseBlocks(body, subtitle),
		})
	}

	return blocks, true
}

func trimBlankLines(lines []string) []string {
	start := 0
	for start < len(lines) && strings.TrimSpace(lines[start]) == "" {
		start++
	}
	end := len(lines)
	for end > start && strings.TrimSpace(lines[end-1]) == "" {
		end--
	}
	return lines[start:end]
}

func splitParagraphs(lines []string) [][]string {
	var paragraphs [][]string
	var current []string
	for _, line := range lines {
		if strings.TrimSpace(line) == "" {
			if len(current) != 0 {
				paragraphs = append(paragraphs, current)
				current = nil
			}
			continue
		}
		current = append(current, line)
	}
	if len(current) != 0 {
		paragraphs = append(paragraphs, current)
	}
	return paragraphs
}

func parseHeadingItems(lines []string) ([]Item, bool) {
	var items []Item
	var current *Item
	for _, line := range lines {
		if strings.TrimSpace(line) == "" {
			continue
		}
		if strings.HasPrefix(line, "### ") {
			items = append(items, Item{Label: parseInlines(strings.TrimSpace(line[4:]))})
			current = &items[len(items)-1]
			continue
		}
		if current == nil {
			return nil, false
		}
		if len(current.Body) != 0 {
			current.Body = append(current.Body, Inline{Kind: InlineText, Text: " "})
		}
		current.Body = append(current.Body, parseInlines(strings.TrimSpace(line))...)
	}
	return items, len(items) != 0
}

func parseSequentialTaggedItems(lines []string) ([]Item, bool) {
	var items []Item
	for i := 0; i < len(lines); {
		for i < len(lines) && strings.TrimSpace(lines[i]) == "" {
			i++
		}
		if i >= len(lines) {
			break
		}

		label := strings.TrimSpace(lines[i])
		if label == "" || i+1 >= len(lines) {
			return nil, false
		}
		first := strings.TrimSpace(lines[i+1])
		if !strings.HasPrefix(first, ":") {
			return nil, false
		}

		bodyLines := []string{strings.TrimSpace(strings.TrimPrefix(first, ":"))}
		i += 2
		for i < len(lines) {
			line := strings.TrimSpace(lines[i])
			if line == "" {
				i++
				break
			}
			if i+1 < len(lines) && strings.TrimSpace(lines[i]) != "" &&
				strings.HasPrefix(strings.TrimSpace(lines[i+1]), ":") {
				break
			}
			bodyLines = append(bodyLines, line)
			i++
		}

		items = append(items, Item{
			Label: parseInlines(label),
			Body:  parseInlines(strings.Join(compactLines(bodyLines), " ")),
		})
	}
	return items, len(items) != 0
}

func parseTaggedItem(lines []string) (Item, bool) {
	if len(lines) < 2 {
		return Item{}, false
	}
	label := strings.TrimSpace(lines[0])
	bodyLines := lines[1:]
	if label == "" {
		return Item{}, false
	}

	first := strings.TrimSpace(bodyLines[0])
	if !strings.HasPrefix(first, ":") {
		return Item{}, false
	}

	bodyLines[0] = strings.TrimSpace(strings.TrimPrefix(first, ":"))
	return Item{
		Label: parseInlines(label),
		Body:  parseInlines(strings.Join(compactLines(bodyLines), " ")),
	}, true
}

func parseInlines(s string) []Inline {
	var out []Inline
	var text strings.Builder
	flushText := func() {
		if text.Len() == 0 {
			return
		}
		out = append(out, Inline{Kind: InlineText, Text: text.String()})
		text.Reset()
	}

	// inline semantics are parsed here so the renderer never has to recover
	// any richness like paths, flags, emphasis, or xrefs by reparsing raw strings
	for i := 0; i < len(s); {
		switch s[i] {
		case '`':
			end := strings.IndexByte(s[i+1:], '`')
			if end < 0 {
				text.WriteByte(s[i])
				i++
				continue
			}
			end += i + 1
			flushText()
			out = append(out, classifyInlineLiteral(s[i+1:end]))
			i = end + 1
		case '_':
			end := strings.IndexByte(s[i+1:], '_')
			if end < 0 {
				text.WriteByte(s[i])
				i++
				continue
			}
			end += i + 1
			flushText()
			out = append(out, Inline{
				Kind:     InlineEmph,
				Children: parseInlines(s[i+1 : end]),
			})
			i = end + 1
		default:
			text.WriteByte(s[i])
			i++
		}
	}

	flushText()
	return out
}

func classifyInlineLiteral(s string) Inline {
	s = strings.TrimSpace(s)
	if match := xrefPattern.FindStringSubmatch(s); match != nil {
		return Inline{Kind: InlineXRef, Text: match[1], Section: match[2]}
	}
	if strings.HasPrefix(s, "/") {
		return Inline{Kind: InlinePath, Text: s}
	}
	if strings.HasPrefix(s, "-") {
		return Inline{Kind: InlineFlag, Text: strings.TrimPrefix(s, "-")}
	}
	return Inline{Kind: InlineLiteral, Text: s}
}

func synthesizeSynopsis(options []Option, args string) SynopsisForm {
	type unit struct {
		sortKey string
		items   []SynopsisItem
	}

	groups := make(map[string][]Option)
	var groupOrder []string
	var units []unit

	for _, opt := range options {
		if opt.Group == "" {
			if opt.Required {
				units = append(units, unit{
					sortKey: optionSortKey(opt),
					items:   cloneSynopsisItems(opt.Spec),
				})
			} else {
				units = append(units, unit{
					sortKey: optionSortKey(opt),
					items: []SynopsisItem{{
						Kind:     SynOptional,
						Children: cloneSynopsisItems(opt.Spec),
					}},
				})
			}
			continue
		}
		if _, seen := groups[opt.Group]; !seen {
			groupOrder = append(groupOrder, opt.Group)
		}
		groups[opt.Group] = append(groups[opt.Group], opt)
	}

	for _, name := range groupOrder {
		members := groups[name]
		required := false
		for _, m := range members {
			if m.Required {
				required = true
				break
			}
		}

		var children []SynopsisItem
		for i, m := range members {
			if i != 0 {
				children = append(children, SynopsisItem{Kind: SynPipe, Text: "|"})
			}
			children = append(children, cloneSynopsisItems(m.Spec)...)
		}

		kind := SynOptional
		if required {
			kind = SynRequiredGroup
		}
		units = append(units, unit{
			sortKey: optionSortKey(members[0]),
			items:   []SynopsisItem{{Kind: kind, Children: children}},
		})
	}

	slices.SortFunc(units, func(a, b unit) int {
		return strings.Compare(a.sortKey, b.sortKey)
	})

	var items []SynopsisItem
	for _, u := range units {
		items = append(items, u.items...)
	}
	items = append(items, parseSynopsis(args, false).Items...)
	return SynopsisForm{Items: items}
}

func optionSortKey(opt Option) string {
	flag := optionPrimaryFlag(opt)
	if flag == "" {
		return "zzzz"
	}
	r := flag[0]
	prefix := "2"
	switch {
	case r >= '0' && r <= '9':
		prefix = "0"
	case r >= 'A' && r <= 'Z':
		prefix = "1"
	}
	return prefix + flag
}

func cloneSynopsisItems(items []SynopsisItem) []SynopsisItem {
	out := make([]SynopsisItem, len(items))
	for i, item := range items {
		out[i] = item
		if len(item.Children) != 0 {
			out[i].Children = cloneSynopsisItems(item.Children)
		}
	}
	return out
}

func compactLines(lines []string) []string {
	var out []string
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line != "" {
			out = append(out, line)
		}
	}
	return out
}

func isSeeAlsoTitle(title string) bool {
	return strings.EqualFold(strings.TrimSpace(title), "SEE ALSO")
}

func parseXRefs(s string) ([]XRef, bool) {
	var refs []XRef
	for _, part := range strings.Split(s, ",") {
		part = strings.TrimSpace(part)
		match := xrefPattern.FindStringSubmatch(part)
		if match == nil {
			return nil, false
		}
		refs = append(refs, XRef{Name: match[1], Section: match[2]})
	}
	return refs, len(refs) != 0
}

func parseSynopsis(raw string, explicit bool) SynopsisForm {
	tokens := tokenizeSynopsis(raw)
	items, _ := parseSynopsisSeq(tokens, 0, explicit, true)
	return SynopsisForm{Items: items}
}

func tokenizeSynopsis(raw string) []string {
	var tokens []string
	var current strings.Builder
	flush := func() {
		if current.Len() != 0 {
			tokens = append(tokens, current.String())
			current.Reset()
		}
	}

	for i := 0; i < len(raw); i++ {
		ch := raw[i]
		switch ch {
		case '[', ']', '{', '}', '|':
			flush()
			tokens = append(tokens, string(ch))
		case ' ', '\t', '\n':
			flush()
		default:
			current.WriteByte(ch)
		}
	}
	flush()
	return tokens
}

func parseSynopsisSeq(tokens []string, start int, explicit bool, topLevel bool) ([]SynopsisItem, int) {
	var items []SynopsisItem
	seenNonFlag := false

	for i := start; i < len(tokens); i++ {
		switch tokens[i] {
		case "]", "}":
			return items, i
		case "[":
			children, end := parseSynopsisSeq(tokens, i+1, explicit, false)
			items = append(items, SynopsisItem{Kind: SynOptional, Children: children})
			i = end
		case "{":
			children, end := parseSynopsisSeq(tokens, i+1, explicit, false)
			items = append(items, SynopsisItem{Kind: SynRequiredGroup, Children: children})
			i = end
		case "|":
			items = append(items, SynopsisItem{Kind: SynPipe, Text: "|"})
		default:
			if flags, ok := splitGroupedFlags(tokens[i]); ok {
				items = append(items, flags...)
				continue
			}
			kind := classifySynopsisToken(tokens, i, explicit, topLevel, seenNonFlag)
			text := tokens[i]
			if text == "..." && len(items) != 0 {
				items[len(items)-1].Text += " ..."
				continue
			}
			items = append(items, SynopsisItem{Kind: kind, Text: text})
			if kind != SynFlag && kind != SynPipe {
				seenNonFlag = true
			}
		}
	}

	return items, len(tokens)
}

func splitGroupedFlags(tok string) ([]SynopsisItem, bool) {
	// we expand compacted synopsis tokens like some -abcdef into separate
	// semantic flags so the renderer can emit a separate fl for each one
	if len(tok) < 3 || tok[0] != '-' {
		return nil, false
	}
	for i := 1; i < len(tok); i++ {
		c := tok[i]
		if (c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z') {
			return nil, false
		}
	}

	items := make([]SynopsisItem, 0, len(tok)-1)
	for i := 1; i < len(tok); i++ {
		items = append(items, SynopsisItem{
			Kind: SynFlag,
			Text: "-" + string(tok[i]),
		})
	}
	return items, true
}

func classifySynopsisToken(tokens []string, i int, explicit, topLevel, seenNonFlag bool) SynopsisItemKind {
	tok := tokens[i]
	if strings.HasPrefix(tok, "-") {
		return SynFlag
	}
	if strings.HasPrefix(tok, "<") && strings.HasSuffix(tok, ">") {
		return SynArg
	}
	if strings.Contains(tok, "...") || strings.ContainsAny(tok, "/=:") {
		return SynArg
	}
	if i > 0 && tokens[i-1] == "|" {
		if explicit {
			return SynCommand
		}
		return SynArg
	}
	if i+1 < len(tokens) && tokens[i+1] == "|" {
		if explicit {
			return SynCommand
		}
		return SynArg
	}
	if i > 0 && strings.HasPrefix(tokens[i-1], "-") {
		return SynArg
	}
	if explicit && topLevel && !seenNonFlag && i+1 < len(tokens) && tokens[i+1] == "[" {
		return SynCommand
	}
	return SynArg
}

func (p *Page) validate() error {
	if p.Name == "" {
		return fmt.Errorf("missing manpage name")
	}
	if p.Summary == "" {
		return fmt.Errorf("missing manpage summary")
	}
	return nil
}

func normalizePageInlines(page *Page) {
	for i := range page.Description {
		page.Description[i] = mergeAdjacentTextInBlock(page.Description[i])
	}
	for i := range page.Options {
		for j := range page.Options[i].Body {
			page.Options[i].Body[j] = mergeAdjacentTextInBlock(page.Options[i].Body[j])
		}
	}
	for i := range page.Sections {
		for j := range page.Sections[i].Blocks {
			page.Sections[i].Blocks[j] = mergeAdjacentTextInBlock(page.Sections[i].Blocks[j])
		}
	}
}

func mergeAdjacentTextInBlock(block Block) Block {
	block.Inlines = mergeAdjacentText(block.Inlines)
	for i := range block.Items {
		block.Items[i].Label = mergeAdjacentText(block.Items[i].Label)
		block.Items[i].Body = mergeAdjacentText(block.Items[i].Body)
	}
	return block
}

func mergeAdjacentText(inlines []Inline) []Inline {
	if len(inlines) == 0 {
		return nil
	}
	out := []Inline{inlines[0]}
	for _, inline := range inlines[1:] {
		last := &out[len(out)-1]
		if last.Kind == InlineText && inline.Kind == InlineText {
			last.Text += inline.Text
			continue
		}
		out = append(out, inline)
	}
	return out
}
