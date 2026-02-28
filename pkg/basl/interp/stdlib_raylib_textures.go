package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func valTexture(t rl.Texture2D) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Texture2D",
		Fields: map[string]value.Value{
			"_ptr":  {T: value.TypePtr, Data: &t},
			"width": value.NewI32(t.Width), "height": value.NewI32(t.Height),
		},
	}}
}

func valRenderTexture(rt rl.RenderTexture2D) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "RenderTexture2D",
		Fields: map[string]value.Value{
			"_ptr":    {T: value.TypePtr, Data: &rt},
			"texture": valTexture(rt.Texture),
		},
	}}
}

func valImage(img *rl.Image) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Image",
		Fields: map[string]value.Value{
			"_ptr":  {T: value.TypePtr, Data: img},
			"width": value.NewI32(img.Width), "height": value.NewI32(img.Height),
		},
	}}
}

func (interp *Interpreter) rlTextures(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── Image loading ──
	nf("load_image", func(a []value.Value) (value.Value, error) {
		img := rl.LoadImage(a[0].AsString())
		if img.Width == 0 {
			return value.Void, fmt.Errorf("rl.load_image: failed to load %q", a[0].AsString())
		}
		return valImage(img), nil
	})
	nf("unload_image", func(a []value.Value) (value.Value, error) {
		rl.UnloadImage(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("export_image", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.ExportImage(*argNative[rl.Image](a[0]), a[1].AsString())), nil
	})
	nf("gen_image_color", func(a []value.Value) (value.Value, error) {
		img := rl.GenImageColor(int(a[0].AsI32()), int(a[1].AsI32()), argsColor(a[2:]))
		return valImage(img), nil
	})
	nf("gen_image_checked", func(a []value.Value) (value.Value, error) {
		img := rl.GenImageChecked(int(a[0].AsI32()), int(a[1].AsI32()), int(a[2].AsI32()), int(a[3].AsI32()), argsColor(a[4:8]), argsColor(a[8:]))
		return valImage(img), nil
	})
	nf("gen_image_gradient_linear", func(a []value.Value) (value.Value, error) {
		img := rl.GenImageGradientLinear(int(a[0].AsI32()), int(a[1].AsI32()), int(a[2].AsI32()), argsColor(a[3:7]), argsColor(a[7:]))
		return valImage(img), nil
	})
	nf("gen_image_gradient_radial", func(a []value.Value) (value.Value, error) {
		img := rl.GenImageGradientRadial(int(a[0].AsI32()), int(a[1].AsI32()), f32(a[2]), argsColor(a[3:7]), argsColor(a[7:]))
		return valImage(img), nil
	})
	nf("image_copy", func(a []value.Value) (value.Value, error) {
		img := rl.ImageCopy(argNative[rl.Image](a[0]))
		return valImage(img), nil
	})
	nf("image_resize", func(a []value.Value) (value.Value, error) {
		img := argNative[rl.Image](a[0])
		rl.ImageResize(img, a[1].AsI32(), a[2].AsI32())
		return value.Void, nil
	})
	nf("image_flip_vertical", func(a []value.Value) (value.Value, error) {
		rl.ImageFlipVertical(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_flip_horizontal", func(a []value.Value) (value.Value, error) {
		rl.ImageFlipHorizontal(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_rotate", func(a []value.Value) (value.Value, error) {
		rl.ImageRotate(argNative[rl.Image](a[0]), a[1].AsI32())
		return value.Void, nil
	})
	nf("image_rotate_cw", func(a []value.Value) (value.Value, error) {
		rl.ImageRotateCW(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_rotate_ccw", func(a []value.Value) (value.Value, error) {
		rl.ImageRotateCCW(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_color_tint", func(a []value.Value) (value.Value, error) {
		rl.ImageColorTint(argNative[rl.Image](a[0]), argsColor(a[1:]))
		return value.Void, nil
	})
	nf("image_color_invert", func(a []value.Value) (value.Value, error) {
		rl.ImageColorInvert(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_color_grayscale", func(a []value.Value) (value.Value, error) {
		rl.ImageColorGrayscale(argNative[rl.Image](a[0]))
		return value.Void, nil
	})
	nf("image_color_contrast", func(a []value.Value) (value.Value, error) {
		rl.ImageColorContrast(argNative[rl.Image](a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("image_color_brightness", func(a []value.Value) (value.Value, error) {
		rl.ImageColorBrightness(argNative[rl.Image](a[0]), a[1].AsI32())
		return value.Void, nil
	})
	nf("image_draw", func(a []value.Value) (value.Value, error) {
		rl.ImageDraw(argNative[rl.Image](a[0]), argNative[rl.Image](a[1]), argRect(a[2]), argRect(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("image_draw_rectangle_rec", func(a []value.Value) (value.Value, error) {
		rl.ImageDrawRectangleRec(argNative[rl.Image](a[0]), argRect(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("image_draw_rectangle_lines", func(a []value.Value) (value.Value, error) {
		rl.ImageDrawRectangleLines(argNative[rl.Image](a[0]), argRect(a[1]), int(a[2].AsI32()), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("image_clear_background", func(a []value.Value) (value.Value, error) {
		rl.ImageClearBackground(argNative[rl.Image](a[0]), argsColor(a[1:]))
		return value.Void, nil
	})

	// ── Texture loading ──
	nf("load_texture", func(a []value.Value) (value.Value, error) {
		t := rl.LoadTexture(a[0].AsString())
		if t.ID == 0 {
			return value.Void, fmt.Errorf("rl.load_texture: failed to load %q", a[0].AsString())
		}
		return valTexture(t), nil
	})
	nf("load_texture_from_image", func(a []value.Value) (value.Value, error) {
		t := rl.LoadTextureFromImage(argNative[rl.Image](a[0]))
		return valTexture(t), nil
	})
	nf("load_render_texture", func(a []value.Value) (value.Value, error) {
		rt := rl.LoadRenderTexture(a[0].AsI32(), a[1].AsI32())
		return valRenderTexture(rt), nil
	})
	nf("unload_texture", func(a []value.Value) (value.Value, error) {
		rl.UnloadTexture(*argNative[rl.Texture2D](a[0]))
		return value.Void, nil
	})
	nf("unload_render_texture", func(a []value.Value) (value.Value, error) {
		rl.UnloadRenderTexture(*argNative[rl.RenderTexture2D](a[0]))
		return value.Void, nil
	})
	nf("begin_texture_mode", func(a []value.Value) (value.Value, error) {
		rl.BeginTextureMode(*argNative[rl.RenderTexture2D](a[0]))
		return value.Void, nil
	})
	nf("end_texture_mode", func(a []value.Value) (value.Value, error) { rl.EndTextureMode(); return value.Void, nil })

	// ── Texture drawing ──
	nf("draw_texture", func(a []value.Value) (value.Value, error) {
		rl.DrawTexture(*argNative[rl.Texture2D](a[0]), a[1].AsI32(), a[2].AsI32(), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_texture_v", func(a []value.Value) (value.Value, error) {
		rl.DrawTextureV(*argNative[rl.Texture2D](a[0]), argVec2(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_texture_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawTextureEx(*argNative[rl.Texture2D](a[0]), argVec2(a[1]), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_texture_rec", func(a []value.Value) (value.Value, error) {
		rl.DrawTextureRec(*argNative[rl.Texture2D](a[0]), argRect(a[1]), argVec2(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_texture_pro", func(a []value.Value) (value.Value, error) {
		rl.DrawTexturePro(*argNative[rl.Texture2D](a[0]), argRect(a[1]), argRect(a[2]), argVec2(a[3]), f32(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})

	// ── Texture config ──
	nf("set_texture_filter", func(a []value.Value) (value.Value, error) {
		rl.SetTextureFilter(*argNative[rl.Texture2D](a[0]), rl.TextureFilterMode(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_texture_wrap", func(a []value.Value) (value.Value, error) {
		rl.SetTextureWrap(*argNative[rl.Texture2D](a[0]), rl.TextureWrapMode(a[1].AsI32()))
		return value.Void, nil
	})

	// Texture filter constants
	env.Define("TEXTURE_FILTER_POINT", value.NewI32(int32(rl.FilterPoint)))
	env.Define("TEXTURE_FILTER_BILINEAR", value.NewI32(int32(rl.FilterBilinear)))
	env.Define("TEXTURE_FILTER_TRILINEAR", value.NewI32(int32(rl.FilterTrilinear)))
	env.Define("TEXTURE_FILTER_ANISOTROPIC_4X", value.NewI32(int32(rl.FilterAnisotropic4x)))
	env.Define("TEXTURE_FILTER_ANISOTROPIC_8X", value.NewI32(int32(rl.FilterAnisotropic8x)))
	env.Define("TEXTURE_FILTER_ANISOTROPIC_16X", value.NewI32(int32(rl.FilterAnisotropic16x)))
}
