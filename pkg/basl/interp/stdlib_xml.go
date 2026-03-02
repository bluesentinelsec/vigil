package interp

import (
	"encoding/xml"
	"fmt"
	"io"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- XML ---

type xmlNode struct {
	Tag      string
	Attrs    map[string]string
	Text     string
	Children []*xmlNode
}

func parseXmlNodes(decoder *xml.Decoder) ([]*xmlNode, error) {
	var nodes []*xmlNode
	for {
		tok, err := decoder.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		switch t := tok.(type) {
		case xml.StartElement:
			node := &xmlNode{Tag: t.Name.Local, Attrs: make(map[string]string)}
			for _, a := range t.Attr {
				node.Attrs[a.Name.Local] = a.Value
			}
			children, err := parseXmlNodes(decoder)
			if err != nil {
				return nil, err
			}
			node.Children = children
			nodes = append(nodes, node)
		case xml.CharData:
			s := strings.TrimSpace(string(t))
			if s != "" && len(nodes) == 0 {
				// text content before any child — attach to parent via special return
				nodes = append(nodes, &xmlNode{Tag: "__text", Text: s})
			} else if s != "" && len(nodes) > 0 {
				// text after children — rare, just append
				nodes = append(nodes, &xmlNode{Tag: "__text", Text: s})
			}
		case xml.EndElement:
			return nodes, nil
		}
	}
	return nodes, nil
}

func (interp *Interpreter) wrapXmlNode(node *xmlNode) value.Value {
	obj := &value.ObjectVal{
		ClassName: "xml.Value",
		Fields:    map[string]value.Value{"__xml": {T: value.TypeVoid, Data: node}},
		Methods:   make(map[string]*value.FuncVal),
	}
	return value.Value{T: value.TypeObject, Data: obj}
}

func extractXml(v value.Value) *xmlNode {
	if v.T != value.TypeObject {
		return nil
	}
	n, _ := v.AsObject().Fields["__xml"].Data.(*xmlNode)
	return n
}

func (interp *Interpreter) xmlMethod(obj value.Value, method string, line int) (value.Value, error) {
	node := extractXml(obj)
	if node == nil {
		return value.Void, fmt.Errorf("line %d: not an xml.Value", line)
	}
	switch method {
	case "tag":
		return value.NewNativeFunc("xml.Value.tag", func(args []value.Value) (value.Value, error) {
			return value.NewString(node.Tag), nil
		}), nil
	case "text":
		return value.NewNativeFunc("xml.Value.text", func(args []value.Value) (value.Value, error) {
			// Return own text or first __text child
			if node.Text != "" {
				return value.NewString(node.Text), nil
			}
			for _, c := range node.Children {
				if c.Tag == "__text" {
					return value.NewString(c.Text), nil
				}
			}
			return value.NewString(""), nil
		}), nil
	case "attr":
		return value.NewNativeFunc("xml.Value.attr", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("xml.Value.attr: expected string name")
			}
			v, ok := node.Attrs[args[0].AsString()]
			if !ok {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.False}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(v), value.True}}
		}), nil
	case "children":
		return value.NewNativeFunc("xml.Value.children", func(args []value.Value) (value.Value, error) {
			var elems []value.Value
			for _, c := range node.Children {
				if c.Tag != "__text" {
					elems = append(elems, interp.wrapXmlNode(c))
				}
			}
			return value.NewArray(elems), nil
		}), nil
	case "find":
		return value.NewNativeFunc("xml.Value.find", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("xml.Value.find: expected string tag")
			}
			tag := args[0].AsString()
			var elems []value.Value
			for _, c := range node.Children {
				if c.Tag == tag {
					elems = append(elems, interp.wrapXmlNode(c))
				}
			}
			return value.NewArray(elems), nil
		}), nil
	case "find_one":
		return value.NewNativeFunc("xml.Value.find_one", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("xml.Value.find_one: expected string tag")
			}
			tag := args[0].AsString()
			for _, c := range node.Children {
				if c.Tag == tag {
					return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapXmlNode(c), value.Ok}}
				}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("not found: "+tag, value.ErrKindNotFound)}}
		}), nil
	case "len":
		return value.NewNativeFunc("xml.Value.len", func(args []value.Value) (value.Value, error) {
			count := 0
			for _, c := range node.Children {
				if c.Tag != "__text" {
					count++
				}
			}
			return value.NewI32(int32(count)), nil
		}), nil
	}
	return value.Void, fmt.Errorf("line %d: xml.Value has no method %q", line, method)
}

func (interp *Interpreter) makeXmlModule() *Env {
	env := NewEnv(nil)
	env.Define("parse", value.NewNativeFunc("xml.parse", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("xml.parse: expected string")
		}
		decoder := xml.NewDecoder(strings.NewReader(args[0].AsString()))
		nodes, err := parseXmlNodes(decoder)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		if len(nodes) == 0 {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("empty XML", value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapXmlNode(nodes[0]), value.Ok}}
	}))
	return env
}
