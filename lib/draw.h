#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "trig.h"
#include "../ps1/cop0.h"
#include "../ps1/gpucmd.h"
#include "../ps1/gte.h"
#include "../ps1/registers.h"

#pragma once

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define ENABLE_Z_CLIP false
#define CAMERA_DIST_RADIUS 256
// The GTE uses a 20.12 fixed-point format for most values. What this means is
// that fractional values will be stored as integers by multiplying them by a
// fixed unit, in this case 4096 or 1 << 12 (hence making the fractional part 12
// bits long). We'll define this unit value to make their handling easier.
#define ONE (1 << 12)
#define NEAR_Z 16

//based on raylib math
typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t w;
} Quat;

Quat QuatMult(Quat q1, Quat q2)
{
    Quat result = { 0 };

    int qax = q1.x, qay = q1.y, qaz = q1.z, qaw = q1.w;
    int qbx = q2.x, qby = q2.y, qbz = q2.z, qbw = q2.w;

    result.x = (qax*qbw + qaw*qbx + qay*qbz - qaz*qby) >> 12; //each component is in fixed point space, but after a multiply that doubles, so shift back down
    result.y = (qay*qbw + qaw*qby + qaz*qbx - qax*qbz) >> 12;
    result.z = (qaz*qbw + qaw*qbz + qax*qby - qay*qbx) >> 12;
    result.w = (qaw*qbw - qax*qbx - qay*qby - qaz*qbz) >> 12;

    return result;
}

static Quat QuatRot(int yaw, int pitch, int roll)
{
	int ySin = isin(yaw >> 1);
    int yCos = icos(yaw >> 1);
	int pSin = isin(pitch >> 1);
    int pCos = icos(pitch >> 1);
	int rSin = isin(roll >> 1);
    int rCos = icos(roll >> 1);
	Quat yQuat = { 0, ySin, 0, yCos };
	Quat pQuat = { pSin, 0, 0, pCos };
	Quat rQuat = { 0, 0, rSin, rCos };
	Quat q = QuatMult(yQuat, QuatMult(pQuat, rQuat));
	return q;
}

typedef enum {
	ADD_TRI_GOOD = 0,
	ADD_TRI_BAD = 1,
	ADD_TRI_CLIP = 2
} AddTriResult;

typedef struct {
    int16_t x, y, z;        // world position
    int16_t yaw, pitch;     // camera orientation
	int16_t orbit_yaw;      //position around player
} Camera;

typedef struct {
	int16_t x, y, z; 
	int16_t yaw, pitch, roll; 
	uint16_t numFaces;
	const Face *faces;
	uint16_t numVerts; //TODO: I didnt need this early on, if its wasteful, remove
	const GTEVector16 *vertices;
	bool isTextured;
	const TextureInfo *textinfo;
} DrawObj;

static DrawObj CreateDrawObj(
	int16_t x, int16_t y, int16_t z, 
	int16_t yaw, int16_t pitch, int16_t roll, 
	uint16_t numFaces, const Face *faces, 
	uint16_t numVerts, const GTEVector16 *vertices
)
{
	DrawObj obj = {0};
	obj.x = x;
	obj.y = y;
	obj.z = z;
	obj.yaw = yaw;
	obj.pitch = pitch;
	obj.roll = roll;
	obj.numFaces = numFaces;
	obj.faces = faces;
	obj.numVerts = numVerts;
	obj.vertices = vertices;
	obj.isTextured = false;
	return obj;
}

static void setupGTE(int width, int height) {
	// Ensure the GTE, which is coprocessor 2, is enabled. MIPS coprocessors are
	// enabled through the status register in coprocessor 0, which is always
	// accessible.
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	// Set the offset to be added to all calculated screen space coordinates (we
	// want our cube to appear at the center of the screen) Note that OFX and
	// OFY are 16.16 fixed-point rather than 20.12.
	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);

	// Set the distance of the perspective projection plane (i.e. the camera's
	// focal length), which affects the field of view.
	int focalLength = (width < height) ? width : height;

	gte_setControlReg(GTE_H, focalLength / 2);

	// Set the scaling factor for Z averaging. For each polygon drawn, the GTE
	// will sum the transformed Z coordinates of its vertices multiplied by this
	// value in order to derive the ordering table bucket index the polygon will
	// be sorted into. This will work best if the ordering table length is a
	// multiple of 12 (i.e. both 3 and 4) or high enough to make any rounding
	// error negligible.
	gte_setControlReg(GTE_ZSF3, ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, ORDERING_TABLE_SIZE / 4);
}

// When transforming vertices, the GTE will multiply their vectors by a 3x3
// matrix stored in its registers. This matrix can be used, among other things,
// to rotate the model by multiplying it by the appropriate rotation matrices.
// The two functions below handle manipulation of this matrix.
static void multiplyCurrentMatrixByVectors(GTEMatrix *output) {
	// Multiply the GTE's current matrix by the matrix whose column vectors are
	// V0/V1/V2, then store the result to the provided location. This has to be
	// done one column at a time, as the GTE only supports multiplying a matrix
	// by a vector using the MVMVA command.
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_NONE);
	output->values[0][0] = gte_getDataReg(GTE_IR1);
	output->values[1][0] = gte_getDataReg(GTE_IR2);
	output->values[2][0] = gte_getDataReg(GTE_IR3);

	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V1 | GTE_CV_NONE);
	output->values[0][1] = gte_getDataReg(GTE_IR1);
	output->values[1][1] = gte_getDataReg(GTE_IR2);
	output->values[2][1] = gte_getDataReg(GTE_IR3);

	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V2 | GTE_CV_NONE);
	output->values[0][2] = gte_getDataReg(GTE_IR1);
	output->values[1][2] = gte_getDataReg(GTE_IR2);
	output->values[2][2] = gte_getDataReg(GTE_IR3);
}

static void SetMatrixFromQuatRot(Quat q)
{
	static GTEMatrix multiplied;
	int a2 = (q.x*q.x) >> 12; //rescale back to single fixed point space
    int b2 = (q.y*q.y) >> 12;
    int c2 = (q.z*q.z) >> 12;
    int ac = (q.x*q.z) >> 12;
    int ab = (q.x*q.y) >> 12;
    int bc = (q.y*q.z) >> 12;
    int ad = (q.w*q.x) >> 12;
    int bd = (q.w*q.y) >> 12;
    int cd = (q.w*q.z) >> 12;

    int m0 = ONE - ((b2 + c2) << 1);
    int m1 = (ab + cd) << 1;
    int m2 = (ac - bd) << 1;

    int m4 = (ab - cd) << 1;
    int m5 = ONE - ((a2 + c2) << 1);
    int m6 = (bc + ad) << 1;

    int m8 = (ac + bd) << 1;
    int m9 = (bc - ad) << 1;
    int m10 = ONE - ((a2 + b2) << 1);
	gte_setColumnVectors
	(
		m0, m1, m2,
		m4, m5, m6,
		m8, m9, m10
	);
	multiplyCurrentMatrixByVectors(&multiplied);
	gte_loadRotationMatrix(&multiplied);
}

static void rotateCurrentMatrix(int yaw, int pitch, int roll) {
	static GTEMatrix multiplied;
	int s, c;

	// For each axis, compute the rotation matrix then "combine" it with the
	// GTE's current matrix by multiplying the two and writing the result back
	// to the GTE's registers.
	//Adjusted based on observation, yaw -> roll, pitch -> yaw, roll -> pitch
	if (yaw) {
		s = isin(yaw);
		c = icos(yaw);

		gte_setColumnVectors(
			 c,   0, s,
			 0, ONE, 0,
			-s,   0, c
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	if (pitch) {
		s = isin(pitch);
		c = icos(pitch);

		gte_setColumnVectors(
			ONE, 0,  0,
			  0, c, -s,
			  0, s,  c
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	if (roll) {
		s = isin(roll);
		c = icos(roll);

		gte_setColumnVectors(
			c, -s,   0,
			s,  c,   0,
			0,  0, ONE
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
}

static void SetGtePosAndRot(int x, int y, int z, int yaw, int pitch, int roll)
{
	// Reset the GTE's translation vector (added to each vertex) and
		// transformation matrix, then modify the matrix to rotate the cube. The
		// translation vector is used here to move the cube away from the camera
		// so it can be seen.
		gte_setControlReg(GTE_TRX,   x);
		gte_setControlReg(GTE_TRY,   y);
		gte_setControlReg(GTE_TRZ,   z);
		gte_setRotationMatrix(
			ONE,   0,   0,
			  0, ONE,   0,
			  0,   0, ONE
		);
		rotateCurrentMatrix(yaw, pitch, roll);
}

// Build view+model into GTE for this object
static void SetGteViewAndModel(const Camera* cam, const DrawObj* obj)
{
    // 1) Start from identity
    gte_setRotationMatrix(
        ONE, 0,   0,
        0,   ONE, 0,
        0,   0,   ONE
    );

	//2) create view quaterion and set view rot matrix
	//  ... quat version for camera gimble lock fix...
	Quat rot = QuatRot(cam->yaw, cam->pitch, 0); //not inverted here
	SetMatrixFromQuatRot(rot);

    // 3) Compute T = R_view * (P_obj - C)
    GTEVector16 diff;
    diff.x = (int16_t)(obj->x - cam->x);
    diff.y = (int16_t)(obj->y - cam->y);
    diff.z = (int16_t)(obj->z - cam->z);
    diff._padding = 0;

    gte_loadV0(&diff);
    gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_NONE);

    int offx = (int16_t)gte_getDataReg(GTE_IR1);
    int offy = (int16_t)gte_getDataReg(GTE_IR2);
    int offz = (int16_t)gte_getDataReg(GTE_IR3);

    gte_setControlReg(GTE_TRX, offx);
    gte_setControlReg(GTE_TRY, offy);
    gte_setControlReg(GTE_TRZ, offz);

    // 4) Now apply OBJECT rotation, giving R = R_view * R_obj
    rotateCurrentMatrix(obj->yaw, obj->pitch, obj->roll);
}

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Intersect segment A->B with plane z = nearZ.
// Assumes A is inside (Az >= nearZ) and B is outside (Bz < nearZ).
static GTEVector16 IntersectNear(const GTEVector16* A, const GTEVector16* B, int nearZ)
{
    int32_t Az = A->z;
    int32_t Bz = B->z;
    int32_t denom = (Bz - Az);              // negative in the "inside->outside" case
    if (denom == 0) {
        GTEVector16 P = *A;
        P.z = (int16_t)nearZ;
        return P;
    }

    // t in 12-bit fixed: t12 = ((nearZ - Az) / (Bz - Az)) * 4096
    int32_t t12 = ((int32_t)(nearZ - Az) << 12) / denom;
    t12 = clampi(t12, 0, 1 << 12);          // keep it sane (0..4096)

    int32_t dx = (int32_t)B->x - (int32_t)A->x;
    int32_t dy = (int32_t)B->y - (int32_t)A->y;

    GTEVector16 P;
    P.x = (int16_t)( (int32_t)A->x + ((dx * t12) >> 12) );
    P.y = (int16_t)( (int32_t)A->y + ((dy * t12) >> 12) );
    P.z = (int16_t)nearZ;
    P._padding = 0;
    return P;
}

static GTEVector16 gte_mvmva_cam(const GTEVector16* v)
{
    gte_loadV0(v);
    //gte_command(GTE_CMD_MVMVA | GTE_SF);
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_TR);

    GTEVector16 o;
	o.x = (int16_t)gte_getDataReg(GTE_IR1);
    o.y = (int16_t)gte_getDataReg(GTE_IR2);
    o.z = (int16_t)gte_getDataReg(GTE_IR3);
    return o;
}

static bool AddClippedTri(
	const GTEVector16* tv0, const GTEVector16* tv1, const GTEVector16* tv2, 
	DMAChain *chain, const Face *face
)
{
	// apply perspective to computed tris
	SetGtePosAndRot( 0, 0, 0, 0, 0, 0 );
	gte_loadV0(tv0);
	gte_loadV1(tv1);
	gte_loadV2(tv2);
	//gte_command(GTE_CMD_RTPT | GTE_SF );
	gte_command(GTE_CMD_RTPT | GTE_SF);
	
	//detect overflow
	uint32_t gte_flag = (uint32_t)gte_getControlReg(GTE_FLAG); //GTE_FLAG_DIVIDE_OVERFLOW
	if((gte_flag & GTE_FLAG_DIVIDE_OVERFLOW) && (tv0->z < NEAR_Z || tv1->z < NEAR_Z || tv2->z < NEAR_Z))
	{
		return false;
	}

	// Determine the winding order of the vertices on screen. If they
	// are ordered clockwise then the face is visible, otherwise it can
	// be skipped as it is not facing the camera.
	gte_command(GTE_CMD_NCLIP); 
	int order = gte_getDataReg(GTE_MAC0);

	if (order <= 0){return false;}

	// Save the first transformed vertex (the GTE only keeps the X/Y
	// coordinates of the last 3 vertices processed and Z coordinates of
	// the last 4 vertices processed) and apply projection to the last
	// vertex.
	uint32_t xy0 = gte_getDataReg(GTE_SXY0);

	// Calculate the average Z coordinate of all vertices and use it to
	// determine the ordering table bucket index for this face.
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	int zIndex = gte_getDataReg(GTE_OTZ);
	//see if flipping it helps?
	//zIndex = (ORDERING_TABLE_SIZE - 1) - zIndex;
	if ((zIndex < 0) || (zIndex >= ORDERING_TABLE_SIZE)) {return false;}

	// Create a new tri and give its vertices the X/Y coordinates
	// calculated by the GTE.
	uint32_t *ptr;
	ptr    = allocatePacket(chain, zIndex, 4, false);
	if (!ptr){ return false; }
	ptr[0] = face->color | gp0_shadedTriangle(false, false, false);
	//ptr[0] = face->color | gp0_triangle(false, false);
	ptr[1] = xy0;
	gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
	gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
	gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
	return true;
}

/// @brief set obj matrix before call
/// @param tv0 - triangle vector 0
/// @param tv1 - triangle vector 1
/// @param tv2 - triangle vector 2
/// @param chain - the DMA chain pointer
/// @param face - the triangle face pointer
/// @return 
static AddTriResult AddTri(
	const GTEVector16* tv0, const GTEVector16* tv1, const GTEVector16* tv2, 
	DMAChain *chain, const Face *face,
	bool textured,const TextureInfo *textInfo
)
{
	// apply perspective to computed tris
	//SetGtePosAndRot( 0, 0, 0, 0, 0, 0 ); // this function will not do this for you
	gte_loadV0(tv0);
	gte_loadV1(tv1);
	gte_loadV2(tv2);
	//gte_command(GTE_CMD_RTPT | GTE_SF );
	gte_command(GTE_CMD_RTPT | GTE_SF);
	//gl_Position = uProjectionMatrix * uViewMatrix * uModelMatrix * aVertexPosition; //todo: need something weith the camera in mind?
	
	//detect overflow
	uint32_t gte_flag = (uint32_t)gte_getControlReg(GTE_FLAG); //GTE_FLAG_DIVIDE_OVERFLOW
	if((gte_flag & GTE_FLAG_DIVIDE_OVERFLOW) && (tv0->z < NEAR_Z || tv1->z < NEAR_Z || tv2->z < NEAR_Z))
	{
		return ADD_TRI_CLIP;
	}

	// backface culling
	gte_command(GTE_CMD_NCLIP); 
	int order = gte_getDataReg(GTE_MAC0);
	if (order <= 0){return ADD_TRI_BAD;}

	// Calculate the average Z coordinate of all vertices and use it to
	// determine the ordering table bucket index for this face.
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	int zIndex = gte_getDataReg(GTE_OTZ);
	//see if flipping it helps?
	//zIndex = (ORDERING_TABLE_SIZE - 1) - zIndex;
	if ((zIndex < 0) || (zIndex >= ORDERING_TABLE_SIZE)) {return ADD_TRI_BAD;}

	// Create a new tri and give its vertices the X/Y coordinates
	// calculated by the GTE.
	uint32_t *ptr;
	if(textured)
	{
		//--best guess for texture
		ptr    = allocatePacket(chain, zIndex, 7, false);
		ptr[0] = 0xFFFFFF | gp0_triangle(true, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		//word 2 = CLUT<<16 | (V1<<8) | U1
		ptr[2] = textInfo->clut<<16 | (0<<8) | 0;
		gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);
		//word 4 = PAGE<<16 | (V2<<8) | U2
		ptr[4] = textInfo->page<<16 | (10<<8) | 12;
		gte_storeDataReg(GTE_SXY2, 5 * 4, ptr);
		//word 6 = 0<<16 | (V3<<8) | U3
		ptr[6] = 0<<16 | (5<<8) | 2;
		//--and then page (rem after cause exec in rev) //todo: do this per object if textured
		ptr    = allocatePacket(chain, 0, 1, false);
		ptr[0] = gp0_texpage(textInfo->page, false, false);
	}
	else
	{
		ptr    = allocatePacket(chain, zIndex, 4, false);
		if (!ptr){ return ADD_TRI_BAD; }
		ptr[0] = face->color | gp0_shadedTriangle(false, false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);//I think 4 because bytes, 4 bytes is 32 bits
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
	}
	return ADD_TRI_GOOD;
}

static void DrawObject(
	DMAChain *chain,
	const DrawObj *obj,
	const Camera *camera
)
{
	//set the matrix, initial
	//SetGtePosAndRot( obj->x, obj->y, obj->z, obj->yaw, obj->pitch, obj->roll);
	SetGteViewAndModel(camera, obj);
	// Draw the obj one face at a time.
	for (int i = 0; i < obj->numFaces; i++) 
	{
		const Face *face = &(obj->faces)[i];
		AddTriResult res = AddTri(
			&(obj->vertices)[face->vertices[0]],
			&(obj->vertices)[face->vertices[1]],
			&(obj->vertices)[face->vertices[2]], 
			chain, face, 
			obj->isTextured, obj->textinfo
		);
		if(ENABLE_Z_CLIP && res==ADD_TRI_CLIP) //handle clipping of near plane
		{
			//initial tri work (no perspective because we dont want to risk overflow yet)
			GTEVector16 tv0 = gte_mvmva_cam(&(obj->vertices)[face->vertices[0]]);
			GTEVector16 tv1 = gte_mvmva_cam(&(obj->vertices)[face->vertices[1]]);
			GTEVector16 tv2 = gte_mvmva_cam(&(obj->vertices)[face->vertices[2]]);
			
			//DEFINE NEAR PLANE
			int16_t sz0 = tv0.z;
			int16_t sz1 = tv1.z;
			int16_t sz2 = tv2.z;
			//handle the near clip stuff, deal with off screen
			bool wasCorrected = false; //marks if was corrected
			if (sz0 < NEAR_Z && sz1 < NEAR_Z && sz2 < NEAR_Z) 
			{
				continue;
			}
			else if (sz0 < NEAR_Z)
			{
				if (sz1 < NEAR_Z) //sz1 and sz0
				{
					tv0 = IntersectNear(&tv2, &tv0, NEAR_Z);
					tv1 = IntersectNear(&tv2, &tv1, NEAR_Z);
					wasCorrected = true;
				}
				else if (sz2 < NEAR_Z) //sz2 and sz0
				{
					tv0 = IntersectNear(&tv1, &tv0, NEAR_Z);
					tv2 = IntersectNear(&tv1, &tv2, NEAR_Z);
					wasCorrected = true;
				}
				else //just sz0
				{
					GTEVector16 tv3 = IntersectNear(&tv1, &tv0, NEAR_Z);
					GTEVector16 tv4 = IntersectNear(&tv2, &tv0, NEAR_Z);
					if(!AddClippedTri(&tv1,&tv2,&tv4,chain,face)){}
					if(!AddClippedTri(&tv1,&tv4,&tv3,chain,face)){}
					continue;
				}
			}
			else if (sz1 < NEAR_Z)
			{
				if (sz2 < NEAR_Z) //sz1 and sz2
				{
					tv1 = IntersectNear(&tv0, &tv1, NEAR_Z);
					tv2 = IntersectNear(&tv0, &tv2, NEAR_Z);
					wasCorrected = true;
				}
				else //just sz1
				{
					GTEVector16 tv3 = IntersectNear(&tv0, &tv1, NEAR_Z);
					GTEVector16 tv4 = IntersectNear(&tv2, &tv1, NEAR_Z);
					if(!AddClippedTri(&tv0,&tv2,&tv4,chain,face)){}
					if(!AddClippedTri(&tv0,&tv4,&tv3,chain,face)){}
					continue;
				}
			}
			else if (sz2 < NEAR_Z) //we know because this is the 3rd check, only sz2
			{
				GTEVector16 tv3 = IntersectNear(&tv0, &tv2, NEAR_Z);
				GTEVector16 tv4 = IntersectNear(&tv1, &tv2, NEAR_Z);
				if(!AddClippedTri(&tv0,&tv1,&tv4,chain,face)){}
				if(!AddClippedTri(&tv0,&tv4,&tv3,chain,face)){}
				continue;
			}
			//call add tri
			if(!AddClippedTri(&tv0,&tv1,&tv2,chain,face)){}
			// prepare for next tri, this way I dont have to store which needs clipped and handle later
			SetGtePosAndRot( obj->x, obj->y, obj->z, obj->yaw, obj->pitch, obj->roll);
		}
	}
}

static bool FinishDraw(DMAChain *chain, int bufferX, int bufferY)
{
	//finalize
	uint32_t *ptr;
	ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3, true);
	if (!ptr){ return false; }
	ptr[0] = gp0_rgb(16, 32, 64) | gp0_vramFill();
	ptr[1] = gp0_xy(bufferX, bufferY);
	ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

	ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4, true);
	if (!ptr){ return false; }
	ptr[0] = gp0_texpage(0, true, false);
	ptr[1] = gp0_fbOffset1(bufferX, bufferY);
	ptr[2] = gp0_fbOffset2( bufferX + SCREEN_WIDTH  - 1, bufferY + SCREEN_HEIGHT - 2 );
	ptr[3] = gp0_fbOrigin(bufferX, bufferY);
	return true;
}