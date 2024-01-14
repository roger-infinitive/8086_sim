#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <assert.h>

#define OPCODE_MOV_REGISTER_MEMORY_TO_FROM_REGISTER 0x22
#define OPCODE_MOV_IMMEDIATE_TO_REGISTER            0x0B
#define OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY     0x63
#define OPCODE_MOV_MEMORY_TO_ACCUMULATOR            0x50
#define OPCODE_MOV_ACCUMULATOR_TO_MEMORY            0x51

const char* RegisterMap0[8] = {
	"al",
	"cl",
	"dl",
	"bl",
	"ah",
	"ch",
	"dh",
	"bh"
};

const char* RegisterMap1[8] = {
	"ax",
	"cx",
	"dx",
	"bx",
	"sp",
	"bp",
	"si",
	"di"
};

int main(int argc, char* argv[]) {
	if (argc < 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	char* filename = argv[1];

	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		printf("Error opening file.\n");
		return 1;
	}

	//Get the binary size of file
	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	rewind(file);

	//Create a memory buffer and read
	unsigned char* buffer = (unsigned char*)malloc(fileSize);
	if (buffer == NULL)
		return 1;

	size_t bytesRead = fread(buffer, 1, fileSize, file);

	//bit mode for asm.
	printf("bits 16\n");

	for (unsigned int i = 0; i < fileSize;) {
        unsigned char opcode = 0;
        
        opcode = buffer[i] >> 1;
        if ((opcode ^ OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY) == 0) {
            char wide = buffer[i] & 0x01;
            char mode = buffer[i+1] >> 6;
            char rm   = buffer[i+1] & 0x07;
            
            const char** regs = NULL;
            regs = wide == 0 ? RegisterMap0 : RegisterMap1;
            
            switch(mode) {
                case 0x00: {
                    const char* addressCalculation = NULL;
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si]"; break;
                        case 0x01: addressCalculation = "[bx + di]"; break;
                        case 0x02: addressCalculation = "[bp + si]"; break;
                        case 0x03: addressCalculation = "[bp + di]"; break;
                        case 0x04: addressCalculation = "[si]";      break;
                        case 0x05: addressCalculation = "[di]";      break;
                        case 0x06:
                            assert(false && "Direct Address is not valid for OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY.");
                        	break;
                        case 0x07: addressCalculation = "[bx]";      break;
                    }
                    
                    char dataStr[32];
                    if (wide == 0) {
                        unsigned char data = buffer[i + 2];
                        sprintf(dataStr, "byte %d\0", data);
                        i += 3;
                    } else {
                        unsigned short data = *((unsigned short*)(&buffer[i + 2]));
                        sprintf(dataStr, "word %d\0", data);
                        i += 4;
                    }
                    
                	printf("\nmov %s, %s", addressCalculation, dataStr);
                    break;
                }
                case 0x01: {
                    const char* addressCalculation;
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si"; break;
                        case 0x01: addressCalculation = "[bx + di"; break;
                        case 0x02: addressCalculation = "[bp + si"; break;
                        case 0x03: addressCalculation = "[bp + di"; break;
                        case 0x04: addressCalculation = "[si";      break;
                        case 0x05: addressCalculation = "[di";      break;
                        case 0x06: addressCalculation = "[bp";      break;
                        case 0x07: addressCalculation = "[bx";      break;
                    }
                    
                    char address[32];
                    char displacement = buffer[i + 2];
                    if (displacement > 0) {
                        sprintf(address, "%s + %d]\0", addressCalculation, displacement);
                    } else {
                        sprintf(address, "%s - %d]\0", addressCalculation, abs(displacement));
                    }
                                   
                    char dataStr[32];
                    if (wide == 0) {
                        unsigned char data = buffer[i + 3];
                        sprintf(dataStr, "byte %d\0", data);
                        i += 4;
                    } else {
                        unsigned short data = *((unsigned short*)(&buffer[i + 3]));
                        sprintf(dataStr, "word %d\0", data);
                        i += 5;
                    }
                    
                	printf("\nmov %s, %s", address, dataStr);
                    break;
                }
                case 0x02: {
                    const char* addressCalculation;
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si"; break;
                        case 0x01: addressCalculation = "[bx + di"; break;
                        case 0x02: addressCalculation = "[bp + si"; break;
                        case 0x03: addressCalculation = "[bp + di"; break;
                        case 0x04: addressCalculation = "[si";      break;
                        case 0x05: addressCalculation = "[di";      break;
                        case 0x06: addressCalculation = "[bp";      break;
                        case 0x07: addressCalculation = "[bx";      break;
                    }
                    
                    char address[32];
                    short displacement = *((short*)(&buffer[i + 2]));
                    if (displacement > 0) {
                        sprintf(address, "%s + %d]\0", addressCalculation, displacement);
                    } else {
                        sprintf(address, "%s - %d]\0", addressCalculation, abs(displacement));
                    }
                                   
                    char dataStr[32];
                    if (wide == 0) {
                        unsigned char data = buffer[i + 4];
                        sprintf(dataStr, "byte %d\0", data);
                        i += 5;
                    } else {
                        unsigned short data = *((unsigned short*)(&buffer[i + 4]));
                        sprintf(dataStr, "word %d\0", data);
                        i += 6;
                    }
                    
                    printf("\nmov %s, %s", address, dataStr);
                    break;
                }
                case 0x03:
                    assert(false && "MOD 0x03 is not valid for OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY!");
                    break;
            }
        }
        
        if ((opcode ^ OPCODE_MOV_MEMORY_TO_ACCUMULATOR) == 0) {
            char wide = buffer[i] & 0x01;
            unsigned short address = *((unsigned short*)(&buffer[i+1]));
            const char* reg;
            if (wide == 0) {
                reg = "al";
            } else {
                reg = "ax";
            }
            printf("\nmov %s, [%u]", reg, address);
            i += 3;
        }
        
        if ((opcode ^ OPCODE_MOV_ACCUMULATOR_TO_MEMORY) == 0) {
            char wide = buffer[i] & 0x01;
            unsigned short address = *((unsigned short*)(&buffer[i+1]));
            const char* reg;
            if (wide == 0) {
                reg = "al";
            } else {
                reg = "ax";
            }
            printf("\nmov [%u], %s", address, reg);
            i += 3;
        }
        
		opcode = buffer[i] >> 2;
		if ((opcode ^ OPCODE_MOV_REGISTER_MEMORY_TO_FROM_REGISTER) == 0) {
			unsigned char dir  = (buffer[i] & 0x02) >> 1;
			unsigned char wide = buffer[i] & 0x01;
			unsigned char mode = buffer[i+1] >> 6;
			unsigned char reg  = (buffer[i+1] & 0x38) >> 3;
			unsigned char rm   = (buffer[i+1] & 0x07);

			const char** regs = NULL;
            regs = wide == 0 ? RegisterMap0 : RegisterMap1; 

			switch (mode) {
				//Memory Mode (no displacement with exception for Direct Address)
				case 0x00:
				{
                    const char* addressCalculation = NULL;
                    char directAddress[32];
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si]"; break;
                        case 0x01: addressCalculation = "[bx + di]"; break;
                        case 0x02: addressCalculation = "[bp + si]"; break;
                        case 0x03: addressCalculation = "[bp + di]"; break;
                        case 0x04: addressCalculation = "[si]";      break;
                        case 0x05: addressCalculation = "[di]";      break;
                        case 0x06: {
                            short displacement = *((short*)(&buffer[i + 2]));
                            sprintf(directAddress, "[%d]\0", displacement); 
                            addressCalculation = directAddress;
                            i += 2;
                        	break;
                        }
                        case 0x07: addressCalculation = "[bx]";      break;
                    }
                    
                    const char* regStr = regs[reg];
                    if (dir == 1) {
                    	printf("\nmov %s, %s", regStr, addressCalculation);
                    } else {
                    	printf("\nmov %s, %s", addressCalculation, regStr);
                    }
                    i += 2;
                    break;
				}
				case 0x01: // Memory mode, 8 bit displacement
				{
                    const char* addressCalculation;
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si"; break;
                        case 0x01: addressCalculation = "[bx + di"; break;
                        case 0x02: addressCalculation = "[bp + si"; break;
                        case 0x03: addressCalculation = "[bp + di"; break;
                        case 0x04: addressCalculation = "[si";      break;
                        case 0x05: addressCalculation = "[di";      break;
                        case 0x06: addressCalculation = "[bp";      break;
                        case 0x07: addressCalculation = "[bx";      break;
                    }

                    char address[32];
                    char displacement = buffer[i + 2];
                    if (displacement > 0) {
                        sprintf(address, "%s + %d]\0", addressCalculation, displacement);
                    } else {
                        sprintf(address, "%s - %d]\0", addressCalculation, abs(displacement));
                    }

					const char* regStr = regs[reg];
					if (dir == 1) {
						printf("\nmov %s, %s", regStr, address);
					} else {
						printf("\nmov %s, %s", address, regStr);
					}
					i += 3;
					break;
				}
				case 0x02: //Memory mode, 16 bit displacement
				{
                    const char* addressCalculation;
                    switch (rm) {
                        case 0x00: addressCalculation = "[bx + si"; break;
                        case 0x01: addressCalculation = "[bx + di"; break;
                        case 0x02: addressCalculation = "[bp + si"; break;
                        case 0x03: addressCalculation = "[bp + di"; break;
                        case 0x04: addressCalculation = "[si";      break;
                        case 0x05: addressCalculation = "[di";      break;
                        case 0x06: addressCalculation = "[bp";      break;
                        case 0x07: addressCalculation = "[bx";      break;
                    }

                    char address[32];
                    short displacement = *((short*)(&buffer[i + 2]));
                    if (displacement > 0) {
                        sprintf(address, "%s + %d]\0", addressCalculation, displacement);
                    } else {
                        sprintf(address, "%s - %d]\0", addressCalculation, abs(displacement));
                    }

					const char* regStr = regs[reg];
					if (dir == 1) {
						printf("\nmov %s, %s", regStr, address);
					} else {
						printf("\nmov %s, %s", address, regStr);
					}
					i += 4;
					break;
				}
				case 0x03:
				{
                    const char* source;
                    const char* dest;
                    if (dir == 0) {
                    	source = regs[reg];
                    	dest = regs[rm];
                    }
                    else {
                    	source = regs[rm];
                    	dest = regs[reg];
                    }
                    
                    printf("\nmov %s, %s", dest, source);
                    i += 2;
                    break;
				}
			}
		}
        
        opcode = buffer[i] >> 4;
        if ((opcode ^ OPCODE_MOV_IMMEDIATE_TO_REGISTER) == 0) {
            unsigned char wide = buffer[i] & 0x08;
            unsigned char reg  = buffer[i] & 0x07;
        
            unsigned short data = 0;
            const char* regStr = NULL;
            if (wide == 0) {
                regStr = RegisterMap0[reg];
                data = buffer[i + 1];
                i += 2;
            } else {
                regStr = RegisterMap1[reg];
                data = *((unsigned short*)(&buffer[i + 1]));
                i += 3;
            }

			printf("\nmov %s, %i", regStr, data);
		}
	}

	fclose(file);
	return 0;
}