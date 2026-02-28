package interp

import (
	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func (interp *Interpreter) rlShapes(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── Pixel ──
	nf("draw_pixel", func(a []value.Value) (value.Value, error) {
		rl.DrawPixel(a[0].AsI32(), a[1].AsI32(), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_pixel_v", func(a []value.Value) (value.Value, error) {
		rl.DrawPixelV(argVec2(a[0]), argsColor(a[1:]))
		return value.Void, nil
	})

	// ── Line ──
	nf("draw_line", func(a []value.Value) (value.Value, error) {
		rl.DrawLine(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_line_v", func(a []value.Value) (value.Value, error) {
		rl.DrawLineV(argVec2(a[0]), argVec2(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_line_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawLineEx(argVec2(a[0]), argVec2(a[1]), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_line_bezier", func(a []value.Value) (value.Value, error) {
		rl.DrawLineBezier(argVec2(a[0]), argVec2(a[1]), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})

	// ── Circle ──
	nf("draw_circle", func(a []value.Value) (value.Value, error) {
		rl.DrawCircle(a[0].AsI32(), a[1].AsI32(), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_circle_v", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleV(argVec2(a[0]), f32(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_circle_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleLines(a[0].AsI32(), a[1].AsI32(), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_circle_lines_v", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleLinesV(argVec2(a[0]), f32(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_circle_sector", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleSector(argVec2(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_circle_sector_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleSectorLines(argVec2(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_circle_gradient", func(a []value.Value) (value.Value, error) {
		rl.DrawCircleGradient(a[0].AsI32(), a[1].AsI32(), f32(a[2]), argsColor(a[3:7]), argsColor(a[7:]))
		return value.Void, nil
	})
	nf("draw_ring", func(a []value.Value) (value.Value, error) {
		rl.DrawRing(argVec2(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), f32(a[4]), a[5].AsI32(), argsColor(a[6:]))
		return value.Void, nil
	})
	nf("draw_ring_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawRingLines(argVec2(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), f32(a[4]), a[5].AsI32(), argsColor(a[6:]))
		return value.Void, nil
	})

	// ── Ellipse ──
	nf("draw_ellipse", func(a []value.Value) (value.Value, error) {
		rl.DrawEllipse(a[0].AsI32(), a[1].AsI32(), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_ellipse_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawEllipseLines(a[0].AsI32(), a[1].AsI32(), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})

	// ── Rectangle ──
	nf("draw_rectangle", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangle(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_rectangle_v", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleV(argVec2(a[0]), argVec2(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_rectangle_rec", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleRec(argRect(a[0]), argsColor(a[1:]))
		return value.Void, nil
	})
	nf("draw_rectangle_pro", func(a []value.Value) (value.Value, error) {
		rl.DrawRectanglePro(argRect(a[0]), argVec2(a[1]), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_rectangle_gradient_v", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleGradientV(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:8]), argsColor(a[8:]))
		return value.Void, nil
	})
	nf("draw_rectangle_gradient_h", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleGradientH(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:8]), argsColor(a[8:]))
		return value.Void, nil
	})
	nf("draw_rectangle_gradient_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleGradientEx(argRect(a[0]), argsColor(a[1:5]), argsColor(a[5:9]), argsColor(a[9:13]), argsColor(a[13:]))
		return value.Void, nil
	})
	nf("draw_rectangle_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleLines(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_rectangle_lines_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleLinesEx(argRect(a[0]), f32(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_rectangle_rounded", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleRounded(argRect(a[0]), f32(a[1]), a[2].AsI32(), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_rectangle_rounded_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleRoundedLines(argRect(a[0]), f32(a[1]), a[2].AsI32(), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_rectangle_rounded_lines_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawRectangleRoundedLinesEx(argRect(a[0]), f32(a[1]), a[2].AsI32(), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})

	// ── Triangle ──
	nf("draw_triangle", func(a []value.Value) (value.Value, error) {
		rl.DrawTriangle(argVec2(a[0]), argVec2(a[1]), argVec2(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_triangle_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawTriangleLines(argVec2(a[0]), argVec2(a[1]), argVec2(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})

	// ── Poly ──
	nf("draw_poly", func(a []value.Value) (value.Value, error) {
		rl.DrawPoly(argVec2(a[0]), a[1].AsI32(), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_poly_lines", func(a []value.Value) (value.Value, error) {
		rl.DrawPolyLines(argVec2(a[0]), a[1].AsI32(), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_poly_lines_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawPolyLinesEx(argVec2(a[0]), a[1].AsI32(), f32(a[2]), f32(a[3]), f32(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})

	// ── Capsule ──
	nf("draw_capsule", func(a []value.Value) (value.Value, error) {
		rl.DrawCapsule(argVec3(a[0]), argVec3(a[1]), f32(a[2]), a[3].AsI32(), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_capsule_wires", func(a []value.Value) (value.Value, error) {
		rl.DrawCapsuleWires(argVec3(a[0]), argVec3(a[1]), f32(a[2]), a[3].AsI32(), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
}
