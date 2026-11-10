## Prints an imported character's node tree and skeleton (rest poses).
## godot --headless --path Game -s res://tools/skeleton_dump.gd -- OperatorA
extends SceneTree


func _initialize() -> void:
	var asset := OS.get_cmdline_user_args()[0] if OS.get_cmdline_user_args().size() > 0 else "OperatorA"
	var root: Node = (load("res://assets/%s/SM_%s.fbx" % [asset, asset]) as PackedScene).instantiate()
	_print(root, 0)
	for sk in root.find_children("*", "Skeleton3D", true, false):
		var s := sk as Skeleton3D
		for i in s.get_bone_count():
			var g := s.get_bone_global_rest(i)
			print("bone ", i, " ", s.get_bone_name(i), " parent ", s.get_bone_parent(i), " at ", g.origin.snapped(Vector3.ONE * 0.001), " y ", g.basis.y.snapped(Vector3.ONE * 0.01))
	root.free()
	quit()


func _print(n: Node, depth: int) -> void:
	var extra := ""
	if n is Node3D:
		extra = " pos %s rot %s scale %s" % [(n as Node3D).position.snapped(Vector3.ONE * 0.001), (n as Node3D).rotation_degrees.snapped(Vector3.ONE * 0.1), (n as Node3D).scale.snapped(Vector3.ONE * 0.001)]
	print("  ".repeat(depth), n.name, " (", n.get_class(), ")", extra)
	for c in n.get_children():
		_print(c, depth + 1)
