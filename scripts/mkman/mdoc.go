package main

import (
	"fmt"
	"io"
	"strings"
)

type MdocRenderer struct {
	page *Page
	w    *trackingWriter
}

func NewMdocRenderer(page *Page, w io.Writer) *MdocRenderer {
	return &MdocRenderer{page: page, w: newTrackingWriter(w)}
}

func (r *MdocRenderer) Render() error {
	if err := r.page.validate(); err != nil {
		return err
	}

	fmt.Fprintf(r.w, ".Dd %s\n", troffEscape(r.page.Date))
	fmt.Fprintf(r.w, ".Dt %s %d\n", strings.ToUpper(r.page.Name), r.page.Section)
	fmt.Fprintf(r.w, ".Os %s\n", "aruu")

	r.renderName()
	if len(r.page.Synopsis) != 0 {
		r.renderSynopsis()
	}
	if len(r.page.Description) != 0 {
		r.renderDescription()
	}
	if len(r.page.Options) != 0 {
		r.renderOptions()
	}

	for _, section := range r.page.Sections {
		r.renderSection(section.Title, section.Blocks)
	}

	return r.w.err
}

func (r *MdocRenderer) renderName() {
	fmt.Fprintln(r.w, ".Sh NAME")
	fmt.Fprintf(r.w, ".Nm %s\n", troffEscape(r.page.Name))
	fmt.Fprintf(r.w, ".Nd %s\n", renderInlineSeq(parseInlines(r.page.Summary)))
}

func (r *MdocRenderer) renderDescription() {
	fmt.Fprintln(r.w, ".Sh DESCRIPTION")
	for _, block := range r.page.Description {
		r.renderBlock(block)
	}
}

func (r *MdocRenderer) renderSynopsis() {
	fmt.Fprintln(r.w, ".Sh SYNOPSIS")
	for _, form := range r.page.Synopsis {
		// synopsis macros are emitted one phrase per line
		fmt.Fprintf(r.w, ".Nm %s\n", troffEscape(r.page.Name))
		for i := 0; i < len(form.Items); {
			item := form.Items[i]
			if item.Kind == SynOptional {
				fmt.Fprintf(r.w, ".Op %s\n", renderSynopsisPhrase(item.Children, false))
				i++
				continue
			}
			if item.Kind == SynRequiredGroup {
				fmt.Fprintf(r.w, ".Brq %s\n", renderSynopsisPhrase(item.Children, false))
				i++
				continue
			}

			phrase, next := consumeSynopsisPhrase(form.Items, i)
			fmt.Fprintln(r.w, renderSynopsisPhrase(phrase, true))
			i = next
		}
	}
}

func consumeSynopsisPhrase(items []SynopsisItem, start int) ([]SynopsisItem, int) {
	phrase := []SynopsisItem{items[start]}
	i := start + 1

	if items[start].Kind == SynFlag && i < len(items) && items[i].Kind == SynArg {
		phrase = append(phrase, items[i])
		i++
	}

	for i+1 < len(items) && items[i].Kind == SynPipe {
		phrase = append(phrase, items[i], items[i+1])
		i += 2
	}

	return phrase, i
}

func renderSynopsisPhrase(items []SynopsisItem, topLevel bool) string {
	var parts []string
	for _, item := range items {
		switch item.Kind {
		case SynFlag:
			parts = append(parts, renderMacro("Fl", troffEscape(strings.TrimPrefix(item.Text, "-")), topLevel && len(parts) == 0))
		case SynArg:
			parts = append(parts, renderMacro("Ar", troffEscape(strings.Trim(item.Text, "<>")), topLevel && len(parts) == 0))
		case SynCommand:
			parts = append(parts, renderMacro("Cm", troffEscape(item.Text), topLevel && len(parts) == 0))
		case SynPipe:
			parts = append(parts, "|")
		case SynLiteral:
			parts = append(parts, troffEscape(item.Text))
		case SynOptional:
			parts = append(parts, renderMacro("Op", renderSynopsisPhrase(item.Children, false), topLevel && len(parts) == 0))
		case SynRequiredGroup:
			parts = append(parts, renderMacro("Brq", renderSynopsisPhrase(item.Children, false), topLevel && len(parts) == 0))
		}
	}
	return strings.Join(parts, " ")
}

func renderMacro(name, body string, topLevel bool) string {
	if body == "" {
		if topLevel {
			return "." + name
		}

		return name
	}
	if topLevel {
		return "." + name + " " + body
	}

	return name + " " + body
}

func (r *MdocRenderer) renderOptions() {
	fmt.Fprintln(r.w, ".Sh OPTIONS")
	fmt.Fprintln(r.w, ".Bl -tag -width Ds")
	for _, opt := range r.page.Options {
		fmt.Fprintf(r.w, ".It %s\n", renderSynopsisPhrase(opt.Spec, false))
		for _, block := range opt.Body {
			r.renderBlock(block)
		}
	}
	fmt.Fprintln(r.w, ".El")
}

func (r *MdocRenderer) renderSection(title string, blocks []Block) {
	fmt.Fprintf(r.w, ".Sh %s\n", troffEscape(title))
	for _, block := range blocks {
		r.renderBlock(block)
	}
}

func (r *MdocRenderer) renderBlock(block Block) {
	switch block.Kind {
	case BlockParagraph:
		fmt.Fprintln(r.w, ".Pp")
		fmt.Fprintf(r.w, ".No %s\n", renderInlineSeq(block.Inlines))
	case BlockTaggedList:
		fmt.Fprintln(r.w, ".Bl -tag -width Ds")
		for _, item := range block.Items {
			fmt.Fprintf(r.w, ".It %s\n", renderInlineSeq(item.Label))
			if len(item.Body) != 0 {
				fmt.Fprintln(r.w, ".Pp")
				fmt.Fprintf(r.w, ".No %s\n", renderInlineSeq(item.Body))
			}
		}
		fmt.Fprintln(r.w, ".El")
	case BlockSeeAlso:
		for i, ref := range block.Refs {
			line := fmt.Sprintf(".Xr %s %s", troffEscape(ref.Name), troffEscape(ref.Section))
			if i != len(block.Refs)-1 {
				line += " ,"
			}
			fmt.Fprintln(r.w, line)
		}
	case BlockSubsection:
		fmt.Fprintf(r.w, ".Ss %s\n", troffEscape(block.Title))
		for _, child := range block.Blocks {
			r.renderBlock(child)
		}
	}
}

func troffEscape(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "-", "\\-")
	if len(s) > 0 && s[0] == '.' {
		s = "\\&" + s
	}
	return s
}

func renderInlineSeq(inlines []Inline) string {
	var b strings.Builder
	// adjacent semantic nodes sometimes need to join without an intervening
	// space, like some mixed path/emphasis/path forms in FILES
	for i, inline := range inlines {
		if i > 0 && needsNoSpace(inlines[i-1], inline) {
			b.WriteString(" Ns ")
		}
		b.WriteString(renderInline(inline))
	}
	return b.String()
}

func renderInline(inline Inline) string {
	switch inline.Kind {
	case InlineText:
		return troffEscape(inline.Text)
	case InlineNm:
		return renderMacro("Nm", "", false)
	case InlineEmph:
		return renderMacro("Em", renderInlineSeq(inline.Children), false)
	case InlineLiteral:
		return renderMacro("Ql", troffEscape(inline.Text), false)
	case InlinePath:
		return renderMacro("Pa", troffEscape(inline.Text), false)
	case InlineXRef:
		return renderMacro("Xr", troffEscape(inline.Text)+" "+troffEscape(inline.Section), false)
	case InlineFlag:
		return renderMacro("Fl", troffEscape(inline.Text), false)
	default:
		return ""
	}
}

func needsNoSpace(prev, curr Inline) bool {
	if trailingSpace(prev) || leadingSpace(curr) {
		return false
	}
	return true
}

func leadingSpace(inline Inline) bool {
	switch inline.Kind {
	case InlineText:
		return len(inline.Text) != 0 && isSpaceByte(inline.Text[0])
	case InlineEmph:
		return len(inline.Children) != 0 && leadingSpace(inline.Children[0])
	default:
		return false
	}
}

func trailingSpace(inline Inline) bool {
	switch inline.Kind {
	case InlineText:
		return len(inline.Text) != 0 && isSpaceByte(inline.Text[len(inline.Text)-1])
	case InlineEmph:
		return len(inline.Children) != 0 && trailingSpace(inline.Children[len(inline.Children)-1])
	default:
		return false
	}
}

func isSpaceByte(b byte) bool { return b == ' ' || b == '\t' || b == '\n' }
