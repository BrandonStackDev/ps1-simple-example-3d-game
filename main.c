#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "./ps1/gte.h"
#include "ps1/registers.h"
#include "trig.h"
#include "char.h"

// The GTE uses a 20.12 fixed-point format for most values. What this means is
// that fractional values will be stored as integers by multiplying them by a
// fixed unit, in this case 4096 or 1 << 12 (hence making the fractional part 12
// bits long). We'll define this unit value to make their handling easier.
#define ONE (1 << 12)
#define NEAR_Z 16

typedef enum {
	ADD_TRI_GOOD = 0,
	ADD_TRI_BAD = 1,
	ADD_TRI_CLIP = 2
} AddTriResult;

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

static void rotateCurrentMatrix(int yaw, int pitch, int roll) {
	static GTEMatrix multiplied;
	int s, c;

	// For each axis, compute the rotation matrix then "combine" it with the
	// GTE's current matrix by multiplying the two and writing the result back
	// to the GTE's registers.
	if (yaw) {
		s = isin(yaw);
		c = icos(yaw);

		gte_setColumnVectors(
			c, -s,   0,
			s,  c,   0,
			0,  0, ONE
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	if (pitch) {
		s = isin(pitch);
		c = icos(pitch);

		gte_setColumnVectors(
			 c,   0, s,
			 0, ONE, 0,
			-s,   0, c
		);
		multiplyCurrentMatrixByVectors(&multiplied);
		gte_loadRotationMatrix(&multiplied);
	}
	if (roll) {
		s = isin(roll);
		c = icos(roll);

		gte_setColumnVectors(
			ONE, 0,  0,
			  0, c, -s,
			  0, s,  c
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
	DMAChain *chain, const Face *face
)
{
	// apply perspective to computed tris
	//SetGtePosAndRot( 0, 0, 0, 0, 0, 0 ); // this function will not do this for you
	gte_loadV0(tv0);
	gte_loadV1(tv1);
	gte_loadV2(tv2);
	//gte_command(GTE_CMD_RTPT | GTE_SF );
	gte_command(GTE_CMD_RTPT | GTE_SF);
	
	//detect overflow
	uint32_t gte_flag = (uint32_t)gte_getControlReg(GTE_FLAG); //GTE_FLAG_DIVIDE_OVERFLOW
	if((gte_flag & GTE_FLAG_DIVIDE_OVERFLOW) && (tv0->z < NEAR_Z || tv1->z < NEAR_Z || tv2->z < NEAR_Z))
	{
		return ADD_TRI_CLIP;
	}

	// Determine the winding order of the vertices on screen. If they
	// are ordered clockwise then the face is visible, otherwise it can
	// be skipped as it is not facing the camera.
	gte_command(GTE_CMD_NCLIP); 
	int order = gte_getDataReg(GTE_MAC0);

	if (order <= 0){return ADD_TRI_BAD;}

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
	if ((zIndex < 0) || (zIndex >= ORDERING_TABLE_SIZE)) {return ADD_TRI_BAD;}

	// Create a new tri and give its vertices the X/Y coordinates
	// calculated by the GTE.
	uint32_t *ptr;
	ptr    = allocatePacket(chain, zIndex, 4, false);
	if (!ptr){ return ADD_TRI_BAD; }
	ptr[0] = face->color | gp0_shadedTriangle(false, false, false);
	//ptr[0] = face->color | gp0_triangle(false, false);
	ptr[1] = xy0;
	gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
	gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
	gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
	return ADD_TRI_GOOD;
}

static void DrawObject(
	DMAChain *chain,
	int x, int y, int z, 
	int yaw, int pitch, int roll, 
	int numFaces, const Face *faces, const GTEVector16 *vertices
)
{
	//set the matrix, initial
	SetGtePosAndRot( x, y, z, yaw, pitch, roll);
	// Draw the obj one face at a time.
	for (int i = 0; i < numFaces; i++) 
	{
		const Face *face = &faces[i];
		AddTriResult res = AddTri(&vertices[face->vertices[0]],&vertices[face->vertices[1]],&vertices[face->vertices[2]],chain, face);
		if(res==ADD_TRI_CLIP) //handle clipping of near plane
		{
			//initial tri work (no perspective because we dont want to risk overflow yet)
			GTEVector16 tv0 = gte_mvmva_cam(&vertices[face->vertices[0]]);
			GTEVector16 tv1 = gte_mvmva_cam(&vertices[face->vertices[1]]);
			GTEVector16 tv2 = gte_mvmva_cam(&vertices[face->vertices[2]]);
			
			//DEFINE NEAR PLANE
			int sz0 = tv0.z;
			int sz1 = tv1.z;
			int sz2 = tv2.z;
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
			SetGtePosAndRot( x, y, z, yaw, pitch, roll); // prepare for next tri, this way I dont have to store which needs clipped and handle later
		}
	}
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

static bool FinishDraw(DMAChain *chain, int bufferX, int bufferY)
{
	//finalize
	uint32_t *ptr;
	ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3, true);
	if (!ptr){ return false; }
	ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
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



int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	DMA_DPCR |= 0
		| DMA_DPCR_CH_ENABLE(DMA_GPU)
		| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;
	int      frameCounter     = 0;

	while(true)
	{
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		//will be a loop in the future over each object in the display arena
			//draw the ground
			DrawObject(chain, 0,0,0, 0,0,0, NUM_GROUND_FACES, groundFaces, groundVertices);
			//draw the character
			DrawObject(chain, 0,0,128, 0, frameCounter*32, frameCounter*16, NUM_CUBE_FACES, cubeFaces, cubeVertices);
		//finish it up
		FinishDraw(chain, bufferX, bufferY);
		
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
		frameCounter++;
	}
	return 0;
}
