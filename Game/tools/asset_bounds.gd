## Prints each imported asset's bounding box (Godot axes, metres).
extends SceneTree


func _initialize() -> void:
	for name in DirAccess.get_directories_at("res://assets"):
		var scene: PackedScene = load("res://assets/%s/SM_%s.fbx" % [name, name])
		if scene == null:
			print(name, ": not imported")
			continue
		var root: Node3D = scene.instantiate()
		var box := AABB()
		var first := true
		for mi in root.find_children("*", "MeshInstance3D", true, false):
			var m: MeshInstance3D = mi
			var xf := _global(m, root)
			var b := xf * m.get_aabb()
			box = b if first else box.merge(b)
			first = false
		print(name, ": pos ", box.position, " size ", box.size, " meshes ", root.find_children("*", "MeshInstance3D", true, false).size())
		root.free()
	quit()


func _global(n: Node3D, root: Node3D) -> Transform3D:
	var t := Transform3D.IDENTITY
	var cur: Node = n
	while cur != null and cur != root:
		t = (cur as Node3D).transform * t
		cur = cur.get_parent()
	return root.transform * t
