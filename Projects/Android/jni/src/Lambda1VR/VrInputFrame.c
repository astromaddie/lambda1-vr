/************************************************************************************

Filename	:	VrInputFrame.c
Content		:	The Steam Frame controller map (the table in VrFrameMap.c) applied to the game

The scheme code (VrInputDefault.c and the others) reads Quest-shaped controller states: a dominant
and an off hand, and which bits of their buttons mean what. This turns what the Frame's buttons
mean, by the map, into those bits, and into commands for what the scheme code doesn't do.

*************************************************************************************/

#include <common/common.h>
#include <common/library.h>
#include <common/cvardef.h>
#include <common/xash3d_types.h>
#include <engine/keydefs.h>
#include <client/touch.h>
#include <client/client.h>

#include "VrInput.h"
#include "VrCvars.h"
#include "VrFrameMap.h"
#include "VrInputFrame.h"

// every bit of the Quest-shaped state that the map decides
#define MAP_BUTTONS (xrButton_A | xrButton_B | xrButton_X | xrButton_Y | xrButton_Enter | xrButton_Joystick | \
					 xrButton_LThumb | xrButton_RThumb | xrButton_GripTrigger | xrButton_Trigger | \
					 xrButton_LShoulder | xrButton_RShoulder)

#define MENU_HOLD_MS 500.0
#define VIEW_HOLD_1S_MS 1000.0
#define VIEW_HOLD_3S_MS 3000.0

typedef struct {
	bool wasDown;
	double downTime;
	int stage;
} holdState_t;

// A button that does one thing when it's tapped (reported once, on release) and another when it's
// held (reported once, when the time is up). Returns the stage reached, 0 for none.
static void holdButton(holdState_t* hold, bool down, double firstMs, double secondMs, bool* tapped, bool* first, bool* second)
{
	const double now = TBXR_GetTimeInMilliSeconds();
	*tapped = *first = *second = false;

	if (down && !hold->wasDown) {
		hold->downTime = now;
		hold->stage = 0;
	}

	if (down) {
		const double held = now - hold->downTime;
		if (hold->stage < 1 && held >= firstMs) {
			*first = true;
			hold->stage = 1;
		}
		if (secondMs > 0.0 && hold->stage < 2 && held >= secondMs) {
			*second = true;
			hold->stage = 2;
		}
	} else if (hold->wasDown && hold->stage == 0) {
		*tapped = true;
	}

	hold->wasDown = down;
}

static bool frameHandUse = false;

bool VR_FrameHandUse()
{
	return frameHandUse;
}

static float handDistance(const ovrTrackedController* a, const ovrTrackedController* b)
{
	const float dx = a->Pose.position.x - b->Pose.position.x;
	const float dy = a->Pose.position.y - b->Pose.position.y;
	const float dz = a->Pose.position.z - b->Pose.position.z;
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

// The other hand's grip steadies the gun when that hand is on or near it and grabs things when it is
// well away from it. Decided when the grip goes down, and kept until it comes up. Between the two
// distances the last choice stands, so a normal two-handed hold never turns into a grab.
#define GRIP_STEADY_BELOW_M 0.35f
#define GRIP_GRAB_ABOVE_M 0.55f

static bool flick(float along, float across, float threshold)
{
	return along > threshold && along > fabsf(across) * 1.5f;
}

void VrInputFrame_Apply(const frameRaw_t* raw)
{
	static uint32_t previousActions[2];		// [0] the bits of actions 0..31, [1] those above
	static holdState_t menuHold, viewHold;
	static bool gripWasDown = false;
	static bool gripZone = false;
	static bool crouchToggled = false;
	static bool offGripWasDown = false;
	static bool offGripAway = false;
	static bool scoreboardShown = false;

	frameHandUse = true;

	const bool leftHanded = (vr_control_scheme != NULL && vr_control_scheme->integer >= 10);
	const bool menu = cls.key_dest != key_game;
	const bool multiplayer = isMultiplayer();
	if (!multiplayer) {
		scoreboardShown = false;
	}

	ovrInputStateTrackedRemote* dom = leftHanded ? &leftTrackedRemoteState_new : &rightTrackedRemoteState_new;
	ovrInputStateTrackedRemote* off = leftHanded ? &rightTrackedRemoteState_new : &leftTrackedRemoteState_new;
	ovrTrackedController* domTracking = leftHanded ? &leftRemoteTracking_new : &rightRemoteTracking_new;
	ovrTrackedController* offTracking = leftHanded ? &rightRemoteTracking_new : &leftRemoteTracking_new;

	// the physical inputs that are down
	bool menuTap, menuHeld, menuUnused, viewTap, view1s, view3s;
	holdButton(&menuHold, raw->menu, MENU_HOLD_MS, 0.0, &menuTap, &menuHeld, &menuUnused);
	holdButton(&viewHold, raw->view, VIEW_HOLD_1S_MS, VIEW_HOLD_3S_MS, &viewTap, &view1s, &view3s);

	bool down[FI_COUNT] = {0};
	down[FI_L_TRIGGER] = raw->triggerLeft > 0.5f;
	down[FI_R_TRIGGER] = raw->triggerRight > 0.5f;
	down[FI_L_GRIP] = raw->gripLeft > 0.5f;
	down[FI_R_GRIP] = raw->gripRight > 0.5f;
	down[FI_L_BUMPER] = raw->bumperLeft;
	down[FI_R_BUMPER] = raw->bumperRight;
	down[FI_L_STICK_CLICK] = raw->stickClickLeft;
	down[FI_R_STICK_CLICK] = raw->stickClickRight;
	down[FI_A] = raw->a;
	down[FI_B] = raw->b;
	down[FI_X] = raw->x;
	down[FI_Y] = raw->y;
	down[FI_MENU_SHORT] = menuTap;
	down[FI_MENU_HOLD] = menuHeld;
	down[FI_VIEW_SHORT] = viewTap;
	down[FI_VIEW_HOLD_1S] = view1s;
	down[FI_VIEW_HOLD_3S] = view3s;
	down[FI_DPAD_UP] = raw->dpadUp;
	down[FI_DPAD_DOWN] = raw->dpadDown;
	down[FI_DPAD_LEFT] = raw->dpadLeft;
	down[FI_DPAD_RIGHT] = raw->dpadRight;
	down[FI_L_STICK_UP] = flick(raw->stickLeftY, raw->stickLeftX, 0.6f);
	down[FI_L_STICK_DOWN] = flick(-raw->stickLeftY, raw->stickLeftX, 0.6f);
	down[FI_L_STICK_LEFT] = flick(-raw->stickLeftX, raw->stickLeftY, 0.6f);
	down[FI_L_STICK_RIGHT] = flick(raw->stickLeftX, raw->stickLeftY, 0.6f);
	down[FI_R_STICK_UP] = flick(raw->stickRightY, raw->stickRightX, 0.75f);
	down[FI_R_STICK_DOWN] = flick(-raw->stickRightY, raw->stickRightX, 0.75f);
	down[FI_R_STICK_LEFT] = flick(-raw->stickRightX, raw->stickRightY, 0.3f);
	down[FI_R_STICK_RIGHT] = flick(raw->stickRightX, raw->stickRightY, 0.3f);

	// Where the hands are decides some things. The weapon hand's grip is the backpack when it was
	// pressed with the hand behind the head, and stays that until it's let go (the crowbar stays out
	// while you swing it).
	const int weaponGrip = leftHanded ? FI_L_GRIP : FI_R_GRIP;
	if (down[weaponGrip] && !gripWasDown) {
		gripZone = isBackpack(domTracking);
	}
	gripWasDown = down[weaponGrip];

	const int offGrip = leftHanded ? FI_R_GRIP : FI_L_GRIP;
	if (down[offGrip] && !offGripWasDown) {
		const float apart = handDistance(domTracking, offTracking);
		if (apart < GRIP_STEADY_BELOW_M) {
			offGripAway = false;
		} else if (apart > GRIP_GRAB_ABOVE_M) {
			offGripAway = true;
		}
		ALOGI("[vr] other hand grip: %s (hands %.2f m apart)", offGripAway ? "interact" : "steady the gun", apart);
	}
	offGripWasDown = down[offGrip];

	int context = 0;
	if (offGripAway) context |= CTX_OFF_GRIP_AWAY;
	if (menu) context |= CTX_MENU;
	if (down[weaponGrip] ? gripZone : isBackpack(domTracking)) context |= CTX_DOM_ZONE;
	if (isBackpack(offTracking) && !multiplayer) context |= CTX_OFF_ZONE;
	if (multiplayer) context |= CTX_MULTI;

	// say what was pressed and what the map made of it, once a press, for the log (not the stick directions)
	static bool wasDown[FI_COUNT];
	for (int input = 0; input < FI_COUNT; input++) {
		const int mapped = VrFrameMap_Action(input, context, leftHanded);
		if (down[input] && !wasDown[input] && mapped != ACT_NONE && mapped != ACT_MOVE && mapped != ACT_TURN) {
			ALOGI("[vr] %s -> %s%s%s", VrFrameMap_InputName(input),
				  VrFrameMap_ActionName(mapped),
				  menu ? " (menu)" : "", (context & CTX_OFF_ZONE) ? " (hand behind the head)" : "");
		}
		wasDown[input] = down[input];
	}

	// what they do: a bit for each action
	uint32_t actions[2] = {0, 0};
	uint32_t quickSaveFromMenuHold = 0;
	for (int input = 0; input < FI_COUNT; input++) {
		if (!down[input]) {
			continue;
		}
		const int action = VrFrameMap_Action(input, context, leftHanded);
		if (action == ACT_NONE) {
			continue;
		}
		actions[action / 32] |= 1u << (action % 32);
		if (input == FI_MENU_HOLD && action == ACT_QUICKSAVE) {
			quickSaveFromMenuHold = 1;
		}
	}
#define HAS(a) ((actions[(a) / 32] & (1u << ((a) % 32))) != 0)
#define PRESSED(a) (HAS(a) && !(previousActions[(a) / 32] & (1u << ((a) % 32))))
#define RELEASED(a) (!HAS(a) && (previousActions[(a) / 32] & (1u << ((a) % 32))))

	if (PRESSED(ACT_CROUCH_TOGGLE)) {
		crouchToggled = !crouchToggled;
	}

	// The Quest-shaped states the scheme code reads. The dominant hand: trigger fires (or clicks in a
	// menu), grip (only the backpack, from behind the head), stick turns, use on the stick click
	// bit, crouch and jump on its two buttons. The off hand: grip steadies the gun, its stick
	// moves, its two buttons are the torch and (with the hand behind the head) quick save and
	// load, the stick click bit is the laser sight.
	const uint32_t domCrouch = leftHanded ? xrButton_X : xrButton_A;
	const uint32_t domJump = leftHanded ? xrButton_Y : xrButton_B;
	const uint32_t offTorch = leftHanded ? xrButton_A : xrButton_X;
	const uint32_t offLoad = leftHanded ? xrButton_B : xrButton_Y;

	leftTrackedRemoteState_new.Buttons &= ~MAP_BUTTONS;
	rightTrackedRemoteState_new.Buttons &= ~MAP_BUTTONS;

	if (HAS(ACT_FIRE) || HAS(ACT_MENU_CLICK)) dom->Buttons |= xrButton_Trigger;
	if (HAS(ACT_BACKPACK)) dom->Buttons |= xrButton_GripTrigger;
	if (HAS(ACT_STEADY)) off->Buttons |= xrButton_GripTrigger;
	if (HAS(ACT_JUMP)) dom->Buttons |= domJump;
	if (HAS(ACT_CROUCH_HOLD) || crouchToggled) dom->Buttons |= domCrouch;
	if (HAS(ACT_USE)) dom->Buttons |= xrButton_Joystick;
	// what the Quest does with its off-hand buttons, with the hand behind the head
	if (HAS(ACT_FLASHLIGHT) || (HAS(ACT_QUICKSAVE) && !quickSaveFromMenuHold)) off->Buttons |= offTorch;
	if (HAS(ACT_QUICKLOAD)) off->Buttons |= offLoad;
	if (HAS(ACT_LASER)) off->Buttons |= xrButton_Joystick;
	// one escape for pause and for every way back
	if (HAS(ACT_PAUSE) || HAS(ACT_MENU_BACK)) leftTrackedRemoteState_new.Buttons |= xrButton_Enter;

	// triggers and sticks: the left stick is always the move stick and the right stick the turn stick
	// (the weapon hand only decides the aim, the triggers and the grips), and there's no weapon
	// select on the turn stick, the bumpers and the D-pad do that.
	dom->IndexTrigger = leftHanded ? raw->triggerLeft : raw->triggerRight;
	off->IndexTrigger = 0.0f;
	dom->GripTrigger = HAS(ACT_BACKPACK) ? 1.0f : 0.0f;
	off->GripTrigger = HAS(ACT_STEADY) ? 1.0f : 0.0f;
	dom->Joystick.x = raw->stickRightX;
	dom->Joystick.y = 0.0f;
	off->Joystick.x = raw->stickLeftX;
	off->Joystick.y = raw->stickLeftY;

	// commands the scheme code doesn't have
	if (PRESSED(ACT_RECENTRE)) {
		TBXR_RecenterToHead(false);
		TBXR_Vibrate(80, 0, 0.6f);
		TBXR_Vibrate(80, 1, 0.6f);
	}
	if (PRESSED(ACT_RECENTRE_HEIGHT)) {
		TBXR_RecenterToHead(true);
		TBXR_Vibrate(80, 0, 0.6f);
		TBXR_Vibrate(80, 1, 0.6f);
	}

	// the grips and X are "use", from the hand: the server finds what's in reach of that hand
	if (PRESSED(ACT_USE_OFF)) sendButtonActionSimple("+use2");
	if (RELEASED(ACT_USE_OFF)) sendButtonActionSimple("-use2");

	if (!menu) {
		if (PRESSED(ACT_ALTFIRE)) sendButtonActionSimple("+attack2");
		if (RELEASED(ACT_ALTFIRE)) sendButtonActionSimple("-attack2");
		if (PRESSED(ACT_RELOAD)) sendButtonActionSimple("+reload");
		if (RELEASED(ACT_RELOAD)) sendButtonActionSimple("-reload");
		if (PRESSED(ACT_PREV_WEAPON)) sendButtonActionSimple("invprev");
		if (PRESSED(ACT_NEXT_WEAPON)) sendButtonActionSimple("invnext");
		if (PRESSED(ACT_LAST_WEAPON)) sendButtonActionSimple("lastinv");
		if (PRESSED(ACT_SCOREBOARD) && multiplayer) {
			// the scoreboard only. The flat screen view is the vr_flat_screen setting, no button does it
			scoreboardShown = !scoreboardShown;
			sendButtonAction("+showscores", scoreboardShown);
		}
		if (quickSaveFromMenuHold && PRESSED(ACT_QUICKSAVE) && !multiplayer) {
			sendButtonActionSimple("savequick");
			TBXR_Vibrate(80, leftHanded ? 0 : 1, 0.6f);
		}
	} else {
		// let go in a menu of what was held in the game, so nothing stays down
		if (RELEASED(ACT_ALTFIRE)) sendButtonActionSimple("-attack2");
		if (RELEASED(ACT_RELOAD)) sendButtonActionSimple("-reload");

		static const struct { int action; int key; } keys[] = {
			{ ACT_MENU_CONFIRM, K_ENTER }, { ACT_MENU_UP, K_UPARROW }, { ACT_MENU_DOWN, K_DOWNARROW },
			{ ACT_MENU_LEFT, K_LEFTARROW }, { ACT_MENU_RIGHT, K_RIGHTARROW },
			{ ACT_MENU_PAGE_PREV, K_PGUP }, { ACT_MENU_PAGE_NEXT, K_PGDN },
		};
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			if (PRESSED(keys[i].action)) Key_Event(keys[i].key, true);
			if (RELEASED(keys[i].action)) Key_Event(keys[i].key, false);
		}
		if (PRESSED(ACT_MENU_SCROLL_UP)) { Key_Event(K_MWHEELUP, true); Key_Event(K_MWHEELUP, false); }
		if (PRESSED(ACT_MENU_SCROLL_DOWN)) { Key_Event(K_MWHEELDOWN, true); Key_Event(K_MWHEELDOWN, false); }
	}

	previousActions[0] = actions[0];
	previousActions[1] = actions[1];
#undef HAS
#undef PRESSED
#undef RELEASED
}
