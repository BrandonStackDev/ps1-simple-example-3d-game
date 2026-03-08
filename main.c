#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "lib/gpu.h"
#include "lib/trig.h"
#include "lib/char.h"
#include "lib/draw.h"
#include "lib/pad.h"


int main(int argc, const char **argv) 
{
	//init stuff
	initSerialIO(115200);
	initControllerBus();
	
	//setup gpu
	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL)
	{
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} 
	else 
	{
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	//setup gte
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	//setup dma chain and double buffer
	DMA_DPCR |= 0
		| DMA_DPCR_CH_ENABLE(DMA_GPU)
		| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	//create draw stuff
	Camera camera = {0};
	// - first camera
	// - create drawable ground object
	DrawObj groundObj = CreateDrawObj(
		0,0,0, 0,0,0, 
		NUM_GROUND_FACES, groundFaces, 
		NUM_GROUND_VERTICES, groundVertices
	);
	// - create drawable player object
	DrawObj playerObj = CreateDrawObj(
		0,0,128, 0,0,0, 
		NUM_PLAYER_FACES, playerFaces, 
		NUM_PLAYER_VERTICES, playerVertices
	);

	while(true)
	{
		//prep for next frame
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		//gather user input
		PlayerInput in = GetControllerInput(PLAYER_ONE);
		// - player
		if(in.up){playerObj.z+=1;}
		if(in.down){playerObj.z-=1;}
		if(in.right){playerObj.x+=1;}
		if(in.left){playerObj.x-=1;}
		// - cam
		if(in.L1){camera.orbit_yaw-=8;}
		if(in.R1){camera.orbit_yaw+=8;}
		if(in.L2){camera.pitch+=8;}
		if(in.R2){camera.pitch-=8;}
		//set camera
		int16_t rise = isin(camera.orbit_yaw);
		int16_t run = icos(camera.orbit_yaw);
		//fixed point math, bit shift after multiply for better precision, 
		// - 12 bits because 2048 = PI 
		// - and 1 on unit circle is 4096
		camera.x = playerObj.x + ((run * CAMERA_DIST_RADIUS) >> 12); 
		camera.z = playerObj.z - ((rise * CAMERA_DIST_RADIUS) >> 12);
		camera.y = playerObj.y;   // some height
		// - example from donogan, player is target, pos is camera pos
		// 		float dxT = b->targetPos.x - b->pos.x;
		// 		float dzT = b->targetPos.z - b->pos.z;
		//		float yawToTarget = (RAD2DEG * atan2f(dxT, dzT));
		int16_t dx = playerObj.x - camera.x;
		int16_t dz = playerObj.z - camera.z;
		camera.yaw = 1024+atan2(dx,dz);


		//will be a loop in the future over each object in the display arena
			//draw the ground
			DrawObject(chain, &groundObj, &camera);
			//draw the character
			DrawObject(chain, &playerObj, &camera);
		//finish it up
		FinishDraw(chain, bufferX, bufferY);
		
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}
	return 0;
}
