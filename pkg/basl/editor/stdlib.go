package editor

import (
	"encoding/json"

	editorassets "github.com/bluesentinelsec/basl/editors"
)

type builtinMetadata struct {
	Modules map[string]builtinModule `json:"modules"`
}

type builtinModule struct {
	Summary string                   `json:"summary"`
	Members map[string]builtinSymbol `json:"members"`
}

type builtinSymbol struct {
	Name          string         `json:"name"`
	FullName      string         `json:"full_name"`
	Signature     string         `json:"signature"`
	Detail        string         `json:"detail"`
	Documentation string         `json:"documentation"`
	Params        []builtinParam `json:"params"`
	Returns       string         `json:"returns"`
}

type builtinParam struct {
	Label string `json:"label"`
}

func loadBuiltinMetadata() (*builtinMetadata, error) {
	data, err := editorassets.Assets.ReadFile("vscode/stdlib.json")
	if err != nil {
		return nil, err
	}
	var out builtinMetadata
	if err := json.Unmarshal(data, &out); err != nil {
		return nil, err
	}
	if out.Modules == nil {
		out.Modules = make(map[string]builtinModule)
	}
	return &out, nil
}
