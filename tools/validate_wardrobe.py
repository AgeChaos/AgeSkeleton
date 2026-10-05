"""Validate the Wayfarer fixture through a running isolated AgeSkeleton AI session."""
import argparse
import json
import time
from pathlib import Path
from skeleton_ai import SkeletonClient


def run(session, report):
    client = SkeletonClient(session)
    status = client.call("status")
    assert status["path"] == "res://Assets/Wayfarer.ecsrig.tres", "Use an isolated Wayfarer fixture"
    by_name = {e["name"]: e["index"] for e in status["entities"]}
    checks = []

    def definition():
        return client.call("inspect", entity=0)["entity"]["skeleton_2d"]

    def active():
        return definition()["active_skins"]["items"]

    def controls():
        tree = client.call("ui_tree")
        return [x for v in tree.values() if isinstance(v, list) for x in v if isinstance(x, dict) and "class" in x]

    def change(group, skin):
        before = active()
        result = client.call("wardrobe", entity=0, group=group, skin=skin)
        now = result["active_skins"]
        assert [s for s in now if not s.startswith(group+"/")] == [s for s in before if not s.startswith(group+"/")]
        assert [s for s in now if s.startswith(group+"/")] == ([skin] if skin else [])
        return set(result["visible_attachments"])

    initial = definition()
    ui = controls()
    assert len([c for c in ui if "/Wardrobe/" in c["path"] and c["class"] == "OptionButton"]) == 4
    top = next(c for c in ui if c["path"].endswith("/Tops"))
    client.call("ui_action", path=top["path"], action="select", index=3)
    assert "Tops/Steel Armor" in active() and "Tops/Scout Tunic" not in active()
    client.call("undo")
    assert definition() == initial
    client.call("redo")
    assert "Tops/Steel Armor" in active()
    checks.append("native dropdown, undo, redo")

    for outfit, prefix in [("Scout Tunic", "Scout"), ("Travel Jacket", "Jacket"), ("Steel Armor", "Armor")]:
        visible = change("Tops", "Tops/"+outfit)
        assert {by_name[prefix+" Body"], by_name[prefix+" Left Sleeve"], by_name[prefix+" Right Sleeve"]} <= visible
        for other in {"Scout", "Jacket", "Armor", "Undershirt"}-{prefix}:
            assert by_name[other+" Body"] not in visible
            assert by_name[other+" Left Sleeve"] not in visible
            assert by_name[other+" Right Sleeve"] not in visible
    visible = change("Bottoms", "Bottoms/Ranger Boots")
    assert {by_name["Ranger Left Leg"], by_name["Ranger Right Leg"]} <= visible
    assert not {by_name["Canvas Left Leg"], by_name["Canvas Right Leg"]} & visible
    visible = change("Headwear", "Headwear/Knight Helm")
    assert by_name["Knight Helm"] in visible and by_name["Feather Cap"] not in visible
    visible = change("Weapons", "Weapons/Oak Staff")
    assert by_name["Oak Staff"] in visible and by_name["Short Sword"] not in visible
    visible = change("Headwear", "")
    assert not {by_name["Knight Helm"], by_name["Feather Cap"]} & visible
    visible = change("Weapons", "")
    assert not {by_name["Oak Staff"], by_name["Short Sword"]} & visible
    visible = change("Tops", "")
    assert by_name["Undershirt Body"] in visible and by_name["Armor Body"] not in visible
    checks.append("independent parts, exclusive replacement, default fallback, unequip")

    before = definition()
    for group, skin in [("Tops", "Weapons/Short Sword"), ("Missing", ""), ("Tops", "Tops/Missing")]:
        try:
            client.call("wardrobe", entity=0, group=group, skin=skin)
        except RuntimeError:
            pass
        else:
            raise AssertionError("Invalid outfit accepted")
        assert definition() == before
    checks.append("invalid requests leave scene unchanged")

    client.call("preview", entity=9, animation="Walk", time=.35, playing=False)
    visible = change("Tops", "Tops/Travel Jacket")
    after = client.call("status")
    assert abs(after["time"]-.35)<1e-6 and after["animation"] == "Walk"
    client.call("preview", entity=9, animation="Walk", time=.1, playing=True)
    change("Weapons", "Weapons/Short Sword")
    after = client.call("status")
    assert after["playing"] and after["animation"] == "Walk"
    at = after["time"]; time.sleep(.15)
    assert client.call("status")["time"] != at
    client.call("preview", entity=9, animation="Walk", time=.35, playing=False)
    checks.append("paused playhead and active animation playback survive outfit changes")
    valid = client.call("validate")
    assert not valid["warnings"] and all(m["bound"] and m["max_weight_sum_error"]<1e-5 for m in valid["meshes"])
    before = definition()
    client.call("save", path="res://Assets/WardrobeRoundtrip.tres", overwrite=True)
    client.call("open", path="res://Assets/WardrobeRoundtrip.tres", replace=True)
    assert definition() == before
    checks.append("weights and saved skin composition roundtrip")
    report.write_text(json.dumps({"ok":True,"checks":checks,"attachments":len(valid["meshes"])},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("WARDROBE_PASS " + "; ".join(checks))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    run(args.session, args.report)
