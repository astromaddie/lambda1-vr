// Unattended test switches for the Steam Frame build: the headset on a desk, no
// one wearing it. They come from vr-diag.ini in the data folder and do nothing
// when that file isn't there. See docs/VR_ARCHITECTURE.md.
#ifndef VRDIAG_H
#define VRDIAG_H

#include "TBXR_Common.h"
#include "VrInputFrame.h"

void VrDiag_Init(void);						// once, reads vr-diag.ini
bool VrDiag_ForceRender(void);				// draw and run while shouldRender is false
void VrDiag_FrameBegin(void);				// each XR frame, after xrWaitFrame and the input
void VrDiag_FrameEnd(void);					// each XR frame, after xrEndFrame
void VrDiag_AfterEye(int eye);				// the eye is drawn, still bound, before it's released

// Stand-ins for what the runtime can't give an unworn headset. They return false
// (and touch nothing) unless force_render is on.
bool VrDiag_SyntheticHead(XrPosef* pose);
bool VrDiag_SyntheticViews(XrView* views);
bool VrDiag_SyntheticHand(int hand, ovrTrackedController* controller);

// diag_press: holds buttons down for a while, so the controller map can run with nobody holding
// controllers. Returns true when the buttons were changed; Injecting says there are some to come.
bool VrDiag_InjectInputs(frameRaw_t* raw);
bool VrDiag_Injecting(void);

// time spent in the blocking XR calls, for the [vr-perf] split between work and waiting
enum { VRDIAG_TIME_WAIT_FRAME, VRDIAG_TIME_ACQUIRE };
void VrDiag_NoteTiming(int what, double ms);

#endif
