// Host test for the menu laser's maths (VrPointerMath.c)
#include <math.h>
#include <stdio.h>

#include "VrPointerMath.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
#define NEAR(a, b, eps) (fabsf((a) - (b)) <= (eps))

static void rotate(const float q[4], const float v[3], float out[3])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	const float cx = y * v[2] - z * v[1], cy = z * v[0] - x * v[2], cz = x * v[1] - y * v[0];
	const float dx = y * cz - z * cy, dy = z * cx - x * cz, dz = x * cy - y * cx;
	out[0] = v[0] + 2 * (w * cx + dx); out[1] = v[1] + 2 * (w * cy + dy); out[2] = v[2] + 2 * (w * cz + dz);
}

int main(void)
{
	const float W = 5.0f, H = 4.5f;

	// a quad 4.5 m ahead, 1 m up, facing the viewer, and a hand a little right of and above the middle
	const float quad[7] = {0.0f, 1.0f, -4.5f, 0, 0, 0, 1};
	const float hand[3] = {0.3f, 1.2f, -0.3f};
	const float ahead[3] = {0, 0, -1};
	float u, v, t;
	CHECK(VrPointer_RayQuad(hand, ahead, quad, W, H, &u, &v, &t));
	CHECK(NEAR(u, 0.5f + 0.3f / W, 1e-4f) && NEAR(v, 0.5f - 0.2f / H, 1e-4f) && NEAR(t, 4.2f, 1e-4f));

	// pointing right by 10 degrees moves it right by 4.2 * tan(10)
	const float right10[3] = {sinf(10.0f * (float)M_PI / 180.0f), 0, -cosf(10.0f * (float)M_PI / 180.0f)};
	CHECK(VrPointer_RayQuad(hand, right10, quad, W, H, &u, &v, &t));
	CHECK(NEAR(u, 0.5f + (0.3f + 4.2f * tanf(10.0f * (float)M_PI / 180.0f)) / W, 1e-3f));
	// the direction needn't be a unit vector
	const float longAhead[3] = {0, 0, -7};
	CHECK(VrPointer_RayQuad(hand, longAhead, quad, W, H, &u, &v, &t) && NEAR(t, 4.2f, 1e-4f));

	// pointing off the panel still gives u and v, outside 0..1
	const float wide[3] = {1.0f, 0, -0.2f};
	CHECK(VrPointer_RayQuad(hand, wide, quad, W, H, &u, &v, &t) && u > 1.0f);

	// away from it, along it, behind the hand
	const float back[3] = {0, 0, 1};
	CHECK(!VrPointer_RayQuad(hand, back, quad, W, H, &u, &v, &t));
	const float along[3] = {1, 0, 0};
	CHECK(!VrPointer_RayQuad(hand, along, quad, W, H, &u, &v, &t));
	const float behindHand[3] = {0.3f, 1.2f, -9.0f};
	CHECK(!VrPointer_RayQuad(behindHand, ahead, quad, W, H, &u, &v, &t));

	// a quad turned 90 degrees left about y (what the screen does after a snap turn): it sits to the left
	// of the origin facing +x; a hand at the origin pointing at it
	const float s = sqrtf(0.5f);
	const float turned[7] = {-4.5f, 1.0f, 0.0f, 0, s, 0, s};
	const float toLeft[3] = {-1, 0, 0};
	const float origin[3] = {0, 1.0f, 0};
	CHECK(VrPointer_RayQuad(origin, toLeft, turned, W, H, &u, &v, &t));
	CHECK(NEAR(u, 0.5f, 1e-4f) && NEAR(v, 0.5f, 1e-4f) && NEAR(t, 4.5f, 1e-4f));
	// aimed a little toward -z: the quad turned left faces +x, so its right is toward -z
	const float slightly[3] = {-1, 0, -0.1f};
	CHECK(VrPointer_RayQuad(origin, slightly, turned, W, H, &u, &v, &t));
	CHECK(u > 0.5f);

	// the aim pose's forward
	const float aim[7] = {0, 0, 0, 0, s, 0, s};	// turned 90 left: forward is -x
	float f[3];
	VrPointer_Forward(aim, f);
	CHECK(NEAR(f[0], -1.0f, 1e-5f) && NEAR(f[1], 0.0f, 1e-5f) && NEAR(f[2], 0.0f, 1e-5f));

	// the beam: its long axis along the line, its face to the eye, whatever way the line goes
	const float starts[4][3] = {{0.3f, 1.2f, -0.3f}, {0, 1, 0}, {1, 2, -1}, {-0.4f, 0.9f, -0.2f}};
	const float ends[4][3] = {{0.5f, 1.0f, -4.4f}, {0, 1, -4}, {1, -3, -1.0f}, {-0.4f, 5.0f, -0.2f}};
	const float eye[3] = {0.0f, 1.6f, 0.0f};
	for (int i = 0; i < 4; i++)
	{
		float pose[7], axisY[3] = {0, 1, 0}, axisZ[3] = {0, 0, 1}, axisX[3] = {1, 0, 0}, y[3], z[3], x[3];
		const float length = VrPointer_BeamPose(starts[i], ends[i], eye, pose);
		const float dx = ends[i][0] - starts[i][0], dy = ends[i][1] - starts[i][1], dz = ends[i][2] - starts[i][2];
		const float expected = sqrtf(dx * dx + dy * dy + dz * dz);
		CHECK(NEAR(length, expected, 1e-4f));
		rotate(pose + 3, axisY, y);
		rotate(pose + 3, axisZ, z);
		rotate(pose + 3, axisX, x);
		CHECK(NEAR(y[0], dx / expected, 1e-4f) && NEAR(y[1], dy / expected, 1e-4f) && NEAR(y[2], dz / expected, 1e-4f));
		CHECK(NEAR(x[0] * y[0] + x[1] * y[1] + x[2] * y[2], 0.0f, 1e-4f));
		const float toEye[3] = {eye[0] - pose[0], eye[1] - pose[1], eye[2] - pose[2]};
		CHECK(z[0] * toEye[0] + z[1] * toEye[1] + z[2] * toEye[2] > 0.0f);
		CHECK(NEAR(pose[3] * pose[3] + pose[4] * pose[4] + pose[5] * pose[5] + pose[6] * pose[6], 1.0f, 1e-4f));
		CHECK(NEAR(pose[0], (starts[i][0] + ends[i][0]) / 2, 1e-5f));
	}

	// the dot: turned like the panel, a little out of its face
	const float point[3] = {0.5f, 1.0f, -4.5f};
	float dot[7];
	VrPointer_DotPose(point, quad, 0.01f, dot);
	CHECK(NEAR(dot[2], -4.49f, 1e-5f) && NEAR(dot[6], 1.0f, 1e-6f));
	VrPointer_DotPose((const float[3]){-4.5f, 1.0f, 0.0f}, turned, 0.01f, dot);
	CHECK(NEAR(dot[0], -4.49f, 1e-4f));	// the turned quad faces +x, so toward the origin is +x

	printf(failures ? "pointer math: FAILED (%d)\n" : "pointer math: ok\n", failures);
	return failures ? 1 : 0;
}
