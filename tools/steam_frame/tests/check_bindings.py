#!/usr/bin/env python3
"""Binding table sanity for the Steam Frame build (Projects/Android/jni/src/Lambda1VR/OpenXrInput.c).

A runtime refuses a whole interaction profile when one suggested path isn't a component of it, and it
refuses to create an action whose name or localized name is already taken in the action set (both have
cost other ports their input). This checks, without a headset:

  - every action and localized name given to CreateAction() is unique (names are lower case);
  - every path in the Frame and Index tables, and in the Touch block, is a component of that profile
    (the component lists are the OpenXR spec's and Valve's; the Frame's is what the other Steam Frame
    ports bind: ABXY, Menu and the right bumper on the right controller, View and the D-pad on the left);
  - every action in a table was created, and the Frame profile binds what the game needs.

Run: tools/steam_frame/tests/check_bindings.py [OpenXrInput.c]   (exit 1 on a problem)
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[3] / "Projects/Android/jni/src/Lambda1VR/OpenXrInput.c"

POSES = {"grip/pose", "aim/pose"}
HAPTIC = {"output/haptic"}
STICK = {"thumbstick", "thumbstick/x", "thumbstick/y", "thumbstick/click", "thumbstick/touch"}
PAD = {"trackpad", "trackpad/x", "trackpad/y", "trackpad/touch"}

PROFILES = {
    "/interaction_profiles/oculus/touch_controller": (
        {"left", "right"}, {"x/click", "x/touch", "y/click", "y/touch", "menu/click", "a/click", "a/touch", "b/click",
                            "b/touch", "squeeze/value", "trigger/value", "trigger/touch", "thumbrest/touch"}
        | STICK | POSES | HAPTIC),
    "/interaction_profiles/valve/index_controller": (
        {"left", "right"}, {"system/click", "system/touch", "a/click", "a/touch", "b/click", "b/touch",
                            "squeeze/value", "squeeze/force", "trigger/click", "trigger/value", "trigger/touch",
                            "trackpad/force"} | STICK | PAD | POSES | HAPTIC),
    "/interaction_profiles/valve/frame_controller_valve": (
        {"left", "right"}, {"a/click", "b/click", "x/click", "y/click", "menu/click", "view/click", "bumper/click",
                            "trigger/value", "trigger/click", "squeeze/value", "dpad_up/click", "dpad_down/click",
                            "dpad_left/click", "dpad_right/click"} | STICK | POSES | HAPTIC),
}
# The Frame is a split gamepad: which hand has which button
FRAME_HAND = {"a/click": "right", "b/click": "right", "x/click": "right", "y/click": "right", "menu/click": "right",
              "view/click": "left", "dpad_up/click": "left", "dpad_down/click": "left", "dpad_left/click": "left",
              "dpad_right/click": "left"}
# What the game needs from the Frame profile (the layout in TBXR_UpdateFrameLayout)
FRAME_NEEDS = ["touchpadAction", "joystickAction", "thumbstickTouchAction", "triggerAction", "GripAction",
               "poseAction", "aimAction", "vibrateAction", "AAction", "BAction", "XAction", "YAction",
               "backAction", "viewAction", "bumperAction", "dpadUpAction", "dpadDownAction", "dpadLeftAction",
               "dpadRightAction"]


def main():
    text = (Path(sys.argv[1]) if len(sys.argv) > 1 else SRC).read_text()
    bad = 0

    def problem(msg):
        nonlocal bad
        print("BAD  " + msg)
        bad += 1

    # ---- actions: unique names, unique localized names
    created = {}
    names, localized = {}, {}
    for m in re.finditer(r'CreateAction\(actionSet,\s*\w+,\s*"([^"]+)",\s*"([^"]+)",\s*SIDE_COUNT,\s*handSubactionPath,\s*&(\w+)\)', text):
        name, loc, var = m.groups()
        created[var] = name
        if not re.fullmatch(r"[a-z0-9_.-]+", name):
            problem(f"action name '{name}' has characters other than a-z 0-9 _ . -")
        if name.lower() in names:
            problem(f"action name '{name}' is used twice (also by {names[name.lower()]})")
        if loc.lower() in localized:
            problem(f"localized name '{loc}' is used twice (also by {localized[loc.lower()]}): XR_ERROR_LOCALIZED_NAME_DUPLICATED")
        names[name.lower()] = var
        localized[loc.lower()] = var
    if not created:
        problem("no CreateAction calls found")

    # ---- the Frame and Index tables
    suggested = {}
    for m in re.finditer(r'TBXR_SuggestBindings\((\w+|"[^"]+"),\s*(\w+),', text):
        suggested[m.group(2)] = m.group(1)
    defines = dict(re.findall(r'#define (\w+_PROFILE) "([^"]+)"', text))
    tables = {}
    for m in re.finditer(r"static const profileBinding_t (\w+)\[\] = \{(.*?)\n\};", text, re.S):
        tables[m.group(1)] = m.group(2)

    checked = 0
    for table, profile_ref in suggested.items():
        profile = defines.get(profile_ref, profile_ref.strip('"'))
        if table not in tables:
            problem(f"table {table} not found")
            continue
        hands, comps = PROFILES.get(profile, (None, None))
        if comps is None:
            problem(f"{profile}: no component list in this test")
            continue
        checked += 1
        actions = set()
        for line in tables[table].splitlines():
            line = line.split("//")[0]
            e = re.search(r'\{\s*&(\w+),\s*"(/user/hand/(\w+)/(input|output)/([\w/]+))"\s*\}', line)
            if not e:
                continue
            action, path, hand, kind, comp = e.groups()
            comp = comp if kind == "input" else "output/" + comp
            actions.add(action)
            if action not in created:
                problem(f"{profile}: {action} is never created")
            if hand not in hands or comp not in comps:
                problem(f"{profile}: {path} is not a component of that profile")
            if profile.endswith("frame_controller_valve") and comp in FRAME_HAND and FRAME_HAND[comp] != hand:
                problem(f"{profile}: {path}: the Frame has {comp} on the {FRAME_HAND[comp]} controller")
        if profile.endswith("frame_controller_valve"):
            for need in FRAME_NEEDS:
                if need not in actions:
                    problem(f"the Frame profile doesn't bind {need}")
    if not any(p.endswith("frame_controller_valve") for p in (defines.get(r, r.strip('"')) for r in suggested.values())):
        problem("no Steam Frame profile suggested")

    # ---- the Touch block (paths come from the xrStringToPath lines)
    var_path = {}
    for m in re.finditer(r'xrStringToPath\(gAppState\.Instance,\s*"(/user/hand/(?:left|right)/[\w/]+)",\s*&(\w+)\[SIDE_(LEFT|RIGHT)\]', text):
        var_path[(m.group(2), m.group(3))] = m.group(1)
    touch = re.search(r'touchControllerInteractionProfilePath\)\);(.*?)xrSuggestInteractionProfileBindings', text, re.S)
    if touch:
        hands, comps = PROFILES["/interaction_profiles/oculus/touch_controller"]
        checked += 1
        for m in re.finditer(r"ActionSuggestedBinding\((\w+),\s*(\w+)\[SIDE_(LEFT|RIGHT)\]\)", touch.group(1)):
            action, var, side = m.groups()
            path = var_path.get((var, side))
            if path is None:
                problem(f"Touch: no path for {var}[SIDE_{side}]")
                continue
            hand, kind, comp = re.match(r"/user/hand/(\w+)/(input|output)/([\w/]+)", path).groups()
            comp = comp if kind == "input" else "output/" + comp
            if comp not in comps:
                problem(f"Touch: {path} is not a component of that profile")
            if action not in created:
                problem(f"Touch: {action} is never created")
    else:
        problem("Touch block not found")

    print(f"checked {checked} profiles and {len(created)} actions: {'FAILED' if bad else 'ok'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
