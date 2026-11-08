## Entry point. Command line (after "--"):
##   --connect host:port --map NAME --name NAME   join a server directly
##   --lobby http://host:7700 [--invite CODE]     use a lobby service
## Test options: --shot PATH --after SECONDS [--yaw D --pitch D --fire --walk]
##   take a screenshot after a delay with scripted input, then quit.
extends Node

var args := {}
var net: TfpsNet
var game: TfpsGame
var _shot_at := -1.0


func _ready() -> void:
	args = _parse(OS.get_cmdline_user_args())
	if args.has("quality"):
		TfpsWorld.quality = clampi(int(args["quality"]), 0, 3)
	TfpsControls.install()
	net = TfpsNet.new()
	if args.has("connect"):
		_start_direct(args["connect"], args.get("map", "test_arena"), args.get("name", "Operator"))
	else:
		push_warning("no --connect given (the menu comes next)")
	if args.has("shot"):
		_shot_at = float(args.get("after", "6"))


func _parse(a: PackedStringArray) -> Dictionary:
	var out := {}
	var i := 0
	while i < a.size():
		var k := a[i].trim_prefix("--")
		if i + 1 < a.size() and not a[i + 1].begins_with("--"):
			out[k] = a[i + 1]
			i += 2
		else:
			out[k] = "true"
			i += 1
	return out


func _start_direct(address: String, map_name: String, player_name: String) -> void:
	if not net.load_map(map_name):
		push_error(net.error)
		return
	game = TfpsGame.new()
	add_child(game)
	if not game.start(net, address, player_name):
		push_error(net.error)
	if not args.has("shot"):
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _process(_delta: float) -> void:
	if _shot_at < 0.0 or game == null:
		return
	# Scripted input for screenshot tests.
	game.player.yaw = float(args.get("yaw", str(game.player.yaw)))
	game.player.pitch = float(args.get("pitch", "0"))
	if args.has("fire"):
		Input.action_press("fire")
	if args.has("walk"):
		Input.action_press("forward")
	if Time.get_ticks_msec() / 1000.0 >= _shot_at:
		get_viewport().get_texture().get_image().save_png(args["shot"])
		print("saved ", args["shot"], " phase=", game.hud.state.get("phase"), " players=", game.operators.size())
		get_tree().quit()
