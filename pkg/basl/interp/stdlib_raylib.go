package interp

// Raylib bindings for BASL — helpers, struct conversions, module registration.

import (
	"image/color"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

// ── BASL → Go conversions ──────────────────────────────────────────

func argsColor(args []value.Value) color.RGBA {
	return color.RGBA{uint8(args[0].AsI32()), uint8(args[1].AsI32()), uint8(args[2].AsI32()), uint8(args[3].AsI32())}
}

func argVec2(v value.Value) rl.Vector2 {
	o := v.AsObject()
	return rl.Vector2{X: float32(o.Fields["x"].AsF64()), Y: float32(o.Fields["y"].AsF64())}
}

func argVec3(v value.Value) rl.Vector3 {
	o := v.AsObject()
	return rl.Vector3{X: float32(o.Fields["x"].AsF64()), Y: float32(o.Fields["y"].AsF64()), Z: float32(o.Fields["z"].AsF64())}
}

func argRect(v value.Value) rl.Rectangle {
	o := v.AsObject()
	return rl.Rectangle{X: float32(o.Fields["x"].AsF64()), Y: float32(o.Fields["y"].AsF64()), Width: float32(o.Fields["width"].AsF64()), Height: float32(o.Fields["height"].AsF64())}
}

func argCam2D(v value.Value) rl.Camera2D {
	o := v.AsObject()
	return rl.Camera2D{
		Offset:   argVec2(o.Fields["offset"]),
		Target:   argVec2(o.Fields["target"]),
		Rotation: float32(o.Fields["rotation"].AsF64()),
		Zoom:     float32(o.Fields["zoom"].AsF64()),
	}
}

func argCam3D(v value.Value) rl.Camera3D {
	o := v.AsObject()
	return rl.Camera3D{
		Position:   argVec3(o.Fields["position"]),
		Target:     argVec3(o.Fields["target"]),
		Up:         argVec3(o.Fields["up"]),
		Fovy:       float32(o.Fields["fovy"].AsF64()),
		Projection: rl.CameraProjection(o.Fields["projection"].AsI32()),
	}
}

// argNative extracts a stored Go pointer from an opaque BASL object.
func argNative[T any](v value.Value) *T {
	return v.AsObject().Fields["_ptr"].Data.(*T)
}

// ── Go → BASL conversions ──────────────────────────────────────────

func valColor(c color.RGBA) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Color",
		Fields: map[string]value.Value{
			"r": value.NewI32(int32(c.R)), "g": value.NewI32(int32(c.G)),
			"b": value.NewI32(int32(c.B)), "a": value.NewI32(int32(c.A)),
		},
	}}
}

func valVec2(v rl.Vector2) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Vec2",
		Fields:    map[string]value.Value{"x": value.NewF64(float64(v.X)), "y": value.NewF64(float64(v.Y))},
	}}
}

func valVec3(v rl.Vector3) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Vec3",
		Fields:    map[string]value.Value{"x": value.NewF64(float64(v.X)), "y": value.NewF64(float64(v.Y)), "z": value.NewF64(float64(v.Z))},
	}}
}

func valVec4(v rl.Vector4) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Vec4",
		Fields:    map[string]value.Value{"x": value.NewF64(float64(v.X)), "y": value.NewF64(float64(v.Y)), "z": value.NewF64(float64(v.Z)), "w": value.NewF64(float64(v.W))},
	}}
}

func valRect(r rl.Rectangle) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Rect",
		Fields: map[string]value.Value{
			"x": value.NewF64(float64(r.X)), "y": value.NewF64(float64(r.Y)),
			"width": value.NewF64(float64(r.Width)), "height": value.NewF64(float64(r.Height)),
		},
	}}
}

func valRayCollision(rc rl.RayCollision) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "RayCollision",
		Fields: map[string]value.Value{
			"hit": value.NewBool(rc.Hit), "distance": value.NewF64(float64(rc.Distance)),
			"point": valVec3(rc.Point), "normal": valVec3(rc.Normal),
		},
	}}
}

func valBBox(b rl.BoundingBox) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "BoundingBox",
		Fields:    map[string]value.Value{"min": valVec3(b.Min), "max": valVec3(b.Max)},
	}}
}

// valNative wraps a Go pointer as an opaque BASL object.
func valNative[T any](className string, ptr *T) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: className,
		Fields:    map[string]value.Value{"_ptr": {T: value.TypePtr, Data: ptr}},
	}}
}

// ── Arg helpers ────────────────────────────────────────────────────

func f32(v value.Value) float32 { return float32(v.AsF64()) }
func i32(v int) int32           { return int32(v) }
func argRay(v value.Value) rl.Ray {
	o := v.AsObject()
	return rl.Ray{Position: argVec3(o.Fields["position"]), Direction: argVec3(o.Fields["direction"])}
}

// ── Module constructor ─────────────────────────────────────────────

func (interp *Interpreter) makeRaylibModule() *Env {
	env := NewEnv(nil)

	// Struct constructors
	env.Define("vec2", value.NewNativeFunc("rl.vec2", func(args []value.Value) (value.Value, error) {
		return valVec2(rl.Vector2{X: f32(args[0]), Y: f32(args[1])}), nil
	}))
	env.Define("vec3", value.NewNativeFunc("rl.vec3", func(args []value.Value) (value.Value, error) {
		return valVec3(rl.Vector3{X: f32(args[0]), Y: f32(args[1]), Z: f32(args[2])}), nil
	}))
	env.Define("vec4", value.NewNativeFunc("rl.vec4", func(args []value.Value) (value.Value, error) {
		return valVec4(rl.Vector4{X: f32(args[0]), Y: f32(args[1]), Z: f32(args[2]), W: f32(args[3])}), nil
	}))
	env.Define("rect", value.NewNativeFunc("rl.rect", func(args []value.Value) (value.Value, error) {
		return valRect(rl.Rectangle{X: f32(args[0]), Y: f32(args[1]), Width: f32(args[2]), Height: f32(args[3])}), nil
	}))
	env.Define("color", value.NewNativeFunc("rl.color", func(args []value.Value) (value.Value, error) {
		return valColor(color.RGBA{uint8(args[0].AsI32()), uint8(args[1].AsI32()), uint8(args[2].AsI32()), uint8(args[3].AsI32())}), nil
	}))
	env.Define("ray", value.NewNativeFunc("rl.ray", func(args []value.Value) (value.Value, error) {
		return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
			ClassName: "Ray",
			Fields:    map[string]value.Value{"position": args[0], "direction": args[1]},
		}}, nil
	}))
	env.Define("camera2d", value.NewNativeFunc("rl.camera2d", func(args []value.Value) (value.Value, error) {
		return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
			ClassName: "Camera2D",
			Fields: map[string]value.Value{
				"offset": args[0], "target": args[1],
				"rotation": args[2], "zoom": args[3],
			},
		}}, nil
	}))
	env.Define("camera3d", value.NewNativeFunc("rl.camera3d", func(args []value.Value) (value.Value, error) {
		return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
			ClassName: "Camera3D",
			Fields: map[string]value.Value{
				"position": args[0], "target": args[1], "up": args[2],
				"fovy": args[3], "projection": args[4],
			},
		}}, nil
	}))
	env.Define("bbox", value.NewNativeFunc("rl.bbox", func(args []value.Value) (value.Value, error) {
		return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
			ClassName: "BoundingBox",
			Fields:    map[string]value.Value{"min": args[0], "max": args[1]},
		}}, nil
	}))

	// Register all subsystems
	interp.rlCore(env)
	interp.rlShapes(env)
	interp.rlTextures(env)
	interp.rlText(env)
	interp.rl3D(env)
	interp.rlAudio(env)
	interp.rlGui(env)
	interp.rlColors(env)
	interp.rlKeys(env)

	return env
}

// ── Predefined colors ──────────────────────────────────────────────

func (interp *Interpreter) rlColors(env *Env) {
	colors := map[string]color.RGBA{
		"LIGHTGRAY": rl.LightGray, "GRAY": rl.Gray, "DARKGRAY": rl.DarkGray,
		"YELLOW": rl.Yellow, "GOLD": rl.Gold, "ORANGE": rl.Orange,
		"PINK": rl.Pink, "RED": rl.Red, "MAROON": rl.Maroon,
		"GREEN": rl.Green, "LIME": rl.Lime, "DARKGREEN": rl.DarkGreen,
		"SKYBLUE": rl.SkyBlue, "BLUE": rl.Blue, "DARKBLUE": rl.DarkBlue,
		"PURPLE": rl.Purple, "VIOLET": rl.Violet, "DARKPURPLE": rl.DarkPurple,
		"BEIGE": rl.Beige, "BROWN": rl.Brown, "DARKBROWN": rl.DarkBrown,
		"WHITE": rl.White, "BLACK": rl.Black, "BLANK": rl.Blank,
		"MAGENTA": rl.Magenta, "RAYWHITE": rl.RayWhite,
	}
	for name, c := range colors {
		env.Define(name, valColor(c))
	}
}

// ── Key & mouse constants ──────────────────────────────────────────

func (interp *Interpreter) rlKeys(env *Env) {
	// Alphanumeric
	for i := int32(0); i < 26; i++ {
		name := string(rune('A' + i))
		env.Define("KEY_"+name, value.NewI32(int32(rl.KeyA)+i))
	}
	for i := int32(0); i <= 9; i++ {
		name := string(rune('0' + i))
		env.Define("KEY_"+name, value.NewI32(int32(rl.KeyZero)+i))
	}
	// Special keys
	keys := map[string]int32{
		"KEY_SPACE": int32(rl.KeySpace), "KEY_ESCAPE": int32(rl.KeyEscape),
		"KEY_ENTER": int32(rl.KeyEnter), "KEY_TAB": int32(rl.KeyTab),
		"KEY_BACKSPACE": int32(rl.KeyBackspace), "KEY_INSERT": int32(rl.KeyInsert),
		"KEY_DELETE": int32(rl.KeyDelete), "KEY_RIGHT": int32(rl.KeyRight),
		"KEY_LEFT": int32(rl.KeyLeft), "KEY_DOWN": int32(rl.KeyDown),
		"KEY_UP": int32(rl.KeyUp), "KEY_PAGE_UP": int32(rl.KeyPageUp),
		"KEY_PAGE_DOWN": int32(rl.KeyPageDown), "KEY_HOME": int32(rl.KeyHome),
		"KEY_END": int32(rl.KeyEnd), "KEY_CAPS_LOCK": int32(rl.KeyCapsLock),
		"KEY_SCROLL_LOCK": int32(rl.KeyScrollLock), "KEY_NUM_LOCK": int32(rl.KeyNumLock),
		"KEY_PRINT_SCREEN": int32(rl.KeyPrintScreen), "KEY_PAUSE": int32(rl.KeyPause),
		"KEY_LEFT_SHIFT": int32(rl.KeyLeftShift), "KEY_LEFT_CONTROL": int32(rl.KeyLeftControl),
		"KEY_LEFT_ALT": int32(rl.KeyLeftAlt), "KEY_LEFT_SUPER": int32(rl.KeyLeftSuper),
		"KEY_RIGHT_SHIFT": int32(rl.KeyRightShift), "KEY_RIGHT_CONTROL": int32(rl.KeyRightControl),
		"KEY_RIGHT_ALT": int32(rl.KeyRightAlt), "KEY_RIGHT_SUPER": int32(rl.KeyRightSuper),
	}
	for i := int32(0); i < 12; i++ {
		keys["KEY_F"+string(rune('1'+i))] = int32(rl.KeyF1) + i
	}
	for name, val := range keys {
		env.Define(name, value.NewI32(val))
	}

	// Mouse buttons
	env.Define("MOUSE_LEFT", value.NewI32(int32(rl.MouseButtonLeft)))
	env.Define("MOUSE_RIGHT", value.NewI32(int32(rl.MouseButtonRight)))
	env.Define("MOUSE_MIDDLE", value.NewI32(int32(rl.MouseButtonMiddle)))

	// Gamepad buttons
	env.Define("GAMEPAD_BUTTON_LEFT_FACE_UP", value.NewI32(int32(rl.GamepadButtonLeftFaceUp)))
	env.Define("GAMEPAD_BUTTON_LEFT_FACE_RIGHT", value.NewI32(int32(rl.GamepadButtonLeftFaceRight)))
	env.Define("GAMEPAD_BUTTON_LEFT_FACE_DOWN", value.NewI32(int32(rl.GamepadButtonLeftFaceDown)))
	env.Define("GAMEPAD_BUTTON_LEFT_FACE_LEFT", value.NewI32(int32(rl.GamepadButtonLeftFaceLeft)))
	env.Define("GAMEPAD_BUTTON_RIGHT_FACE_UP", value.NewI32(int32(rl.GamepadButtonRightFaceUp)))
	env.Define("GAMEPAD_BUTTON_RIGHT_FACE_RIGHT", value.NewI32(int32(rl.GamepadButtonRightFaceRight)))
	env.Define("GAMEPAD_BUTTON_RIGHT_FACE_DOWN", value.NewI32(int32(rl.GamepadButtonRightFaceDown)))
	env.Define("GAMEPAD_BUTTON_RIGHT_FACE_LEFT", value.NewI32(int32(rl.GamepadButtonRightFaceLeft)))
	env.Define("GAMEPAD_BUTTON_LEFT_TRIGGER_1", value.NewI32(int32(rl.GamepadButtonLeftTrigger1)))
	env.Define("GAMEPAD_BUTTON_LEFT_TRIGGER_2", value.NewI32(int32(rl.GamepadButtonLeftTrigger2)))
	env.Define("GAMEPAD_BUTTON_RIGHT_TRIGGER_1", value.NewI32(int32(rl.GamepadButtonRightTrigger1)))
	env.Define("GAMEPAD_BUTTON_RIGHT_TRIGGER_2", value.NewI32(int32(rl.GamepadButtonRightTrigger2)))
	env.Define("GAMEPAD_BUTTON_MIDDLE_LEFT", value.NewI32(int32(rl.GamepadButtonMiddleLeft)))
	env.Define("GAMEPAD_BUTTON_MIDDLE", value.NewI32(int32(rl.GamepadButtonMiddle)))
	env.Define("GAMEPAD_BUTTON_MIDDLE_RIGHT", value.NewI32(int32(rl.GamepadButtonMiddleRight)))
	env.Define("GAMEPAD_BUTTON_LEFT_THUMB", value.NewI32(int32(rl.GamepadButtonLeftThumb)))
	env.Define("GAMEPAD_BUTTON_RIGHT_THUMB", value.NewI32(int32(rl.GamepadButtonRightThumb)))

	// Gamepad axes
	env.Define("GAMEPAD_AXIS_LEFT_X", value.NewI32(int32(rl.GamepadAxisLeftX)))
	env.Define("GAMEPAD_AXIS_LEFT_Y", value.NewI32(int32(rl.GamepadAxisLeftY)))
	env.Define("GAMEPAD_AXIS_RIGHT_X", value.NewI32(int32(rl.GamepadAxisRightX)))
	env.Define("GAMEPAD_AXIS_RIGHT_Y", value.NewI32(int32(rl.GamepadAxisRightY)))
	env.Define("GAMEPAD_AXIS_LEFT_TRIGGER", value.NewI32(int32(rl.GamepadAxisLeftTrigger)))
	env.Define("GAMEPAD_AXIS_RIGHT_TRIGGER", value.NewI32(int32(rl.GamepadAxisRightTrigger)))

	// Camera modes
	env.Define("CAMERA_CUSTOM", value.NewI32(int32(rl.CameraCustom)))
	env.Define("CAMERA_FREE", value.NewI32(int32(rl.CameraFree)))
	env.Define("CAMERA_ORBITAL", value.NewI32(int32(rl.CameraOrbital)))
	env.Define("CAMERA_FIRST_PERSON", value.NewI32(int32(rl.CameraFirstPerson)))
	env.Define("CAMERA_THIRD_PERSON", value.NewI32(int32(rl.CameraThirdPerson)))

	// Camera projections
	env.Define("CAMERA_PERSPECTIVE", value.NewI32(int32(rl.CameraPerspective)))
	env.Define("CAMERA_ORTHOGRAPHIC", value.NewI32(int32(rl.CameraOrthographic)))

	// Blend modes
	env.Define("BLEND_ALPHA", value.NewI32(int32(rl.BlendAlpha)))
	env.Define("BLEND_ADDITIVE", value.NewI32(int32(rl.BlendAdditive)))
	env.Define("BLEND_MULTIPLIED", value.NewI32(int32(rl.BlendMultiplied)))
	env.Define("BLEND_ADD_COLORS", value.NewI32(int32(rl.BlendAddColors)))
	env.Define("BLEND_SUBTRACT_COLORS", value.NewI32(int32(rl.BlendSubtractColors)))
	env.Define("BLEND_ALPHA_PREMULTIPLY", value.NewI32(int32(rl.BlendAlphaPremultiply)))

	// Gestures
	env.Define("GESTURE_TAP", value.NewI32(int32(rl.GestureTap)))
	env.Define("GESTURE_DOUBLETAP", value.NewI32(int32(rl.GestureDoubletap)))
	env.Define("GESTURE_HOLD", value.NewI32(int32(rl.GestureHold)))
	env.Define("GESTURE_DRAG", value.NewI32(int32(rl.GestureDrag)))
	env.Define("GESTURE_SWIPE_RIGHT", value.NewI32(int32(rl.GestureSwipeRight)))
	env.Define("GESTURE_SWIPE_LEFT", value.NewI32(int32(rl.GestureSwipeLeft)))
	env.Define("GESTURE_SWIPE_UP", value.NewI32(int32(rl.GestureSwipeUp)))
	env.Define("GESTURE_SWIPE_DOWN", value.NewI32(int32(rl.GestureSwipeDown)))
	env.Define("GESTURE_PINCH_IN", value.NewI32(int32(rl.GesturePinchIn)))
	env.Define("GESTURE_PINCH_OUT", value.NewI32(int32(rl.GesturePinchOut)))
}
