package main

import (
	"fmt"
	"io"
	"strings"
)

// fixed-width ascii layout, matching the column widths a classic
// nroff -tascii rendering of an mdoc page would produce
const (
	txtWidth     = 78
	txtIndent    = 7
	txtTagIndent = 15
	txtSubIndent = 4
)

type TxtRenderer struct {
	page  *Page
	w     io.Writer
	wrote bool
}

func NewTxtRenderer(page *Page, w io.Writer) *TxtRenderer {
	return &TxtRenderer{page: page, w: w}
}

func (r *TxtRenderer) Render() error {
	if err := r.page.validate(); err != nil {
		return err
	}

	r.heading("NAME")
	r.writeIndented(r.page.Name+" - "+renderInlinesTxt(parseInlines(r.page.Summary)), txtIndent)

	if len(r.page.Synopsis) != 0 {
		r.renderSynopsis()
	}
	if len(r.page.Description) != 0 {
		r.heading("DESCRIPTION")
		for _, block := range r.page.Description {
			r.renderBlock(block, txtIndent)
		}
	}
	if len(r.page.Options) != 0 {
		r.renderOptions()
	}
	for _, section := range r.page.Sections {
		r.heading(section.Title)
		for _, block := range section.Blocks {
			r.renderBlock(block, txtIndent)
		}
	}
	return nil
}

// heading prints a flush-left section title, with a blank line above
// every one but the first
func (r *TxtRenderer) heading(title string) {
	if r.wrote {
		fmt.Fprintln(r.w)
	}
	fmt.Fprintln(r.w, title)
	r.wrote = true
}

func (r *TxtRenderer) renderSynopsis() {
	r.heading("SYNOPSIS")
	hang := len(r.page.Name) + 1

	for _, form := range r.page.Synopsis {
		body := renderSynopsisItemsTxt(form.Items)

		avail := txtWidth - txtIndent - hang
		if avail < 10 {
			avail = 10
		}
		lines := wrapWords(body, avail)
		if len(lines) == 0 {
			lines = []string{""}
		}

		fmt.Fprintln(r.w, strings.Repeat(" ", txtIndent)+strings.TrimSpace(r.page.Name+" "+lines[0]))
		for _, l := range lines[1:] {
			fmt.Fprintln(r.w, strings.Repeat(" ", txtIndent+hang)+l)
		}
	}
}

func (r *TxtRenderer) renderOptions() {
	r.heading("OPTIONS")
	for i, opt := range r.page.Options {
		if i != 0 {
			fmt.Fprintln(r.w)
		}
		r.writeIndented(renderSynopsisItemsTxt(opt.Spec), txtIndent)
		for _, block := range opt.Body {
			r.renderBlock(block, txtTagIndent)
		}
	}
}

func (r *TxtRenderer) renderBlock(block Block, indent int) {
	switch block.Kind {
	case BlockParagraph:
		fmt.Fprintln(r.w)
		r.writeIndented(renderInlinesTxt(block.Inlines), indent)
	case BlockTaggedList:
		for i, item := range block.Items {
			if i != 0 {
				fmt.Fprintln(r.w)
			}
			r.writeIndented(renderInlinesTxt(item.Label), indent)
			if len(item.Body) != 0 {
				fmt.Fprintln(r.w)
				r.writeIndented(renderInlinesTxt(item.Body), indent+8)
			}
		}
	case BlockSeeAlso:
		var refs []string
		for _, ref := range block.Refs {
			refs = append(refs, ref.Name+"("+ref.Section+")")
		}
		fmt.Fprintln(r.w)
		r.writeIndented(strings.Join(refs, ", "), indent)
	case BlockSubsection:
		fmt.Fprintln(r.w)
		r.writeIndented(block.Title, indent)
		for _, child := range block.Blocks {
			r.renderBlock(child, indent+txtSubIndent)
		}
	}
}

// writeindented word-wraps text to the page width minus indent, then
// writes each line prefixed by indent spaces
func (r *TxtRenderer) writeIndented(text string, indent int) {
	width := txtWidth - indent
	if width < 10 {
		width = 10
	}
	for _, line := range wrapWords(text, width) {
		fmt.Fprintln(r.w, strings.Repeat(" ", indent)+line)
	}
}

func wrapWords(text string, width int) []string {
	text = strings.Join(strings.Fields(text), " ")
	if text == "" {
		return nil
	}
	if width < 1 {
		width = 1
	}

	words := strings.Fields(text)
	lines := []string{words[0]}
	for _, word := range words[1:] {
		last := lines[len(lines)-1]
		if len(last)+1+len(word) <= width {
			lines[len(lines)-1] = last + " " + word
		} else {
			lines = append(lines, word)
		}
	}
	return lines
}

// rendersynopsisitemstxt mirrors rendersynopsisphrase from mdoc.go but
// writes literal brackets and braces instead of mdoc macros
func renderSynopsisItemsTxt(items []SynopsisItem) string {
	var parts []string
	for _, item := range items {
		switch item.Kind {
		case SynFlag:
			parts = append(parts, "-"+strings.TrimPrefix(item.Text, "-"))
		case SynArg:
			parts = append(parts, strings.Trim(item.Text, "<>"))
		case SynCommand:
			parts = append(parts, item.Text)
		case SynPipe:
			parts = append(parts, "|")
		case SynLiteral:
			parts = append(parts, item.Text)
		case SynOptional:
			parts = append(parts, "["+renderSynopsisItemsTxt(item.Children)+"]")
		case SynRequiredGroup:
			parts = append(parts, "{"+renderSynopsisItemsTxt(item.Children)+"}")
		}
	}
	return strings.Join(parts, " ")
}

func renderInlinesTxt(inlines []Inline) string {
	var b strings.Builder
	for _, inline := range inlines {
		b.WriteString(renderInlineTxt(inline))
	}
	return b.String()
}

func renderInlineTxt(inline Inline) string {
	switch inline.Kind {
	case InlineText:
		return inline.Text
	case InlineNm:
		return inline.Text
	case InlineEmph:
		return "_" + renderInlinesTxt(inline.Children) + "_"
	case InlineLiteral:
		return "`" + inline.Text + "'"
	case InlinePath:
		return inline.Text
	case InlineXRef:
		return inline.Text + "(" + inline.Section + ")"
	case InlineFlag:
		return "-" + inline.Text
	default:
		return ""
	}
}
