// Unattended test switches for the Steam Frame build: the headset on a desk, no
// one wearing it. They come from vr-diag.ini in the data folder and do nothing
// when that file isn't there. See docs/VR_ARCHITECTURE.md.
#ifndef VRDIAG_H
#define VRDIAG_H

#include "TBXR_Common.h"

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

#endif
