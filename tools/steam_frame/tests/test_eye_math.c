// Host test for the eye helpers (VrEyeMath.c): eye offsets, the cant, the HUD shift and the
// pose the layer is submitted with. The HUD check is the one that matters: the shifted 2D
// content has to land exactly where a point at that distance lands when projected through
// the same asymmetric frustum from the same eye.
#include <math.h>
#include <stdio.h>

#include "VrEyeMath.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

int main(void)
{
	// the Frame's recommended image and a typical pair of asymmetric views
	const int W = 1728;
	const float L0 = -1.028f, R0 = 0.885f;		// left eye
	const float L1 = -0.877f, R1 = 1.032f;		// right eye
	const float eyeX0 = -0.0315f, eyeX1 = 0.0315f;

	// ---- offsets: an eye to the left of the head is to the engine's left (+y)
	{
		const float left[3] = {-0.0315f, 0.0f, 0.0f};
		const float right[3] = {0.0315f, 0.0f, 0.0f};
		float o[3];
		VrEye_ToEngineOffset(left, 40.0f, o);
		CHECK(NEAR(o[0], 0.0f, 1e-6f) && NEAR(o[1], 1.26f, 1e-4f) && NEAR(o[2], 0.0f, 1e-6f));
		VrEye_ToEngineOffset(right, 40.0f, o);
		CHECK(NEAR(o[1], -1.26f, 1e-4f));
		// forward (-z) is +x, up is +z
		const float fwd[3] = {0.0f, 0.01f, -0.02f};
		VrEye_ToEngineOffset(fwd, 100.0f, o);
		CHECK(NEAR(o[0], 2.0f, 1e-4f) && NEAR(o[2], 1.0f, 1e-4f));
	}

	// ---- cant: none, then 5 degrees about the up axis (OpenXR y), then the shorter way round
	{
		float axis[3], deg;
		const float none[4] = {0, 0, 0, 1};
		CHECK(!VrEye_CantAxisAngle(none, axis, &deg));
		const float zero[4] = {0, 0, 0, 0};
		CHECK(!VrEye_CantAxisAngle(zero, axis, &deg));

		const float half = 2.5f * (float)M_PI / 180.0f;
		const float yaw5[4] = {0, sinf(half), 0, cosf(half)};
		CHECK(VrEye_CantAxisAngle(yaw5, axis, &deg));
		CHECK(NEAR(deg, 5.0f, 1e-3f));
		// up is z in the engine's axes
		CHECK(NEAR(axis[0], 0.0f, 1e-5f) && NEAR(axis[1], 0.0f, 1e-5f) && NEAR(axis[2], 1.0f, 1e-5f));

		const float yaw5neg[4] = {0, -sinf(half), 0, -cosf(half)};	// the same turn, other sign of q
		CHECK(VrEye_CantAxisAngle(yaw5neg, axis, &deg));
		CHECK(NEAR(deg, 5.0f, 1e-3f) && NEAR(axis[2], 1.0f, 1e-5f));

		// about x (right) is the engine's -y (left): pitch up 3 degrees
		const float h3 = 1.5f * (float)M_PI / 180.0f;
		const float pitch3[4] = {sinf(h3), 0, 0, cosf(h3)};
		CHECK(VrEye_CantAxisAngle(pitch3, axis, &deg));
		CHECK(NEAR(deg, 3.0f, 1e-3f) && NEAR(axis[1], -1.0f, 1e-5f));
	}

	// ---- the axis pixel is where straight ahead lands
	{
		const float axis0 = VrEye_AxisPixel(L0, R0, W);
		CHECK(NEAR(VrEye_ProjectX(0.0f, 0.0f, -10.0f, L0, R0, W), axis0, 1e-2f));
		// the formula VR_Get2DOffset uses: centre plus dx
		const float tanL = tanf(L0), tanR = tanf(R0);
		const float dx = -(tanR + tanL) / (2.0f * (tanR - tanL)) * (float)W;
		CHECK(NEAR(W / 2.0f + dx, axis0, 1e-2f));
	}

	// ---- the HUD: shifted 2D content equals a world point at that distance, both eyes
	{
		const float distances[] = {0.5f, 1.0f, 2.0f, 5.0f};
		for (int i = 0; i < 4; i++)
		{
			const float D = distances[i];
			const float s0 = VrEye_HudShiftPixels(eyeX0, L0, R0, W, D);
			const float s1 = VrEye_HudShiftPixels(eyeX1, L1, R1, W, D);

			// 2D content anchored at the buffer centre: axis offset, then the shift
			const float tanL0 = tanf(L0), tanR0 = tanf(R0), tanL1 = tanf(L1), tanR1 = tanf(R1);
			const float u0 = W / 2.0f - (tanR0 + tanL0) / (2.0f * (tanR0 - tanL0)) * W + s0;
			const float u1 = W / 2.0f - (tanR1 + tanL1) / (2.0f * (tanR1 - tanL1)) * W + s1;
			CHECK(NEAR(u0, VrEye_ProjectX(eyeX0, 0.0f, -D, L0, R0, W), 0.05f));
			CHECK(NEAR(u1, VrEye_ProjectX(eyeX1, 0.0f, -D, L1, R1, W), 0.05f));

			// the left eye's content moves right, the right eye's left
			CHECK(s0 > 0.0f && s1 < 0.0f);
		}
		// and the old fixed shift (width / 36 = 48 px) really was about 0.4 m
		float D = 0.0f;
		for (float d = 0.1f; d < 3.0f; d += 0.01f)
			if (VrEye_HudShiftPixels(eyeX0, L0, R0, W, d) > 48.0f) D = d;
		CHECK(D > 0.3f && D < 0.5f);
		// no distance, no shift
		CHECK(VrEye_HudShiftPixels(eyeX0, L0, R0, W, 0.0f) == 0.0f);
	}

	// ---- disparity of world points: IPD * f / depth, and nothing at infinity
	{
		const float f = VrEye_FocalPixels(L0, R0, W);
		const float ipd = eyeX1 - eyeX0;
		const float depths[] = {0.5f, 1.0f, 3.0f, 20.0f, 1000.0f};
		for (int i = 0; i < 5; i++)
		{
			const float z = -depths[i];
			const float uL = VrEye_ProjectX(eyeX0, 0.0f, z, L0, R0, W);
			const float uR = VrEye_ProjectX(eyeX1, 0.0f, z, L0, R0, W);
			CHECK(NEAR(uL - uR, ipd * f / depths[i], 1e-2f + 1e-5f * ipd * f / depths[i]));
		}
		CHECK(NEAR(VrEye_ProjectX(eyeX0, 0.0f, -1e6f, L0, R0, W) - VrEye_ProjectX(eyeX1, 0.0f, -1e6f, L0, R0, W), 0.0f, 0.1f));
	}

	// ---- the pose a layer view is submitted with: head placed in the stage, eye placed in the head
	{
		const float s = sqrtf(0.5f);
		// head 1.6 m up, 2 m to the right of the origin, turned 90 degrees left (about y)
		const float head[7] = {2.0f, 1.6f, 0.0f, 0.0f, s, 0.0f, s};
		// eye 32 mm to the head's left, no cant
		const float eye[7] = {-0.032f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
		float out[7];
		VrEye_ComposePose(head, eye, out);
		// facing left, the head's left is the stage's +z... turned 90 deg left, the head's -x
		// becomes the stage's +z
		CHECK(NEAR(out[0], 2.0f, 1e-5f) && NEAR(out[1], 1.6f, 1e-5f) && NEAR(out[2], 0.032f, 1e-5f));
		CHECK(NEAR(out[3], 0.0f, 1e-6f) && NEAR(out[4], s, 1e-5f) && NEAR(out[6], s, 1e-5f));

		// with the eye's own cant of 5 degrees about y: the orientations add
		const float half = 2.5f * (float)M_PI / 180.0f;
		const float eyeCant[7] = {0, 0, 0, 0, sinf(half), 0, cosf(half)};
		VrEye_ComposePose(head, eyeCant, out);
		const float total = (90.0f + 5.0f) * 0.5f * (float)M_PI / 180.0f;
		CHECK(NEAR(out[4], sinf(total), 1e-5f) && NEAR(out[6], cosf(total), 1e-5f));
	}

	printf(failures ? "eye math: FAILED (%d)\n" : "eye math: ok\n", failures);
	return failures ? 1 : 0;
}
