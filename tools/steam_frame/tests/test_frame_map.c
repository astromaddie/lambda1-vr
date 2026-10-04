// Host test for the Steam Frame controller map (VrFrameMap.c). It builds the effective table, physical
// input to game action, for both hands and every context (playing, the hands behind the head, a
// menu, multiplayer), and fails on:
//   - one physical input doing two different things in the same context;
//   - an action with two inputs that isn't a documented alias;
//   - an action that nothing can reach;
//   - a grip or trigger doing something that isn't a hand's job.
// With --markdown it prints the table for the docs.
#include <stdio.h>
#include <string.h>

#include "VrFrameMap.h"

static int failures = 0;
#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } while (0)

// actions that may have more than one input in a context, because they are meant to
static bool aliasAllowed(int action)
{
	switch (action)
	{
		case ACT_USE:				// X and the weapon hand's grip
		case ACT_PREV_WEAPON:		// left bumper and D-pad up
		case ACT_NEXT_WEAPON:		// right bumper and D-pad down
		case ACT_FLASHLIGHT:		// right stick click and View
		case ACT_SCREEN_VIEW:		// right stick up, and View in multiplayer (the scoreboard)
		case ACT_QUICKSAVE:			// hold Menu; with the hand behind the head right stick click and View
		case ACT_MENU_BACK:			// B, Menu, View
		case ACT_MENU_UP: case ACT_MENU_DOWN: case ACT_MENU_LEFT: case ACT_MENU_RIGHT:	// D-pad and left stick
		case ACT_MOVE: case ACT_TURN:	// four stick directions
			return true;
		default:
			return false;
	}
}

static bool isHandInput(int input)
{
	return input == FI_L_GRIP || input == FI_R_GRIP;
}

static bool handAction(int action)
{
	return action == ACT_BACKPACK || action == ACT_USE || action == ACT_USE_OFF || action == ACT_STEADY || action == ACT_NONE;
}

int main(int argc, char** argv)
{
	int rowCount;
	const frameMapRow_t* rows = VrFrameMap_Rows(&rowCount);

	bool reached[ACT_COUNT] = {false};

	for (int hand = 0; hand < 2; hand++)
	{
		const bool left = hand == 1;
		for (int context = 0; context < 32; context++)
		{
			// a menu has no hands behind the head
			if ((context & CTX_MENU) && (context & (CTX_DOM_ZONE | CTX_OFF_ZONE)))
				continue;
			// quick save and load aren't offered in multiplayer
			if ((context & CTX_MULTI) && (context & CTX_OFF_ZONE))
				continue;

			int inputsOfAction[ACT_COUNT] = {0};
			for (int input = 0; input < FI_COUNT; input++)
			{
				// overlapping rows: how many match this input in this context
				int matches = 0, firstAction = ACT_NONE;
				for (int r = 0; r < rowCount; r++)
				{
					const int rowInput = rows[r].role ? VrFrameMap_RoleInput(rows[r].role, left) : rows[r].input;
					if (rowInput != input) continue;
					if ((context & rows[r].require) != rows[r].require || (context & rows[r].forbid) != 0) continue;
					if (matches++ == 0) firstAction = rows[r].action;
					else FAIL("%s: %s does %s and %s (context %d, %s)", left ? "left-handed" : "right-handed",
							  VrFrameMap_InputName(input), VrFrameMap_ActionName(firstAction), VrFrameMap_ActionName(rows[r].action),
							  context, "rows overlap");
				}
				const int action = VrFrameMap_Action(input, context, left);
				if (action != firstAction) FAIL("Action() and the rows disagree for %s", VrFrameMap_InputName(input));
				if (action == ACT_NONE) continue;

				inputsOfAction[action]++;
				reached[action] = true;
				if (isHandInput(input) && !handAction(action))
					FAIL("%s does %s, which isn't a hand's job", VrFrameMap_InputName(input), VrFrameMap_ActionName(action));
			}
			for (int action = 1; action < ACT_COUNT; action++)
				if (inputsOfAction[action] > 1 && !aliasAllowed(action))
					FAIL("%s: %s has %d inputs in context %d and isn't a listed alias", left ? "left-handed" : "right-handed",
						 VrFrameMap_ActionName(action), inputsOfAction[action], context);
		}
	}

	// everything must be reachable somewhere
	for (int action = 1; action < ACT_COUNT; action++)
		if (!reached[action]) FAIL("nothing does %s", VrFrameMap_ActionName(action));

	// the hands: face buttons, bumpers, D-pad, Menu, View and sticks don't change with the weapon hand;
	// the weapon hand's trigger fires and its grip is the interaction / backpack one
	for (int context = 0; context < 32; context++)
	{
		for (int input = 0; input < FI_COUNT; input++)
		{
			if (input == FI_L_TRIGGER || input == FI_R_TRIGGER || input == FI_L_GRIP || input == FI_R_GRIP) continue;
			if (VrFrameMap_Action(input, context, false) != VrFrameMap_Action(input, context, true))
				FAIL("%s changes with the hand in context %d", VrFrameMap_InputName(input), context);
		}
		if (!(context & CTX_MENU))
		{
			if (VrFrameMap_Action(FI_R_TRIGGER, context, false) != ACT_FIRE || VrFrameMap_Action(FI_L_TRIGGER, context, false) != ACT_ALTFIRE)
				FAIL("right-handed triggers");
			if (VrFrameMap_Action(FI_L_TRIGGER, context, true) != ACT_FIRE || VrFrameMap_Action(FI_R_TRIGGER, context, true) != ACT_ALTFIRE)
				FAIL("left-handed triggers");
		}
		else if (VrFrameMap_Action(FI_R_TRIGGER, context, false) != ACT_MENU_CLICK || VrFrameMap_Action(FI_L_TRIGGER, context, true) != ACT_MENU_CLICK)
			FAIL("the laser's trigger in a menu");
	}

	// the things the user asked for by name
	struct { int input; int context; int action; } expect[] = {
		{ FI_A, 0, ACT_JUMP }, { FI_B, 0, ACT_CROUCH_HOLD }, { FI_X, 0, ACT_USE }, { FI_Y, 0, ACT_RELOAD },
		{ FI_L_STICK_CLICK, 0, ACT_CROUCH_TOGGLE }, { FI_R_STICK_CLICK, 0, ACT_FLASHLIGHT },
		{ FI_L_BUMPER, 0, ACT_PREV_WEAPON }, { FI_R_BUMPER, 0, ACT_NEXT_WEAPON },
		{ FI_DPAD_UP, 0, ACT_PREV_WEAPON }, { FI_DPAD_DOWN, 0, ACT_NEXT_WEAPON },
		{ FI_DPAD_RIGHT, 0, ACT_LAST_WEAPON }, { FI_DPAD_LEFT, 0, ACT_CROWBAR },
		{ FI_MENU_SHORT, 0, ACT_PAUSE }, { FI_MENU_HOLD, 0, ACT_QUICKSAVE },
		{ FI_VIEW_SHORT, 0, ACT_FLASHLIGHT }, { FI_VIEW_HOLD_1S, 0, ACT_RECENTRE }, { FI_VIEW_HOLD_3S, 0, ACT_RECENTRE_HEIGHT },
		{ FI_R_GRIP, 0, ACT_USE }, { FI_R_GRIP, CTX_DOM_ZONE, ACT_BACKPACK },
		{ FI_L_GRIP, 0, ACT_STEADY }, { FI_L_GRIP, CTX_OFF_GRIP_AWAY, ACT_USE_OFF },
		{ FI_A, CTX_MENU, ACT_MENU_CONFIRM }, { FI_B, CTX_MENU, ACT_MENU_BACK },
		{ FI_DPAD_UP, CTX_MENU, ACT_MENU_UP }, { FI_L_STICK_UP, CTX_MENU, ACT_MENU_UP },
	};
	for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++)
		if (VrFrameMap_Action(expect[i].input, expect[i].context, false) != expect[i].action)
			FAIL("%s should be %s (context %d)", VrFrameMap_InputName(expect[i].input), VrFrameMap_ActionName(expect[i].action), expect[i].context);

	if (argc > 1 && strcmp(argv[1], "--markdown") == 0)
	{
		printf("| Frame input | Playing | Notes |\n| --- | --- | --- |\n");
		for (int r = 0; r < rowCount; r++)
		{
			if (rows[r].require & CTX_MENU) continue;
			const char* name = rows[r].role == ROLE_TRIGGER_WEAPON ? "weapon hand trigger" : rows[r].role == ROLE_TRIGGER_OTHER ? "other hand trigger"
							 : rows[r].role == ROLE_GRIP_WEAPON ? "weapon hand grip" : rows[r].role == ROLE_GRIP_OTHER ? "other hand grip" : VrFrameMap_InputName(rows[r].input);
			printf("| %s | %s | %s |\n", name, VrFrameMap_ActionName(rows[r].action), rows[r].note);
		}
		printf("\n| Frame input | In a menu | Notes |\n| --- | --- | --- |\n");
		for (int r = 0; r < rowCount; r++)
		{
			if (!(rows[r].require & CTX_MENU)) continue;
			const char* name = rows[r].role == ROLE_TRIGGER_WEAPON ? "weapon hand trigger" : VrFrameMap_InputName(rows[r].input);
			printf("| %s | %s | %s |\n", name, VrFrameMap_ActionName(rows[r].action), rows[r].note);
		}
	}

	printf(failures ? "frame map: FAILED (%d)\n" : "frame map: ok\n", failures);
	return failures ? 1 : 0;
}
