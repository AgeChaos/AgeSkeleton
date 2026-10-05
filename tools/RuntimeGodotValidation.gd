# Test driver only; animation evaluation and rendering live in the C++ extension.
extends SceneTree

func _initialize():
	call_deferred("run")

func run():
	if not ClassDB.class_exists("AgeSkeletonPlayer"):
		push_error("AgeSkeleton C++ class not registered")
		quit(1)
		return
	var player = ClassDB.instantiate("AgeSkeletonPlayer")
	root.add_child(player)
	player.position = Vector2(320, 320)
	var ok = player.load_file("res://Assets/Wayfarer/skeleton.ageskel.json")
	ok = player.get_batch_count() > 0 and player.get_batch_count() < 23 and ok
	print("ATLAS_BATCHES ", player.get_batch_count())
	ok = player.play("Walk") and ok
	ok = player.seek(0.35) and ok
	player.pause()
	ok = player.set_wardrobe("Tops", "Tops/Steel Armor") and ok
	ok = not player.set_wardrobe("Tops", "Weapons/Short Sword") and ok
	ok = player.set_slot_visible("Torso", false) and ok
	ok = player.set_slot_visible("Torso", true) and ok
	ok = player.set_attachment("Torso", "") and ok
	ok = player.restore_attachment("Torso") and ok
	ok = not player.load_file("res://missing.json") and ok
	ok = abs(player.get_playback_time()-0.35)<0.001 and ok
	var tip = player.get_bone_tip("Left Forearm")
	ok = tip is Vector2 and ok
	ok = player.set_ik_target("Left Forearm", tip + Vector2(10, -10), 2, 1.0, 64, 0.01) and ok
	ok = player.get_bone_tip("Left Forearm").distance_to(tip) > 1.0 and ok
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image = root.get_texture().get_image()
	var distinct = 0
	var background = image.get_pixel(0, 0)
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			var c = image.get_pixel(x, y)
			if abs(c.r-background.r)+abs(c.g-background.g)+abs(c.b-background.b)>0.2:
				distinct += 1
	ok = distinct>500 and ok
	image.save_png("res://runtime-preview.png")
	player.resume()
	await create_timer(0.1).timeout
	ok = player.get_playback_time()>0.35 and ok
	player.stop()
	ok = not player.is_playing() and player.get_playback_time()==0 and ok
	# Deterministic native IK and deferred event signal verification.
	player.set_process(false)
	ok = player.load_file("res://Assets/MotionFixture/skeleton.ageskel.json") and ok
	var received: Array = []
	player.connect("animation_event", func(event_name, payload): received.append([event_name, payload]))
	ok = player.play("Loop") and player.advance(0.25) and ok
	await process_frame
	ok = received.size() == 2 and ok
	if received.size() == 2:
		ok = received[0][0] == "start" and received[1][0] == "quarter" and received[0][1]["int"] == -2147483648 and ok
	player.pause()
	ok = player.advance(1.0) and ok
	await process_frame
	ok = received.size() == 2 and ok
	ok = player.set_ik_target("Tip", Vector2(1, 1), 2, 1.0, 64, 0.0001) and ok
	ok = player.get_bone_tip("Tip").distance_to(Vector2.ONE) < 0.001 and ok
	ok = player.clear_ik_target("Tip") and player.get_bone_tip("Tip") == Vector2(2, 0) and ok
	print("AGESKELETON_MOTION_GODOT_", "PASS" if ok else "FAIL")
	print("AGESKELETON_GODOT_NATIVE_", "PASS" if ok else "FAIL", " pixels=",distinct)
	quit(0 if ok else 1)
