extends Node3D
## Deterministic reference scene for the headless tests.
##
## User arguments (after `--`):
##   --capture=<path>  Save the main viewport to <path> as PNG, then quit.
##   --frames=<n>      Number of frames to draw before capturing (default 10).

var capture_path := ""
var frames_to_draw := 10
var frames_drawn := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):
			capture_path = arg.trim_prefix("--capture=")
		elif arg.begins_with("--frames="):
			frames_to_draw = arg.trim_prefix("--frames=").to_int()
	_build_scene()


func _build_scene() -> void:
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.1, 0.12, 0.2)
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	add_child(world_environment)

	var camera := Camera3D.new()
	camera.position = Vector3(0, 1.5, 4)
	camera.look_at_from_position(camera.position, Vector3.ZERO)
	add_child(camera)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, 30, 0)
	add_child(light)

	var cube := MeshInstance3D.new()
	cube.mesh = BoxMesh.new()
	cube.rotation_degrees = Vector3(0, 35, 0)
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.9, 0.35, 0.1)
	cube.material_override = material
	add_child(cube)

	var canvas := CanvasLayer.new()
	add_child(canvas)
	var rect := ColorRect.new()
	rect.color = Color(0.2, 0.8, 0.3)
	rect.position = Vector2(8, 8)
	rect.size = Vector2(64, 32)
	canvas.add_child(rect)


func _process(_delta: float) -> void:
	if capture_path.is_empty():
		return
	frames_drawn += 1
	if frames_drawn < frames_to_draw:
		return
	var image := get_viewport().get_texture().get_image()
	var error := image.save_png(capture_path)
	if error != OK:
		push_error("Could not save %s: %s" % [capture_path, error_string(error)])
		get_tree().quit(1)
		return
	print("captured %dx%d to %s" % [image.get_width(), image.get_height(), capture_path])
	get_tree().quit(0)
