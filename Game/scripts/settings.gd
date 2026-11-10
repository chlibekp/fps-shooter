## Player settings, saved to user://settings.cfg.
class_name TfpsSettings
extends RefCounted

const PATH := "user://settings.cfg"

var player_name := "Operator"
var sensitivity := 0.07
var ads_sensitivity := 0.65
var invert_y := false
var toggle_crouch := true
var fov := 90.0
var master_volume := 0.8
var effects_volume := 1.0
var quality := 2
var armor := 0   # 0 medium, 1 light, 2 heavy
var primary := 1  # primary weapon id (weapons.json): 1 carbine, 2 SMG, 3 marksman, 5 shotgun
var barrel := 0  # 0 none, 1 compensator, 2 suppressor
var grip := 0    # 0 none, 1 vertical, 2 angled
var lobby_url := "http://127.0.0.1:7700"
var last_server := "127.0.0.1:7777"
var last_map := "harbor"
var keys := {}   # action -> [codes] overrides


static func load_or_default() -> TfpsSettings:
	var s := TfpsSettings.new()
	var c := ConfigFile.new()
	if c.load(PATH) == OK:
		for p in ["player_name", "sensitivity", "ads_sensitivity", "invert_y", "toggle_crouch", "fov", "master_volume",
				"effects_volume", "quality", "armor", "primary", "barrel", "grip", "lobby_url", "last_server", "last_map", "keys"]:
			if c.has_section_key("settings", p):
				s.set(p, c.get_value("settings", p))
	s.sanitize()
	return s


func sanitize() -> void:
	player_name = player_name.strip_edges().left(16)
	if player_name.is_empty():
		player_name = "Operator"
	sensitivity = clampf(sensitivity, 0.01, 0.4)
	ads_sensitivity = clampf(ads_sensitivity, 0.2, 1.5)
	fov = clampf(fov, 70.0, 110.0)
	master_volume = clampf(master_volume, 0.0, 1.0)
	effects_volume = clampf(effects_volume, 0.0, 1.0)
	quality = clampi(quality, 0, 3)
	armor = clampi(armor, 0, 2)
	if not primary in PRIMARIES:
		primary = 1
	barrel = clampi(barrel, 0, 2)
	grip = clampi(grip, 0, 2)


## Primary weapons a player can pick (weapons.json ids; the server rejects pistols).
const PRIMARIES := [1, 2, 3, 5]


## Attachment byte the server expects: barrel in bits 0-1, grip in bits 2-3.
func attachments() -> int:
	return barrel | (grip << 2)


func save() -> void:
	sanitize()
	var c := ConfigFile.new()
	for p in ["player_name", "sensitivity", "ads_sensitivity", "invert_y", "toggle_crouch", "fov", "master_volume",
			"effects_volume", "quality", "armor", "primary", "barrel", "grip", "lobby_url", "last_server", "last_map", "keys"]:
		c.set_value("settings", p, get(p))
	c.save(PATH)
