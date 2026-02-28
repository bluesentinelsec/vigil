package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func valFont(f rl.Font) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Font",
		Fields: map[string]value.Value{
			"_ptr":      {T: value.TypePtr, Data: &f},
			"base_size": value.NewI32(f.BaseSize),
		},
	}}
}

func (interp *Interpreter) rlText(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── Font loading ──
	nf("get_font_default", func(a []value.Value) (value.Value, error) { return valFont(rl.GetFontDefault()), nil })
	nf("load_font", func(a []value.Value) (value.Value, error) {
		f := rl.LoadFont(a[0].AsString())
		if f.BaseSize == 0 {
			return value.Void, fmt.Errorf("rl.load_font: failed to load %q", a[0].AsString())
		}
		return valFont(f), nil
	})
	nf("load_font_ex", func(a []value.Value) (value.Value, error) {
		f := rl.LoadFontEx(a[0].AsString(), a[1].AsI32(), nil, 0)
		if f.BaseSize == 0 {
			return value.Void, fmt.Errorf("rl.load_font_ex: failed to load %q", a[0].AsString())
		}
		return valFont(f), nil
	})
	nf("unload_font", func(a []value.Value) (value.Value, error) {
		rl.UnloadFont(*argNative[rl.Font](a[0]))
		return value.Void, nil
	})

	// ── Text drawing ──
	nf("draw_text", func(a []value.Value) (value.Value, error) {
		rl.DrawText(a[0].AsString(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_text_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawTextEx(*argNative[rl.Font](a[0]), a[1].AsString(), argVec2(a[2]), f32(a[3]), f32(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_text_pro", func(a []value.Value) (value.Value, error) {
		rl.DrawTextPro(*argNative[rl.Font](a[0]), a[1].AsString(), argVec2(a[2]), argVec2(a[3]), f32(a[4]), f32(a[5]), f32(a[6]), argsColor(a[7:]))
		return value.Void, nil
	})

	// ── Text measurement ──
	nf("measure_text", func(a []value.Value) (value.Value, error) {
		return value.NewI32(rl.MeasureText(a[0].AsString(), a[1].AsI32())), nil
	})
	nf("measure_text_ex", func(a []value.Value) (value.Value, error) {
		return valVec2(rl.MeasureTextEx(*argNative[rl.Font](a[0]), a[1].AsString(), f32(a[2]), f32(a[3]))), nil
	})

	// ── Color utilities ──
	nf("color_alpha", func(a []value.Value) (value.Value, error) {
		return valColor(rl.ColorAlpha(argsColor(a[0:4]), f32(a[4]))), nil
	})
	nf("color_brightness", func(a []value.Value) (value.Value, error) {
		return valColor(rl.ColorBrightness(argsColor(a[0:4]), f32(a[4]))), nil
	})
	nf("color_contrast", func(a []value.Value) (value.Value, error) {
		return valColor(rl.ColorContrast(argsColor(a[0:4]), f32(a[4]))), nil
	})
	nf("color_tint", func(a []value.Value) (value.Value, error) {
		return valColor(rl.ColorTint(argsColor(a[0:4]), argsColor(a[4:]))), nil
	})
	nf("color_from_hsv", func(a []value.Value) (value.Value, error) {
		return valColor(rl.ColorFromHSV(f32(a[0]), f32(a[1]), f32(a[2]))), nil
	})
	nf("fade", func(a []value.Value) (value.Value, error) {
		return valColor(rl.Fade(argsColor(a[0:4]), f32(a[4]))), nil
	})
}
