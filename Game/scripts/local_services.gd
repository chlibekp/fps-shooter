## Starts the bundled server, bots and lobby service (the Server/ folder of
## the player download, next to Game/) so the game works on its own: the
## practice match and hosting need no separate launcher.
class_name TfpsLocalServices
extends RefCounted

static var _pids: Array[int] = []


## Folder with tfps-server / tfps-lobby / tfps-bot and Shared/Definitions.
static func server_dir() -> String:
	var exe_dir := OS.get_executable_path().get_base_dir()
	var candidates := [exe_dir.path_join("../Server"), exe_dir.path_join("../../../../Server"),  # macOS .app bundle
		ProjectSettings.globalize_path("res://").path_join("../target/release")]                # running from the editor
	for c in candidates:
		if FileAccess.file_exists(c.path_join(_exe("tfps-server"))):
			return c.simplify_path()
	return ""


static func _exe(name: String) -> String:
	return name + (".exe" if OS.get_name() == "Windows" else "")


static func _defs(dir: String) -> String:
	var d := dir.path_join("Shared/Definitions")
	if DirAccess.dir_exists_absolute(d):
		return d
	return ProjectSettings.globalize_path("res://").path_join("../Shared/Definitions").simplify_path()


static func available() -> bool:
	return server_dir() != ""


## Starts a deathmatch server and moving target bots on 127.0.0.1:port.
static func start_practice(map_name: String, bots: int, port: int) -> bool:
	var dir := server_dir()
	if dir == "":
		return false
	var defs := _defs(dir)
	var pid := OS.create_process(dir.path_join(_exe("tfps-server")), ["--bind", "127.0.0.1:%d" % port, "--map", defs.path_join("maps/%s.json" % map_name),
		"--weapons", defs.path_join("weapons.json"), "--gadgets", defs.path_join("gadgets.json"), "--mode", "deathmatch",
		"--max-players", str(bots + 1), "--stats-interval", "3600"])
	if pid <= 0:
		return false
	_pids.append(pid)
	if bots > 0:
		OS.delay_msec(400)
		var bp := OS.create_process(dir.path_join(_exe("tfps-bot")), ["--server", "127.0.0.1:%d" % port, "--count", str(bots), "--seconds", "86400",
			"--pattern", "random", "--map", defs.path_join("maps/%s.json" % map_name), "--weapons", defs.path_join("weapons.json"), "--report", "0"])
		if bp > 0:
			_pids.append(bp)
	return true


## Starts a lobby service on this computer (for hosting) if none answers.
static func start_lobby(public_host: String) -> bool:
	var dir := server_dir()
	if dir == "":
		return false
	var pid := OS.create_process(dir.path_join(_exe("tfps-lobby")), ["--bind", "0.0.0.0:7700", "--public-host", public_host,
		"--definitions", _defs(dir)])
	if pid > 0:
		_pids.append(pid)
		OS.delay_msec(400)
	return pid > 0


static func stop_all() -> void:
	for pid in _pids:
		OS.kill(pid)
	_pids.clear()


static func lan_address() -> String:
	for a in IP.get_local_addresses():
		if a.count(".") == 3 and not a.begins_with("127.") and not a.begins_with("169.254."):
			return a
	return "127.0.0.1"
