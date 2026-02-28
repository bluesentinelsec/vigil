package interp

import (
	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func (interp *Interpreter) rlCore(env *Env) {
	// ── Window management ──
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	nf("init_window", func(a []value.Value) (value.Value, error) {
		rl.InitWindow(a[0].AsI32(), a[1].AsI32(), a[2].AsString())
		return value.Void, nil
	})
	nf("close_window", func(a []value.Value) (value.Value, error) { rl.CloseWindow(); return value.Void, nil })
	nf("window_should_close", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.WindowShouldClose()), nil })
	nf("is_window_ready", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowReady()), nil })
	nf("is_window_fullscreen", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowFullscreen()), nil })
	nf("is_window_hidden", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowHidden()), nil })
	nf("is_window_minimized", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowMinimized()), nil })
	nf("is_window_maximized", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowMaximized()), nil })
	nf("is_window_focused", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowFocused()), nil })
	nf("is_window_resized", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsWindowResized()), nil })
	nf("toggle_fullscreen", func(a []value.Value) (value.Value, error) { rl.ToggleFullscreen(); return value.Void, nil })
	nf("toggle_borderless_windowed", func(a []value.Value) (value.Value, error) { rl.ToggleBorderlessWindowed(); return value.Void, nil })
	nf("maximize_window", func(a []value.Value) (value.Value, error) { rl.MaximizeWindow(); return value.Void, nil })
	nf("minimize_window", func(a []value.Value) (value.Value, error) { rl.MinimizeWindow(); return value.Void, nil })
	nf("restore_window", func(a []value.Value) (value.Value, error) { rl.RestoreWindow(); return value.Void, nil })
	nf("set_window_title", func(a []value.Value) (value.Value, error) { rl.SetWindowTitle(a[0].AsString()); return value.Void, nil })
	nf("set_window_position", func(a []value.Value) (value.Value, error) {
		rl.SetWindowPosition(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_window_min_size", func(a []value.Value) (value.Value, error) {
		rl.SetWindowMinSize(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_window_max_size", func(a []value.Value) (value.Value, error) {
		rl.SetWindowMaxSize(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_window_size", func(a []value.Value) (value.Value, error) {
		rl.SetWindowSize(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_window_opacity", func(a []value.Value) (value.Value, error) {
		rl.SetWindowOpacity(f32(a[0]))
		return value.Void, nil
	})
	nf("get_screen_width", func(a []value.Value) (value.Value, error) { return value.NewI32(i32(rl.GetScreenWidth())), nil })
	nf("get_screen_height", func(a []value.Value) (value.Value, error) { return value.NewI32(i32(rl.GetScreenHeight())), nil })
	nf("get_render_width", func(a []value.Value) (value.Value, error) { return value.NewI32(i32(rl.GetRenderWidth())), nil })
	nf("get_render_height", func(a []value.Value) (value.Value, error) { return value.NewI32(i32(rl.GetRenderHeight())), nil })
	nf("get_monitor_count", func(a []value.Value) (value.Value, error) { return value.NewI32(int32(rl.GetMonitorCount())), nil })
	nf("get_current_monitor", func(a []value.Value) (value.Value, error) { return value.NewI32(int32(rl.GetCurrentMonitor())), nil })
	nf("get_monitor_width", func(a []value.Value) (value.Value, error) {
		return value.NewI32(int32(rl.GetMonitorWidth(int(a[0].AsI32())))), nil
	})
	nf("get_monitor_height", func(a []value.Value) (value.Value, error) {
		return value.NewI32(int32(rl.GetMonitorHeight(int(a[0].AsI32())))), nil
	})
	nf("get_monitor_refresh_rate", func(a []value.Value) (value.Value, error) {
		return value.NewI32(int32(rl.GetMonitorRefreshRate(int(a[0].AsI32())))), nil
	})
	nf("get_monitor_name", func(a []value.Value) (value.Value, error) {
		return value.NewString(rl.GetMonitorName(int(a[0].AsI32()))), nil
	})
	nf("get_window_position", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetWindowPosition()), nil })
	nf("get_window_scale_dpi", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetWindowScaleDPI()), nil })
	nf("set_clipboard_text", func(a []value.Value) (value.Value, error) {
		rl.SetClipboardText(a[0].AsString())
		return value.Void, nil
	})
	nf("get_clipboard_text", func(a []value.Value) (value.Value, error) { return value.NewString(rl.GetClipboardText()), nil })
	nf("show_cursor", func(a []value.Value) (value.Value, error) { rl.ShowCursor(); return value.Void, nil })
	nf("hide_cursor", func(a []value.Value) (value.Value, error) { rl.HideCursor(); return value.Void, nil })
	nf("is_cursor_hidden", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsCursorHidden()), nil })
	nf("enable_cursor", func(a []value.Value) (value.Value, error) { rl.EnableCursor(); return value.Void, nil })
	nf("disable_cursor", func(a []value.Value) (value.Value, error) { rl.DisableCursor(); return value.Void, nil })
	nf("is_cursor_on_screen", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsCursorOnScreen()), nil })

	// ── Drawing ──
	nf("begin_drawing", func(a []value.Value) (value.Value, error) { rl.BeginDrawing(); return value.Void, nil })
	nf("end_drawing", func(a []value.Value) (value.Value, error) { rl.EndDrawing(); return value.Void, nil })
	nf("clear_background", func(a []value.Value) (value.Value, error) { rl.ClearBackground(argsColor(a)); return value.Void, nil })
	nf("begin_mode2d", func(a []value.Value) (value.Value, error) { rl.BeginMode2D(argCam2D(a[0])); return value.Void, nil })
	nf("end_mode2d", func(a []value.Value) (value.Value, error) { rl.EndMode2D(); return value.Void, nil })
	nf("begin_mode3d", func(a []value.Value) (value.Value, error) { rl.BeginMode3D(argCam3D(a[0])); return value.Void, nil })
	nf("end_mode3d", func(a []value.Value) (value.Value, error) { rl.EndMode3D(); return value.Void, nil })
	nf("begin_blend_mode", func(a []value.Value) (value.Value, error) {
		rl.BeginBlendMode(rl.BlendMode(a[0].AsI32()))
		return value.Void, nil
	})
	nf("end_blend_mode", func(a []value.Value) (value.Value, error) { rl.EndBlendMode(); return value.Void, nil })
	nf("begin_scissor_mode", func(a []value.Value) (value.Value, error) {
		rl.BeginScissorMode(a[0].AsI32(), a[1].AsI32(), a[2].AsI32(), a[3].AsI32())
		return value.Void, nil
	})
	nf("end_scissor_mode", func(a []value.Value) (value.Value, error) { rl.EndScissorMode(); return value.Void, nil })

	// ── Timing ──
	nf("set_target_fps", func(a []value.Value) (value.Value, error) { rl.SetTargetFPS(a[0].AsI32()); return value.Void, nil })
	nf("get_fps", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetFPS()), nil })
	nf("get_frame_time", func(a []value.Value) (value.Value, error) { return value.NewF64(float64(rl.GetFrameTime())), nil })
	nf("get_time", func(a []value.Value) (value.Value, error) { return value.NewF64(rl.GetTime()), nil })

	// ── Input: Keyboard ──
	nf("is_key_pressed", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsKeyPressed(a[0].AsI32())), nil })
	nf("is_key_pressed_repeat", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsKeyPressedRepeat(a[0].AsI32())), nil
	})
	nf("is_key_down", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsKeyDown(a[0].AsI32())), nil })
	nf("is_key_released", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsKeyReleased(a[0].AsI32())), nil })
	nf("is_key_up", func(a []value.Value) (value.Value, error) { return value.NewBool(rl.IsKeyUp(a[0].AsI32())), nil })
	nf("get_key_pressed", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetKeyPressed()), nil })
	nf("get_char_pressed", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetCharPressed()), nil })
	nf("set_exit_key", func(a []value.Value) (value.Value, error) { rl.SetExitKey(a[0].AsI32()); return value.Void, nil })

	// ── Input: Mouse ──
	nf("is_mouse_button_pressed", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsMouseButtonPressed(rl.MouseButton(a[0].AsI32()))), nil
	})
	nf("is_mouse_button_down", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsMouseButtonDown(rl.MouseButton(a[0].AsI32()))), nil
	})
	nf("is_mouse_button_released", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsMouseButtonReleased(rl.MouseButton(a[0].AsI32()))), nil
	})
	nf("is_mouse_button_up", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsMouseButtonUp(rl.MouseButton(a[0].AsI32()))), nil
	})
	nf("get_mouse_x", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetMouseX()), nil })
	nf("get_mouse_y", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetMouseY()), nil })
	nf("get_mouse_position", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetMousePosition()), nil })
	nf("get_mouse_delta", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetMouseDelta()), nil })
	nf("set_mouse_position", func(a []value.Value) (value.Value, error) {
		rl.SetMousePosition(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_mouse_offset", func(a []value.Value) (value.Value, error) {
		rl.SetMouseOffset(int(a[0].AsI32()), int(a[1].AsI32()))
		return value.Void, nil
	})
	nf("set_mouse_scale", func(a []value.Value) (value.Value, error) {
		rl.SetMouseScale(f32(a[0]), f32(a[1]))
		return value.Void, nil
	})
	nf("get_mouse_wheel_move", func(a []value.Value) (value.Value, error) { return value.NewF64(float64(rl.GetMouseWheelMove())), nil })
	nf("get_mouse_wheel_move_v", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetMouseWheelMoveV()), nil })
	nf("set_mouse_cursor", func(a []value.Value) (value.Value, error) { rl.SetMouseCursor(a[0].AsI32()); return value.Void, nil })

	// ── Input: Gamepad ──
	nf("is_gamepad_available", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGamepadAvailable(a[0].AsI32())), nil
	})
	nf("get_gamepad_name", func(a []value.Value) (value.Value, error) {
		return value.NewString(rl.GetGamepadName(a[0].AsI32())), nil
	})
	nf("is_gamepad_button_pressed", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGamepadButtonPressed(a[0].AsI32(), a[1].AsI32())), nil
	})
	nf("is_gamepad_button_down", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGamepadButtonDown(a[0].AsI32(), a[1].AsI32())), nil
	})
	nf("is_gamepad_button_released", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGamepadButtonReleased(a[0].AsI32(), a[1].AsI32())), nil
	})
	nf("is_gamepad_button_up", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGamepadButtonUp(a[0].AsI32(), a[1].AsI32())), nil
	})
	nf("get_gamepad_button_pressed", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetGamepadButtonPressed()), nil })
	nf("get_gamepad_axis_count", func(a []value.Value) (value.Value, error) {
		return value.NewI32(rl.GetGamepadAxisCount(a[0].AsI32())), nil
	})
	nf("get_gamepad_axis_movement", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetGamepadAxisMovement(a[0].AsI32(), a[1].AsI32()))), nil
	})

	// ── Input: Touch ──
	nf("get_touch_x", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetTouchX()), nil })
	nf("get_touch_y", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetTouchY()), nil })
	nf("get_touch_position", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetTouchPosition(a[0].AsI32())), nil })
	nf("get_touch_point_id", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetTouchPointId(a[0].AsI32())), nil })
	nf("get_touch_point_count", func(a []value.Value) (value.Value, error) { return value.NewI32(rl.GetTouchPointCount()), nil })

	// ── Input: Gestures ──
	nf("set_gestures_enabled", func(a []value.Value) (value.Value, error) {
		rl.SetGesturesEnabled(uint32(a[0].AsI32()))
		return value.Void, nil
	})
	nf("is_gesture_detected", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.IsGestureDetected(rl.Gestures(a[0].AsI32()))), nil
	})
	nf("get_gesture_detected", func(a []value.Value) (value.Value, error) { return value.NewI32(int32(rl.GetGestureDetected())), nil })
	nf("get_gesture_hold_duration", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetGestureHoldDuration())), nil
	})
	nf("get_gesture_drag_vector", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetGestureDragVector()), nil })
	nf("get_gesture_drag_angle", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetGestureDragAngle())), nil
	})
	nf("get_gesture_pinch_vector", func(a []value.Value) (value.Value, error) { return valVec2(rl.GetGesturePinchVector()), nil })
	nf("get_gesture_pinch_angle", func(a []value.Value) (value.Value, error) {
		return value.NewF64(float64(rl.GetGesturePinchAngle())), nil
	})

	// ── Camera ──
	nf("update_camera", func(a []value.Value) (value.Value, error) {
		cam := argCam3D(a[0])
		rl.UpdateCamera(&cam, rl.CameraMode(a[1].AsI32()))
		o := a[0].AsObject()
		o.Fields["position"] = valVec3(cam.Position)
		o.Fields["target"] = valVec3(cam.Target)
		o.Fields["up"] = valVec3(cam.Up)
		o.Fields["fovy"] = value.NewF64(float64(cam.Fovy))
		return value.Void, nil
	})
	nf("get_screen_to_world_2d", func(a []value.Value) (value.Value, error) {
		return valVec2(rl.GetScreenToWorld2D(argVec2(a[0]), argCam2D(a[1]))), nil
	})
	nf("get_world_to_screen_2d", func(a []value.Value) (value.Value, error) {
		return valVec2(rl.GetWorldToScreen2D(argVec2(a[0]), argCam2D(a[1]))), nil
	})
	nf("get_world_to_screen", func(a []value.Value) (value.Value, error) {
		return valVec2(rl.GetWorldToScreen(argVec3(a[0]), argCam3D(a[1]))), nil
	})

	// ── Misc ──
	nf("set_trace_log_level", func(a []value.Value) (value.Value, error) {
		rl.SetTraceLogLevel(rl.TraceLogLevel(a[0].AsI32()))
		return value.Void, nil
	})
	nf("take_screenshot", func(a []value.Value) (value.Value, error) { rl.TakeScreenshot(a[0].AsString()); return value.Void, nil })
	nf("open_url", func(a []value.Value) (value.Value, error) { rl.OpenURL(a[0].AsString()); return value.Void, nil })

	// ── Collision detection (2D) ──
	nf("check_collision_recs", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionRecs(argRect(a[0]), argRect(a[1]))), nil
	})
	nf("check_collision_circles", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionCircles(argVec2(a[0]), f32(a[1]), argVec2(a[2]), f32(a[3]))), nil
	})
	nf("check_collision_circle_rec", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionCircleRec(argVec2(a[0]), f32(a[1]), argRect(a[2]))), nil
	})
	nf("check_collision_point_rec", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionPointRec(argVec2(a[0]), argRect(a[1]))), nil
	})
	nf("check_collision_point_circle", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionPointCircle(argVec2(a[0]), argVec2(a[1]), f32(a[2]))), nil
	})
	nf("check_collision_point_triangle", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionPointTriangle(argVec2(a[0]), argVec2(a[1]), argVec2(a[2]), argVec2(a[3]))), nil
	})
	nf("check_collision_point_line", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionPointLine(argVec2(a[0]), argVec2(a[1]), argVec2(a[2]), a[3].AsI32())), nil
	})
	nf("get_collision_rec", func(a []value.Value) (value.Value, error) {
		return valRect(rl.GetCollisionRec(argRect(a[0]), argRect(a[1]))), nil
	})
}
