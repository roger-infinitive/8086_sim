// TODO(roger): 8086 uses 1 mb for memory. Load file into this range. 
//      MEMORY_ACCESS_MASK can be used to prevent reading out of bounds.

#include "utility.h"

enum InstructionType {
    InstructionType_Undefined = 0,
    
    #define INSTRUCTION(inst) InstructionType_##inst, 
    #include "instructions.inc"
    #undef INSTRUCTION
};

const char* instruction_strings[] {
    #define INSTRUCTION(inst) #inst,
    #include "instructions.inc"
    #undef INSTRUCTION
};

#define MODE_MEMORY_NO_DISPLACEMENT     0
#define MODE_MEMORY_8_BIT_DISPLACEMENT  1
#define MODE_MEMORY_16_BIT_DISPLACEMENT 2
#define MODE_REGISTER                   3

enum Register {
    Register_A,
    Register_C,
    Register_D,
    Register_B,
    Register_SP,
    Register_BP,
    Register_SI,
    Register_DI,
};

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

const char* segments[] = { "es", "cs", "ss", "ds" };

const InstructionType group_one_mnemonics[] = {
    InstructionType_add, 
    InstructionType_or,
    InstructionType_adc,
    InstructionType_sbb,
    InstructionType_and,
    InstructionType_sub,
    InstructionType_xor,
    InstructionType_cmp
};

enum OpClass {
    OP_CLASS_NONE                         = 0,
    OP_CLASS_REGISTER_MEMORY              = 1,
    OP_CLASS_REGISTER_MEMORY_AND_REGISTER = 2,
    OP_CLASS_IMMEDIATE                    = 3,
    OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY = 4,
    OP_CLASS_IMMEDIATE_TO_ACCUMULATOR     = 5,
    OP_CLASS_SEG_REG                      = 6,
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

const char* instruction_prefix = 0;


// CPU
u16 registers[8]; 


Instruction* capture_instruction(int address, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsprintf(instruction_buffer, fmt, ap);
    va_end(ap);
    
    char* captured = (char*)&main_arena.buffer[main_arena.index];

    if (instruction_prefix) {
        const char* current = instruction_prefix; 
        while (current[0] != 0) {
            main_arena.buffer[main_arena.index++] = current[0];
            current += 1;
        }
    }

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
    
    instruction_prefix = 0;
    return instruction;
}

Instruction* capture_jump_instruction(int address, int jump_address, const char* mnemonic) {
    Instruction* instruction = capture_instruction(address, mnemonic);
    instruction->is_jump = true;
    instruction->jump_address = jump_address;
    return instruction;
}

int extract_encoded_data(u8* bytes, int current_byte, bool extract_word, bool use_signed_immediate, u16* data) {
    if (extract_word) {
        if (use_signed_immediate) {
            u8 sign = bytes[current_byte] & 0x80;
            if (sign) {
                *data = 0xFF00;
            }
            *data |= bytes[current_byte];
            return 1; 
        }

        *data = bytes[current_byte] | (bytes[current_byte + 1] << 8);
        return 2; 
    }
 
    *data = bytes[current_byte];
    return 1;
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
        
        bool is_bit_shift = false;
        u8 bit_shift_type = 0;

        bool use_signed_immediate = false;
        bool decode_register_memory = false;
        bool extract_data = false;
        bool extract_word = false;
        bool use_lock = false;
        
        OpClass op_class = OP_CLASS_NONE;
        // nocheckin: remove in favor of instruction_type and instruction_strings[]
        const char* op_text = 0;
        InstructionType instruction_type = InstructionType_Undefined;
        
        if (bytes[0] == 0xF0) {
            instruction_prefix = "lock ";
            use_lock = true;
            i += 1;
            bytes = &file.buffer[i];
        }
        
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
        
        int byte_count = 0;
        
        u8 word = bytes[0] & 0x01;
        u8 mode = bytes[1] >> 6;
        u8 rm   = bytes[1] & 0x07;
        u8 dir  = bytes[0] & 0x02;
        
        if (bytes[0] >= 0x00 && bytes[0] <= 0x3F) {
            if ((bytes[0] & 0x06) == 0x06) {
                if ((bytes[0] & 0xF0) <= 0x10) {
                    op_text = (bytes[0] & 0x01) ? "pop" : "push";
                    
                    capture_instruction(address, "%s %s\n", op_text, segments[(bytes[0] >> 3) & 0x03]);
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
                instruction_type = group_one_mnemonics[((bytes[0] >> 3) & 0x07)];
                op_text = instruction_strings[instruction_type];

                if (bytes[0] & 0x04) {
                    op_class = OP_CLASS_IMMEDIATE_TO_ACCUMULATOR;
                    extract_data = true;
                    extract_word = word != 0;
                    byte_count += 1;
                } else {
                    op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
                    decode_register_memory = true;
                    byte_count += 2;
                }
            }
            
        } else if (bytes[0] >= 0x40 && bytes[0] <= 0x5F) {
            const char* reg = register_map_word[bytes[0] & 0x07];
            
            const char* mnemonics[] = {
                "inc",
                "dec",
                "push",
                "pop",
            };
            
            op_text = mnemonics[(bytes[0] >> 3) & 0x03];
            capture_instruction(address, "%s %s\n", op_text, reg);
            
            i += 1;
            continue;
            
        } else if ((bytes[0] & 0xF0) == 0x70) {
            const char* mnemonics[] = {
                "jo",
                "jno",
                "jb",
                "jnb",
                "je",
                "jne",
                "jbe",
                "jnbe",
                "js",
                "jns",
                "jp",
                "jnp",
                "jl",
                "jnl",
                "jle",
                "jnle"
            };
            
            op_text = mnemonics[bytes[0] & 0x0F];
            
            i += 2; // move forward before capturing target address.
            capture_jump_instruction(address, i + (char)bytes[1], op_text);
            continue;
        
        } else if (bytes[0] >= 0x80 && bytes[0] <= 0x83) {
            op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            decode_register_memory = true;
            extract_data = true;
            extract_word = word != 0;
            instruction_type = group_one_mnemonics[((bytes[1] >> 3) & 0x07)];
            op_text = instruction_strings[instruction_type];
            
            if (bytes[0] == 0x83) {
                use_signed_immediate = true;
            } else {
                use_signed_immediate = (bytes[0] & 0b00000010) != 0;
            }
            
            byte_count += 2;
            
        } else if (bytes[0] >= 0x84 && bytes[0] <= 0x87) {
            const InstructionType mnemonics[] = {
                InstructionType_test,
                InstructionType_test,
                InstructionType_xchg,
                InstructionType_xchg
            };
            
            instruction_type = mnemonics[(bytes[0] & 0x0F) - 0x04];
            op_text = instruction_strings[instruction_type];
            op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
            decode_register_memory = true;
            
            if (use_lock) {
                dir = 0;
            } else {
                dir = bytes[0] & 0x02;
            }
            
            byte_count += 2;
        
        } else if (bytes[0] >= 0x88 && bytes[0] <= 0x8B) {
            op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
            decode_register_memory = true;
            instruction_type = InstructionType_mov;
            op_text = instruction_strings[instruction_type];
            byte_count += 2;
            
        } else if (bytes[0] == 0x8C || bytes[0] == 0x8E) {
            op_class = OP_CLASS_SEG_REG;
            decode_register_memory = true;
            dir = bytes[0] & 0x02;
            word = 1;
            op_text = "mov";
            byte_count += 2;
        
        } else if (bytes[0] == 0x8D) {
            op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
            decode_register_memory = true;
            instruction_type = InstructionType_lea;
            op_text = instruction_strings[instruction_type];
            dir = 1;
            byte_count += 2;
        
        } else if (bytes[0] == 0x8F) {
            op_class = OP_CLASS_REGISTER_MEMORY;
            decode_register_memory = true;
            op_text = "pop";
            byte_count += 2;
            
        } else if (bytes[0] >= 0x90 && bytes[0] <= 0x97) {
            capture_instruction(address, "xchg ax, %s\n", register_map_word[bytes[0] & 0x0F]);
            i += 1;
            continue;
            
        } else if ((bytes[0] & 0xFE) == 0x98) { 
            capture_instruction(address, "%s\n", (bytes[0] & 0x01) ? "cwd" : "cbw");
            i += 1;
            continue;
        
        } else if (bytes[0] == 0x9A) {
            u16 displacement = bytes[1] | (bytes[2] << 8);
            u16 seg = bytes[3] | (bytes[4] << 8);

            capture_instruction(address, "call %lu:%lu\n", seg, displacement);
            i += 5;
            continue;
        
        } else if (bytes[0] == 0x9B) { 
            capture_instruction(address, "wait\n");
            i += 1;
            continue;
        
        } else if ((bytes[0] & 0xFC) == 0x9C) {
            const char* mnemonics[] = {
                "pushf",
                "popf",
                "sahf",
                "lahf",
            };
            
            capture_instruction(address, "%s\n", mnemonics[bytes[0] & 0x03]);
            i += 1;
            continue;
        
        } else if ((bytes[0] & 0xFC) == 0xA0) {
            u16 address = bytes[1] | bytes[2] << 8;
            if (dir) {
                capture_instruction(address, "mov [%hd], %s\n", address, word ? "ax" : "al");
            } else {
                capture_instruction(address, "mov %s, [%hd]\n", word ? "ax" : "al", address);
            }
            i += 3;
            continue;
            
        } else if ((bytes[0] & 0xFE) == 0xA8) {
            extract_data = true;
            extract_word = word != 0;
            op_class = OP_CLASS_IMMEDIATE_TO_ACCUMULATOR;
            op_text = "test";
            byte_count += 1;
        
        } else if ((bytes[0] & 0xFC) == 0xA4 || (bytes[0] & 0xFC) == 0xAC) {
            if (bytes[0] & 0x08) {
                op_text = (bytes[0] & 0x02) ? "lods" : "scas";
            } else {
                op_text = (bytes[0] & 0x02) ? "movs" : "cmps";
            }
            
            word = bytes[0] & 0x01;
            capture_instruction(address, "%s%s\n", op_text, word ? "w" : "b");
            
            i += 1;
            continue;
            
        } else if ((bytes[0] & 0xF0) == 0xB0) {
            u16 data = 0;
            u8 reg = bytes[0] & 0x07;
            bool use_word = (bytes[0] & 0x08) != 0;
        
            byte_count += 1;
            byte_count += extract_encoded_data(bytes, byte_count, use_word, false, &data);

            // nocheckin: somewhat duplicate.
            // simulate

            // TODO(roger): for simulate debug
            u16 previous_value = registers[reg];
            
            // nocheckin: use Register enum
            int reg_index = 0;
            if (use_word) {
                reg_index = reg;
                registers[reg] = data;
            } else {
                reg_index = reg & 0x03;
                if (reg & 0x04) {
                    // Set high bits of register.
                    registers[reg_index] = (data << 8) | (registers[reg_index] & 0x00FF);
                } else {
                    // Set low bits of register.
                    registers[reg_index] = data | (registers[reg_index] & 0xFF00);
                }
            }
            
            // TODO(roger): for simulate debug
            const char** reg_table = use_word ? register_map_word : register_map_byte; 
            const char* reg_label = reg_table[reg];
            printf("mov %s, %hu ; %s:0x%01hx->0x%01hx\n", reg_label, data, register_map_word[reg_index], previous_value, registers[reg_index]);

            // TODO(roger): printing a decoded asm file should be a separate process from simulation
            // print decoding
            const char* size_label = use_word ? "word" : "byte";
            capture_instruction(address, "mov %s, %s %hu\n", reg_label, size_label, data);
        
            i += byte_count;    
            continue;
        
        } else if ((bytes[0] & 0xFE) == 0xC2) {
            u8 no_immediate = bytes[0] & 0x01;
            if (no_immediate) {
                capture_instruction(address, "ret\n");
                i += 1;
                continue;
                
            } else {
                extract_data = true;
                extract_word = true;
                byte_count += 1;
                op_text = "ret";
                op_class = OP_CLASS_IMMEDIATE;
            }
        
        } else if ((bytes[0] & 0xFE) == 0xC4) {
            op_class = OP_CLASS_REGISTER_MEMORY_AND_REGISTER;
            decode_register_memory = true;
            instruction_type = (bytes[0] & 0x01) ? InstructionType_lds : InstructionType_les;
            op_text = instruction_strings[instruction_type];
            word = 1;
            dir = 1;
            byte_count += 2;
        
        } else if ((bytes[0] & 0xFE) == 0xC6) {
            op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            decode_register_memory = true;
            extract_data = true;
            extract_word = word != 0;
            op_text = "mov";
            byte_count += 2;
            
        } else if ((bytes[0] & 0xFE) == 0xCA) {
            if (bytes[0] & 0x01) {
                capture_instruction(address, "retf\n");
                i += 1;
                continue;
            } else {
                extract_data = true;
                extract_word = true;
                byte_count += 1;
                op_text = "retf";
                op_class = OP_CLASS_IMMEDIATE;
            }
            
        } else if ((bytes[0] & 0xFC) == 0xCC) {
            const char* mnemonics[] = {
                "int3",
                "int",
                "into",
                "iret"
            };
        
            u8 op = bytes[0] & 0x03;
            op_text = mnemonics[op];
            
            if (op == 1) {
                extract_data = true;
                extract_word = false;
                op_class = OP_CLASS_IMMEDIATE;
                byte_count += 1;
            } else {
                capture_instruction(address, "%s\n", op_text);
                i += 1;
                continue;
            }
        
        } else if (bytes[0] >= 0xD0 && bytes[0] <= 0xD3) { 
            const char* mnemonics[] = {
                "rol",
                "ror",
                "rcl",
                "rcr",
                "shl", // "sal"
                "shr",
                "unused",
                "sar"
            };
            
            op_text = mnemonics[(bytes[1] >> 3) & 0x07];
            op_class = OP_CLASS_REGISTER_MEMORY;
            decode_register_memory = true;
            is_bit_shift = true;
            bit_shift_type = bytes[0] & 0x02;

            byte_count += 2;
        
        } else if ((bytes[0] & 0xFC) == 0xD4) {
            const char* mnemonics[] = {
                "aam",
                "aad",
                "unused",
                "xlat",
            };
        
            u8 index = bytes[0] & 0x03;
            capture_instruction(address, "%s\n", mnemonics[index]);
            
            if (index >= 2) {
                i += 1;
            } else {
                i += 2;
            }
            
            continue;
            
        } else if ((bytes[0] & 0xFC) == 0xE0) {
            const char* mnemonics[] = {
                "loopnz",
                "loopz",
                "loop",
                "jcxz",
            };
        
            i += 2; 
            capture_jump_instruction(address, i + (char)bytes[1], mnemonics[bytes[0] & 0x03]);
            continue;
        
        } else if (bytes[0] >= 0xE4 && bytes[0] <= 0xE7) {
            op_text = (bytes[0] & 0x2) ? "out" : "in";
            op_class = OP_CLASS_IMMEDIATE_TO_ACCUMULATOR;
            
            extract_data = true;
            extract_word = false;
            
            word = bytes[0] & 0x01;
            byte_count += 1;
            
        } else if ((bytes[0] & 0xFE) == 0xE8) {
            op_text = (bytes[0] & 0x01) ? "jmp" : "call";
            short disp = bytes[1] | (bytes[2] << 8);
            short next_ip = address + 3 + disp;
            
            capture_instruction(address, "%s %ld\n", op_text, next_ip);
            i += 3;
            continue;
            
        } else if (bytes[0] == 0xEA) {
            u16 ip = bytes[1] | (bytes[2] << 8);
            u16 cs = bytes[3] | (bytes[4] << 8);

            capture_instruction(address, "jmp %lu:%lu\n", cs, ip);
            i += 5;
            continue;
            
        } else if (bytes[0] == 0xEB) {
            capture_instruction(address, "jmp %lld\n", (char)bytes[1]);
            i += 2;
            continue;
            
        } else if (bytes[0] >= 0xEC && bytes[0] <= 0xEF) {
            dir = bytes[0] & 0x02;
            op_text = dir ? "out" : "in";
            u8 word = bytes[0] & 0x01;

            if (dir) {
                capture_instruction(address, "%s dx, %s\n", op_text, word ? "ax" : "al");
            } else {
                capture_instruction(address, "%s %s, dx\n", op_text, word ? "ax" : "al");
            }
            
            i += 1;
            continue;
            
        } else if (bytes[0] == 0xF3) {
            if ((bytes[1] & 0xFE) == 0xAA) {
                op_text = "stos";
            } else if (bytes[1] & 0x08) {
                op_text = (bytes[1] & 0x02) ? "scas" : "lods";
            } else {
                op_text = (bytes[1] & 0x02) ? "cmps" : "movs";
            }
            
            word = bytes[1] & 0x01;
            capture_instruction(address, "rep %s%s\n", op_text, word ? "w" : "b");
            
            i += 2;
            continue;
        
        } else if ((bytes[0] & 0xFE) == 0xF4) {
            capture_instruction(address, "%s\n", (bytes[0] & 0x01) ? "cmc" : "hlt");
            i += 1;
            continue;
        
        } else if ((bytes[0] & 0xFE) == 0xF6) {
            const char* mnemonics[] = {
                "test",
                "unused",
                "not",
                "neg",
                "mul",
                "imul",
                "div",
                "idiv",
            };
            
            u8 op = (bytes[1] >> 3) & 0x07;
            
            if (op == 0) {
                extract_data = true;
                extract_word = word != 0;
                op_class = OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY;
            } else {
                op_class = OP_CLASS_REGISTER_MEMORY;
            }
            
            decode_register_memory = true;
            op_text = mnemonics[op];
            byte_count += 2;
        
        } else if (bytes[0] >= 0xF8 && bytes[0] <= 0xFD) {
            u8 op = bytes[0] & 0x07;
            
            const char* mnemonics[] = {
                "clc",
                "stc",
                "cli",
                "sti",
                "cld",
                "std"
            };
            
            op_text = mnemonics[op];
            capture_instruction(address, "%s\n", op_text);
            i += 1;
            continue;
        
        } else if ((bytes[0] & 0xFE) == 0xFE) {            
            const char* mnemonics[] = {
                "inc",
                "dec",
                "call",
                "call far",
                "jmp",
                "jmp far",
                "push"
            };
            
            op_class = OP_CLASS_REGISTER_MEMORY;
            decode_register_memory = true;
            op_text = mnemonics[(bytes[1] >> 3) & 0x07]; 
            byte_count += 2;
        }

        // nocheckin: not fully implemented.
        Register reg_address;  
        char address_operand[32];
        memset(address_operand, 0, 32);

        StringBuilder sb = {};
        sb.buffer = address_operand;
        
        if (decode_register_memory) {
            if (mode == MODE_REGISTER) {
                const char** reg_table = word ? register_map_word : register_map_byte;
                sb_appendf(&sb, reg_table[rm]); 
                
                int reg_index = rm;
                if (!word) {
                    reg_index = rm & 0x03;
                }
                
                reg_address = (Register)reg_index;
            
            } else {
                short displacement = 0;
                bool use_effective_address = true;
                
                if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                    displacement = bytes[2] | (bytes[3] << 8);
                    use_effective_address = false;
                    byte_count += 2;
            
                } else if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
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
                
                if (use_segment_override) {
                   sb_appendf(&sb, "%s:", segments[segment_override]);
                }
                
                sb_appendf(&sb, "[");
                
                if (use_effective_address) {
                    sb_appendf(&sb, effective_address_table[rm]);
                }
                
                if (displacement != 0) {
                    sb_appendf(&sb, " + %hd", displacement);
                }
                
                sb_appendf(&sb, "]");
            }
        }
        
        u16 data = 0;
        if (extract_data) {
            byte_count += extract_encoded_data(bytes, byte_count, extract_word, use_signed_immediate, &data);
        }
        
        switch (op_class) {
            case OP_CLASS_SEG_REG: {
                u8 sr = (bytes[1] >> 3) & 0x03;
                if (dir) {
                    capture_instruction(address, "%s %s, %s\n", op_text, segments[sr], address_operand);
                } else {
                    capture_instruction(address, "%s %s, %s\n", op_text, address_operand, segments[sr]);
                }
                i += byte_count;
                continue;
            } break;
            
            case OP_CLASS_IMMEDIATE: {
                const char* size_label = extract_word ? "word" : "byte";
                capture_instruction(address, "%s %s %hu\n", op_text, size_label, data);
                i += byte_count;
                continue;
            } break;
            
            case OP_CLASS_REGISTER_MEMORY: {            
                const char* size_label = word ? "word" : "byte";
                
                if (is_bit_shift) {
                    capture_instruction(address, "%s %s %s, %s\n", op_text, size_label, address_operand, bit_shift_type ? "cl" : "1");
                } else {
                    capture_instruction(address, "%s %s %s\n", op_text, size_label, address_operand);
                }
                
                i += byte_count;
                continue;
            } break;
                
            case OP_CLASS_REGISTER_MEMORY_AND_REGISTER: {
                u8 reg_byte = (bytes[1] & 0x38) >> 3; 
                
                // TODO(roger): decoder
                const char** reg_table = word ? register_map_word : register_map_byte; 
                const char* reg_operand = reg_table[reg_byte];
                const char* dest   = dir ? reg_operand : address_operand;
                const char* source = dir ? address_operand : reg_operand;
                
                capture_instruction(address, "%s %s, %s\n", instruction_strings[instruction_type], dest, source);
                
                // TODO(roger): sim  
                int reg_index = reg_byte;
                if (!word) {
                    reg_index = reg_index & 0x03;
                }
                Register reg = (Register)reg_index;
                
                Register reg_dest = dir ? reg : reg_address;
                Register reg_source = dir ? reg_address : reg;

                switch (instruction_type) {
                    case InstructionType_mov: {
                    
                        u16 previous_value = registers[reg_dest];
                    
                        if (word) {
                            registers[reg_dest] = registers[reg_source];
                        } else if (reg_byte & 0x04) {
                            // Set high bits of register.
                            registers[reg_dest] = (registers[reg_source] << 8) | (registers[reg_dest] & 0x00FF);
                        } else {
                            // Set low bits of register.
                            registers[reg_dest] = registers[reg_source] | (registers[reg_dest] & 0xFF00);
                        }
                    
                        printf("mov %s, %s ; %s:0x%01hx->0x%01hx\n", dest, source, register_map_word[reg_dest], previous_value, registers[reg_dest]);
                    
                    } break;
                    
                    // nocheckin
                    //default: not_implemented();
                }
    
                i += byte_count;
                continue;
            } break;
            
            case OP_CLASS_IMMEDIATE_TO_REGISTER_MEMORY: {
                const char* size_label = extract_word ? "word" : "byte";
                capture_instruction(address, "%s %s, %s %hu\n", op_text, address_operand, size_label, data);
    
                i += byte_count;
                continue;
            } break;
          
            case OP_CLASS_IMMEDIATE_TO_ACCUMULATOR: {
                const char* size_label = extract_word ? "word" : "byte";
                const char* reg = word ? "ax" : "al";
                
                if (dir) {
                    capture_instruction(address, "%s %s %hu, %s\n", op_text, size_label, data, reg);
                } else {
                    capture_instruction(address, "%s %s, %s %hu\n", op_text, reg, size_label, data);
                }
            
                i += byte_count;    
                continue;
            } break;
        }
        
        fputs("Unable to decode byte: ", stderr);
        print_byte(bytes[0], stderr);
        fputc('\n', stderr);
        ERROR_ABORT();
    }
    
    // TODO(roger): for simulate debug
    printf("\nFinal registers:\n");
    for (int i = 0; i < 8; i++) {
        printf("      %s: 0x%04hx (%hu)\n", register_map_word[i], registers[i], registers[i]);
    }
    printf("\n");
    
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

    int label_count = label_counter; 
    bubble_sort(label_addresses, label_count);
    
    label_counter = 0;
    printf("bits 16\n");
    for (int i = 0; i < instruction_count; i++) {
        if (label_counter < label_count && label_addresses[label_counter] == instructions[i].address) {
            printf("label_%d:\n", label_addresses[label_counter]);
            label_counter++;
        }
    
        printf(instructions[i].string);
        if (instructions[i].is_jump) {
            printf(" label_%d\n", instructions[i].jump_address);
        }
    }

    return 0;
}