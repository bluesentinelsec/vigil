package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
	rl "github.com/gen2brain/raylib-go/raylib"
)

func valModel(m rl.Model) value.Value {
	return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
		ClassName: "Model",
		Fields:    map[string]value.Value{"_ptr": {T: value.TypePtr, Data: &m}},
	}}
}

func (interp *Interpreter) rl3D(env *Env) {
	nf := func(name string, fn func([]value.Value) (value.Value, error)) {
		env.Define(name, value.NewNativeFunc("rl."+name, fn))
	}

	// ── 3D Shape drawing ──
	nf("draw_grid", func(a []value.Value) (value.Value, error) {
		rl.DrawGrid(a[0].AsI32(), f32(a[1]))
		return value.Void, nil
	})
	nf("draw_cube", func(a []value.Value) (value.Value, error) {
		rl.DrawCube(argVec3(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_cube_v", func(a []value.Value) (value.Value, error) {
		rl.DrawCubeV(argVec3(a[0]), argVec3(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_cube_wires", func(a []value.Value) (value.Value, error) {
		rl.DrawCubeWires(argVec3(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_cube_wires_v", func(a []value.Value) (value.Value, error) {
		rl.DrawCubeWiresV(argVec3(a[0]), argVec3(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_sphere", func(a []value.Value) (value.Value, error) {
		rl.DrawSphere(argVec3(a[0]), f32(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_sphere_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawSphereEx(argVec3(a[0]), f32(a[1]), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_sphere_wires", func(a []value.Value) (value.Value, error) {
		rl.DrawSphereWires(argVec3(a[0]), f32(a[1]), a[2].AsI32(), a[3].AsI32(), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_cylinder", func(a []value.Value) (value.Value, error) {
		rl.DrawCylinder(argVec3(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_cylinder_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawCylinderEx(argVec3(a[0]), argVec3(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_cylinder_wires", func(a []value.Value) (value.Value, error) {
		rl.DrawCylinderWires(argVec3(a[0]), f32(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_cylinder_wires_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawCylinderWiresEx(argVec3(a[0]), argVec3(a[1]), f32(a[2]), f32(a[3]), a[4].AsI32(), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_plane", func(a []value.Value) (value.Value, error) {
		rl.DrawPlane(argVec3(a[0]), argVec2(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_ray", func(a []value.Value) (value.Value, error) {
		rl.DrawRay(argRay(a[0]), argsColor(a[1:]))
		return value.Void, nil
	})

	// ── Model loading ──
	nf("load_model", func(a []value.Value) (value.Value, error) {
		m := rl.LoadModel(a[0].AsString())
		if m.Meshes == nil {
			return value.Void, fmt.Errorf("rl.load_model: failed to load %q", a[0].AsString())
		}
		return valModel(m), nil
	})
	nf("load_model_from_mesh", func(a []value.Value) (value.Value, error) {
		return valModel(rl.LoadModelFromMesh(*argNative[rl.Mesh](a[0]))), nil
	})
	nf("unload_model", func(a []value.Value) (value.Value, error) {
		rl.UnloadModel(*argNative[rl.Model](a[0]))
		return value.Void, nil
	})

	// ── Model drawing ──
	nf("draw_model", func(a []value.Value) (value.Value, error) {
		rl.DrawModel(*argNative[rl.Model](a[0]), argVec3(a[1]), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_model_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawModelEx(*argNative[rl.Model](a[0]), argVec3(a[1]), argVec3(a[2]), f32(a[3]), argVec3(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_model_wires", func(a []value.Value) (value.Value, error) {
		rl.DrawModelWires(*argNative[rl.Model](a[0]), argVec3(a[1]), f32(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
	nf("draw_model_wires_ex", func(a []value.Value) (value.Value, error) {
		rl.DrawModelWiresEx(*argNative[rl.Model](a[0]), argVec3(a[1]), argVec3(a[2]), f32(a[3]), argVec3(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})
	nf("draw_billboard", func(a []value.Value) (value.Value, error) {
		rl.DrawBillboard(argCam3D(a[0]), *argNative[rl.Texture2D](a[1]), argVec3(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_billboard_rec", func(a []value.Value) (value.Value, error) {
		rl.DrawBillboardRec(argCam3D(a[0]), *argNative[rl.Texture2D](a[1]), argRect(a[2]), argVec3(a[3]), argVec2(a[4]), argsColor(a[5:]))
		return value.Void, nil
	})

	// ── Mesh generation ──
	nf("gen_mesh_cube", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshCube(f32(a[0]), f32(a[1]), f32(a[2]))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_sphere", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshSphere(f32(a[0]), int(a[1].AsI32()), int(a[2].AsI32()))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_plane", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshPlane(f32(a[0]), f32(a[1]), int(a[2].AsI32()), int(a[3].AsI32()))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_cylinder", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshCylinder(f32(a[0]), f32(a[1]), int(a[2].AsI32()))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_torus", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshTorus(f32(a[0]), f32(a[1]), int(a[2].AsI32()), int(a[3].AsI32()))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_knot", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshKnot(f32(a[0]), f32(a[1]), int(a[2].AsI32()), int(a[3].AsI32()))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_heightmap", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshHeightmap(*argNative[rl.Image](a[0]), argVec3(a[1]))
		return valNative("Mesh", &m), nil
	})
	nf("gen_mesh_cubicmap", func(a []value.Value) (value.Value, error) {
		m := rl.GenMeshCubicmap(*argNative[rl.Image](a[0]), argVec3(a[1]))
		return valNative("Mesh", &m), nil
	})
	nf("upload_mesh", func(a []value.Value) (value.Value, error) {
		rl.UploadMesh(argNative[rl.Mesh](a[0]), a[1].AsBool())
		return value.Void, nil
	})
	nf("unload_mesh", func(a []value.Value) (value.Value, error) {
		rl.UnloadMesh(argNative[rl.Mesh](a[0]))
		return value.Void, nil
	})

	// ── Bounding box / collision 3D ──
	nf("get_mesh_bounding_box", func(a []value.Value) (value.Value, error) {
		return valBBox(rl.GetMeshBoundingBox(*argNative[rl.Mesh](a[0]))), nil
	})
	nf("get_model_bounding_box", func(a []value.Value) (value.Value, error) {
		return valBBox(rl.GetModelBoundingBox(*argNative[rl.Model](a[0]))), nil
	})
	nf("draw_bounding_box", func(a []value.Value) (value.Value, error) {
		o := a[0].AsObject()
		bb := rl.BoundingBox{Min: argVec3(o.Fields["min"]), Max: argVec3(o.Fields["max"])}
		rl.DrawBoundingBox(bb, argsColor(a[1:]))
		return value.Void, nil
	})
	nf("check_collision_boxes", func(a []value.Value) (value.Value, error) {
		o1, o2 := a[0].AsObject(), a[1].AsObject()
		b1 := rl.BoundingBox{Min: argVec3(o1.Fields["min"]), Max: argVec3(o1.Fields["max"])}
		b2 := rl.BoundingBox{Min: argVec3(o2.Fields["min"]), Max: argVec3(o2.Fields["max"])}
		return value.NewBool(rl.CheckCollisionBoxes(b1, b2)), nil
	})
	nf("check_collision_box_sphere", func(a []value.Value) (value.Value, error) {
		o := a[0].AsObject()
		bb := rl.BoundingBox{Min: argVec3(o.Fields["min"]), Max: argVec3(o.Fields["max"])}
		return value.NewBool(rl.CheckCollisionBoxSphere(bb, argVec3(a[1]), f32(a[2]))), nil
	})
	nf("check_collision_spheres", func(a []value.Value) (value.Value, error) {
		return value.NewBool(rl.CheckCollisionSpheres(argVec3(a[0]), f32(a[1]), argVec3(a[2]), f32(a[3]))), nil
	})
	nf("get_ray_collision_sphere", func(a []value.Value) (value.Value, error) {
		return valRayCollision(rl.GetRayCollisionSphere(argRay(a[0]), argVec3(a[1]), f32(a[2]))), nil
	})
	nf("get_ray_collision_box", func(a []value.Value) (value.Value, error) {
		o := a[1].AsObject()
		bb := rl.BoundingBox{Min: argVec3(o.Fields["min"]), Max: argVec3(o.Fields["max"])}
		return valRayCollision(rl.GetRayCollisionBox(argRay(a[0]), bb)), nil
	})
	nf("get_ray_collision_mesh", func(a []value.Value) (value.Value, error) {
		return valRayCollision(rl.GetRayCollisionMesh(argRay(a[0]), *argNative[rl.Mesh](a[1]), rl.MatrixIdentity())), nil
	})
	nf("get_mouse_ray", func(a []value.Value) (value.Value, error) {
		r := rl.GetScreenToWorldRay(argVec2(a[0]), argCam3D(a[1]))
		return value.Value{T: value.TypeObject, Data: &value.ObjectVal{
			ClassName: "Ray",
			Fields:    map[string]value.Value{"position": valVec3(r.Position), "direction": valVec3(r.Direction)},
		}}, nil
	})

	// ── Line 3D ──
	nf("draw_line_3d", func(a []value.Value) (value.Value, error) {
		rl.DrawLine3D(argVec3(a[0]), argVec3(a[1]), argsColor(a[2:]))
		return value.Void, nil
	})
	nf("draw_point_3d", func(a []value.Value) (value.Value, error) {
		rl.DrawPoint3D(argVec3(a[0]), argsColor(a[1:]))
		return value.Void, nil
	})
	nf("draw_circle_3d", func(a []value.Value) (value.Value, error) {
		rl.DrawCircle3D(argVec3(a[0]), f32(a[1]), argVec3(a[2]), f32(a[3]), argsColor(a[4:]))
		return value.Void, nil
	})
	nf("draw_triangle_3d", func(a []value.Value) (value.Value, error) {
		rl.DrawTriangle3D(argVec3(a[0]), argVec3(a[1]), argVec3(a[2]), argsColor(a[3:]))
		return value.Void, nil
	})
}
