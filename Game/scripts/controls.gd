## Default key bindings (Godot InputMap actions), rebindable from Settings.
class_name TfpsControls
extends RefCounted

const DEFAULTS := {
	"forward": [KEY_W], "back": [KEY_S], "left": [KEY_A], "right": [KEY_D],
	"jump": [KEY_SPACE], "crouch": [KEY_C], "crouch_hold": [KEY_CTRL], "prone": [KEY_Z],
	"sprint": [KEY_SHIFT], "walk": [KEY_ALT], "lean_left": [KEY_Q], "lean_right": [KEY_E],
	"fire": [MOUSE_BUTTON_LEFT], "aim": [MOUSE_BUTTON_RIGHT], "reload": [KEY_R], "use": [KEY_F],
	"primary": [KEY_1], "secondary": [KEY_2], "gadget1": [KEY_3], "gadget2": [KEY_4],
	"gadget3": [KEY_5], "gadget4": [KEY_6], "next_weapon": [MOUSE_BUTTON_WHEEL_DOWN],
	"prev_weapon": [MOUSE_BUTTON_WHEEL_UP], "net_stats": [KEY_F3], "scoreboard": [KEY_TAB],
	"menu": [KEY_ESCAPE],
}


static func install(overrides: Dictionary = {}) -> void:
	for action in DEFAULTS:
		if InputMap.has_action(action):
			InputMap.erase_action(action)
		InputMap.add_action(action)
		var codes: Array = overrides.get(action, DEFAULTS[action])
		for code in codes:
			InputMap.action_add_event(action, _event(code))


static func _event(code: int) -> InputEvent:
	if code < 16:  # mouse buttons are small numbers, keys are not
		var m := InputEventMouseButton.new()
		m.button_index = code
		return m
	var k := InputEventKey.new()
	k.physical_keycode = code
	return k


static func describe(action: String) -> String:
	var events := InputMap.action_get_events(action)
	if events.is_empty():
		return "-"
	var e := events[0]
	if e is InputEventKey:
		return OS.get_keycode_string(e.physical_keycode)
	if e is InputEventMouseButton:
		return ["", "Left Mouse", "Right Mouse", "Middle Mouse", "Wheel Up", "Wheel Down"][clampi(e.button_index, 0, 5)]
	return "?"
