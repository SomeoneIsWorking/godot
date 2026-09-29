extends Node3D
## Deterministic reference scene for the headless tests.
##
## User arguments (after `--`):
##   --capture=<path>  Save the main viewport to <path> as PNG, then quit.
##   --frames=<n>      Number of frames to draw before capturing (default 10).
##   --trace=<n>       Run a physics simulation seeded by randomize() for <n> frames, print
##                     a digest of every frame's state and Time readings, then quit.

var capture_path := ""
var frames_to_draw := 10
var frames_drawn := 0
var key_seen := false
var player: AudioStreamPlayer
var trace_frames := 0
var trace := PackedStringArray()
var bodies: Array[RigidBody3D] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):
			capture_path = arg.trim_prefix("--capture=")
		elif arg.begins_with("--frames="):
			frames_to_draw = arg.trim_prefix("--frames=").to_int()
		elif arg.begins_with("--trace="):
			trace_frames = arg.trim_prefix("--trace=").to_int()
	_build_scene()
	_start_audio()
	if trace_frames > 0:
		_build_simulation()


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


func _build_simulation() -> void:
	var ground := StaticBody3D.new()
	var ground_shape := CollisionShape3D.new()
	ground_shape.shape = WorldBoundaryShape3D.new()
	ground.add_child(ground_shape)
	add_child(ground)
	# The global generator and a new RandomNumberGenerator are both seeded by randomize().
	var rng := RandomNumberGenerator.new()
	for i in 8:
		var body := RigidBody3D.new()
		var shape := CollisionShape3D.new()
		shape.shape = BoxShape3D.new()
		body.add_child(shape)
		body.position = Vector3(randf_range(-2, 2), 2 + i, rng.randf_range(-2, 2))
		body.linear_velocity = Vector3(rng.randf_range(-1, 1), 0, randf_range(-1, 1))
		add_child(body)
		bodies.append(body)


func _record_trace_frame() -> void:
	var state := "%d %d %.6f %s" % [Time.get_ticks_usec(), Time.get_unix_time_from_system(), player.get_playback_position(), Time.get_datetime_string_from_system()]
	for body in bodies:
		state += " %s" % body.position
	trace.append(state)
	if trace.size() == trace_frames:
		print("trace %s" % "\n".join(trace).sha256_text())
		print("trace-last %s" % state)
		get_tree().quit(0)


func _start_audio() -> void:
	# Ten seconds of silence at the default mix rate, so positions map 1:1 to mixed frames.
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = 44100
	var data := PackedByteArray()
	data.resize(44100 * 2 * 10)
	stream.data = data
	player = AudioStreamPlayer.new()
	player.stream = stream
	add_child(player)
	player.play()


## Seconds of the test stream that have been mixed; the audio tests read it.
func audio_position() -> float:
	return player.get_playback_position()


## The audible stream time, per the audio synchronization tutorial.
func audio_sync_time() -> float:
	return player.get_playback_position() + AudioServer.get_time_since_last_mix() - AudioServer.get_output_latency()


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed:
		key_seen = true


## Whether a key press has reached `_input()`; the control channel tests read it.
func is_key_seen() -> bool:
	return key_seen


func _process(_delta: float) -> void:
	if trace_frames > 0:
		_record_trace_frame()
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
