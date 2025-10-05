#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define u8  unsigned char
#define u16 unsigned short
#define u32 unsigned int
#define u64 unsigned long long

typedef void* (*AllocFunc) (size_t size);
typedef void  (*FreeFunc)  (void*);

void log_error_impl(const char *fmt, ...) {
    va_list ap;
    fputs("\nerror: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

#ifdef _MSC_VER
    #include <intrin.h>
    #define BREAKPOINT() __debugbreak()
#elif defined(__has_builtin) && __has_builtin(__builtin_trap)
    #define BREAKPOINT() __builtin_trap()
#else
    #include <signal.h>
    #define BREAKPOINT() raise(SIGTRAP)
#endif

#if defined(NDEBUG)
    #define ERROR_ABORT() exit(1)
#else
    #define ERROR_ABORT() BREAKPOINT();
#endif 

#define log_error(...)      do { log_error_impl(__VA_ARGS__);  } while (0)
#define critical_error(...) do { log_error_impl(__VA_ARGS__); ERROR_ABORT(); } while (0)
#define not_implemented()   critical_error("NOT IMPLEMENTED %s(%d)\n", __FILE__, __LINE__)

struct MemoryArena {
    size_t index;
    size_t capacity;
    u8* buffer;    
};

void init_arena(MemoryArena* arena, size_t size) {
    arena->index = 0;
    arena->capacity = size;
    arena->buffer = (u8*)malloc(size);
}

void* arena_alloc(MemoryArena* arena, size_t size) {
    if (arena->index + size >= arena->capacity) {
        critical_error("arena is out of memory! attempted to allocate %llu.", size);
        return 0;
    }
    
    void* mem = &arena->buffer[arena->index];
    arena->index += size;
    
    return mem;
}

struct MemoryBuffer {
    size_t size;
    u8* buffer;
};

bool read_entire_file(MemoryBuffer* fileBuffer, const char* filename, AllocFunc mem_alloc) {
    FILE* file = fopen(filename, "rb");
    if (file == 0) {
        log_error("Failed to open file %s (%s)", filename, strerror(errno));    
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
	rewind(file);
    
    fileBuffer->buffer = (u8*)mem_alloc(fileSize);
    fread(fileBuffer->buffer, 1, fileSize, file);
    fileBuffer->size = fileSize;

    fclose(file);
    return true;
}

MemoryArena main_arena = {};

void* main_arena_alloc(size_t size) {
    return arena_alloc(&main_arena, size);
}

const char* N2B[16] = {
    "0000","0001","0010","0011","0100","0101","0110","0111",
    "1000","1001","1010","1011","1100","1101","1110","1111"
};

void print_byte(u8 b, FILE* stream = stdout) {
    fputs(N2B[b>>4], stream);
    fputs(N2B[b&0xF], stream);
}

#define OPCODE_MOV_REGISTER_OR_MEMORY_TO_FROM_REGISTER  0x88
#define OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY         0xC6
#define OPCODE_MOV_IMMEDIATE_TO_REGISTER                0xB0
#define OPCODE_MOV_MEMORY_TO_ACCUMULATOR                0xA0

#define MODE_MEMORY_NO_DISPLACEMENT     0
#define MODE_MEMORY_8_BIT_DISPLACEMENT  1
#define MODE_MEMORY_16_BIT_DISPLACEMENT 2
#define MODE_REGISTER                   3

const char* register_map_byte[8] = { 
    "al", 
    "cl", 
    "dl", 
    "bl", 
    "ah", 
    "ch", 
    "dh", 
    "bh" 
};

const char* register_map_word[8] = {
    "ax",
    "cx",
    "dx",
    "bx",
    "sp",
    "bp",
    "si",
    "di"
};

const char* effective_address_table[8] = {
    "bx + si",
    "bx + di",
    "bp + si",
    "bp + di",
    "si",
    "di",
    "bp",
    "bx"
};

enum OpClass {
    OP_CLASS_NONE                         = 0,
    OP_CLASS_REGISTER_MEMORY_AND_REGISTER = 1,
    OP_CLASS_IMMEDIATE_TO_ACCUMULATOR     = 2,
    OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY = 3,
    OP_CLASS_REGISTER_MEMORY              = 4,
};

struct Instruction {
    int address;
    char* string;
    
    bool is_jump;
    int jump_address;
};

char instruction_buffer[64];
int instruction_count;
Instruction instructions[4096];

Instruction* capture_instruction(int address, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsprintf(instruction_buffer, fmt, ap);
    va_end(ap);

    char* captured = (char*)&main_arena.buffer[main_arena.index];

    const char* current = instruction_buffer; 
    while (current[0] != 0) {
        main_arena.buffer[main_arena.index++] = current[0];
        current += 1;
    }
    main_arena.buffer[main_arena.index++] = '\0';

    Instruction* instruction = &instructions[instruction_count];
    instruction_count += 1;

    instruction->address = address;
    instruction->string = captured;

    return instruction;
}

Instruction* capture_jump_instruction(int address, int jump_address, const char* mnemonic) {
    Instruction* instruction = capture_instruction(address, mnemonic);
    instruction->is_jump = true;
    instruction->jump_address = jump_address;
    return instruction;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    init_arena(&main_arena, 32*1024*1024);
    
    MemoryBuffer file = {};
    if (!read_entire_file(&file, argv[1], main_arena_alloc)) {
        return 1;
    }
    
    for (int i = 0; i < file.size;) {
        int address = i;
        u8* bytes = &file.buffer[i];
        
        bool use_segment_override = false;
        u8 segment_override = 0;
        
        switch (bytes[0]) {
            case 0x26:
            case 0x2E:
            case 0x36:
            case 0x3E: {
                use_segment_override = true;
                segment_override = bytes[0] >> 3 & 0x03;
                i += 1;
                bytes = &file.buffer[i];
            } break;
        }
        
        OpClass op_class = OP_CLASS_NONE;
        u8 opcode_byte = 0;
        bool use_signed_immediate = false;
        
        if ((bytes[0] & 0b11111100) == OPCODE_MOV_REGISTER_OR_MEMORY_TO_FROM_REGISTER) {
            op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
            opcode_byte = OPCODE_MOV_REGISTER_OR_MEMORY_TO_FROM_REGISTER;
        
        } else if ((bytes[0] & 0b11111110) == OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY) {
            op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            opcode_byte = OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY;
        
        } else if ((bytes[0] & 0b11111100) == 0b10000000) {
            op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            opcode_byte = bytes[1] & 0b00111000;
            use_signed_immediate = (bytes[0] & 0b00000010) != 0; 
                    
        } else if ((bytes[0] & 0b11000110) == 0b00000100) {
            op_class = OP_CLASS_IMMEDIATE_TO_ACCUMULATOR;
            opcode_byte = bytes[0] & 0b00111000;
            
        }
        
        const char* op_text = 0;
        
        const char* segment_overrides[] = {
            "es",
            "cs",
            "ss",
            "ds",
        };
        
        const char* group_one_mnemonics[] = {
            "add", 
            "or",
            "adc",
            "sbb",
            "and",
            "sub",
            "xor",
            "cmp"
        };
        
        if (bytes[0] >= 0x00 && bytes[0] <= 0x3F) {
            if ((bytes[0] & 0x06) == 0x06) {
                if ((bytes[0] & 0xF0) <= 0x10) {
                    op_text = (bytes[0] & 0x01) ? "pop" : "push";
                    
                    const char* target = 0;
                    switch ((bytes[0] >> 3) & 0x03) {
                        case 0: target = "es"; break;
                        case 1: target = "cs"; break;
                        case 2: target = "ss"; break;
                        case 3: target = "ds"; break;
                    }
                    
                    capture_instruction(address, "%s %s\n", op_text, target);
                    i += 1;
                    continue;
                    
                } else if (bytes[0] & 0x07 == 0x07) {
                    switch ((bytes[0] >> 3) & 0x03) {
                        case 0: op_text = "daa"; break;
                        case 1: op_text = "das"; break;
                        case 2: op_text = "aaa"; break;
                        case 3: op_text = "aas"; break;
                    }
                
                    capture_instruction(address, "%s\n", op_text);
                    i += 1;
                    continue;

                }
                
            } else { 
                op_text = group_one_mnemonics[((bytes[0] >> 3) & 0x07)];

                if (bytes[0] & 0x04) {
                    op_class = OP_CLASS_IMMEDIATE_TO_ACCUMULATOR;
                } else {
                    op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
                } 
            }
        
        } else if (bytes[0] >= 0x80 && bytes[0] <= 0x83) {
            op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            op_text = group_one_mnemonics[((bytes[1] >> 3) & 0x07)];
        
        } else if (bytes[0] & 0x8F) {
            op_class = OP_CLASS_REGISTER_MEMORY;
            op_text = "pop";
            
        } else {
            // nocheckin: this is now confusing as we add more types of instructions that use different tables.
            switch (opcode_byte) {
                case OPCODE_MOV_IMMEDIATE_TO_REGISTER_MEMORY:
                case OPCODE_MOV_REGISTER_OR_MEMORY_TO_FROM_REGISTER: 
                    op_text = "mov"; 
                break;
                
                case 0x00: op_text = "add"; break;
                case 0x28: op_text = "sub"; break;
                case 0x38: op_text = "cmp"; break;
                
                default: print_byte(bytes[0]); not_implemented(); 
            }
        }
        
        if (op_class == OP_CLASS_REGISTER_MEMORY) {
            u8 word = bytes[0] & 0x01;
            u8 mode = bytes[1] >> 6;
            u8 rm   = bytes[1] & 0x07;
            
            int byte_count = 2;
            
            // nocheckin: duplicate.
            char address_operand[32];
            short displacement = 0;
            
            if (mode == MODE_REGISTER) {
                const char** reg_table = word ? register_map_word : register_map_byte; 
                strcpy(address_operand, reg_table[rm]);
                
            } else if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                displacement = bytes[2] | (bytes[3] << 8);                    
                sprintf(address_operand, "[%hd]", displacement);
                byte_count += 2;
    
            } else {
                if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
                    u8 sign = bytes[2] & 0x80;
                    if (sign) {
                        displacement = 0xFF00;
                    }
                    displacement |= bytes[2];
                    byte_count += 1;
                
                } else if (mode == MODE_MEMORY_16_BIT_DISPLACEMENT) {
                    displacement = bytes[2] | (bytes[3] << 8);
                    byte_count += 2;
                }                    
            
                const char* effective_address = effective_address_table[rm];
                if (displacement == 0) {
                    sprintf(address_operand, "[%s]", effective_address);
                } else {
                    sprintf(address_operand, "[%s + %hd]", effective_address, displacement);
                }
            }
            
            const char* size_label = word ? "word" : "byte";
            capture_instruction(address, "%s %s %s\n", op_text, size_label, address_operand);
            
            i += byte_count;
            continue;
            
        } else if (op_class == OP_CLASS_REGISTER_MEMORY_AND_REGISTER) {

            u8 word = bytes[0] & 0x01;
            u8 dir  = bytes[0] & 0x02;
            u8 mode = bytes[1] >> 6;
            u8 reg  = (bytes[1] & 0x38) >> 3; 
            u8 rm   = bytes[1] & 0x07;
            
            int byte_count = 2;
    
            const char** reg_table = word ? register_map_word : register_map_byte; 
            
            if (mode == MODE_REGISTER) {
                const char* r1 = reg_table[reg];
                const char* r2 = reg_table[rm];
                
                const char* dest   = dir ? r1 : r2;
                const char* source = dir ? r2 : r1;
            
                capture_instruction(address, "%s %s, %s\n", op_text, dest, source);
                
            } else {
                char address_operand[32];
                short displacement = 0;
 
                if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                    displacement = bytes[2] | (bytes[3] << 8);                    
                    sprintf(address_operand, "[%hd]", displacement);
                    byte_count += 2;

                } else {
                    if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
                        u8 sign = bytes[2] & 0x80;
                        if (sign) {
                            displacement = 0xFF00;
                        }
                        displacement |= bytes[2];
                        byte_count += 1;
                    
                    } else if (mode == MODE_MEMORY_16_BIT_DISPLACEMENT) {
                        displacement = bytes[2] | (bytes[3] << 8);
                        byte_count += 2;
                    }                    
                
                    const char* effective_address = effective_address_table[rm];
                    if (displacement == 0) {
                        if (use_segment_override) {
                            sprintf(address_operand, "[%s:%s]", segment_overrides[segment_override], effective_address);
                        } else {
                            sprintf(address_operand, "[%s]", effective_address);
                        }
                    } else {
                        sprintf(address_operand, "[%s + %hd]", effective_address, displacement);
                    }
                }
                
                const char* reg_operand = reg_table[reg];
                const char* dest   = dir ? reg_operand : address_operand;
                const char* source = dir ? address_operand : reg_operand;
                
                capture_instruction(address, "%s %s, %s\n", op_text, dest, source);
            }
            
            i += byte_count;
            continue;
        
        } else if (op_class == OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY) {
            u8 word = bytes[0] & 0x01;
            u8 mode = bytes[1] >> 6;
            u8 rm   = bytes[1] & 0x07;
            
            int byte_count = 2; 
            
            char address_operand[32];
            short displacement = 0;
            
            if (mode == MODE_REGISTER) {
                const char** reg_table = word ? register_map_word : register_map_byte; 
                strcpy(address_operand, reg_table[rm]);
                
            } else if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                displacement = bytes[2] | (bytes[3] << 8);                    
                sprintf(address_operand, "[%hd]", displacement);
                byte_count += 2;
    
            } else {
                if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
                    u8 sign = bytes[2] & 0x80;
                    if (sign) {
                        displacement = 0xFF00;
                    }
                    displacement |= bytes[2];
                    byte_count += 1;
                
                } else if (mode == MODE_MEMORY_16_BIT_DISPLACEMENT) {
                    displacement = bytes[2] | (bytes[3] << 8);
                    byte_count += 2;
                }                    
            
                const char* effective_address = effective_address_table[rm];
                if (displacement == 0) {
                    sprintf(address_operand, "[%s]", effective_address);
                } else {
                    sprintf(address_operand, "[%s + %hd]", effective_address, displacement);
                }
            }
            
            u16 data = 0;
            if (word) {
                // TODO(roger): do we need use_signed_immediate? perhaps we can parse 's' for mov as well?
                if (use_signed_immediate) {
                    u8 sign = bytes[byte_count] & 0x80;
                    if (sign) {
                        data = 0xFF00;
                    }
                    data |= bytes[byte_count];
                    byte_count += 1; 
                } else {
                    data = bytes[byte_count] | (bytes[byte_count + 1] << 8);
                    byte_count += 2; 
                }
            } else {
                data = bytes[byte_count];
                byte_count += 1;
            }
            
            const char* size_label = word ? "word" : "byte";
            capture_instruction(address, "%s %s, %s %hu\n", op_text, address_operand, size_label, data);

            i += byte_count;
            continue;
            
        } else if ((bytes[0] & 0b11110000) == OPCODE_MOV_MEMORY_TO_ACCUMULATOR) {
            u8 word = bytes[0] & 0x01;
            u8 dir  = bytes[0] & 0x02;

            u16 address = bytes[1] | bytes[2] << 8;

            if (dir) {
                capture_instruction(address, "mov [%hd], %s\n", address, word ? "ax" : "al");
            } else {
                capture_instruction(address, "mov %s, [%hd]\n", word ? "ax" : "al", address);
            }
            
            i += 3;
            continue;
      
        } else if ((bytes[0] & 0b11110000) == OPCODE_MOV_IMMEDIATE_TO_REGISTER) {
            u8 reg  = bytes[0] & 0x07;
            u8 word = bytes[0] & 0x08;
            int byte_count = 1;

            const char** reg_table = word ? register_map_word : register_map_byte; 
            
            u16 data = 0;
            if (word) {
                data = bytes[byte_count] | (bytes[byte_count + 1] << 8);
                byte_count += 2;
            } else {
                data = bytes[byte_count];
                byte_count += 1;
            }
            
            const char* size_label = word ? "word" : "byte";
            capture_instruction(address, "mov %s, %s %hu\n", reg_table[reg], size_label, data);
        
            i += byte_count;    
            continue;
            
        } else if (op_class == OP_CLASS_IMMEDIATE_TO_ACCUMULATOR) {
            u8 word = bytes[0] & 0x01;
            int byte_count = 1;

            u16 data = 0;
            if (word) {
                data = bytes[byte_count] | (bytes[byte_count + 1] << 8);
                byte_count += 2;
            } else {
                data = bytes[byte_count];
                byte_count += 1;
            }
            
            const char* size_label = word ? "word" : "byte";
            const char* reg = word ? "ax" : "al";
            
            capture_instruction(address, "%s %s, %s %hu\n", op_text, reg, size_label, data);
        
            i += byte_count;    
            continue;
        
        } else if ((bytes[0] >> 1) == 0x7F) {
        
            u8 word = bytes[0] & 0x01;
            u8 mnemonic = (bytes[1] >> 3) & 0x07;
            u8 mode = (bytes[1] >> 6);
            u8 rm = bytes[1] & 0x07; 
            
            int byte_count = 2;
            
            // nocheckin: duplicated from OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY
            
            char address_operand[32];
            short displacement = 0;
            
            if (mode == MODE_REGISTER) {
                const char** reg_table = word ? register_map_word : register_map_byte; 
                strcpy(address_operand, reg_table[rm]);
                
            } else if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                displacement = bytes[2] | (bytes[3] << 8);                    
                sprintf(address_operand, "[%hd]", displacement);
                byte_count += 2;
    
            } else {
                if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
                    u8 sign = bytes[2] & 0x80;
                    if (sign) {
                        displacement = 0xFF00;
                    }
                    displacement |= bytes[2];
                    byte_count += 1;
                
                } else if (mode == MODE_MEMORY_16_BIT_DISPLACEMENT) {
                    displacement = bytes[2] | (bytes[3] << 8);
                    byte_count += 2;
                }                    
            
                const char* effective_address = effective_address_table[rm];
                if (displacement == 0) {
                    sprintf(address_operand, "[%s]", effective_address);
                } else {
                    sprintf(address_operand, "[%s + %hd]", effective_address, displacement);
                }
            }
            
            const char* mnemonics[] = {
                "inc",
                "dec",
                "call",
                "call",
                "jmp",
                "jmp",
                "push"
            };
            
            const char* size_label = word ? "word" : "byte";
            capture_instruction(address, "%s %s %s\n", mnemonics[mnemonic], size_label, address_operand);
            
            i += byte_count;
            continue;
            
        } else if ((bytes[0] & 0xE0) == 0x40) {
            const char* reg = register_map_word[bytes[0] & 0x0F];
            
            const char* mnemonics[] = {
                "inc",
                "dec",
                "push",
                "pop",
            };
            
            const char* mnemonic = mnemonics[(bytes[0] & 0x18) >> 3];
            capture_instruction(address, "%s %s\n", mnemonic, reg);
            
            i += 1;
            continue;
        } else if (bytes[0] == 0x0E) {
            capture_instruction(address, "push cs\n");
            i += 1;
            continue;
        
        } else if (bytes[0] == 0x70) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jo");
            continue;
        
        } else if (bytes[0] == 0x71) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jno");
            continue;
            
        } else if (bytes[0] == 0x72) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jb");
            continue;
            
        } else if (bytes[0] == 0x73) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jnb");
            continue;
                                
        } else if (bytes[0] == 0x74) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "je");
            continue;
        
        } else if (bytes[0] == 0x75) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jne");
            continue;
        
        } else if (bytes[0] == 0x76) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jbe");
            continue;
        
        } else if (bytes[0] == 0x77) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jnbe");
            continue;
            
        } else if (bytes[0] == 0x78) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "js");
            continue;        
        
        } else if (bytes[0] == 0x79) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jns");
            continue;        
            
        } else if (bytes[0] == 0x7A) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jp");
            continue;
            
        } else if (bytes[0] == 0x7B) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jnp");
            continue;

        } else if (bytes[0] == 0x7C) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jl");
            continue;
            
        } else if (bytes[0] == 0x7D) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jnl");
            continue;
            
        } else if (bytes[0] == 0x7E) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jle");
            continue;            
        
        } else if (bytes[0] == 0x7F) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jnle");
            continue;            
        
        } else if (bytes[0] == 0xE0) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "loopnz");
            continue;
            
        } else if (bytes[0] == 0xE1) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "loopz");
            continue;
            
        } else if (bytes[0] == 0xE2) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "loop");
            continue;
             
        } else if (bytes[0] == 0xE3) {
            i += 2;
            capture_jump_instruction(address, i + (char)bytes[1], "jcxz");
            continue;
             
        }
        
        fputs("Unable to decode byte: ", stderr);
        print_byte(bytes[0], stderr);
        fputc('\n', stderr);
        ERROR_ABORT();
    }
    
    // Labels
    int label_counter = 0;
    int label_addresses[1024];
    memset(label_addresses, 0, 1024 * sizeof(int)); 
    
    for (int i = 0; i < instruction_count; i++) {
        if (!instructions[i].is_jump) {
            continue;
        }
    
        instructions[i].jump_address;

        bool found = false;
        for (int j = 0; j < label_counter; j++) {
            if (label_addresses[j] == instructions[i].jump_address) {
                found = true;
                break;
            } 
        }
        
        if (found) {
            continue;
        }

        label_addresses[label_counter] = instructions[i].jump_address;
        label_counter++;
    }
    
    printf("bits 16\n");
    for (int i = 0; i < instruction_count; i++) {
    
        // TODO(roger): if we sort the label_addresses, then we can eliminate this loop and use a current index to check
        // if we need to instead the next label into the output or not.
        for (int j = 0; j < label_counter; j++) {
            if (label_addresses[j] == instructions[i].address) {
                printf("label_%d:\n", label_addresses[j]);
                break;
            }
        }
    
        printf(instructions[i].string);
        if (instructions[i].is_jump) {
            printf(" label_%d\n", instructions[i].jump_address);
        }
    }

    return 0;
}