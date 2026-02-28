
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "../ps1/gpucmd.h"
#include "../ps1/registers.h"

static void delayMicroseconds(int time) {
	// Calculate the approximate number of CPU cycles that need to be burned,
	// assuming a 33.8688 MHz clock (1 us = 33.8688 = ~33.875 = 271 / 8 cycles).
	// The loop consists of a branch and a decrement, thus each iteration will
	// burn 2 cycles.
	time = ((time * 271) + 4) / 8;

	__asm__ volatile(
		// The .set noreorder directive will prevent the assembler from trying
		// to "hide" the branch instruction's delay slot by shuffling nearby
		// instructions. .set push and .set pop are used to save and restore the
		// assembler's settings respectively, ensuring the noreorder flag will
		// not affect any other code.
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

static void initControllerBus(void) {
	// Reset the serial interface, initialize it with the settings used by
	// controllers and memory cards (250000bps, 8 data bits) and configure it to
	// send a signal to the interrupt controller whenever the DSR input is
	// pulsed (see below).
	SIO_CTRL(0) = SIO_CTRL_RESET;

	SIO_MODE(0) = 0
		| SIO_MODE_BAUD_DIV1
		| SIO_MODE_DATA_8;
	SIO_BAUD(0) = F_CPU / 250000;
	SIO_CTRL(0) = 0
		| SIO_CTRL_TX_ENABLE
		| SIO_CTRL_RX_ENABLE
		| SIO_CTRL_DSR_IRQ_ENABLE;
}

static bool waitForAcknowledge(int timeout) {
	// Controllers and memory cards will acknowledge bytes received by sending
	// short pulses over the DSR line, which will be forwarded by the serial
	// interface to the interrupt controller. This is not guaranteed to happen
	// (it will not if e.g. no device is connected), so we have to implement a
	// timeout to avoid waiting forever in such cases.
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			// Reset the interrupt controller and serial interface's flags to
			// ensure the interrupt can be triggered again.
			IRQ_STAT     = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;

			return true;
		}

		delayMicroseconds(10);
	}

	return false;
}

// As the controller bus is shared with memory cards, an addressing mechanism is
// used to ensure packets are processed by a single device at a time. The first
// byte of each request packet is thus the "address" of the peripheral that
// shall respond to it.
typedef enum {
	ADDR_CONTROLLER  = 0x01,
	ADDR_MEMORY_CARD = 0x81
} DeviceAddress;

// The address is followed by a command byte and any required parameters. The
// only command used in this example (and supported by all controllers) is
// CMD_POLL, however some controllers additionally support a "configuration
// mode" which grants access to an extended command set.
typedef enum {
	CMD_INIT_PRESSURE   = '@', // Initialize DualShock pressure sensors (config)
	CMD_POLL            = 'B', // Read controller state
	CMD_CONFIG_MODE     = 'C', // Enter or exit configuration mode
	CMD_SET_ANALOG      = 'D', // Set analog mode/LED state (config)
	CMD_GET_ANALOG      = 'E', // Get analog mode/LED state (config)
	CMD_GET_MOTOR_INFO  = 'F', // Get information about a motor (config)
	CMD_GET_MOTOR_LIST  = 'G', // Get list of all motors (config)
	CMD_GET_MOTOR_STATE = 'H', // Get current state of vibration motors (config)
	CMD_GET_MODE        = 'L', // Get list of all supported modes (config)
	CMD_REQUEST_CONFIG  = 'M', // Configure poll request format (config)
	CMD_RESPONSE_CONFIG = 'O', // Configure poll response format (config)
	CMD_CARD_READ       = 'R', // Read 128-byte memory card sector
	CMD_CARD_GET_SIZE   = 'S', // Retrieve memory card size information
	CMD_CARD_WRITE      = 'W'  // Write 128-byte memory card sector
} DeviceCommand;

#define DTR_DELAY    60
#define DSR_TIMEOUT 120

static void selectPort(int port) {
	// Set or clear the bit that controls which set of controller and memory
	// card ports is going to have its DTR (port select) signal asserted. The
	// actual serial bus is shared between all ports, however devices will not
	// process packets if DTR is not asserted on the port they are plugged into.
	if (port)
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	else
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;
}

static uint8_t exchangeByte(uint8_t value) {
	// Wait until the interface is ready to accept a byte to send, then wait for
	// it to finish receiving the byte sent by the device.
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL))
		__asm__ volatile("");

	SIO_DATA(0) = value;

	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile("");

	return SIO_DATA(0);
}

static int exchangePacket(
	DeviceAddress address,
	const uint8_t *request,
	uint8_t       *response,
	int           reqLength,
	int           maxRespLength
) {
	// Reset the interrupt flag and assert the DTR signal to tell the controller
	// or memory card that we're about to send a packet. Devices may take some
	// time to prepare for incoming bytes so we need a small delay here.
	IRQ_STAT     = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(DTR_DELAY);

	int respLength = 0;

	// Send the address byte and wait for the device to respond with a pulse on
	// the DSR line. If no response is received assume no device is connected,
	// otherwise make sure the serial interface's data buffer is empty to
	// prepare for the actual packet transfer.
	SIO_DATA(0) = address;

	if (waitForAcknowledge(DSR_TIMEOUT)) {
		while (SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)
			SIO_DATA(0);

		// Send and receive the packet simultaneously one byte at a time,
		// padding it with zeroes if the packet we are receiving is longer than
		// the data being sent.
		while (respLength < maxRespLength) {
			if (reqLength > 0) {
				*(response++) = exchangeByte(*(request++));
				reqLength--;
			} else {
				*(response++) = exchangeByte(0);
			}

			respLength++;

			// The device will keep sending DSR pulses as long as there is more
			// data to transfer. If no more pulses are received, terminate the
			// transfer.
			if (!waitForAcknowledge(DSR_TIMEOUT))
				break;
		}
	}

	// Release DSR, allowing the device to go idle.
	delayMicroseconds(DTR_DELAY);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;

	return respLength;
}

// All packets sent by controllers in response to a poll command include a 4-bit
// device type identifier as well as a bitfield describing the state of up to 16
// buttons.
static const char *const controllerTypes[] = {
	"Unknown",            // ID 0x0
	"Mouse",              // ID 0x1
	"neGcon",             // ID 0x2
	"Konami Justifier",   // ID 0x3
	"Digital controller", // ID 0x4
	"Analog stick",       // ID 0x5
	"Guncon",             // ID 0x6
	"Analog controller",  // ID 0x7
	"Multitap",           // ID 0x8
	"Keyboard",           // ID 0x9
	"Unknown",            // ID 0xa
	"Unknown",            // ID 0xb
	"Unknown",            // ID 0xc
	"Unknown",            // ID 0xd
	"Jogcon",             // ID 0xe
	"Configuration mode"  // ID 0xf
};

typedef enum {
	CT_IDK_IDC = 0,
	CT_DIGITAL = 0x4,
	CT_ANALOG = 0x7,
} ControllerType;

static const char *const buttonNames[] = {
	"Select",   // Bit  0
	"L3",       // Bit  1
	"R3",       // Bit  2
	"Start",    // Bit  3
	"Up",       // Bit  4
	"Right",    // Bit  5
	"Down",     // Bit  6
	"Left",     // Bit  7
	"L2",       // Bit  8
	"R2",       // Bit  9
	"L1",       // Bit 10
	"R1",       // Bit 11
	"Triangle", // Bit 12
	"Circle",   // Bit 13
	"X",        // Bit 14
	"Square"    // Bit 15
};

typedef enum {
	CB_SELECT = 0,
	CB_L3,
	CB_R3,
	CB_START,
	CB_UP,
	CB_RIGHT,
	CB_DOWN,
	CB_LEFT,
	CB_L2,
	CB_R2,
	CB_L1,
	CB_R1,
	CB_TRIANGLE,
	CB_CIRCLE,
	CB_CROSS,
	CB_SQUARE
} ControllerButton;

static void LogControllerInfo(int port) {
	// Build the request packet.
	char output[512];
	uint8_t request[4], response[8];
	char    *ptr = output;

	request[0] = CMD_POLL; // Command
	request[1] = 0x00;     // Multitap address
	request[2] = 0x00;     // Rumble motor control 1
	request[3] = 0x00;     // Rumble motor control 2

	// Send the request to the specified controller port and grab the response.
	// Note that this is a relatively slow process and should be done only once
	// per frame, unless higher polling rates are desired.
	selectPort(port);
	int respLength = exchangePacket(
		ADDR_CONTROLLER,
		request,
		response,
		sizeof(request),
		sizeof(response)
	);

	ptr += sprintf(ptr, "Port %d:\n", port + 1);

	if (respLength < 4) {
		// All controllers reply with at least 4 bytes of data.
		ptr += sprintf(ptr, "  No controller connected");
		return;
	}

	// The first byte of the response contains the device type ID in the upper
	// nibble, as well as the length of the packet's payload in 2-byte units in
	// the lower nibble.
	ptr += sprintf(
		ptr,
		"  Controller type:\t%s\n"
		"  Buttons pressed:\t",
		controllerTypes[response[0] >> 4]
	);

	// Bytes 2 and 3 hold a bitfield representing the state all buttons. As each
	// bit is active low (i.e. a zero represents a button being pressed), the
	// entire field must be inverted.
	uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;
	bool anyButt = false; //technically a valid variable name, however, a terrible life philosophy
	for (int i = 0; i < 16; i++) 
	{
		if ((buttons >> i) & 1)
		{
			anyButt = true;
			ptr += sprintf(ptr, "%s ", buttonNames[i]);
		}		
	}

	ptr += sprintf(ptr, "\n  Response data:\t");

	for (int i = 0; i < respLength; i++)
	{
		ptr += sprintf(ptr, "%02X ", response[i]);
	}

	if(anyButt){printf("\n%s \n\n", output);}
}

typedef struct {
	bool connected; //is the controller connected
	char type;
	bool type_ok;
	bool select; // 	"Select",   // Bit  0
	bool L3;// "L3",       // Bit  1
	bool R3;// "R3",       // Bit  2
	bool start;// "Start",    // Bit  3
	bool up;// "Up",       // Bit  4
	bool right;// "Right",    // Bit  5
	bool down;// "Down",     // Bit  6
	bool left;// "Left",     // Bit  7
	bool L2;// "L2",       // Bit  8
	bool R2;// "R2",       // Bit  9
	bool L1;// "L1",       // Bit 10
	bool R1;// "R1",       // Bit 11
	bool triangle;// "Triangle", // Bit 12
	bool circle;// "Circle",   // Bit 13
	bool cross;// "X",        // Bit 14
	bool square;// "Square"    // Bit 15
	bool analog_on; // do we have joysticks
	char analog_lv; //left stick vertical direction
	char analog_lh; //left stick horizontal direction
	char analog_rv; // right stick vertical
	char analog_rh; //right stick horizontal
	//these guys are the analog stick values but not the raw chars, 
	//80 is center, so this will be the value 
	// - 80 so its centered at 0 and goes negative
	short left_x; //left stick vertical direction, adjusted //todo: probably a better choice than short, like signed 8bit int...?
	short left_y; //left stick horizontal direction, adjusted
	short right_x; // right stick vertical, adjusted
	short right_y; //right stick horizontal, adjusted
} PlayerInput;

//todo, how to handle players holding and releasing actions?

char CONT_1_RUMBLE = 0;
char CONT_2_RUMBLE = 0;
const char PLAYER_ONE = 0;
const char PLAYER_TWO = 1;

static PlayerInput GetControllerInput(int port)
{
	// Build the request packet.
	uint8_t request[4], response[8];
	PlayerInput input = {0};

	request[0] = CMD_POLL; // Command
	request[1] = 0x00;     // Multitap address
	request[2] = CONT_1_RUMBLE;  // Rumble motor control 1
	request[3] = CONT_2_RUMBLE;  // Rumble motor control 2

	selectPort(port);
	int respLength = exchangePacket(
		ADDR_CONTROLLER,
		request,
		response,
		sizeof(request),
		sizeof(response)
	);

	//is it connected?
	if (respLength < 4) {
		// All controllers reply with at least 4 bytes of data.
		input.connected = false;
		return input;
	}
	else
	{
		input.connected = true;
	}

	//do we support this type of controller?
	input.type = response[0] >> 4;
	if(input.type==CT_DIGITAL || input.type==CT_ANALOG)
	{
		input.type_ok = true;
	}
	else
	{
		input.type_ok = false;
		return input;
	}

	uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;

	if ((buttons >> CB_SELECT) & 1){input.select = true;} // 	CB_SELECT = 0,
	if ((buttons >> CB_L3) & 1){input.L3 = true;}// CB_L3,
	if ((buttons >> CB_R3) & 1){input.R3 = true;}// CB_R3,
	if ((buttons >> CB_START) & 1){input.start = true;}// CB_START,
	if ((buttons >> CB_UP) & 1){input.up = true;}// CB_UP,
	if ((buttons >> CB_RIGHT) & 1){input.right = true;}// CB_RIGHT,
	if ((buttons >> CB_DOWN) & 1){input.down = true;}// CB_DOWN,
	if ((buttons >> CB_LEFT) & 1){input.left = true;}// CB_LEFT,
	if ((buttons >> CB_L2) & 1){input.L2 = true;}// CB_L2,
	if ((buttons >> CB_R2) & 1){input.R2 = true;}// CB_R2,
	if ((buttons >> CB_L1) & 1){input.L1 = true;}// CB_L1,
	if ((buttons >> CB_R1) & 1){input.R1 = true;}// CB_R1,
	if ((buttons >> CB_TRIANGLE) & 1){input.triangle = true;}// CB_TRIANGLE,
	if ((buttons >> CB_CIRCLE) & 1){input.circle = true;}// CB_CIRCLE,
	if ((buttons >> CB_CROSS) & 1){input.cross = true;}// CB_CROSS,
	if ((buttons >> CB_SQUARE) & 1){input.square = true;}// CB_SQUARE

	if(respLength == 8) //todo: do we need this to be locked to controller type also?
	{
		input.analog_on = true;
		input.analog_rh = response[4];
		input.analog_rv = response[5];
		input.analog_lh = response[6];
		input.analog_lv = response[7];
		input.left_x = 0x80 - input.analog_lh;
		input.left_y = 0x80 - input.analog_lv;
		input.right_x = 0x80 - input.analog_rh;
		input.right_y = 0x80 - input.analog_rv;
	}
	else
	{
		input.analog_on = false;
	}
	return input;
}