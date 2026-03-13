#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "lib/gpu.h"
#include "lib/trig.h"
#include "lib/obj.h"
#include "lib/draw.h"
#include "lib/pad.h"
#include "lib/setup.h"
#include "lib/font.h"


int main(int argc, const char **argv) 
{
	//set up gpu and gte and serial and controller and everything
	GeneralSetup();
	//create dma chains/buffers
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	//create draw stuff
	// - camera
	Camera camera = {0};
	camera.pitch = -128;
	// - create drawable ground object
	DrawObj groundObj = CreateDrawObj(
		0,0,0, 0,0,0, 
		NUM_LEVEL_FACES, levelFaces, 
		NUM_LEVEL_VERTICES, levelVertices
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
		if(in.up){playerObj.z+=4;}
		if(in.down){playerObj.z-=4;}
		if(in.right){playerObj.x+=4;}
		if(in.left){playerObj.x-=4;}
		// - cam
		if(in.L1){camera.orbit_yaw-=8;}
		if(in.R1){camera.orbit_yaw+=8;}
		if(in.L2){camera.pitch+=8;}
		if(in.R2){camera.pitch-=8;}
		//set camera
		int16_t rise = isin(camera.orbit_yaw);
		int16_t run = icos(camera.orbit_yaw);
		//fixed point math, bit shift after multiply
		// - 12 bits because 2048 = PI 
		// - and 1 on unit circle is 4096
		camera.x = playerObj.x + ((run * CAMERA_DIST_RADIUS) >> 12); 
		camera.z = playerObj.z - ((rise * CAMERA_DIST_RADIUS) >> 12);
		camera.y = playerObj.y - (200);   // some height, y is inverted?
		//set the camera yaw to point at the player
		int16_t dx = playerObj.x - camera.x;
		int16_t dz = playerObj.z - camera.z;
		camera.yaw = atan2(dx,dz);

		//font test
		printString(chain, &font, 16, 16, "hello world!\n");
		//*(chain->nextPacket) = gp0_endTag(0);
		//will be a loop in the future over each object in the display arena
			//draw the character
			DrawObject(chain, &playerObj, &camera);
			//draw the ground
			DrawObject(chain, &groundObj, &camera);
		//finish it up
		FinishDraw(chain, bufferX, bufferY);
		
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}
	return 0;
}
