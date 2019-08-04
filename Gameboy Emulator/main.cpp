#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <malloc.h>
#include <windows.h>
#include "cpu.h"
#include "cb.h"
#include <math.h>
#include "include\SDL.h"

using namespace std;

//Screen Dimensions.
const int SCREEN_WIDTH = 160;
const int SCREEN_HEIGHT = 144;

//Clockspeed.
#define CLOCKSPEED 4194304 
#define MAXCYCLES  69905

//Timers.
#define TIMA 0xFF05
#define TMA 0xFF06
#define TMC 0xFF07 

int m_TimerCounter = 1024;

int TimerVariable = 0 ;
int CurrentClockSpeed = 1024 ;
int DividerVariable = 0 ;

//Operands
uint8_t Operand8;
uint16_t Operand16;

//Cycle Counter.
long int cycle_count;

//Stores amount of cycles in last instruction.
int Last_cycles;

//Current scanline.
int scanline_count;

bool IME = 0; //Interrupt Master Enable Flag.

//Allocating Space for Memory.
uint8_t memory[65536];

uint8_t temp_for_boot[0x100];

uint8_t Tile_Map[384][8][8]; //Stores colors of individual pixels in each tile.

uint8_t JoypadState = 0xFF;

/*
Memory MAP.
$FFFF 	Interrupt Enable Flag
$FF80-$FFFE 	Zero Page - 127 bytes
$FF00-$FF7F 	Hardware I/O Registers
$FEA0-$FEFF 	Unusable Memory
$FE00-$FE9F 	OAM - Object Attribute Memory
$E000-$FDFF 	Echo RAM - Reserved, Do Not Use
$D000-$DFFF 	Internal RAM - Bank 1-7 (switchable - CGB only)
$C000-$CFFF 	Internal RAM - Bank 0 (fixed)
$A000-$BFFF 	Cartridge RAM (If Available)
$9C00-$9FFF 	BG Map Data 2
$9800-$9BFF 	BG Map Data 1
$8000-$97FF 	TILE RAM (Graphics)
$4000-$7FFF 	Cartridge ROM - Switchable Banks 1-xx
$0150-$3FFF 	Cartridge ROM - Bank 0 (fixed)
$0100-$014F 	Cartridge Header Area
$0000-$00FF 	Restart and Interrupt Vectors
http://gameboy.mongenel.com/dmg/asmmemmap.html <-- Good website.
*/

//Creating Registers.

struct registers {
	//Registers can be access individually or as a union. eg. |a| is 8bit, |af| is 16bit.

    struct {
		union {
			struct {
				uint8_t f; //Flag Register. [z,n,h,c,0,0,0,0]
				uint8_t a;
			};
			uint16_t af;
		};
	};
	
	struct {
		union {
			struct {
				uint8_t c;
				uint8_t b;
			};
			uint16_t bc;
		};
	};
	
	struct {
		union {
			struct {
				uint8_t e;
				uint8_t d;
			};
			uint16_t de;
		};
	};
	
	struct {
		union {
			struct {
				uint8_t l;
				uint8_t h;
			};
			uint16_t hl;
		};
	};
	
	uint16_t sp; //Stack pointer.
	uint16_t pc; //Program counter.
}registers;

//Struct for instruction lookup.
struct instruction {
    char name[15]; // Name of Instruction.
		int length; // Number of operands instructions takes.
		void (* fcnPtr)(); //Pointer to function that executes instruction.
};

//Struct for storing pixel colors.
struct RGB {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
}frameBuffer[23040], colorPalette[4];

//Necessary for read rom to be in a function.
int argc;
char **argv;

//Rom info.
int rom_size;

//SDL.
SDL_Event event; //Create Event Handler.
SDL_Renderer *renderer; //Create Renderer.
SDL_Window *window; //Create Window.
SDL_Texture *texture; //Create a texture to store framebuffer.

//Function Protoypes.

void Read_Rom(); //Loads romfile into memory.
void Load_Bootrom();
void Restore_Rom();

uint8_t Key_State(); //Sets up FF00 depending on keypresses.
void Handle_Input(); //Detects kepresses.
void KeyPress(int key);
void KeyRelease(int key);

uint8_t Read_Byte(uint16_t location); //Read memory at location. 
void Write_Byte(uint8_t data ,uint16_t location); 

void DmaTransfer(uint8_t data); //Does a direct memory transfer.

void Cpu_Cycle(); //Reads current opcode then executes instruction. Also prints output.
void Registers_Info(); //Prints registers info.

//Graphics functions.
void Initialize_SDL(); //Starts SDL Window and render surface.
void Setup_Color_Pallete(); //Sets up the colours. (Todo: load from rom)
void Load_Tiles(); //Loads Tiles into Array Tiles[][x][y].
void Render_Tile_Map(); //Arranges tiles according to tilemap and displays onto screen.
void Render_All_Tiles(); //Test function to render all the tiles onto screen.
void Render_Sprites(); //Renders the sprites.
void Display_Buffer(); //Loads buffer into texture and renders it.
void Render_Graphics(); //Combines above.
void Shutdown(); //Shuts down SDL and exits.

void Render_Scan_Line(); //Not used. 

//Interupts functions.
void Interupts(); //Checks if there is any interputs to do and then does them.
void Do_Interupt(uint8_t interupt); //Carries out the specified interupt and resets ime.
void Set_Interupt(uint8_t interupt); //Allows for interupts to be set.

//Scanline Functions
void Set_LCD_Status(); //Sets the lcd status register [0xFF41] according to the current scanline.
void Increment_Scan_Line();

//Timer Functions.
void UpdateTimers();

//Arithmetic Instructions
void Add_Byte(uint8_t value2); //Adds value2 to register a and sets relevent flags.
uint16_t Add_2Byte(uint16_t a, uint16_t b); //Adds a to b and sets relevent flags.
void Sub_Byte(uint8_t value); //Subtracts value from register a and sets relevant flags.
void Adc(uint8_t a); 
void Cp(uint8_t value); //Compare value with register a setting flags. (Basically subtraction without storing value.)

uint8_t inc(uint8_t value); //Increment value and set flags.
uint8_t dec(uint8_t value); //Decrement value and set flags.

//Rotations.
uint8_t RotByteLeft(uint8_t number); //Rotate left and set carry flag.
uint8_t RotByteRight(uint8_t number); //Rotate right and set carry flag.

uint8_t Rotate_Left_Carry(uint8_t number); //Rotate left into carry.
uint8_t Rotate_Right_Carry(uint8_t number); //Rotate right into carry.

//Shifts.
uint8_t Shift_Left(uint8_t number); //Shift left into carry.
uint8_t Shift_Right(uint8_t number); //Shift right into carry.

uint8_t Shift_Right_A(uint8_t number); //Arithmetic Shift. 

//Swap.
uint8_t Swap(uint8_t number); //Swaps highest 4 bits with lowest 4 bits.

//registers.a __ Value. //Sets flags.
void And(uint8_t a); //register a AND value. 
void Or(uint8_t a); //register a OR value.
void Xor(uint8_t a); //register a XOR value.

//Stack Instructions.
void Push(uint16_t a); //Places value on top of stack and decrements stack pointer.
uint16_t Pop(); //Stack value off stack, stores it and increments stack pointer. 

//Bit tests.
uint8_t Bit_Test(uint8_t bit, uint8_t number);

uint8_t Test_bit(uint8_t bit, uint8_t number); //Doesn't set flags.


//Bit sets.
uint8_t Res(uint8_t bit, uint8_t number); //Resets specified bit.
uint8_t Set(uint8_t bit, uint8_t number); //Sets specified bit.

//Flag functions.
void Set_Z_Flag(); //Set zero flag.
void Set_N_Flag(); //Set negative flag.
void Set_H_Flag(); //Set half-carry flag.
void Set_C_Flag(); //Set full-carry flag.

void Clear_Z_Flag(); //Clear zero flag.
void CLear_N_Flag(); //Clear negative flag.
void Clear_H_Flag(); //Clear half-carry flag.
void Clear_C_Flag(); //Clear carry flag.

//Array of structures that uses instruction opcode as index and stores name length and function pointer.
const struct instruction instructions[]={
  {"NOP", 1, NOP}, //    0x0
  {"LD BC d16", 3, LD_BC_d16}, //    0x1
  {"LD BCp A", 1, LD_BCp_A}, //    0x2
  {"INC BC", 1, INC_BC}, //    0x3
  {"INC B", 1, INC_B}, //    0x4
  {"DEC B", 1, DEC_B}, //    0x5
  {"LD B d8", 2, LD_B_d8}, //    0x6
  {"RLCA", 1, RLCA}, //    0x7
  {"LD a16p SP", 3, LD_a16p_SP}, //    0x8
  {"ADD HL BC", 1, ADD_HL_BC}, //    0x9
  {"LD A BCp", 1, LD_A_BCp}, //    0xa
  {"DEC BC", 1, DEC_BC}, //    0xb
  {"INC C", 1, INC_C}, //    0xc
  {"DEC C", 1, DEC_C}, //    0xd
  {"LD C d8", 2, LD_C_d8}, //    0xe
  {"RRCA", 1, RRCA}, //    0xf
  {"STOP", 1, STOP_0}, //    0x10
  {"LD DE d16", 3, LD_DE_d16}, //    0x11
  {"LD DEp A", 1, LD_DEp_A}, //    0x12
  {"INC DE", 1, INC_DE}, //    0x13
  {"INC D", 1, INC_D}, //    0x14
  {"DEC D", 1, DEC_D}, //    0x15
  {"LD D d8", 2, LD_D_d8}, //    0x16
  {"RLA", 1, RLA}, //    0x17
  {"JR r8", 2, JR_r8}, //    0x18
  {"ADD HL DE", 1, ADD_HL_DE}, //    0x19
  {"LD A DEp", 1, LD_A_DEp}, //    0x1a
  {"DEC DE", 1, DEC_DE}, //    0x1b
  {"INC E", 1, INC_E}, //    0x1c
  {"DEC E", 1, DEC_E}, //    0x1d
  {"LD E d8", 2, LD_E_d8}, //    0x1e
  {"RRA", 1, RRA}, //    0x1f
  {"JR NZ r8", 2, JR_NZ_r8}, //    0x20
  {"LD HL d16", 3, LD_HL_d16}, //    0x21
  {"LD HLIp A", 1, LD_HLIp_A}, //    0x22
  {"INC HL", 1, INC_HL}, //    0x23
  {"INC H", 1, INC_H}, //    0x24
  {"DEC H", 1, DEC_H}, //    0x25
  {"LD H d8", 2, LD_H_d8}, //    0x26
  {"DAA", 1, DAA}, //    0x27
  {"JR Z r8", 2, JR_Z_r8}, //    0x28
  {"ADD HL HL", 1, ADD_HL_HL}, //    0x29
  {"LD A HLIp", 1, LD_A_HLIp}, //    0x2a
  {"DEC HL", 1, DEC_HL}, //    0x2b
  {"INC L", 1, INC_L}, //    0x2c
  {"DEC L", 1, DEC_L}, //    0x2d
  {"LD L d8", 2, LD_L_d8}, //    0x2e
  {"CPL", 1, CPL}, //    0x2f
  {"JR NC r8", 2, JR_NC_r8}, //    0x30
  {"LD SP d16", 3, LD_SP_d16}, //    0x31
  {"LD HLdp A", 1, LD_HLdp_A}, //    0x32
  {"INC SP", 1, INC_SP}, //    0x33
  {"INC HLp", 1, INC_HLp}, //    0x34
  {"DEC HLp", 1, DEC_HLp}, //    0x35
  {"LD HLp d8", 2, LD_HLp_d8}, //    0x36
  {"SCF", 1, SCF}, //    0x37
  {"JR C r8", 2, JR_C_r8}, //    0x38
  {"ADD HL SP", 1, ADD_HL_SP}, //    0x39
  {"LD A HLdp", 1, LD_A_HLdp}, //    0x3a
  {"DEC SP", 1, DEC_SP}, //    0x3b
  {"INC A", 1, INC_A}, //    0x3c
  {"DEC A", 1, DEC_A}, //    0x3d
  {"LD A d8", 2, LD_A_d8}, //    0x3e
  {"CCF", 1, CCF}, //    0x3f
  {"LD B B", 1, LD_B_B}, //    0x40
  {"LD B C", 1, LD_B_C}, //    0x41
  {"LD B D", 1, LD_B_D}, //    0x42
  {"LD B E", 1, LD_B_E}, //    0x43
  {"LD B H", 1, LD_B_H}, //    0x44
  {"LD B L", 1, LD_B_L}, //    0x45
  {"LD B HLp", 1, LD_B_HLp}, //    0x46
  {"LD B A", 1, LD_B_A}, //    0x47
  {"LD C B", 1, LD_C_B}, //    0x48
  {"LD C C", 1, LD_C_C}, //    0x49
  {"LD C D", 1, LD_C_D}, //    0x4a
  {"LD C E", 1, LD_C_E}, //    0x4b
  {"LD C H", 1, LD_C_H}, //    0x4c
  {"LD C L", 1, LD_C_L}, //    0x4d
  {"LD C HLp", 1, LD_C_HLp}, //    0x4e
  {"LD C A", 1, LD_C_A}, //    0x4f
  {"LD D B", 1, LD_D_B}, //    0x50
  {"LD D C", 1, LD_D_C}, //    0x51
  {"LD D D", 1, LD_D_D}, //    0x52
  {"LD D E", 1, LD_D_E}, //    0x53
  {"LD D H", 1, LD_D_H}, //    0x54
  {"LD D L", 1, LD_D_L}, //    0x55
  {"LD D HLp", 1, LD_D_HLp}, //    0x56
  {"LD D A", 1, LD_D_A}, //    0x57
  {"LD E B", 1, LD_E_B}, //    0x58
  {"LD E C", 1, LD_E_C}, //    0x59
  {"LD E D", 1, LD_E_D}, //    0x5a
  {"LD E E", 1, LD_E_E}, //    0x5b
  {"LD E H", 1, LD_E_H}, //    0x5c
  {"LD E L", 1, LD_E_L}, //    0x5d
  {"LD E HLp", 1, LD_E_HLp}, //    0x5e
  {"LD E A", 1, LD_E_A}, //    0x5f
  {"LD H B", 1, LD_H_B}, //    0x60
  {"LD H C", 1, LD_H_C}, //    0x61
  {"LD H D", 1, LD_H_D}, //    0x62
  {"LD H E", 1, LD_H_E}, //    0x63
  {"LD H H", 1, LD_H_H}, //    0x64
  {"LD H L", 1, LD_H_L}, //    0x65
  {"LD H HLp", 1, LD_H_HLp}, //    0x66
  {"LD H A", 1, LD_H_A}, //    0x67
  {"LD L B", 1, LD_L_B}, //    0x68
  {"LD L C", 1, LD_L_C}, //    0x69
  {"LD L D", 1, LD_L_D}, //    0x6a
  {"LD L E", 1, LD_L_E}, //    0x6b
  {"LD L H", 1, LD_L_H}, //    0x6c
  {"LD L L", 1, LD_L_L}, //    0x6d
  {"LD L HLp", 1, LD_L_HLp}, //    0x6e
  {"LD L A", 1, LD_L_A}, //    0x6f
  {"LD HLp B", 1, LD_HLp_B}, //    0x70
  {"LD HLp C", 1, LD_HLp_C}, //    0x71
  {"LD HLp D", 1, LD_HLp_D}, //    0x72
  {"LD HLp E", 1, LD_HLp_E}, //    0x73
  {"LD HLp H", 1, LD_HLp_H}, //    0x74
  {"LD HLp L", 1, LD_HLp_L}, //    0x75
  {"HALT", 1, HALT}, //    0x76
  {"LD HLp A", 1, LD_HLp_A}, //    0x77
  {"LD A B", 1, LD_A_B}, //    0x78
  {"LD A C", 1, LD_A_C}, //    0x79
  {"LD A D", 1, LD_A_D}, //    0x7a
  {"LD A E", 1, LD_A_E}, //    0x7b
  {"LD A H", 1, LD_A_H}, //    0x7c
  {"LD A L", 1, LD_A_L}, //    0x7d
  {"LD A HLp", 1, LD_A_HLp}, //    0x7e
  {"LD A A", 1, LD_A_A}, //    0x7f
  {"ADD A B", 1, ADD_A_B}, //    0x80
  {"ADD A C", 1, ADD_A_C}, //    0x81
  {"ADD A D", 1, ADD_A_D}, //    0x82
  {"ADD A E", 1, ADD_A_E}, //    0x83
  {"ADD A H", 1, ADD_A_H}, //    0x84
  {"ADD A L", 1, ADD_A_L}, //    0x85
  {"ADD A HLp", 1, ADD_A_HLp}, //    0x86
  {"ADD A A", 1, ADD_A_A}, //    0x87
  {"ADC A B", 1, ADC_A_B}, //    0x88
  {"ADC A C", 1, ADC_A_C}, //    0x89
  {"ADC A D", 1, ADC_A_D}, //    0x8a
  {"ADC A E", 1, ADC_A_E}, //    0x8b
  {"ADC A H", 1, ADC_A_H}, //    0x8c
  {"ADC A L", 1, ADC_A_L}, //    0x8d
  {"ADC A HLp", 1, ADC_A_HLp}, //    0x8e
  {"ADC A A", 1, ADC_A_A}, //    0x8f
  {"SUB B", 1, SUB_B}, //    0x90
  {"SUB C", 1, SUB_C}, //    0x91
  {"SUB D", 1, SUB_D}, //    0x92
  {"SUB E", 1, SUB_E}, //    0x93
  {"SUB H", 1, SUB_H}, //    0x94
  {"SUB L", 1, SUB_L}, //    0x95
  {"SUB HLp", 1, SUB_HLp}, //    0x96
  {"SUB A", 1, SUB_A}, //    0x97
  {"SBC A B", 1, SBC_A_B}, //    0x98
  {"SBC A C", 1, SBC_A_C}, //    0x99
  {"SBC A D", 1, SBC_A_D}, //    0x9a
  {"SBC A E", 1, SBC_A_E}, //    0x9b
  {"SBC A H", 1, SBC_A_H}, //    0x9c
  {"SBC A L", 1, SBC_A_L}, //    0x9d
  {"SBC A HLp", 1, SBC_A_HLp}, //    0x9e
  {"SBC A A", 1, SBC_A_A}, //    0x9f
  {"AND B", 1, AND_B}, //    0xa0
  {"AND C", 1, AND_C}, //    0xa1
  {"AND D", 1, AND_D}, //    0xa2
  {"AND E", 1, AND_E}, //    0xa3
  {"AND H", 1, AND_H}, //    0xa4
  {"AND L", 1, AND_L}, //    0xa5
  {"AND HLp", 1, AND_HLp}, //    0xa6
  {"AND A", 1, AND_A}, //    0xa7
  {"XOR B", 1, XOR_B}, //    0xa8
  {"XOR C", 1, XOR_C}, //    0xa9
  {"XOR D", 1, XOR_D}, //    0xaa
  {"XOR E", 1, XOR_E}, //    0xab
  {"XOR H", 1, XOR_H}, //    0xac
  {"XOR L", 1, XOR_L}, //    0xad
  {"XOR HLp", 1, XOR_HLp}, //    0xae
  {"XOR A", 1, XOR_A}, //    0xaf
  {"OR B", 1, OR_B}, //    0xb0
  {"OR C", 1, OR_C}, //    0xb1
  {"OR D", 1, OR_D}, //    0xb2
  {"OR E", 1, OR_E}, //    0xb3
  {"OR H", 1, OR_H}, //    0xb4
  {"OR L", 1, OR_L}, //    0xb5
  {"OR HLp", 1, OR_HLp}, //    0xb6
  {"OR A", 1, OR_A}, //    0xb7
  {"CP B", 1, CP_B}, //    0xb8
  {"CP C", 1, CP_C}, //    0xb9
  {"CP D", 1, CP_D}, //    0xba
  {"CP E", 1, CP_E}, //    0xbb
  {"CP H", 1, CP_H}, //    0xbc
  {"CP L", 1, CP_L}, //    0xbd
  {"CP HLp", 1, CP_HLp}, //    0xbe
  {"CP A", 1, CP_A}, //    0xbf
  {"RET", 1, RET_NZ}, //    0xc0
  {"POP", 1, POP_BC}, //    0xc1
  {"JP NZ a16", 3, JP_NZ_a16}, //    0xc2
  {"JP", 3, JP_a16}, //    0xc3
  {"CALL NZ a16", 3, CALL_NZ_a16}, //    0xc4
  {"PUSH BC", 1, PUSH_BC}, //    0xc5
  {"ADD A d8", 2, ADD_A_d8}, //    0xc6
  {"RST", 1, RST_00H}, //    0xc7
  {"RET Z", 1, RET_Z}, //    0xc8
  {"RET", 1, RET}, //    0xc9
  {"JP Z a16", 3, JP_Z_a16}, //    0xca
  {"PREFIX", 2, PREFIX_CB}, //    0xcb
  {"CALL Z a16", 3, CALL_Z_a16}, //    0xcc
  {"CALL a16", 3, CALL_a16}, //    0xcd
  {"ADC A d8", 2, ADC_A_d8}, //    0xce
  {"RST", 1, RST_08H}, //    0xcf
  {"RET", 1, RET_NC}, //    0xd0
  {"POP", 1, POP_DE}, //    0xd1
  {"JP NC a16", 3, JP_NC_a16}, //    0xd2
  {"UNKNOWN", 0,NULL},  //    0xd3
  {"CALL NC a16", 3, CALL_NC_a16}, //    0xd4
  {"PUSH DE", 1, PUSH_DE}, //    0xd5
  {"SUB d8", 2, SUB_d8}, //    0xd6
  {"RST", 1, RST_10H}, //    0xd7
  {"RET C", 1, RET_C}, //    0xd8
  {"RETI", 1, RETI}, //    0xd9
  {"JP C a16", 3, JP_C_a16}, //    0xda
  {"UNKNOWN", 0,NULL},  //    0xdb
  {"CALL C a16", 3, CALL_C_a16}, //    0xdc
  {"UNKNOWN", 0,NULL},  //    0xdd
  {"SBC A d8", 2, SBC_A_d8}, //    0xde
  {"RST", 1, RST_18H}, //    0xdf
  {"LDH a8p A", 2, LDH_a8p_A}, //    0xe0
  {"POP HL", 1, POP_HL}, //    0xe1
  {"LD cp A", 1, LD_Cp_A}, //    0xe2
  {"UNKNOWN", 0,NULL},  //    0xe3
  {"UNKNOWN", 0,NULL},  //    0xe4
  {"PUSH HL", 1, PUSH_HL}, //    0xe5
  {"AND D8", 2, AND_d8}, //    0xe6
  {"RST", 1, RST_20H}, //    0xe7
  {"ADD SP r8", 2, ADD_SP_r8}, //    0xe8
  {"JP HLp", 1, JP_HLp}, //    0xe9
  {"LD a16p A", 3, LD_a16p_A}, //    0xea
  {"UNKNOWN", 0,NULL},  //    0xeb
  {"UNKNOWN", 0,NULL},  //    0xec
  {"UNKNOWN", 0,NULL},  //    0xed
  {"XOR D8", 2, XOR_d8}, //    0xee
  {"RST", 1, RST_28H}, //    0xef
  {"LDH A a8p", 2, LDH_A_a8p}, //    0xf0
  {"POP AF", 1, POP_AF}, //    0xf1
  {"LD A cp", 1, LD_A_Cp}, //    0xf2
  {"DI", 1, DI}, //    0xf3
  {"UNKNOWN", 0,NULL},  //    0xf4
  {"PUSH AF", 1, PUSH_AF}, //    0xf5
  {"OR d8", 2, OR_d8}, //    0xf6
  {"RST", 1, RST_30H}, //    0xf7
  {"LD HL SP+r8", 2, LD_HL_SPr8}, //    0xf8
  {"LD SP HL", 1, LD_SP_HL}, //    0xf9
  {"LD A a16p", 3, LD_A_a16p}, //    0xfa
  {"EI", 1, EI}, //    0xfb
  {"UNKNOWN", 0,NULL},  //    0xfc
  {"UNKNOWN", 0,NULL},  //    0xfd
  {"CP d8", 2, CP_d8}, //    0xfe
  {"RST", 1, RST_38H}, //    0xff
};

//Array of structures for cb instruction.
const struct instruction CB_instructions[]={
    {"RLC B", 2, RLC_B}, //    0x0
    {"RLC C", 2, RLC_C}, //    0x1
    {"RLC D", 2, RLC_D}, //    0x2
    {"RLC E", 2, RLC_E}, //    0x3
    {"RLC H", 2, RLC_H}, //    0x4
    {"RLC L", 2, RLC_L}, //    0x5
    {"RLC HLp", 2, RLC_HLp}, //    0x6
    {"RLC A", 2, RLC_A}, //    0x7
    {"RRC B", 2, RRC_B}, //    0x8
    {"RRC C", 2, RRC_C}, //    0x9
    {"RRC D", 2, RRC_D}, //    0xa
    {"RRC E", 2, RRC_E}, //    0xb
    {"RRC H", 2, RRC_H}, //    0xc
    {"RRC L", 2, RRC_L}, //    0xd
    {"RRC HLp", 2, RRC_HLp}, //    0xe
    {"RRC A", 2, RRC_A}, //    0xf
    {"RL B", 2, RL_B}, //    0x10
    {"RL C", 2, RL_C}, //    0x11
    {"RL D", 2, RL_D}, //    0x12
    {"RL E", 2, RL_E}, //    0x13
    {"RL H", 2, RL_H}, //    0x14
    {"RL L", 2, RL_L}, //    0x15
    {"RL HLp", 2, RL_HLp}, //    0x16
    {"RL A", 2, RL_A}, //    0x17
    {"RR B", 2, RR_B}, //    0x18
    {"RR C", 2, RR_C}, //    0x19
    {"RR D", 2, RR_D}, //    0x1a
    {"RR E", 2, RR_E}, //    0x1b
    {"RR H", 2, RR_H}, //    0x1c
    {"RR L", 2, RR_L}, //    0x1d
    {"RR HLp", 2, RR_HLp}, //    0x1e
    {"RR A", 2, RR_A}, //    0x1f
    {"SLA B", 2, SLA_B}, //    0x20
    {"SLA C", 2, SLA_C}, //    0x21
    {"SLA D", 2, SLA_D}, //    0x22
    {"SLA E", 2, SLA_E}, //    0x23
    {"SLA H", 2, SLA_H}, //    0x24
    {"SLA L", 2, SLA_L}, //    0x25
    {"SLA HLp", 2, SLA_HLp}, //    0x26
    {"SLA A", 2, SLA_A}, //    0x27
    {"SRA B", 2, SRA_B}, //    0x28
    {"SRA C", 2, SRA_C}, //    0x29
    {"SRA D", 2, SRA_D}, //    0x2a
    {"SRA E", 2, SRA_E}, //    0x2b
    {"SRA H", 2, SRA_H}, //    0x2c
    {"SRA L", 2, SRA_L}, //    0x2d
    {"SRA HLp", 2, SRA_HLp}, //    0x2e
    {"SRA A", 2, SRA_A}, //    0x2f
    {"SWAP B", 2, SWAP_B}, //    0x30
    {"SWAP C", 2, SWAP_C}, //    0x31
    {"SWAP D", 2, SWAP_D}, //    0x32
    {"SWAP E", 2, SWAP_E}, //    0x33
    {"SWAP H", 2, SWAP_H}, //    0x34
    {"SWAP L", 2, SWAP_L}, //    0x35
    {"SWAP HLp", 2, SWAP_HLp}, //    0x36
    {"SWAP A", 2, SWAP_A}, //    0x37
    {"SRL B", 2, SRL_B}, //    0x38
    {"SRL C", 2, SRL_C}, //    0x39
    {"SRL D", 2, SRL_D}, //    0x3a
    {"SRL E", 2, SRL_E}, //    0x3b
    {"SRL H", 2, SRL_H}, //    0x3c
    {"SRL L", 2, SRL_L}, //    0x3d
    {"SRL HLp", 2, SRL_HLp}, //    0x3e
    {"SRL A", 2, SRL_A}, //    0x3f
    {"BIT 0 B", 2, BIT_0_B}, //    0x40
    {"BIT 0 C", 2, BIT_0_C}, //    0x41
    {"BIT 0 D", 2, BIT_0_D}, //    0x42
    {"BIT 0 E", 2, BIT_0_E}, //    0x43
    {"BIT 0 H", 2, BIT_0_H}, //    0x44
    {"BIT 0 L", 2, BIT_0_L}, //    0x45
    {"BIT 0 HLp", 2, BIT_0_HLp}, //    0x46
    {"BIT 0 A", 2, BIT_0_A}, //    0x47
    {"BIT 1 B", 2, BIT_1_B}, //    0x48
    {"BIT 1 C", 2, BIT_1_C}, //    0x49
    {"BIT 1 D", 2, BIT_1_D}, //    0x4a
    {"BIT 1 E", 2, BIT_1_E}, //    0x4b
    {"BIT 1 H", 2, BIT_1_H}, //    0x4c
    {"BIT 1 L", 2, BIT_1_L}, //    0x4d
    {"BIT 1 HLp", 2, BIT_1_HLp}, //    0x4e
    {"BIT 1 A", 2, BIT_1_A}, //    0x4f
    {"BIT 2 B", 2, BIT_2_B}, //    0x50
    {"BIT 2 C", 2, BIT_2_C}, //    0x51
    {"BIT 2 D", 2, BIT_2_D}, //    0x52
    {"BIT 2 E", 2, BIT_2_E}, //    0x53
    {"BIT 2 H", 2, BIT_2_H}, //    0x54
    {"BIT 2 L", 2, BIT_2_L}, //    0x55
    {"BIT 2 HLp", 2, BIT_2_HLp}, //    0x56
    {"BIT 2 A", 2, BIT_2_A}, //    0x57
    {"BIT 3 B", 2, BIT_3_B}, //    0x58
    {"BIT 3 C", 2, BIT_3_C}, //    0x59
    {"BIT 3 D", 2, BIT_3_D}, //    0x5a
    {"BIT 3 E", 2, BIT_3_E}, //    0x5b
    {"BIT 3 H", 2, BIT_3_H}, //    0x5c
    {"BIT 3 L", 2, BIT_3_L}, //    0x5d
    {"BIT 3 HLp", 2, BIT_3_HLp}, //    0x5e
    {"BIT 3 A", 2, BIT_3_A}, //    0x5f
    {"BIT 4 B", 2, BIT_4_B}, //    0x60
    {"BIT 4 C", 2, BIT_4_C}, //    0x61
    {"BIT 4 D", 2, BIT_4_D}, //    0x62
    {"BIT 4 E", 2, BIT_4_E}, //    0x63
    {"BIT 4 H", 2, BIT_4_H}, //    0x64
    {"BIT 4 L", 2, BIT_4_L}, //    0x65
    {"BIT 4 HLp", 2, BIT_4_HLp}, //    0x66
    {"BIT 4 A", 2, BIT_4_A}, //    0x67
    {"BIT 5 B", 2, BIT_5_B}, //    0x68
    {"BIT 5 C", 2, BIT_5_C}, //    0x69
    {"BIT 5 D", 2, BIT_5_D}, //    0x6a
    {"BIT 5 E", 2, BIT_5_E}, //    0x6b
    {"BIT 5 H", 2, BIT_5_H}, //    0x6c
    {"BIT 5 L", 2, BIT_5_L}, //    0x6d
    {"BIT 5 HLp", 2, BIT_5_HLp}, //    0x6e
    {"BIT 5 A", 2, BIT_5_A}, //    0x6f
    {"BIT 6 B", 2, BIT_6_B}, //    0x70
    {"BIT 6 C", 2, BIT_6_C}, //    0x71
    {"BIT 6 D", 2, BIT_6_D}, //    0x72
    {"BIT 6 E", 2, BIT_6_E}, //    0x73
    {"BIT 6 H", 2, BIT_6_H}, //    0x74
    {"BIT 6 L", 2, BIT_6_L}, //    0x75
    {"BIT 6 HLp", 2, BIT_6_HLp}, //    0x76
    {"BIT 6 A", 2, BIT_6_A}, //    0x77
    {"BIT 7 B", 2, BIT_7_B}, //    0x78
    {"BIT 7 C", 2, BIT_7_C}, //    0x79
    {"BIT 7 D", 2, BIT_7_D}, //    0x7a
    {"BIT 7 E", 2, BIT_7_E}, //    0x7b
    {"BIT 7 H", 2, BIT_7_H}, //    0x7c
    {"BIT 7 L", 2, BIT_7_L}, //    0x7d
    {"BIT 7 HLp", 2, BIT_7_HLp}, //    0x7e
    {"BIT 7 A", 2, BIT_7_A}, //    0x7f
    {"RES 0 B", 2, RES_0_B}, //    0x80
    {"RES 0 C", 2, RES_0_C}, //    0x81
    {"RES 0 D", 2, RES_0_D}, //    0x82
    {"RES 0 E", 2, RES_0_E}, //    0x83
    {"RES 0 H", 2, RES_0_H}, //    0x84
    {"RES 0 L", 2, RES_0_L}, //    0x85
    {"RES 0 HLp", 2, RES_0_HLp}, //    0x86
    {"RES 0 A", 2, RES_0_A}, //    0x87
    {"RES 1 B", 2, RES_1_B}, //    0x88
    {"RES 1 C", 2, RES_1_C}, //    0x89
    {"RES 1 D", 2, RES_1_D}, //    0x8a
    {"RES 1 E", 2, RES_1_E}, //    0x8b
    {"RES 1 H", 2, RES_1_H}, //    0x8c
    {"RES 1 L", 2, RES_1_L}, //    0x8d
    {"RES 1 HLp", 2, RES_1_HLp}, //    0x8e
    {"RES 1 A", 2, RES_1_A}, //    0x8f
    {"RES 2 B", 2, RES_2_B}, //    0x90
    {"RES 2 C", 2, RES_2_C}, //    0x91
    {"RES 2 D", 2, RES_2_D}, //    0x92
    {"RES 2 E", 2, RES_2_E}, //    0x93
    {"RES 2 H", 2, RES_2_H}, //    0x94
    {"RES 2 L", 2, RES_2_L}, //    0x95
    {"RES 2 HLp", 2, RES_2_HLp}, //    0x96
    {"RES 2 A", 2, RES_2_A}, //    0x97
    {"RES 3 B", 2, RES_3_B}, //    0x98
    {"RES 3 C", 2, RES_3_C}, //    0x99
    {"RES 3 D", 2, RES_3_D}, //    0x9a
    {"RES 3 E", 2, RES_3_E}, //    0x9b
    {"RES 3 H", 2, RES_3_H}, //    0x9c
    {"RES 3 L", 2, RES_3_L}, //    0x9d
    {"RES 3 HLp", 2, RES_3_HLp}, //    0x9e
    {"RES 3 A", 2, RES_3_A}, //    0x9f
    {"RES 4 B", 2, RES_4_B}, //    0xa0
    {"RES 4 C", 2, RES_4_C}, //    0xa1
    {"RES 4 D", 2, RES_4_D}, //    0xa2
    {"RES 4 E", 2, RES_4_E}, //    0xa3
    {"RES 4 H", 2, RES_4_H}, //    0xa4
    {"RES 4 L", 2, RES_4_L}, //    0xa5
    {"RES 4 HLp", 2, RES_4_HLp}, //    0xa6
    {"RES 4 A", 2, RES_4_A}, //    0xa7
    {"RES 5 B", 2, RES_5_B}, //    0xa8
    {"RES 5 C", 2, RES_5_C}, //    0xa9
    {"RES 5 D", 2, RES_5_D}, //    0xaa
    {"RES 5 E", 2, RES_5_E}, //    0xab
    {"RES 5 H", 2, RES_5_H}, //    0xac
    {"RES 5 L", 2, RES_5_L}, //    0xad
    {"RES 5 HLp", 2, RES_5_HLp}, //    0xae
    {"RES 5 A", 2, RES_5_A}, //    0xaf
    {"RES 6 B", 2, RES_6_B}, //    0xb0
    {"RES 6 C", 2, RES_6_C}, //    0xb1
    {"RES 6 D", 2, RES_6_D}, //    0xb2
    {"RES 6 E", 2, RES_6_E}, //    0xb3
    {"RES 6 H", 2, RES_6_H}, //    0xb4
    {"RES 6 L", 2, RES_6_L}, //    0xb5
    {"RES 6 HLp", 2, RES_6_HLp}, //    0xb6
    {"RES 6 A", 2, RES_6_A}, //    0xb7
    {"RES 7 B", 2, RES_7_B}, //    0xb8
    {"RES 7 C", 2, RES_7_C}, //    0xb9
    {"RES 7 D", 2, RES_7_D}, //    0xba
    {"RES 7 E", 2, RES_7_E}, //    0xbb
    {"RES 7 H", 2, RES_7_H}, //    0xbc
    {"RES 7 L", 2, RES_7_L}, //    0xbd
    {"RES 7 HLp", 2, RES_7_HLp}, //    0xbe
    {"RES 7 A", 2, RES_7_A}, //    0xbf
    {"SET 0 B", 2, SET_0_B}, //    0xc0
    {"SET 0 C", 2, SET_0_C}, //    0xc1
    {"SET 0 D", 2, SET_0_D}, //    0xc2
    {"SET 0 E", 2, SET_0_E}, //    0xc3
    {"SET 0 H", 2, SET_0_H}, //    0xc4
    {"SET 0 L", 2, SET_0_L}, //    0xc5
    {"SET 0 HLp", 2, SET_0_HLp}, //    0xc6
    {"SET 0 A", 2, SET_0_A}, //    0xc7
    {"SET 1 B", 2, SET_1_B}, //    0xc8
    {"SET 1 C", 2, SET_1_C}, //    0xc9
    {"SET 1 D", 2, SET_1_D}, //    0xca
    {"SET 1 E", 2, SET_1_E}, //    0xcb
    {"SET 1 H", 2, SET_1_H}, //    0xcc
    {"SET 1 L", 2, SET_1_L}, //    0xcd
    {"SET 1 HLp", 2, SET_1_HLp}, //    0xce
    {"SET 1 A", 2, SET_1_A}, //    0xcf
    {"SET 2 B", 2, SET_2_B}, //    0xd0
    {"SET 2 C", 2, SET_2_C}, //    0xd1
    {"SET 2 D", 2, SET_2_D}, //    0xd2
    {"SET 2 E", 2, SET_2_E}, //    0xd3
    {"SET 2 H", 2, SET_2_H}, //    0xd4
    {"SET 2 L", 2, SET_2_L}, //    0xd5
    {"SET 2 HLp", 2, SET_2_HLp}, //    0xd6
    {"SET 2 A", 2, SET_2_A}, //    0xd7
    {"SET 3 B", 2, SET_3_B}, //    0xd8
    {"SET 3 C", 2, SET_3_C}, //    0xd9
    {"SET 3 D", 2, SET_3_D}, //    0xda
    {"SET 3 E", 2, SET_3_E}, //    0xdb
    {"SET 3 H", 2, SET_3_H}, //    0xdc
    {"SET 3 L", 2, SET_3_L}, //    0xdd
    {"SET 3 HLp", 2, SET_3_HLp}, //    0xde
    {"SET 3 A", 2, SET_3_A}, //    0xdf
    {"SET 4 B", 2, SET_4_B}, //    0xe0
    {"SET 4 C", 2, SET_4_C}, //    0xe1
    {"SET 4 D", 2, SET_4_D}, //    0xe2
    {"SET 4 E", 2, SET_4_E}, //    0xe3
    {"SET 4 H", 2, SET_4_H}, //    0xe4
    {"SET 4 L", 2, SET_4_L}, //    0xe5
    {"SET 4 HLp", 2, SET_4_HLp}, //    0xe6
    {"SET 4 A", 2, SET_4_A}, //    0xe7
    {"SET 5 B", 2, SET_5_B}, //    0xe8
    {"SET 5 C", 2, SET_5_C}, //    0xe9
    {"SET 5 D", 2, SET_5_D}, //    0xea
    {"SET 5 E", 2, SET_5_E}, //    0xeb
    {"SET 5 H", 2, SET_5_H}, //    0xec
    {"SET 5 L", 2, SET_5_L}, //    0xed
    {"SET 5 HLp", 2, SET_5_HLp}, //    0xee
    {"SET 5 A", 2, SET_5_A}, //    0xef
    {"SET 6 B", 2, SET_6_B}, //    0xf0
    {"SET 6 C", 2, SET_6_C}, //    0xf1
    {"SET 6 D", 2, SET_6_D}, //    0xf2
    {"SET 6 E", 2, SET_6_E}, //    0xf3
    {"SET 6 H", 2, SET_6_H}, //    0xf4
    {"SET 6 L", 2, SET_6_L}, //    0xf5
    {"SET 6 HLp", 2, SET_6_HLp}, //    0xf6
    {"SET 6 A", 2, SET_6_A}, //    0xf7
    {"SET 7 B", 2, SET_7_B}, //    0xf8
    {"SET 7 C", 2, SET_7_C}, //    0xf9
    {"SET 7 D", 2, SET_7_D}, //    0xfa
    {"SET 7 E", 2, SET_7_E}, //    0xfb
    {"SET 7 H", 2, SET_7_H}, //    0xfc
    {"SET 7 L", 2, SET_7_L}, //    0xfd
    {"SET 7 HLp", 2, SET_7_HLp}, //    0xfe
    {"SET 7 A", 2, SET_7_A}, //    0xff
};

//Array storing the cycle length of each instruction / 2;
const uint8_t Cycles[256] = {
	4, 6, 4, 4, 2, 2, 4, 4, 10, 4, 4, 4, 2, 2, 4, 4, // 0x0_
	2, 6, 4, 4, 2, 2, 4, 4,  4, 4, 4, 4, 2, 2, 4, 4, // 0x1_
	0, 6, 4, 4, 2, 2, 4, 2,  0, 4, 4, 4, 2, 2, 4, 2, // 0x2_
	4, 6, 4, 4, 6, 6, 6, 2,  0, 4, 4, 4, 2, 2, 4, 2, // 0x3_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0x4_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0x5_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0x6_
	4, 4, 4, 4, 4, 4, 2, 4,  2, 2, 2, 2, 2, 2, 4, 2, // 0x7_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0x8_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0x9_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0xa_
	2, 2, 2, 2, 2, 2, 4, 2,  2, 2, 2, 2, 2, 2, 4, 2, // 0xb_
	0, 6, 0, 6, 0, 8, 4, 8,  0, 2, 0, 0, 0, 6, 4, 8, // 0xc_
	0, 6, 0, 0, 0, 8, 4, 8,  0, 8, 0, 0, 0, 0, 4, 8, // 0xd_
	6, 6, 4, 0, 0, 8, 4, 8,  8, 2, 8, 0, 0, 0, 4, 8, // 0xe_
	6, 6, 4, 2, 0, 8, 4, 8,  6, 4, 8, 2, 0, 0, 4, 8  // 0xf_
};

int Stop_Flag = 0;

//Main
int main(int argc, char **argv){	

	::argc = argc; //1337 hacking.
    ::argv = argv;

	Read_Rom(); //Doesn't Support Banking yet.
    //Also bootrom is added to rom..
    Load_Bootrom();

    Setup_Color_Pallete(); //Load up color pallete.

    Initialize_SDL(); //Start sdl.
    registers.pc = 0;
    
	
	//Main loop.
	while (1){        
        cycle_count = 0; //Set cycle count to 0 at beginning of frame.

              

        while (cycle_count < MAXCYCLES){ 
                if (registers.pc == 0x100) {Restore_Rom(); memory[0xFF80] = 0;} //Swaps out bootrom.               

                memory[0x2000] = 0x20; //yea. to be fixed sometime. 

                if (registers.pc == 0xD2B) {Stop_Flag = 1; } 
                //if (registers.pc == 0xb7d & Stop_Flag == 1 ) {Registers_Info(); exit(1); } 
                
                Cpu_Cycle(); //Does a cpu cycle.                                
                UpdateTimers(); //Update timer and does interupt if overflow.          
                Increment_Scan_Line(); //Increments scanline in sync with cpu.
                Interupts(); //Handles Interupts.                      
        }        
        Render_Graphics();        
        
        while( SDL_PollEvent( &event ) ) {
            if (SDL_PollEvent(&event) && event.type == SDL_QUIT){ //If close button pressed.
                Registers_Info(); //Print current state.
                Shutdown(); //Shutdown.
                break; 
            }
            
            Handle_Input();
        }     
    }
    
    Shutdown(); 
}

uint8_t Read_Byte(uint16_t location){
    if(location == 0xFF00){ //Key interupt.
        return Key_State();
    }    
    return memory[location];
}

void Write_Byte(uint8_t data ,uint16_t location){
    
    if (location == 0xFF46){
        DmaTransfer(data);
    }

    memory[location] = data;
}

//Renders the graphics once per frame.
void Render_Graphics(){ 
    SDL_Delay(15); //Wait for 15 milliseconds.
    Setup_Color_Pallete(); //Load colors.
    Load_Tiles(); //Load tiles.     
    Render_Tile_Map(); //Draw tile map.
    Render_Sprites(); //Renders the sprites.
    //Render_All_Tiles(); //test function that displays tilemap.
    Display_Buffer(); //Display everything.
}

//Increment the scanline cyle and do relevent procedures.
void Increment_Scan_Line(){
    Set_LCD_Status();

    //if (memory[0xFF40] & 0x80){ //Check if lcd enable and if so decrement scanline count.
        scanline_count -= Last_cycles;
        
    //}
    //else {return;}
    

    if (scanline_count <= 0){ //Check if scanline has finished.       
        memory[0xFF44]++; //If so increment the currentline.
        scanline_count = 456; //Reset scanline count.  

        if (memory[0xFF44] == 144){ //Check if all lines are finished and if so do a VBLANK.           
            Set_Interupt(0);                      
        }
        else if (memory[0xFF44] > 153){ //Reset scanline once it reaches the end.
            memory[0xFF44] = 0;                
        }
    }
}

//Checks lcd status register and does relevent procedures depending on scanlines etc..
//TODO: Coincidence flag.
void Set_LCD_Status(){
    //uint8_t status = memory[0xFF41]; //Store lcd status.

    if (memory[0xFF40] & 0x80 == 0){ //If lcd is not enabled.
        scanline_count = 456; //reset scanline count.
        memory[0xFF44] = 0; //Reset curentline to 0.
        memory[0xFF41] &= 252; 
        memory[0xFF41] = Set(0, memory[0xFF41]); //enable lcd.
        
        return;
    }    

    uint8_t currentline = memory[0xFF44];
    uint8_t currentmode = memory[0xFF41] & 0x3;
    uint8_t mode = 0;
    bool InteruptRequest = false;    

    if (currentline >= 144){ //IF in VBLANK. 
        mode = 1; //Set to mode 1.
        memory[0xFF41] = Set(0,memory[0xFF41]); //Set status to 01.
        memory[0xFF41] = Res(1,memory[0xFF41]);
        InteruptRequest = memory[0xFF41] & 0x10;
        
    }
    else {
        int mode2bounds = 376;
        int mode3bounds = 204;

        if (scanline_count >= mode2bounds){ //If within bounds of mode 2.
            mode = 2; //Set to mode 2.
            memory[0xFF41] = Set(1,memory[0xFF41]); //Set status to 10.
            memory[0xFF41] = Res(0,memory[0xFF41]); 
            InteruptRequest = memory[0xFF41] & 0x20;

        }

        else if(scanline_count >= mode3bounds){
            mode = 3;
            memory[0xFF41] = Set(1,memory[0xFF41]); //Set status to 11.
            memory[0xFF41] = Set(0,memory[0xFF41]);
        }

        else {
            mode = 0;
            memory[0xFF41] = Res(1,memory[0xFF41]); //Set status to 00.
            memory[0xFF41] = Res(0,memory[0xFF41]);
            InteruptRequest = memory[0xFF41] & 0x8;            
        }
    }

    
    
    if (InteruptRequest && (mode != currentmode)){
       
        Set_Interupt(1);
    }
}

void Handle_Input(){
    if( event.type == SDL_KEYDOWN ) {
		int key = -1 ;        
		switch( event.key.keysym.sym ) {
			case SDLK_TAB:      key = 4;    break; 
			case SDLK_LCTRL:    key = 5;    break;
			case SDLK_RETURN:   key = 7;    break;
			case SDLK_BACKSLASH:key = 6;    break;
			case SDLK_RIGHT:    key = 0;    break;
			case SDLK_LEFT:     key = 1;    break;
			case SDLK_UP:       key = 2;    break;
			case SDLK_DOWN:     key = 3;    break;            
            default:            key = -1;   break;
		}
        
		if ( key != -1 ) {
			KeyPress(key) ;
		}
	} 
    else if( event.type == SDL_KEYUP ) {
		int key = -1 ;
        
		switch( event.key.keysym.sym ) {
			case SDLK_TAB:      key = 4;    break;
			case SDLK_LCTRL:    key = 5;    break;
			case SDLK_RETURN:   key = 7;    break;
			case SDLK_BACKSLASH:key = 6;    break;
			case SDLK_RIGHT:    key = 0;    break;
			case SDLK_LEFT:     key = 1;    break;
			case SDLK_UP:       key = 2;    break;
			case SDLK_DOWN:     key = 3;    break;
            default:            key = -1;   break;
		}
        
		if ( key != -1 ) {
			KeyRelease(key);
		}
    }
}

void KeyPress(int key){
    bool previouslyUnset = false ; 
    
    if(!Test_bit(key,JoypadState)){
        previouslyUnset = true ;
    }
    

    JoypadState = Res(key,JoypadState);    

    bool button;
    
    if (key > 3) { // Standard button.
        button = true ;
        
    }
    else { // Directional button.
        button = false ;    
    }
    bool requestInterupt = false ;    

    //Check which keys game is interested in.
    if (button && !Test_bit(5,memory[0xFF00]))
        {requestInterupt = true ;} 
    else if (!button && !Test_bit(4,memory[0xFF00]))
        {requestInterupt = true ;} 

    if (requestInterupt && !previouslyUnset){Set_Interupt(4);}   

}

void KeyRelease(int key){
    JoypadState = Set(key, JoypadState);
}

uint8_t Key_State(){   

    uint8_t res = memory[0xFF00] ;
   // flip all the bits
    res ^= 0xFF ;

   // are we interested in the standard buttons?
    if (!Test_bit(4, res))
    {
        BYTE topJoypad = JoypadState >> 4 ;
        topJoypad |= 0xF0 ; // turn the top 4 bits on
        res &= topJoypad ; // show what buttons are pressed
    }
    else if (!Test_bit(5,res))//directional buttons
    {
        BYTE bottomJoypad = JoypadState & 0xF ;
        bottomJoypad |= 0xF0 ;
        res &= bottomJoypad ;
    }
    return res ;
   
}

void UpdateTimers(){
    
    uint8_t timerAtts = memory[0xFF07];

	DividerVariable += Last_cycles ;

	if (Test_bit(2, timerAtts))
	{
		TimerVariable += Last_cycles ;

		// time to increment the timer
		if (TimerVariable >= CurrentClockSpeed)
		{
			TimerVariable = 0 ;
			bool overflow = false ;
			if (memory[0xFF05] == 0xFF)
			{
				overflow = true ;
			}
			memory[0xFF05]++ ;

			if (overflow)
			{
				memory[0xFF05] = memory[0xFF06] ;

				// request the interupt
				Set_Interupt(2) ;
			}
		}
	}

	// do divider register
	if (DividerVariable >= 256)
	{
		DividerVariable = 0;
		memory[0xFF04]++ ;
	}

}

//Starts sdl and initiates renderer and textures.
void Initialize_SDL(){
    //Start SDL.
    SDL_Init( SDL_INIT_VIDEO );

    //Initialize the window and renderer.
    SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window, &renderer); 

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
    
    //Create Texture for buffering.
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    
    //Set the render Draw Color to White.
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    
    //Clear Screen (White).
    SDL_RenderClear(renderer);

    //Display on screen.
    SDL_RenderPresent(renderer);
}

//Sets Up Color Pallete with stock colors. (Todo: Load from Rom)
void Setup_Color_Pallete(){
    uint8_t i;
    uint8_t color;
    
    while(i < 4){
        color = ((0x3 << 2*i) & memory[0xFF47]) >> 2*i;       

        switch (color){
            case (0x0):
                colorPalette[i].red = 255;
                colorPalette[i].green = 255;
                colorPalette[i].blue = 255;
                break;
            case (0x1):
                colorPalette[i].red = 180;
                colorPalette[i].green = 180;
                colorPalette[i].blue = 180;
                break;
            case (0x2):
                colorPalette[i].red = 110;
                colorPalette[i].green = 110;
                colorPalette[i].blue = 110;
                break;
            case (0x3):
                colorPalette[i].red = 0;
                colorPalette[i].green = 0;
                colorPalette[i].blue = 0;
                break;            
        }

        i++;
    }   
}

//Loads Rom into Memory.
void Read_Rom(){
	if (argc != 2){
			printf("Usage: ./main romfile/n");			
			exit(1);
	}   
    
    //Loads rom file into memory starting at adress 0x100h.
	FILE* rom = fopen(argv[1], "rb");
	if (rom == NULL) {
		printf( "File not found" );
	}
	
	//Find out how long the rom file is.
	fseek(rom, 0, SEEK_END);
	rom_size = ftell(rom);
	rewind(rom);	
	printf("Rom size is %d bytes. \n", rom_size);
	
	//Create a buffer to store rom. 
	char* rom_buffer = (char*)malloc(rom_size * sizeof(char));
	if (rom_buffer == NULL) {
		cerr << "Could not allocate memory for ROM" << endl;
	}
	//Read File into buffer
	size_t result = fread(rom_buffer, sizeof(char), (size_t)rom_size, rom);
	
	//Copy buffer into memory.
	
    for (int i = 0; i < rom_size; ++i) {
      memory[i] = (uint8_t)rom_buffer[i];
    }    
    
	fclose(rom);
	
}

void Load_Bootrom(){
    //Loads rom file into memory starting at adress 0x100h.
	FILE* rom = fopen("roms/DMG_ROM.bin", "rb");
	if (rom == NULL) {
		printf( "File not found" );
	}
	
	//Find out how long the rom file is.
	fseek(rom, 0, SEEK_END);
	rom_size = ftell(rom);
	rewind(rom);	
	printf("Rom size is %d bytes. \n", rom_size);
	
	//Create a buffer to store rom. 
	char* rom_buffer = (char*)malloc(rom_size * sizeof(char));
	if (rom_buffer == NULL) {
		cerr << "Could not allocate memory for ROM" << endl;
	}
	//Read File into buffer
	size_t result = fread(rom_buffer, sizeof(char), (size_t)rom_size, rom);
	
	//Copy buffer into memory.
	
    for (int i = 0; i < rom_size; ++i) {
        temp_for_boot[i] = memory[i];
        memory[i] = (uint8_t)rom_buffer[i];
    }

    
    
	fclose(rom);
}

void Restore_Rom(){
    for (int i = 0; i < 0x100; ++i) {
        memory[i] = temp_for_boot[i];        
    }
}

void DmaTransfer(uint8_t data){
    uint16_t address = data << 8 ;
    for (int i = 0 ; i < 0xA0; i++)
   {
        Write_Byte(Read_Byte(address+i) , 0xFE00+i) ;
   }
}

//Used for Debugging. (Prints out Registers)
void Registers_Info(){ 
  printf("af: %04x \n",registers.af);
  printf("bc: %04x \n",registers.bc);
  printf("de: %04x \n",registers.de);
  printf("hl: %04x \n",registers.hl);
  printf("sp: %04x \n",registers.sp);
  printf("pc: %04x \n",registers.pc);
  printf("Stack Value: %04x \n",((memory[registers.sp] << 8) | memory[registers.sp + 1]));
  printf("0x%x: %s ",registers.pc, instructions[memory[registers.pc]].name);
  printf("(0x%x)\n",memory[registers.pc]);
  printf("IME: %x\n",IME);
  printf("operand16: %04x \n",Operand16);
  printf("operand8: %02x \n",Operand8);
   
}

//Emulates a cpu cyle.
void Cpu_Cycle(){
	
	uint8_t opcode = memory[registers.pc];    

    if( instructions[opcode].length == 0){
		registers.pc+=1;
        //printf("0x%x: %02x %s \n",registers.pc,opcode,instructions[opcode].name);
		((void (*)(void))instructions[opcode].fcnPtr)();    
	}

	else if( instructions[opcode].length == 1){ //No operand.
		//printf("0x%x: %02x %s \n",registers.pc,opcode,instructions[opcode].name); //Print Info.
		registers.pc+=1;
        ((void (*)(void))instructions[opcode].fcnPtr)(); //Run opcode function.    
	}
	else if( instructions[opcode].length == 2){ //8 bit operand.		
       
        Operand8 = memory[registers.pc + 1];
        registers.pc+=2;
        if (opcode == 0xCB){ //CB Instruction.
            //printf("0x%x: %02x %s \n",registers.pc,opcode, CB_instructions[Operand8].name); 
            ((void (*)(void))CB_instructions[Operand8].fcnPtr)();            
        }
        else{
            //printf("0x%x: %02x %x %s \n",registers.pc,opcode, Operand8 ,instructions[opcode].name); //Print Info.		
            ((void (*)(void))instructions[opcode].fcnPtr)(); //Run opcode function.            
        }       
	}
	else if( instructions[opcode].length == 3){ //16 bit operand.
        Operand16 = (memory[registers.pc + 2] << 8) + memory[registers.pc + 1];//Accounting for endianess.
        registers.pc+=3; 		
		//printf("0x%x: %02x %04x %s \n",registers.pc,opcode, Operand16,instructions[opcode].name); //Print Info.
        ((void (*)(void))instructions[opcode].fcnPtr)(); //Run opcode function.    	
	}

    //Increment the cycle counter.
    cycle_count += 2*Cycles[opcode];
    Last_cycles = 2*Cycles[opcode];
}

//Handles Interupts.
void Interupts(){
	// are interrupts enabled
	if (IME)
	{		
		uint8_t requestFlag = memory[0xFF0F];
		if (requestFlag > 0)
		{			
			for (int bit = 0; bit < 8; bit++)
			{
				if (requestFlag & (0x1 << bit))
				{					
					BYTE enabledReg = memory[0xFFFF];
					if (enabledReg & (0x1 << bit))                        
					{						
						Do_Interupt(bit) ;
					}
				}
			}
		}
	}
}


//Does the specified interupt routine.
void Do_Interupt(uint8_t interupt){ 
    IME = 0; //Set Interupt master switch to 0;
    
    memory[0xFF0F] = Res(interupt,memory[0xFF0F]);   

    Push(registers.pc); //First Save current pc on stack.     
    
    switch (interupt){ //Jump to interupt routine.
        case 0: registers.pc = 0x40; break ; //Vblank.
        case 1: registers.pc = 0x48; break ;
        case 2: registers.pc = 0x50; break ; 
        case 4: registers.pc = 0x68; break ; //Joypad.
    }    
}

void Set_Interupt(uint8_t interupt){
    memory[0xFF0F] = Set(interupt, memory[0xFF0F]);
}

//Loads Tile map into Tile_Map array.
void Load_Tiles(){   
    int s = 0;
    int Rel_x = 0; 
    int Rel_y = 0;    
    int bitIndex;
    
    while (s < 384){        
        Rel_y = 0;
        while(Rel_y < 8){
            Rel_x = 0;
            while(Rel_x < 8){                
                bitIndex = 1 << (7 - Rel_x);
                Tile_Map[s][Rel_x][Rel_y] = (memory[0x8000 + 2*Rel_y + 16*s] & bitIndex ? 1:0) + ((memory[0x8000 + 1 + 2*Rel_y + 16*s] & bitIndex ) ? 2:0);                
                Rel_x++;             
            }
            Rel_y++;       
        }
    s++;
    }
}

//Renders all the tiles in Memory.
void Render_All_Tiles(){
    int i;
    for(i = 0; i < 360; i++) {
        int x;
        for(x = 0; x < 8; x++) {
            int y;
            for(y = 0; y < 8; y++) {
                frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].red = colorPalette[Tile_Map[i][x][y]].red;
                frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].green = colorPalette[Tile_Map[i][x][y]].green;
                frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].blue = colorPalette[Tile_Map[i][x][y]].blue;
            }
        }        
    }
}

//Renders the Current Tilemap.
void Render_Tile_Map(){ //Renders tile map at specified location.
    int i = 0;
    int Map_Offset = 0;
    int Line_Count = 0;
    int location = 0;
    int tile_location;
    bool unsig = true;

    uint8_t ScrollY = memory[0xFF42]; 
    uint8_t ScrollX = memory[0xFF43];
    uint8_t WindowY = memory[0xFF4A];
    uint8_t WindowX = memory[0xFF4B];

    // which tile data?
    if (Test_bit(4, memory[0xFF40])) {
        
    }
    else { 
        
        unsig= false ;
    }
    
    //Check which tilemap to render.
    if (memory[0xFF40] & 0x8){ 
        location = 0x9C00;
    }
    else{
        location = 0x9800;
    }   

    if (unsig){
        for(i = 0; i < 360; i++) {
            int x =0;
            for(x = 0; x < 8; x++) {
                int y = 0;
                for(y = 0; y < 8; y++) { 
                    //What the fuck?
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].red = colorPalette[Tile_Map[memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x )% 8][(y + ScrollY) % 8]].red;
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].green = colorPalette[Tile_Map[memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x) % 8][(y + ScrollY) % 8]].green;
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].blue = colorPalette[Tile_Map[memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x) % 8][(y + ScrollY) % 8]].blue;
                }
            }        
            Line_Count++;
            if (Line_Count == 20){
                Line_Count = 0;
                Map_Offset += 12;
            }       
        }
    }  

    else {
        for(i = 0; i < 360; i++) {
            int x =0;
            for(x = 0; x < 8; x++) {
                int y = 0;
                for(y = 0; y < 8; y++) { 
                    //What the fuck?
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].red = colorPalette[Tile_Map[0x100  + (signed char)memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x )% 8][(y + ScrollY) % 8]].red;
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].green = colorPalette[Tile_Map[0x100 + (signed char)memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x) % 8][(y + ScrollY) % 8]].green;
                    frameBuffer[(i * 8 % 160) + x + (y + i * 8 / 160 * 8) * 160].blue = colorPalette[Tile_Map[0x100 + (signed char)memory[location + i + Map_Offset + (ScrollX+x)/8 + 32*((ScrollY+y)/8)]][(ScrollX+x) % 8][(y + ScrollY) % 8]].blue;
                }
            }        
            Line_Count++;
            if (Line_Count == 20){
                Line_Count = 0;
                Map_Offset += 12;
            }       
        }  
    }
       
}

void Render_Sprites(){
    bool use8x16 = false ; //Check if sprites are 8x16 or 8x8.
    if (Test_bit(2 , memory[0xFF40]));
    use8x16 = true ;

    int sprite = 0;

    while (sprite < 40){
        uint8_t index = sprite*4;
        uint8_t ypos = memory[0xFE00 + index] - 16;
        uint8_t xpos = memory[0xFE00 + index + 1] - 8;
        uint8_t location = memory[0xFE00 + index + 2];
        uint8_t attributes = memory[0xFE00 + index + 3];       

        bool yflip = Test_bit(6, attributes);
        bool xflip = Test_bit(5, attributes);       
        
        int x;
        for(x = 0; x < 8; x++) {
            int y;
            for(y = 0; y < 8; y++) {
                if (Tile_Map[location][abs(8*xflip - x)][abs(8*yflip - y)]){
                    frameBuffer[(xpos + x + (y + ypos)*160)%23040].red = colorPalette[Tile_Map[location][abs(8*xflip - x)][abs(8*yflip - y)]].red;
                    frameBuffer[(xpos + x + (y + ypos)*160)%23040].green = colorPalette[Tile_Map[location][abs(8*xflip - x)][abs(8*yflip - y)]].green;
                    frameBuffer[(xpos + x + (y + ypos)*160)%23040].blue = colorPalette[Tile_Map[location][abs(8*xflip - x)][abs(8*yflip - y)]].blue;
                }
                
            }
        }   
        
        sprite++;
    }
}

void Render_Scan_Line(){
    int i = 0;
    int x = 0;
    uint8_t scanline = memory[0xFF44];
    
    int location = 0;

    uint8_t ScrollY = memory[0xFF42]; 
    uint8_t ScrollX = memory[0xFF43];    
    
    if (memory[0xFF40] & 0x40){ //Check which tilemap to render.
        location = 0x9C00;        
    }
    else{
        location = 0x9800;
    }          
       

    while (x < 160){
        frameBuffer[x + scanline*160].red = colorPalette[Tile_Map[memory[location + 32*(scanline/8) + x/8]][x % 8][scanline % 8]].red;
        frameBuffer[x + scanline*160].green = colorPalette[Tile_Map[memory[location + 32*(scanline/8) + x/8]][x % 8][scanline % 8]].green;
        frameBuffer[x + scanline*160].blue = colorPalette[Tile_Map[memory[location + 32*(scanline/8) + x/8]][x % 8][scanline % 8]].blue;
        x++;
    }
}

void Display_Buffer(){ //Copies frameBuffer to texture. Copies texture to renderer and then displays it.
    SDL_UpdateTexture(texture, NULL, frameBuffer, SCREEN_WIDTH * sizeof(uint8_t) * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer); 
}

//Destroy everything.
void Shutdown(){
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    exit(1); 
}

//Functions to Set Flags.
void Set_Z_Flag(){registers.f = registers.f | 0x80;}

void Set_N_Flag(){registers.f = registers.f | 0x40;}

void Set_H_Flag(){registers.f = registers.f | 0x20;}

void Set_C_Flag(){registers.f = registers.f | 0x10;}

void Clear_Z_Flag(){registers.f = registers.f & 0x7F;}

void Clear_N_Flag(){registers.f = registers.f & 0xBF;}

void Clear_H_Flag(){registers.f = registers.f & 0xDF;}

void Clear_C_Flag(){registers.f = registers.f & 0xEF;}

//Arithmetic and logic functions. TODO Flags.

//Normal rotates (Set carry flag).
uint8_t RotByteLeft(uint8_t number){
    uint8_t carry = number & 0x80;
    number = (number << 1) | (carry >> 7);
    if(carry){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Negative and Half Carry flags.
    Clear_H_Flag();

    return number;
}

uint8_t RotByteRight(uint8_t number){
    uint8_t carry = number & 0x1;
    number = (number >> 1) | (carry << 7);
    if(carry){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Negative and Half Carry flags.
    Clear_H_Flag();

    return number;
}

//Rotates through Carry.
uint8_t Rotate_Right_Carry(uint8_t number){
    uint8_t carry = (number & 0x01);
    number >>= 1; //Shift right 1 bit.
    if (registers.f & 0x10){ //Check if Carry flag is Set.
        number += 0x80;
    }

    if(carry){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    
    //Clear other flags.     
    Clear_N_Flag();
    Clear_H_Flag();
    return number;
}

uint8_t Rotate_Left_Carry(uint8_t number){
    uint8_t carry = (number & 0x80);   
    
    number <<= 1;
    
    if (registers.f & 0x10){ //Check if Carry flag is Set.
        number++;
    }

    if(carry) Set_C_Flag(); // Set Carry flag.
    else Clear_C_Flag();

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}


    //Clear other flags.    
    Clear_N_Flag();
    Clear_H_Flag();
    return number;
}

//Shifts.
uint8_t Shift_Left(uint8_t number){
    if(number & 0x80){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    number <<= 1;    

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Negative and Half Carry flags.
    Clear_H_Flag();

    return number;
}
uint8_t Shift_Right(uint8_t number){
    if(number & 0x1){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    number >>= 1;    

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Negative and Half Carry flags.
    Clear_H_Flag();

    return number;
}

uint8_t Shift_Right_A(uint8_t number){
    if(number & 0x1){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    number = (number >> 1)|(number % 0x80);

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Negative and Half Carry flags.
    Clear_H_Flag();

    return number;
}

//Swap.
uint8_t Swap(uint8_t number){
    number = ((number & 0xf0) >> 4)|((number & 0x0f) << 4); //Swap.

    if(number){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}
    
    Clear_H_Flag(); 
    Clear_N_Flag();    
    Clear_C_Flag();    

    return number;
}


//Add and sub.
void Add_Byte(uint8_t Value2){
    int result = registers.a + Value2;
    
    if ((((registers.a & 0xf) + (Value2 & 0xf)) > 0x0f)){Set_H_Flag();} //Set Half Carry Flag.
    else{Clear_H_Flag();}
    
    if (result & 0xff00){Set_C_Flag();} //Set Full Carry Flag.
    else{Clear_C_Flag();}

    Clear_N_Flag(); //Clear Negative Flag.
    
    registers.a = (uint8_t)(result & 0xff); 

    if(registers.a) Clear_Z_Flag(); //Set Zero Flag.
    else Set_Z_Flag();

    
    
}

uint16_t Add_2Byte(uint16_t Value1, uint16_t Value2){
    
    unsigned long result = Value1 + Value2;
    
    if ((((Value1 & 0xff) + (Value2 & 0xff)) > 0xff)){Set_H_Flag();} //Set Half Carry Flag.
    else{Clear_H_Flag();}
    
    if (result & 0xffff0000){Set_C_Flag();} //Set Full Carry Flag.
    else{Clear_C_Flag();}

    Clear_N_Flag(); //Clear Negative Flag.

    
    return (uint16_t)(result & 0xffff); //Return 16 bytes.
}

void Sub_Byte(uint8_t value){
    Set_N_Flag();

    if(value > registers.a){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if((value & 0x0f) > (registers.a & 0x0f)){Set_H_Flag();} //Set Half Carry Flag.
    else{Clear_H_Flag();}

    registers.a -= value; //Do Sub.

    if(registers.a) Clear_Z_Flag(); //Set Zero Flag.
    else Set_Z_Flag();
    
}

void Cp(uint8_t value){ //Does Subtraction only setting flags.
    Set_N_Flag();

    if(value > registers.a){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if((value & 0x0f) > (registers.a & 0x0f)){Set_H_Flag();} //Set Half Carry Flag.
    else{Clear_H_Flag();}    

    if(registers.a == value) Set_Z_Flag(); //Set Zero Flag.
    else Clear_Z_Flag();    
}

void Adc(uint8_t a){
    Clear_N_Flag();
    int value = a;
    if (registers.f & 0x10){value++;} //Adds Carry.
    
    int result = registers.a + value; //Do addition.

    if(result & 0xff00){Set_C_Flag();} //Check Carry.
    else{Clear_C_Flag();}
    
    if(((result & 0x0f) + (registers.a & 0x0f)) > 0x0f){Set_H_Flag();} //Check Half Carry.
    else{Clear_H_Flag();}

    registers.a = (uint8_t)(result & 0xff); //Set a to addition (8 bytes)
}  

void Sbc(uint8_t value){
    if (registers.f & 0x10){value++;} //Adds Carry.

    Set_N_Flag(); //Set Negative Flag.

    if(value > registers.a){Set_C_Flag();} // Set Carry flag.
    else{Clear_C_Flag();}

    if((value & 0x0f) > (registers.a & 0x0f)){Set_H_Flag();} //Set Half Carry Flag.
    else{Clear_H_Flag();}
    
    registers.a -= value; //Do Sub.

    if(registers.a) Clear_Z_Flag(); //Set Zero Flag.
    else Set_Z_Flag();
}

uint8_t inc(uint8_t value){
    if ((value & 0xf) == 0xf){Set_H_Flag();}
    else {Clear_H_Flag();}

    value++;

    if (value){Clear_Z_Flag();}
    else{Set_Z_Flag();}


    Clear_N_Flag();
    return value;
}

uint8_t dec(uint8_t value){
    if (value & 0xf){Clear_H_Flag();}
    else {Set_H_Flag();}

    value--;

    if (value){Clear_Z_Flag();}
    else{Set_Z_Flag();}

    Set_N_Flag();
    return value;
}

void And(uint8_t a){
    registers.a = a & registers.a;    

    if(registers.a){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}
    
    Set_H_Flag(); //Clear Flags. Set H Flag.
    Clear_N_Flag();    
    Clear_C_Flag();
}

void Or(uint8_t a){
    registers.a |= a;

    if(registers.a){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Other Flags.
    Clear_H_Flag();
    Clear_C_Flag();
}

void Xor(uint8_t a){
    
    registers.a ^= a;
    
	if(registers.a){Clear_Z_Flag();} //Set Zero Flag.
    else{Set_Z_Flag();}

    Clear_N_Flag(); //Clear Other Flags.
    Clear_H_Flag();
    Clear_C_Flag();
}

//Bit test.
uint8_t Bit_Test(uint8_t bit, uint8_t number){
    uint8_t bitindex = 0x1 << bit;
    Clear_N_Flag(); 
    Set_H_Flag();    
    if(number & bitindex){Clear_Z_Flag(); return 1;} //Set Zero Flag.
    else{Set_Z_Flag(); return 0;}
}

uint8_t Test_bit(uint8_t bit, uint8_t number){
    uint8_t bitindex = 0x1 << bit;    
    if(number & bitindex){ return 1;} //Set Zero Flag.
    else{ return 0;}
}

//Bit sets.
//Set bit 0.
uint8_t Res(uint8_t bit, uint8_t number){
    uint8_t bitindex = 0x1 << bit; 
    return number & (0xFF ^ bitindex); //Set Zero Flag.    
}

//Set bit 1.
uint8_t Set(uint8_t bit, uint8_t number){
    uint8_t bitindex = 0x1 << bit; 
    return (number | bitindex);
}

//Stack Instructions.
void Push(uint16_t a){
    registers.sp-=2;
    memory[registers.sp] = (uint8_t)((a >> 8) & 0x00FF);
    memory[registers.sp + 1] = (uint8_t)(a & 0x00FF);
}

uint16_t Pop(){
    uint16_t a;
    a = (memory[registers.sp] << 8) + memory[registers.sp + 1];    
    registers.sp+=2;
    return a;
}

//Instruction Implementations.
void NOP() //    0x0
{
    
}
void LD_BC_d16() //    0x1
{
    registers.bc = Operand16;

}
void LD_BCp_A() //    0x2
{
    Write_Byte(registers.a ,registers.bc);
}
void INC_BC() //    0x3
{
    registers.bc++;
}
void INC_B() //    0x4
{
    registers.b = inc(registers.b);
}
void DEC_B() //    0x5
{
    registers.b = dec(registers.b);
}
void LD_B_d8() //    0x6
{
    registers.b = Operand8;
    
}
void RLCA() //    0x7
{
    registers.a = RotByteLeft(registers.a);
    Clear_Z_Flag();    
}
void LD_a16p_SP() //    0x8
{
    memory[Operand16] = registers.sp;
}
void ADD_HL_BC() //    0x9
{
    registers.hl = Add_2Byte(registers.hl,registers.bc);
}
void LD_A_BCp() //    0xa
{
    registers.a = Read_Byte(registers.bc);
}
void DEC_BC() //    0xb
{
    registers.bc--;
}
void INC_C() //    0xc
{
    registers.c = inc(registers.c);
}
void DEC_C() //    0xd
{
    registers.c = dec(registers.c);
}
void LD_C_d8() //    0xe
{
    registers.c = Operand8;
}
void RRCA() //    0xf
{    
    registers.a = RotByteRight(registers.a);
    Clear_Z_Flag();    
}
void STOP_0() //    0x10
{
    printf("Unimplemented Instruction!!, STOP\n");
}
void LD_DE_d16() //    0x11
{
    registers.de = Operand16;
}
void LD_DEp_A() //    0x12
{
    Write_Byte(registers.a ,registers.de);
}
void INC_DE() //    0x13
{
    registers.de++;
}
void INC_D() //    0x14
{
    registers.d = inc(registers.d);
}
void DEC_D() //    0x15
{
    registers.d = dec(registers.d);
}
void LD_D_d8() //    0x16
{
    registers.d = Operand8;
}
void RLA() //    0x17
{
    registers.a = Rotate_Left_Carry(registers.a);
    Clear_Z_Flag();
}
void JR_r8() //    0x18
{
    registers.pc += (signed char)Operand8;
}
void ADD_HL_DE() //    0x19
{
    registers.hl = Add_2Byte(registers.hl,registers.de);
}
void LD_A_DEp() //    0x1a
{
    registers.a = Read_Byte(registers.de);
}
void DEC_DE() //    0x1b
{
    registers.de--;
}
void INC_E() //    0x1c
{
    registers.e = inc(registers.e);
}
void DEC_E() //    0x1d
{
    registers.e = dec(registers.e);
}
void LD_E_d8() //    0x1e
{
    registers.e = Operand8;
}
void RRA() //    0x1f
{
    registers.a = Rotate_Right_Carry(registers.a);
    Clear_Z_Flag();
}
void JR_NZ_r8() //    0x20
{
    if ((registers.f & 0x80) == 0x00 ){
        registers.pc += (signed char)Operand8;
         
    }


}
void LD_HL_d16() //    0x21
{
    registers.hl = Operand16;    
}
void LD_HLIp_A() //    0x22
{
    Write_Byte(registers.a ,registers.hl);    
    registers.hl++;
}
void INC_HL() //    0x23
{
    registers.hl++;
}
void INC_H() //    0x24
{
    registers.h++;
}
void DEC_H() //    0x25
{
    registers.h = dec(registers.h);
}
void LD_H_d8() //    0x26
{
    registers.h = Operand8;
}
void DAA() //    0x27
{
    {
		unsigned short s = registers.a;
		
		if(Test_bit(6, registers.f)) {
			if(Test_bit(5, registers.f)) s = (s - 0x06)&0xFF;
			if(Test_bit(4, registers.f)) s -= 0x60;
		}
		else {
			if(Test_bit(5, registers.f) || (s & 0xF) > 9) s += 0x06;
			if(Test_bit(4, registers.f) || s > 0x9F) s += 0x60;
		}
		
		registers.a = s;
        Clear_H_Flag();

		if(registers.a) Clear_Z_Flag();
		else Set_Z_Flag();
		
		if(s >= 0x100) Set_C_Flag();
}
}
void JR_Z_r8() //    0x28
{
    if (registers.f & 0x80){
        registers.pc += (signed char)Operand8;
    }
}
void ADD_HL_HL() //    0x29
{
    registers.hl = Add_2Byte(registers.hl,registers.hl);
}
void LD_A_HLIp() //    0x2a
{
    registers.a = Read_Byte(registers.hl);
    registers.hl++;
}
void DEC_HL() //    0x2b
{
    registers.hl--;
}
void INC_L() //    0x2c
{
    registers.l = inc(registers.l);
}
void DEC_L() //    0x2d
{
    registers.l = dec(registers.l);
}
void LD_L_d8() //    0x2e
{
    registers.l = Operand8;
}
void CPL() //    0x2f
{
    registers.a = ~registers.a;
    void CLear_N_Flag();
    void Clear_H_Flag();    
}
void JR_NC_r8() //    0x30
{
    if ((registers.f & 0x10) == 0x00 ){
        registers.pc += (signed char)Operand8;
    }
}
void LD_SP_d16() //    0x31
{
    registers.sp = Operand16;
}
void LD_HLdp_A() //    0x32
{
    Write_Byte(registers.a ,registers.hl);    
    registers.hl--;
}
void INC_SP() //    0x33
{
    registers.sp++;
}
void INC_HLp() //    0x34
{   
    memory[registers.hl] = inc(memory[registers.hl]);
}
void DEC_HLp() //    0x35
{    
    memory[registers.hl] = dec(memory[registers.hl]);
}
void LD_HLp_d8() //    0x36
{
    Write_Byte(Operand8 ,registers.hl);
}
void SCF() //    0x37
{
    Set_C_Flag();
}
void JR_C_r8() //    0x38
{
    if (registers.f & 0x10){
        registers.pc += (signed char)Operand8;
    }
}
void ADD_HL_SP() //    0x39
{
    registers.hl = Add_2Byte(registers.hl,registers.sp);
}
void LD_A_HLdp() //    0x3a
{
    registers.a = Read_Byte(registers.hl);
    registers.hl--;
}
void DEC_SP() //    0x3b
{
    registers.sp--;
}
void INC_A() //    0x3c
{
    registers.a = inc(registers.a);
}
void DEC_A() //    0x3d
{
    registers.a = dec(registers.a);
}
void LD_A_d8() //    0x3e
{
    registers.a = Operand8;
}
void CCF() //    0x3f
{
    if (registers.f & 0x10 ){Clear_C_Flag();}
    else{Set_C_Flag();}    
}
void LD_B_B() //    0x40
{
    registers.b = registers.b;
}
void LD_B_C() //    0x41
{
    registers.b = registers.c;
}
void LD_B_D() //    0x42
{
    registers.b = registers.d;
}
void LD_B_E() //    0x43
{
    registers.b = registers.e;
}
void LD_B_H() //    0x44
{
    registers.b = registers.h;
}
void LD_B_L() //    0x45
{
    registers.b = registers.l;
}
void LD_B_HLp() //    0x46
{
    registers.b = Read_Byte(registers.hl);
}
void LD_B_A() //    0x47
{
    registers.b = registers.a;
}
void LD_C_B() //    0x48
{
    registers.c = registers.b;
}
void LD_C_C() //    0x49
{
    registers.c = registers.c;
}
void LD_C_D() //    0x4a
{
    registers.c = registers.d;
}
void LD_C_E() //    0x4b
{
    registers.c = registers.e;
}
void LD_C_H() //    0x4c
{
    registers.c = registers.h;
}
void LD_C_L() //    0x4d
{
    registers.c = registers.l;
}
void LD_C_HLp() //    0x4e
{
    registers.c = Read_Byte(registers.hl);
}
void LD_C_A() //    0x4f
{
    registers.c = registers.a;
    
}
void LD_D_B() //    0x50
{
    registers.d = registers.b;
}
void LD_D_C() //    0x51
{
    registers.d = registers.c;
}
void LD_D_D() //    0x52
{
    registers.d = registers.d;
}
void LD_D_E() //    0x53
{
    registers.d = registers.e;
}
void LD_D_H() //    0x54
{
    registers.d = registers.h;
}
void LD_D_L() //    0x55
{
    registers.d = registers.l;
}
void LD_D_HLp() //    0x56
{
    registers.d = Read_Byte(registers.hl);
}
void LD_D_A() //    0x57
{
    registers.d = registers.a;
}
void LD_E_B() //    0x58
{
    registers.e = registers.b;
}
void LD_E_C() //    0x59
{
    registers.e = registers.c;
}
void LD_E_D() //    0x5a
{
    registers.e = registers.d;
}
void LD_E_E() //    0x5b
{
    registers.e = registers.e;
}
void LD_E_H() //    0x5c
{
    registers.e = registers.h;
}
void LD_E_L() //    0x5d
{
    registers.e = registers.l;
}
void LD_E_HLp() //    0x5e
{
    registers.e = Read_Byte(registers.hl);
}
void LD_E_A() //    0x5f
{
    registers.e = registers.a;
}
void LD_H_B() //    0x60
{
    registers.h = registers.b;
}
void LD_H_C() //    0x61
{
    registers.h = registers.c;
}
void LD_H_D() //    0x62
{
    registers.h = registers.d;
}
void LD_H_E() //    0x63
{
    registers.h = registers.e;
}
void LD_H_H() //    0x64
{
    registers.h = registers.h;
}
void LD_H_L() //    0x65
{
    registers.h = registers.l;
}
void LD_H_HLp() //    0x66
{
    registers.h = Read_Byte(registers.hl);
}
void LD_H_A() //    0x67
{
    registers.h = registers.a;
}
void LD_L_B() //    0x68
{
    registers.l = registers.b;
}
void LD_L_C() //    0x69
{
    registers.l = registers.c;
}
void LD_L_D() //    0x6a
{
    registers.l = registers.d;
}
void LD_L_E() //    0x6b
{
    registers.l = registers.e;
}
void LD_L_H() //    0x6c
{
    registers.l = registers.h;
}
void LD_L_L() //    0x6d
{
    registers.l = registers.l;
}
void LD_L_HLp() //    0x6e
{
    registers.l = Read_Byte(registers.hl);
}
void LD_L_A() //    0x6f
{
    registers.l = registers.a;
}
void LD_HLp_B() //    0x70
{
    Write_Byte(registers.b ,registers.hl);    
}
void LD_HLp_C() //    0x71
{
    Write_Byte(registers.c ,registers.hl);    
}
void LD_HLp_D() //    0x72
{
    Write_Byte(registers.d ,registers.hl);    
}
void LD_HLp_E() //    0x73
{   
    Write_Byte(registers.e ,registers.hl);    
}
void LD_HLp_H() //    0x74
{
    Write_Byte(registers.h ,registers.hl);    
}
void LD_HLp_L() //    0x75
{
    Write_Byte(registers.l ,registers.hl);    
}
void HALT() //    0x76
{
    //printf("Unimplemented Instruction!! HALT\n");
}
void LD_HLp_A() //    0x77
{
    Write_Byte(registers.a ,registers.hl);
}
void LD_A_B() //    0x78
{
    registers.a = registers.b;
}
void LD_A_C() //    0x79
{
    registers.a = registers.c;
}
void LD_A_D() //    0x7a
{
    registers.a = registers.d;
}
void LD_A_E() //    0x7b
{
    registers.a = registers.e;
}
void LD_A_H() //    0x7c
{
    registers.a = registers.h;
}
void LD_A_L() //    0x7d
{
    registers.a = registers.l;
}
void LD_A_HLp() //    0x7e
{
    registers.a = Read_Byte(registers.hl);
}
void LD_A_A() //    0x7f
{
    registers.a = registers.a;
}
void ADD_A_B() //    0x80
{
    Add_Byte(registers.b);
}
void ADD_A_C() //    0x81
{
    Add_Byte(registers.c);
}
void ADD_A_D() //    0x82
{
    Add_Byte(registers.d);
}
void ADD_A_E() //    0x83
{
    Add_Byte(registers.e);
}
void ADD_A_H() //    0x84
{
    Add_Byte(registers.h);
}
void ADD_A_L() //    0x85
{
    Add_Byte(registers.l);
}
void ADD_A_HLp() //    0x86
{
    Add_Byte(memory[registers.hl]);
}
void ADD_A_A() //    0x87
{
    Add_Byte(registers.a);
}
void ADC_A_B() //    0x88
{
    Adc(registers.b);
}
void ADC_A_C() //    0x89
{
    Adc(registers.c);    
}
void ADC_A_D() //    0x8a
{
    Adc(registers.d);
}
void ADC_A_E() //    0x8b
{
    Adc(registers.e);  
}
void ADC_A_H() //    0x8c
{
    Adc(registers.h);   
}
void ADC_A_L() //    0x8d
{
    Adc(registers.l);    
}
void ADC_A_HLp() //    0x8e
{
    Adc(memory[registers.hl]); 
}
void ADC_A_A() //    0x8f
{
    Adc(registers.a);    
}
void SUB_B() //    0x90
{
    Sub_Byte(registers.b);
}
void SUB_C() //    0x91
{
    Sub_Byte(registers.c);
}
void SUB_D() //    0x92
{
    Sub_Byte(registers.d);
}
void SUB_E() //    0x93
{
    Sub_Byte(registers.e);
}
void SUB_H() //    0x94
{
    Sub_Byte(registers.h);
}
void SUB_L() //    0x95
{
    Sub_Byte(registers.l);
}
void SUB_HLp() //    0x96
{
    Sub_Byte(memory[registers.hl]);
}
void SUB_A() //    0x97
{
    Sub_Byte(registers.a);
}
void SBC_A_B() //    0x98
{
    Sbc(registers.b);    
}
void SBC_A_C() //    0x99
{
    Sbc(registers.c);
}
void SBC_A_D() //    0x9a
{
    Sbc(registers.d);
}
void SBC_A_E() //    0x9b
{
    Sbc(registers.e);
}
void SBC_A_H() //    0x9c
{
    Sbc(registers.h);
}
void SBC_A_L() //    0x9d
{
    Sbc(registers.l);
}
void SBC_A_HLp() //    0x9e
{
    Sbc(memory[registers.hl]);
}
void SBC_A_A() //    0x9f
{
    Sbc(registers.a);
}
void AND_B() //    0xa0
{
    And(registers.b);
}
void AND_C() //    0xa1
{
    And(registers.c);
}
void AND_D() //    0xa2
{
    And(registers.d);
}
void AND_E() //    0xa3
{
    And(registers.e);
}
void AND_H() //    0xa4
{
    And(registers.h);
}
void AND_L() //    0xa5
{
    And(registers.l);
}
void AND_HLp() //    0xa6
{
    And(memory[registers.hl]);
}
void AND_A() //    0xa7
{
    And(registers.a);
}
void XOR_B() //    0xa8
{
    Xor(registers.b);
}
void XOR_C() //    0xa9
{
    Xor(registers.c);
}
void XOR_D() //    0xaa
{
    Xor(registers.d);
}
void XOR_E() //    0xab
{
    Xor(registers.e);
}
void XOR_H() //    0xac
{
    Xor(registers.h);
}
void XOR_L() //    0xad
{
    Xor(registers.l);
}
void XOR_HLp() //    0xae
{
    Xor(memory[registers.hl]);
}
void XOR_A() //    0xaf
{
    Xor(registers.a);
}
void OR_B() //    0xb0
{
    Or(registers.b);
}
void OR_C() //    0xb1
{
    Or(registers.c);
}
void OR_D() //    0xb2
{
    Or(registers.d);
}
void OR_E() //    0xb3
{
    Or(registers.e);
}
void OR_H() //    0xb4
{
    Or(registers.h);
}
void OR_L() //    0xb5
{
    Or(registers.l);
}
void OR_HLp() //    0xb6
{
    Or(memory[registers.hl]);
}
void OR_A() //    0xb7
{
    Or(registers.a);
}
void CP_B() //    0xb8
{
    Cp(registers.b);
}
void CP_C() //    0xb9
{
    Cp(registers.c);
}
void CP_D() //    0xba
{
    Cp(registers.d);
}
void CP_E() //    0xbb
{
    Cp(registers.e);
}
void CP_H() //    0xbc
{
    Cp(registers.h);
}
void CP_L() //    0xbd
{
    Cp(registers.l);
}
void CP_HLp() //    0xbe
{
    Cp(memory[registers.hl]);
}
void CP_A() //    0xbf
{
    Cp(registers.a);
}
void RET_NZ() //    0xc0
{
    if ((registers.f & 0x80) == 0x00){
       registers.pc = Pop();
    }
    
}
void POP_BC() //    0xc1
{
    registers.bc = Pop();
}
void JP_NZ_a16() //    0xc2
{
    if ((registers.f & 0x80) == 0x00 ){
        registers.pc = Operand16;
    }
}
void JP_a16() //    0xc3
{
    registers.pc = Operand16;    
}
void CALL_NZ_a16() //    0xc4
{
    if ((registers.f & 0x80) == 0x00 ){
        Push(registers.pc);
        registers.pc = Operand16;        
    }
}
void PUSH_BC() //    0xc5
{
    Push(registers.bc);
    
}
void ADD_A_d8() //    0xc6
{
    Add_Byte(Operand8);
}
void RST_00H() //    0xc7
{
    Push(registers.pc);
    registers.pc = 0x0000;
}
void RET_Z() //    0xc8
{
    if (registers.f & 0x80){
       registers.pc = Pop();
    }
   
}
void RET() //    0xc9
{
    registers.pc = Pop();
}
void JP_Z_a16() //    0xca
{
    if (registers.f & 0x80){
        registers.pc = Operand16;
    }
}
void PREFIX_CB() //    0xcb
{
    printf("Unimplemented Instruction!!, CB\n");
}
void CALL_Z_a16() //    0xcc
{
    if (registers.f & 0x80){
        Push(registers.pc);
        registers.pc = Operand16;        
    }
}
void CALL_a16() //    0xcd
{
    Push(registers.pc);
    registers.pc = Operand16;   
}
void ADC_A_d8() //    0xce
{
    Adc(Operand8);   
}
void RST_08H() //    0xcf
{
    Push(registers.pc);
    registers.pc = 0x0008;
}
void RET_NC() //    0xd0
{
    if ((registers.f & 0x10) == 0x00 ){
        registers.pc = Pop();
    }
}
void POP_DE() //    0xd1
{
    registers.de = Pop();
}
void JP_NC_a16() //    0xd2
{
    if ((registers.f & 0x10) == 0x00 ){
        registers.pc = Operand16;
    }
}
void CALL_NC_a16() //    0xd4
{
    if ((registers.f & 0x10) == 0x00 ){
        Push(Operand16);
        registers.pc = Operand16;        
    }
}
void PUSH_DE() //    0xd5
{
    Push(registers.de);
}
void SUB_d8() //    0xd6
{
    Sub_Byte(Operand8);
}
void RST_10H() //    0xd7
{
    Push(registers.pc);
    registers.pc = 0x0010;
}
void RET_C() //    0xd8
{
    if (registers.f & 0x10 ){
        registers.pc = Pop();
    }
}
void RETI() //    0xd9
{
    registers.pc = Pop();
    IME = 1; //Enable master interupt flag.
}
void JP_C_a16() //    0xda
{
    if (registers.f & 0x10){
        registers.pc = Operand16;
    }
}
void CALL_C_a16() //    0xdc
{
    if (registers.f & 0x10){
        Push(Operand16);
        registers.pc = Operand16;  
    }
}
void SBC_A_d8() //    0xde
{
    Sbc(Operand8);
}
void RST_18H() //    0xdf
{
    Push(registers.pc);
    registers.pc = 0x0018;
}
void LDH_a8p_A() //    0xe0
{
    Write_Byte(registers.a ,0xFF00+Operand8);
}
void POP_HL() //    0xe1
{
    registers.hl = Pop();
}
void LD_Cp_A() //    0xe2
{
    Write_Byte(registers.a ,0xFF00+registers.c);
}
void PUSH_HL() //    0xe5
{
    Push(registers.hl);
}
void AND_d8() //    0xe6
{
    And(Operand8);
}
void RST_20H() //    0xe7
{
    Push(registers.pc);
    registers.pc = 0x0020;
}
void ADD_SP_r8() //    0xe8
{
    registers.sp = Add_2Byte(registers.sp, (uint16_t)Operand8);
}
void JP_HLp() //    0xe9
{
    registers.pc = registers.hl;
}
void LD_a16p_A() //    0xea
{
    Write_Byte(registers.a , Operand16);
}
void XOR_d8() //    0xee
{
    Xor(Operand8);
}
void RST_28H() //    0xef
{
    Push(registers.pc);
    registers.pc = 0x0028;
}
void LDH_A_a8p() //    0xf0
{
    registers.a = Read_Byte(0xFF00 + Operand8);       
}
void POP_AF() //    0xf1
{
    registers.af = Pop();
}
void LD_A_Cp() //    0xf2
{
    registers.a = Read_Byte(0xFF00 + registers.c); 
    
}
void DI() //    0xf3
{
    IME = 0;
}
void PUSH_AF() //    0xf5
{
    Push(registers.af);
}
void OR_d8() //    0xf6
{
    Or(Operand8);
}
void RST_30H() //    0xf7
{
    Push(registers.pc);
    registers.pc = 0x0030;
}
void LD_HL_SPr8() //    0xf8
{
    registers.hl = registers.sp + Operand8; //Needs work.
}
void LD_SP_HL() //    0xf9
{
    registers.sp = registers.hl;
}
void LD_A_a16p() //    0xfa
{
    registers.a = Read_Byte(Operand16); 
}
void EI() //    0xfb
{
    IME = 1;
}
void CP_d8() //    0xfe
{
    Cp(Operand8);
}
void RST_38H() //    0xff
{
    Push(registers.pc);
    registers.pc = 0x0038;
}

//Declarations for Cb prefixed instructions.

void RLC_B() //    0x0
{    
    registers.b = RotByteLeft(registers.b);
}
void RLC_C() //    0x1
{
    registers.c = RotByteLeft(registers.c);
}
void RLC_D() //    0x2
{
    registers.d = RotByteLeft(registers.d);
}
void RLC_E() //    0x3
{
    registers.e = RotByteLeft(registers.e);
}
void RLC_H() //    0x4
{
    registers.h = RotByteLeft(registers.h);
}
void RLC_L() //    0x5
{
    registers.l = RotByteLeft(registers.l);
}
void RLC_HLp() //    0x6
{
    memory[registers.hl] = RotByteLeft(memory[registers.hl]);
}
void RLC_A() //    0x7
{
    registers.a = RotByteLeft(registers.a);
}
void RRC_B() //    0x8
{
    registers.b = RotByteRight(registers.b);
}
void RRC_C() //    0x9
{
    registers.c = RotByteRight(registers.c);
}
void RRC_D() //    0xa
{
    registers.d = RotByteRight(registers.d);
}
void RRC_E() //    0xb
{
    registers.e = RotByteRight(registers.e);
}
void RRC_H() //    0xc
{
    registers.h = RotByteRight(registers.h);
}
void RRC_L() //    0xd
{
    registers.l = RotByteRight(registers.l);
}
void RRC_HLp() //    0xe
{
    memory[registers.hl] = RotByteRight(memory[registers.hl]);
}
void RRC_A() //    0xf
{
    registers.a = RotByteRight(registers.a);
}
void RL_B() //    0x10
{
    registers.b = Rotate_Left_Carry(registers.b);
}
void RL_C() //    0x11
{
    registers.c = Rotate_Left_Carry(registers.c);
}
void RL_D() //    0x12
{
    registers.d = Rotate_Left_Carry(registers.d);
}
void RL_E() //    0x13
{
    registers.e = Rotate_Left_Carry(registers.e);
}
void RL_H() //    0x14
{
    registers.h = Rotate_Left_Carry(registers.h);
}
void RL_L() //    0x15
{
    registers.l = Rotate_Left_Carry(registers.l);
}
void RL_HLp() //    0x16
{
    memory[registers.hl] = Rotate_Left_Carry(memory[registers.hl]);
}
void RL_A() //    0x17
{
    registers.a = Rotate_Left_Carry(registers.a);
}
void RR_B() //    0x18
{
    registers.b = Rotate_Right_Carry(registers.b);
}
void RR_C() //    0x19
{
    registers.c = Rotate_Right_Carry(registers.c);
}
void RR_D() //    0x1a
{
    registers.d = Rotate_Right_Carry(registers.d);
}
void RR_E() //    0x1b
{
    registers.e = Rotate_Right_Carry(registers.e);
}
void RR_H() //    0x1c
{
    registers.h = Rotate_Right_Carry(registers.h);
}
void RR_L() //    0x1d
{
    registers.l = Rotate_Right_Carry(registers.l);
}
void RR_HLp() //    0x1e
{
    memory[registers.hl]  = Rotate_Right_Carry(memory[registers.hl]);
}
void RR_A() //    0x1f
{
    registers.a = Rotate_Right_Carry(registers.a);
}
void SLA_B() //    0x20
{
    registers.b = Shift_Left(registers.b);
}
void SLA_C() //    0x21
{
    registers.c = Shift_Left(registers.c);
}
void SLA_D() //    0x22
{
    registers.d = Shift_Left(registers.d);
}
void SLA_E() //    0x23
{
    registers.e = Shift_Left(registers.e);
}
void SLA_H() //    0x24
{
    registers.h = Shift_Left(registers.h);
}
void SLA_L() //    0x25
{
    registers.l = Shift_Left(registers.l);
}
void SLA_HLp() //    0x26
{
    memory[registers.hl] = Shift_Left(memory[registers.hl]);
}
void SLA_A() //    0x27
{
    registers.a = Shift_Left(registers.a);
}
void SRA_B() //    0x28
{
    registers.b = Shift_Right_A(registers.b);
}
void SRA_C() //    0x29
{
    registers.c = Shift_Right_A(registers.c);
}
void SRA_D() //    0x2a
{
    registers.d = Shift_Right_A(registers.d);
}
void SRA_E() //    0x2b
{
    registers.e = Shift_Right_A(registers.e);
}
void SRA_H() //    0x2c
{
    registers.h = Shift_Right_A(registers.h);
}
void SRA_L() //    0x2d
{
    registers.l = Shift_Right_A(registers.l);
}
void SRA_HLp() //    0x2e
{
    memory[registers.hl] = Shift_Right_A(memory[registers.hl]);
}
void SRA_A() //    0x2f
{
    registers.a = Shift_Right_A(registers.a);
}
void SWAP_B() //    0x30
{
    registers.b = Swap(registers.b);
}
void SWAP_C() //    0x31
{
    registers.c = Swap(registers.c);
}
void SWAP_D() //    0x32
{
    registers.d = Swap(registers.d);
}
void SWAP_E() //    0x33
{
    registers.e = Swap(registers.e);
}
void SWAP_H() //    0x34
{
    registers.h = Swap(registers.h);
}
void SWAP_L() //    0x35
{
    registers.l = Swap(registers.l);
}
void SWAP_HLp() //    0x36
{
    memory[registers.hl] = Swap(memory[registers.hl]);
}
void SWAP_A() //    0x37
{
    registers.a = Swap(registers.a);
}
void SRL_B() //    0x38
{
    registers.b = Shift_Right(registers.b);
}
void SRL_C() //    0x39
{
    registers.c = Shift_Right(registers.c);
}
void SRL_D() //    0x3a
{
    registers.d = Shift_Right(registers.d);
}
void SRL_E() //    0x3b
{
    registers.e = Shift_Right(registers.e);
}
void SRL_H() //    0x3c
{
    registers.h = Shift_Right(registers.h);
}
void SRL_L() //    0x3d
{
    registers.l = Shift_Right(registers.l);
}
void SRL_HLp() //    0x3e
{
    memory[registers.hl] = Shift_Right(memory[registers.hl]);
}
void SRL_A() //    0x3f
{
    registers.a = Shift_Right(registers.a);
}
void BIT_0_B() //    0x40
{
    Bit_Test(0,registers.b);
}
void BIT_0_C() //    0x41
{
    Bit_Test(0,registers.c);
}
void BIT_0_D() //    0x42
{
    Bit_Test(0,registers.d);
}
void BIT_0_E() //    0x43
{
    Bit_Test(0,registers.e);
}
void BIT_0_H() //    0x44
{
    Bit_Test(0,registers.h);
}
void BIT_0_L() //    0x45
{
    Bit_Test(0,registers.l);
}
void BIT_0_HLp() //    0x46
{
    Bit_Test(0,memory[registers.hl]);
}
void BIT_0_A() //    0x47
{
    Bit_Test(0,registers.a);
}
void BIT_1_B() //    0x48
{
    Bit_Test(1,registers.b);
}
void BIT_1_C() //    0x49
{
    Bit_Test(1,registers.b);
}
void BIT_1_D() //    0x4a
{
    Bit_Test(1,registers.d);
}
void BIT_1_E() //    0x4b
{
    Bit_Test(1,registers.e);
}
void BIT_1_H() //    0x4c
{
    Bit_Test(1,registers.h);
}
void BIT_1_L() //    0x4d
{
    Bit_Test(1,registers.l);
}
void BIT_1_HLp() //    0x4e
{
    Bit_Test(1,memory[registers.hl]);
}
void BIT_1_A() //    0x4f
{
    Bit_Test(1,registers.a);
}
void BIT_2_B() //    0x50
{
    Bit_Test(2,registers.b);
}
void BIT_2_C() //    0x51
{
    Bit_Test(2,registers.c);
}
void BIT_2_D() //    0x52
{
    Bit_Test(2,registers.d);
}
void BIT_2_E() //    0x53
{
    Bit_Test(2,registers.e);
}
void BIT_2_H() //    0x54
{
    Bit_Test(2,registers.h);
}
void BIT_2_L() //    0x55
{
    Bit_Test(2,registers.l);
}
void BIT_2_HLp() //    0x56
{
    Bit_Test(2,memory[registers.hl]);
}
void BIT_2_A() //    0x57
{
    Bit_Test(2,registers.a);
}
void BIT_3_B() //    0x58
{
    Bit_Test(3,registers.b);
}
void BIT_3_C() //    0x59
{
    Bit_Test(3,registers.c);
}
void BIT_3_D() //    0x5a
{
    Bit_Test(3,registers.d);
}
void BIT_3_E() //    0x5b
{
    Bit_Test(3,registers.e);
}
void BIT_3_H() //    0x5c
{
    Bit_Test(3,registers.h);
}
void BIT_3_L() //    0x5d
{
    Bit_Test(3,registers.l);
}
void BIT_3_HLp() //    0x5e
{
    Bit_Test(3,memory[registers.hl]);
}
void BIT_3_A() //    0x5f
{
    Bit_Test(3,registers.a);
}
void BIT_4_B() //    0x60
{
    Bit_Test(4,registers.b);
}
void BIT_4_C() //    0x61
{
    Bit_Test(4,registers.c);
}
void BIT_4_D() //    0x62
{
    Bit_Test(4,registers.d);
}
void BIT_4_E() //    0x63
{
    Bit_Test(4,registers.e);
}
void BIT_4_H() //    0x64
{
    Bit_Test(4,registers.h);
}
void BIT_4_L() //    0x65
{
    Bit_Test(4,registers.l);
}
void BIT_4_HLp() //    0x66
{
    Bit_Test(4,memory[registers.hl]);
}
void BIT_4_A() //    0x67
{
    Bit_Test(4,registers.a);
}
void BIT_5_B() //    0x68
{
    Bit_Test(5,registers.b);
}
void BIT_5_C() //    0x69
{
    Bit_Test(5,registers.c);
}
void BIT_5_D() //    0x6a
{
    Bit_Test(5,registers.d);
}
void BIT_5_E() //    0x6b
{
    Bit_Test(5,registers.e);
}
void BIT_5_H() //    0x6c
{
    Bit_Test(5,registers.h);
}
void BIT_5_L() //    0x6d
{
    Bit_Test(5,registers.l);
}
void BIT_5_HLp() //    0x6e
{
    Bit_Test(5,memory[registers.hl]);
}
void BIT_5_A() //    0x6f
{
    Bit_Test(5,registers.a);
}
void BIT_6_B() //    0x70
{
    Bit_Test(6,registers.b);
}
void BIT_6_C() //    0x71
{
    Bit_Test(6,registers.c);
}
void BIT_6_D() //    0x72
{
    Bit_Test(6,registers.d);
}
void BIT_6_E() //    0x73
{
    Bit_Test(6,registers.e);
}
void BIT_6_H() //    0x74
{
    Bit_Test(6,registers.h);
}
void BIT_6_L() //    0x75
{
    Bit_Test(6,registers.l);
}
void BIT_6_HLp() //    0x76
{
    Bit_Test(6,memory[registers.hl]);
}
void BIT_6_A() //    0x77
{
    Bit_Test(6,registers.a);
}
void BIT_7_B() //    0x78
{
    Bit_Test(7,registers.b);
}
void BIT_7_C() //    0x79
{
    Bit_Test(7,registers.c);
}
void BIT_7_D() //    0x7a
{
    Bit_Test(7,registers.d);
}
void BIT_7_E() //    0x7b
{
    Bit_Test(7,registers.e);
}
void BIT_7_H() //    0x7c
{
    Bit_Test(7,registers.h);
}
void BIT_7_L() //    0x7d
{
    Bit_Test(7,registers.l);
}
void BIT_7_HLp() //    0x7e
{
    Bit_Test(7,memory[registers.hl]);
}
void BIT_7_A() //    0x7f
{
    Bit_Test(7,registers.a);
}
void RES_0_B() //    0x80
{
    registers.b = Res(0,registers.b);
}
void RES_0_C() //    0x81
{
    registers.c = Res(0,registers.c);
}
void RES_0_D() //    0x82
{
    registers.d = Res(0,registers.d);
}
void RES_0_E() //    0x83
{
    registers.e = Res(0,registers.e);
}
void RES_0_H() //    0x84
{
    registers.h = Res(0,registers.h);
}
void RES_0_L() //    0x85
{
    registers.l = Res(0,registers.l);
}
void RES_0_HLp() //    0x86
{
    memory[registers.hl] = Res(0,memory[registers.hl]);
}
void RES_0_A() //    0x87
{
    registers.a = Res(0,registers.a);
}
void RES_1_B() //    0x88
{
    registers.b = Res(1,registers.b);
}
void RES_1_C() //    0x89
{
    registers.c = Res(1,registers.c);
}
void RES_1_D() //    0x8a
{
    registers.d = Res(1,registers.d);
}
void RES_1_E() //    0x8b
{
    registers.e = Res(1,registers.e);
}
void RES_1_H() //    0x8c
{
    registers.h = Res(1,registers.h);
}
void RES_1_L() //    0x8d
{
    registers.l = Res(1,registers.l);
}
void RES_1_HLp() //    0x8e
{
    memory[registers.hl] = Res(1,memory[registers.hl]);
}
void RES_1_A() //    0x8f
{
    registers.a = Res(1,registers.a);
}
void RES_2_B() //    0x90
{
    registers.b = Res(2,registers.b);
}
void RES_2_C() //    0x91
{
    registers.c = Res(2,registers.c);
}
void RES_2_D() //    0x92
{
    registers.d = Res(2,registers.d);
}
void RES_2_E() //    0x93
{
    registers.e = Res(2,registers.e);
}
void RES_2_H() //    0x94
{
    registers.h = Res(2,registers.h);
}
void RES_2_L() //    0x95
{
    registers.l = Res(2,registers.l);
}
void RES_2_HLp() //    0x96
{
    memory[registers.hl] = Res(2,memory[registers.hl]);
}
void RES_2_A() //    0x97
{
    registers.a = Res(2,registers.a);
}
void RES_3_B() //    0x98
{
    registers.b = Res(3,registers.b);
}
void RES_3_C() //    0x99
{
    registers.c = Res(3,registers.c);
}
void RES_3_D() //    0x9a
{
    registers.d = Res(3,registers.d);
}
void RES_3_E() //    0x9b
{
    registers.e = Res(3,registers.e);
}
void RES_3_H() //    0x9c
{
    registers.h = Res(3,registers.h);
}
void RES_3_L() //    0x9d
{
    registers.l = Res(3,registers.l);
}
void RES_3_HLp() //    0x9e
{
    memory[registers.hl] = Res(3,memory[registers.hl]);
}
void RES_3_A() //    0x9f
{
    registers.a = Res(3,registers.a);
}
void RES_4_B() //    0xa0
{
    registers.b = Res(4,registers.b);
}
void RES_4_C() //    0xa1
{
    registers.c = Res(4,registers.c);
}
void RES_4_D() //    0xa2
{
    registers.d = Res(4,registers.d);
}
void RES_4_E() //    0xa3
{
    registers.e = Res(4,registers.e);
}
void RES_4_H() //    0xa4
{
    registers.h = Res(4,registers.h);
}
void RES_4_L() //    0xa5
{
    registers.l = Res(4,registers.l);
}
void RES_4_HLp() //    0xa6
{
    memory[registers.hl] = Res(4,memory[registers.hl]);
}
void RES_4_A() //    0xa7
{
    registers.a = Res(4,registers.a);
}
void RES_5_B() //    0xa8
{
    registers.b = Res(5,registers.b);
}
void RES_5_C() //    0xa9
{
    registers.c = Res(5,registers.c);
}
void RES_5_D() //    0xaa
{
    registers.d = Res(5,registers.d);
}
void RES_5_E() //    0xab
{
    registers.e = Res(5,registers.e);
}
void RES_5_H() //    0xac
{
    registers.h = Res(5,registers.h);
}
void RES_5_L() //    0xad
{
    registers.l = Res(5,registers.l);
}
void RES_5_HLp() //    0xae
{
    memory[registers.hl] = Res(5,memory[registers.hl]);
}
void RES_5_A() //    0xaf
{
    registers.a = Res(5,registers.a);
}
void RES_6_B() //    0xb0
{
    registers.b = Res(6,registers.b);
}
void RES_6_C() //    0xb1
{
    registers.c = Res(6,registers.c);
}
void RES_6_D() //    0xb2
{
    registers.d = Res(6,registers.d);
}
void RES_6_E() //    0xb3
{
    registers.e = Res(6,registers.e);
}
void RES_6_H() //    0xb4
{
    registers.h = Res(6,registers.h);
}
void RES_6_L() //    0xb5
{
    registers.l = Res(6,registers.l);
}
void RES_6_HLp() //    0xb6
{
    memory[registers.hl] = Res(6,memory[registers.hl]);
}
void RES_6_A() //    0xb7
{
    registers.a = Res(6,registers.a);
}
void RES_7_B() //    0xb8
{
    registers.b = Res(7,registers.b);
}
void RES_7_C() //    0xb9
{
    registers.c = Res(7,registers.c);
}
void RES_7_D() //    0xba
{
    registers.d = Res(7,registers.d);
}
void RES_7_E() //    0xbb
{
    registers.e = Res(7,registers.e);
}
void RES_7_H() //    0xbc
{
    registers.h = Res(7,registers.h);
}
void RES_7_L() //    0xbd
{
    registers.l = Res(7,registers.l);
}
void RES_7_HLp() //    0xbe
{
    memory[registers.hl] = Res(7,memory[registers.hl]);
}
void RES_7_A() //    0xbf
{
    registers.a = Res(7,registers.a);
}
void SET_0_B() //    0xc0
{
    registers.b = Set(0,registers.b);
}
void SET_0_C() //    0xc1
{
    registers.c = Set(0,registers.c);
}
void SET_0_D() //    0xc2
{
    registers.d = Set(0,registers.d);
}
void SET_0_E() //    0xc3
{
    registers.e = Set(0,registers.e);
}
void SET_0_H() //    0xc4
{
    registers.h = Set(0,registers.h);
}
void SET_0_L() //    0xc5
{
    registers.l = Set(0,registers.l);
}
void SET_0_HLp() //    0xc6
{
    memory[registers.hl] = Set(0,memory[registers.hl]);
}
void SET_0_A() //    0xc7
{
    registers.a = Set(0,registers.a);
}
void SET_1_B() //    0xc8
{
    registers.b = Set(1,registers.b);
}
void SET_1_C() //    0xc9
{
    registers.c = Set(1,registers.c);
}
void SET_1_D() //    0xca
{
    registers.d = Set(1,registers.d);
}
void SET_1_E() //    0xcb
{
    registers.e = Set(1,registers.e);
}
void SET_1_H() //    0xcc
{
    registers.h = Set(1,registers.h);
}
void SET_1_L() //    0xcd
{
    registers.l = Set(1,registers.l);
}
void SET_1_HLp() //    0xce
{
    memory[registers.hl] = Set(1,memory[registers.hl]);
}
void SET_1_A() //    0xcf
{
    registers.a = Set(1,registers.a);
}
void SET_2_B() //    0xd0
{
    registers.b = Set(2,registers.b);
}
void SET_2_C() //    0xd1
{
    registers.c = Set(2,registers.c);
}
void SET_2_D() //    0xd2
{
    registers.d = Set(2,registers.d);
}
void SET_2_E() //    0xd3
{
    registers.e = Set(2,registers.e);
}
void SET_2_H() //    0xd4
{
    registers.h = Set(2,registers.h);
}
void SET_2_L() //    0xd5
{
    registers.l = Set(2,registers.l);
}
void SET_2_HLp() //    0xd6
{
    memory[registers.hl] = Set(2,memory[registers.hl]);
}
void SET_2_A() //    0xd7
{
    registers.a = Set(2,registers.a);
}
void SET_3_B() //    0xd8
{
    registers.b = Set(3,registers.b);
}
void SET_3_C() //    0xd9
{
    registers.c = Set(3,registers.c);
}
void SET_3_D() //    0xda
{
    registers.d = Set(3,registers.d);
}
void SET_3_E() //    0xdb
{
    registers.e = Set(3,registers.e);
}
void SET_3_H() //    0xdc
{
    registers.h = Set(3,registers.h);
}
void SET_3_L() //    0xdd
{
    registers.l = Set(3,registers.l);
}
void SET_3_HLp() //    0xde
{
    memory[registers.hl] = Set(3,memory[registers.hl]);
}
void SET_3_A() //    0xdf
{
    registers.a = Set(3,registers.a);
}
void SET_4_B() //    0xe0
{
    registers.b = Set(4,registers.b);
}
void SET_4_C() //    0xe1
{
    registers.c = Set(4,registers.c);
}
void SET_4_D() //    0xe2
{
    registers.d = Set(4,registers.d);
}
void SET_4_E() //    0xe3
{
    registers.e = Set(4,registers.e);
}
void SET_4_H() //    0xe4
{
    registers.h = Set(4,registers.h);
}
void SET_4_L() //    0xe5
{
    registers.l = Set(4,registers.l);
}
void SET_4_HLp() //    0xe6
{
    memory[registers.hl] = Set(4,memory[registers.hl]);
}
void SET_4_A() //    0xe7
{
    registers.a = Set(4,registers.a);
}
void SET_5_B() //    0xe8
{
    registers.b = Set(5,registers.b);
}
void SET_5_C() //    0xe9
{
    registers.c = Set(5,registers.c);
}
void SET_5_D() //    0xea
{
    registers.d = Set(5,registers.d);
}
void SET_5_E() //    0xeb
{
    registers.e = Set(5,registers.e);
}
void SET_5_H() //    0xec
{
    registers.h = Set(5,registers.h);
}
void SET_5_L() //    0xed
{
    registers.l = Set(5,registers.l);
}
void SET_5_HLp() //    0xee
{
    memory[registers.hl] = Set(5,memory[registers.hl]);
}
void SET_5_A() //    0xef
{
    registers.a = Set(5,registers.a);
}
void SET_6_B() //    0xf0
{
    registers.b = Set(6,registers.b);
}
void SET_6_C() //    0xf1
{
    registers.c = Set(6,registers.c);
}
void SET_6_D() //    0xf2
{
    registers.d = Set(6,registers.d);
}
void SET_6_E() //    0xf3
{
    registers.e = Set(6,registers.e);
}
void SET_6_H() //    0xf4
{
    registers.h = Set(6,registers.h);
}
void SET_6_L() //    0xf5
{
    registers.l = Set(6,registers.l);
}
void SET_6_HLp() //    0xf6
{
    memory[registers.hl] = Set(6,memory[registers.hl]);
}
void SET_6_A() //    0xf7
{
    registers.a = Set(6,registers.a);
}
void SET_7_B() //    0xf8
{
    registers.b = Set(7,registers.b);
}
void SET_7_C() //    0xf9
{
    registers.c = Set(7,registers.c);
}
void SET_7_D() //    0xfa
{
    registers.d = Set(7,registers.d);
}
void SET_7_E() //    0xfb
{
    registers.e = Set(7,registers.e);
}
void SET_7_H() //    0xfc
{
    registers.h = Set(7,registers.h);
}
void SET_7_L() //    0xfd
{
    registers.l = Set(7,registers.l);
}
void SET_7_HLp() //    0xfe
{
    memory[registers.hl] = Set(7,memory[registers.hl]);
}
void SET_7_A() //    0xff
{
    registers.a = Set(7,registers.a);
}