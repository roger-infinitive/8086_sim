// TODO(roger): 8086 uses 1 mb for memory. Load file into this range. 
//      MEMORY_ACCESS_MASK can be used to prevent reading out of bounds.

#include "utility.h"

enum InstructionType {
    InstructionType_Undefined = 0,
    
    #define INSTRUCTION(inst) InstructionType_##inst, 
    #define INSTRUCTION_NAMED(inst, name) InstructionType_##inst, 
    #include "instructions.inc"
    #undef INSTRUCTION_NAMED
    #undef INSTRUCTION
};

const char* instruction_strings[] {
    "undefined",

    #define INSTRUCTION(inst) #inst,
    #define INSTRUCTION_NAMED(inst, name) name, 
    #include "instructions.inc"
    #undef INSTRUCTION_NAMED
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

enum SegmentRegister {
    SR_ES,
    SR_CS,
    SR_SS,
    SR_DS,
};

const char* segment_register_strings[] {
    "es",
    "cs",
    "ss",
    "ds"
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

enum OpEncoding {
    OP_ENCODING_NONE    = 0,
    OP_ENCODING_RM      = 1,
    OP_ENCODING_RM_R    = 2,
    OP_ENCODING_IMM     = 3,
    OP_ENCODING_IMM_RM  = 4,
    OP_ENCODING_IMM_ACC = 5,
    OP_ENCODING_SEG     = 6,
};

struct Instruction {
    int address;
    char* string;
    
    bool is_jump;
    int jump_address;
};

enum InstFlags : u32{
    InstFlags_Valid                = 1 << 0,
    InstFlags_DecodeRegisterMemory = 1 << 1,
    InstFlags_ExtractData          = 1 << 2,
    InstFlags_ExtractWord          = 1 << 3,
    InstFlags_RegisterWord         = 1 << 4,
    InstFlags_UseSignedImmediate   = 1 << 5,
    InstFlags_DecodeMnemonic       = 1 << 6,
    InstFlags_UseSegmentOverride   = 1 << 7,
};

// nocheckin: instead of booleans use flags
struct DecodedInstruction {
    u32 flags;

    InstructionType type;
    OpEncoding op_encoding;
    u8 reg_encoding;
    SegmentRegister segment_register;
    u8 rm_encoding;
    
    int decode_mnemonic_byte;
    int decode_mnemonic_bitshift;
    u8 decode_mnemonic_mask;
    const InstructionType* decode_mnemonic_table;
    
    int byte_offset;
};

char instruction_buffer[64];
int instruction_count;
Instruction instructions[4096];

const char* instruction_prefix = 0;

// CPU
u16 registers[8]; 
u16 segment_registers[4];

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

struct ProgramState {
    bool exec_enabled;
    const char* binary_file_path;
};

bool parse_args(int argc, char* argv[], ProgramState* state) {
    if (argc < 2) {
        printf("Usage: %s [-exec] <filename>\n", argv[0]);
        return false;
    }
    
    state->exec_enabled = false;
    state->binary_file_path = argv[argc-1];
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "exec") == 0) {
                state->exec_enabled = true;
            }
        }
    }
    
    return true;
}

DecodedInstruction decoded_table[256];

void InitializeDecodedInstructionTable() { 
    // nocheckin:
    
    DecodedInstruction* inst = &decoded_table[0x80];
    inst->flags = (InstFlags_Valid | InstFlags_DecodeRegisterMemory | InstFlags_ExtractData | InstFlags_DecodeMnemonic);
    inst->op_encoding = OP_ENCODING_IMM_RM;
    inst->decode_mnemonic_byte = 1;
    inst->decode_mnemonic_bitshift = 3;
    inst->decode_mnemonic_mask = 0x07;
    inst->decode_mnemonic_table = &group_one_mnemonics[0];
    inst->byte_offset = 2;
}

int main(int argc, char* argv[]) {
    InitializeDecodedInstructionTable();

    ProgramState program_state = {};
    if (!parse_args(argc, argv, &program_state)) {
        return 1;
    }

    init_arena(&main_arena, 32*1024*1024);
    
    MemoryBuffer file = {};
    if (!read_entire_file(&file, program_state.binary_file_path, main_arena_alloc)) {
        return 1;
    }
    
    for (int i = 0; i < file.size;) {
        int address = i;
        u8* bytes = &file.buffer[i];
        
        bool is_bit_shift = false;
        u8 bit_shift_type = 0;

        bool use_lock = false;
                
        // nocheckin: not fully implemented.
        char address_operand[32];
        memset(address_operand, 0, 32);

        StringBuilder address_operand_sb = {};
        address_operand_sb.buffer = address_operand;
        
        if (bytes[0] == 0xF0) {
            instruction_prefix = "lock ";
            use_lock = true;
            i += 1;
            bytes = &file.buffer[i];
        }
        
        bool use_segment_override = false;
        SegmentRegister segment_register;
        switch (bytes[0]) {
            case 0x26:
            case 0x2E:
            case 0x36:
            case 0x3E: {
                use_segment_override = true;
                segment_register = (SegmentRegister)(bytes[0] >> 3 & 0x03);
                i += 1;
                bytes = &file.buffer[i];
            } break;
        }
        
        int byte_count = 0;
        
        DecodedInstruction decoded = {};
        if ((bytes[0] & 0x01) != 0) {
            decoded.flags |= InstFlags_RegisterWord;
        } else {
            decoded.flags &= ~InstFlags_RegisterWord;
        }
        u8 mode = bytes[1] >> 6;
        decoded.rm_encoding = bytes[1] & 0x07;
        u8 dir  = bytes[0] & 0x02;
        if (use_segment_override) {
            decoded.flags |= InstFlags_UseSegmentOverride;
            decoded.segment_register = segment_register;
        }
        
        if (decoded_table[bytes[0]].flags & InstFlags_Valid) {
            decoded = decoded_table[bytes[0]];
            decoded.rm_encoding = bytes[1] & 0x07;
            if (use_segment_override) {
                decoded.flags |= InstFlags_UseSegmentOverride;
                decoded.segment_register = segment_register;
            }
            
            if (decoded.flags & InstFlags_DecodeMnemonic) {
                u8 index = (bytes[decoded.decode_mnemonic_byte] >> decoded.decode_mnemonic_bitshift) & decoded.decode_mnemonic_mask;
                decoded.type = group_one_mnemonics[index];
            }
            
            byte_count += decoded.byte_offset;
            
        } else if (bytes[0] >= 0x00 && bytes[0] <= 0x3F) {
            if ((bytes[0] & 0x06) == 0x06) {
                if ((bytes[0] & 0xF0) <= 0x10) {
                    decoded.type = (bytes[0] & 0x01) ? InstructionType_pop : InstructionType_push;
                    decoded.segment_register = (SegmentRegister)((bytes[0] >> 3) & 0x03);
                    
                    capture_instruction(address, "%s %s\n", instruction_strings[decoded.type], segment_register_strings[decoded.segment_register]);
                    i += 1;
                    goto finish_instruction;
                    
                } else if (bytes[0] & 0x07 == 0x07) {
                    switch ((bytes[0] >> 3) & 0x03) {
                        case 0: decoded.type = InstructionType_daa; break;
                        case 1: decoded.type = InstructionType_das; break;
                        case 2: decoded.type = InstructionType_aaa; break;
                        case 3: decoded.type = InstructionType_aas; break;
                    }
                
                    capture_instruction(address, "%s\n", instruction_strings[decoded.type]);
                    i += 1;
                    goto finish_instruction;

                }
                
            } else { 
                decoded.type = group_one_mnemonics[((bytes[0] >> 3) & 0x07)];
                if (bytes[0] & 0x04) {
                    decoded.op_encoding = OP_ENCODING_IMM_ACC; 
                    decoded.flags |= InstFlags_ExtractData;
                    if (decoded.flags & InstFlags_RegisterWord) {
                        decoded.flags |= InstFlags_ExtractWord;
                    }
                    byte_count += 1;
                } else {
                    decoded.op_encoding = OP_ENCODING_RM_R;
                    decoded.flags |= InstFlags_DecodeRegisterMemory;
                    byte_count += 2;
                }
            }
            
        } else if (bytes[0] >= 0x40 && bytes[0] <= 0x5F) {
            const char* reg = register_map_word[bytes[0] & 0x07];
            
            InstructionType mnemonics[] = {
                InstructionType_inc,
                InstructionType_dec,
                InstructionType_push,
                InstructionType_pop,
            };
            
            decoded.type = mnemonics[(bytes[0] >> 3) & 0x03];
            capture_instruction(address, "%s %s\n", instruction_strings[decoded.type], reg);
            
            i += 1;
            goto finish_instruction;
            
        } else if ((bytes[0] & 0xF0) == 0x70) {
            InstructionType mnemonics[] = {
                InstructionType_jo,
                InstructionType_jno,
                InstructionType_jb,
                InstructionType_jnb,
                InstructionType_je,
                InstructionType_jne,
                InstructionType_jbe,
                InstructionType_jnbe,
                InstructionType_js,
                InstructionType_jns,
                InstructionType_jp,
                InstructionType_jnp,
                InstructionType_jl,
                InstructionType_jnl,
                InstructionType_jle,
                InstructionType_jnle
            };
            
            decoded.type = mnemonics[bytes[0] & 0x0F];
            
            i += 2; // move forward before capturing target address.
            capture_jump_instruction(address, i + (char)bytes[1], instruction_strings[decoded.type]);
            goto finish_instruction;
        
        } else if (bytes[0] >= 0x81 && bytes[0] <= 0x83) {
            decoded.op_encoding = OP_ENCODING_IMM_RM;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.flags |= InstFlags_ExtractData;
            if (decoded.flags & InstFlags_RegisterWord) {
                decoded.flags |= InstFlags_ExtractWord; 
            }
            decoded.type = group_one_mnemonics[((bytes[1] >> 3) & 0x07)];
            
            if (bytes[0] == 0x83) {
                decoded.flags |= InstFlags_UseSignedImmediate;
            } else if ((bytes[0] & 0b00000010) != 0) {
                decoded.flags |= InstFlags_UseSignedImmediate;
            }
            
            byte_count += 2;
            
        } else if (bytes[0] >= 0x84 && bytes[0] <= 0x87) {
            const InstructionType mnemonics[] = {
                InstructionType_test,
                InstructionType_test,
                InstructionType_xchg,
                InstructionType_xchg
            };
            
            decoded.type = mnemonics[(bytes[0] & 0x0F) - 0x04];
            decoded.op_encoding = OP_ENCODING_RM_R;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            
            if (use_lock) {
                dir = 0;
            } else {
                dir = bytes[0] & 0x02;
            }
            
            byte_count += 2;
        
        } else if (bytes[0] >= 0x88 && bytes[0] <= 0x8B) {
            decoded.op_encoding = OP_ENCODING_RM_R;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = InstructionType_mov;
            byte_count += 2;
            
        } else if (bytes[0] == 0x8C || bytes[0] == 0x8E) {
            decoded.op_encoding = OP_ENCODING_SEG;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            dir = bytes[0] & 0x02;
            decoded.flags |= InstFlags_RegisterWord;
            decoded.type = InstructionType_mov;
            byte_count += 2;
        
        } else if (bytes[0] == 0x8D) {
            decoded.op_encoding = OP_ENCODING_RM_R;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = InstructionType_lea;
            dir = 1;
            byte_count += 2;
        
        } else if (bytes[0] == 0x8F) {
            decoded.op_encoding = OP_ENCODING_RM;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = InstructionType_pop;
            byte_count += 2;
            
        } else if (bytes[0] >= 0x90 && bytes[0] <= 0x97) {
            capture_instruction(address, "xchg ax, %s\n", register_map_word[bytes[0] & 0x0F]);
            i += 1;
            goto finish_instruction;
            
        } else if ((bytes[0] & 0xFE) == 0x98) { 
            capture_instruction(address, "%s\n", (bytes[0] & 0x01) ? "cwd" : "cbw");
            i += 1;
            goto finish_instruction;
        
        } else if (bytes[0] == 0x9A) {
            u16 displacement = bytes[1] | (bytes[2] << 8);
            u16 seg = bytes[3] | (bytes[4] << 8);

            capture_instruction(address, "call %lu:%lu\n", seg, displacement);
            i += 5;
            goto finish_instruction;
        
        } else if (bytes[0] == 0x9B) { 
            capture_instruction(address, "wait\n");
            i += 1;
            goto finish_instruction;
        
        } else if ((bytes[0] & 0xFC) == 0x9C) {
            const char* mnemonics[] = {
                "pushf",
                "popf",
                "sahf",
                "lahf",
            };
            
            capture_instruction(address, "%s\n", mnemonics[bytes[0] & 0x03]);
            i += 1;
            goto finish_instruction;
        
        } else if ((bytes[0] & 0xFC) == 0xA0) {
            u16 address = bytes[1] | bytes[2] << 8;
            if (dir) {
                capture_instruction(address, "mov [%hd], %s\n", address, (decoded.flags & InstFlags_RegisterWord) ? "ax" : "al");
            } else {
                capture_instruction(address, "mov %s, [%hd]\n", (decoded.flags & InstFlags_RegisterWord) ? "ax" : "al", address);
            }
            i += 3;
            goto finish_instruction;
            
        } else if ((bytes[0] & 0xFE) == 0xA8) {
            decoded.flags |= InstFlags_ExtractData;
            if (decoded.flags & InstFlags_RegisterWord) {
                decoded.flags |= InstFlags_ExtractWord; 
            }
            decoded.op_encoding = OP_ENCODING_IMM_ACC;
            decoded.type = InstructionType_test;
            byte_count += 1;
        
        } else if ((bytes[0] & 0xFC) == 0xA4 || (bytes[0] & 0xFC) == 0xAC) {
            if (bytes[0] & 0x08) {
                decoded.type = (bytes[0] & 0x02) ? InstructionType_lods : InstructionType_scas;
            } else {
                decoded.type = (bytes[0] & 0x02) ? InstructionType_movs : InstructionType_cmps;
            }
            
            capture_instruction(address, "%s%s\n", instruction_strings[decoded.type], (decoded.flags & InstFlags_RegisterWord) ? "w" : "b");
            
            i += 1;
            goto finish_instruction;
            
        } else if ((bytes[0] & 0xF0) == 0xB0) {
            decoded.type = InstructionType_mov;
            decoded.op_encoding = OP_ENCODING_IMM_RM; 
            decoded.rm_encoding = bytes[0] & 0x07; 
            if ((bytes[0] & 0x08) != 0) {
                decoded.flags |= InstFlags_RegisterWord;
            } else {
                decoded.flags &= ~InstFlags_RegisterWord;
            }
            
            decoded.flags |= InstFlags_ExtractData;
            if (decoded.flags & InstFlags_RegisterWord) {
                decoded.flags |= InstFlags_ExtractWord; 
            }
            
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            mode = MODE_REGISTER;
        
            byte_count += 1;
        
        } else if ((bytes[0] & 0xFE) == 0xC2) {
            u8 no_immediate = bytes[0] & 0x01;
            if (no_immediate) {
                capture_instruction(address, "ret\n");
                i += 1;
                goto finish_instruction;
                
            } else {
                decoded.flags |= InstFlags_ExtractData;
                decoded.flags |= InstFlags_ExtractWord;
                byte_count += 1;
                decoded.type = InstructionType_ret;
                decoded.op_encoding = OP_ENCODING_IMM;
            }
        
        } else if ((bytes[0] & 0xFE) == 0xC4) {
            decoded.op_encoding = OP_ENCODING_RM_R;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = (bytes[0] & 0x01) ? InstructionType_lds : InstructionType_les;
            decoded.flags |= InstFlags_RegisterWord;
            dir = 1;
            byte_count += 2;
        
        } else if ((bytes[0] & 0xFE) == 0xC6) {
            decoded.op_encoding = OP_ENCODING_IMM_RM;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.flags |= InstFlags_ExtractData;
            if (decoded.flags & InstFlags_RegisterWord) {
                decoded.flags |= InstFlags_ExtractWord; 
            }
            decoded.type = InstructionType_mov;
            byte_count += 2;
            
        } else if ((bytes[0] & 0xFE) == 0xCA) {
            if (bytes[0] & 0x01) {
                capture_instruction(address, "retf\n");
                i += 1;
                goto finish_instruction;
            } else {
                decoded.flags |= InstFlags_ExtractData;
                decoded.flags |= InstFlags_ExtractWord;
                byte_count += 1;
                decoded.type = InstructionType_retf;
                decoded.op_encoding = OP_ENCODING_IMM;
            }
            
        } else if ((bytes[0] & 0xFC) == 0xCC) {
            InstructionType mnemonics[] = {
                InstructionType_int3,
                InstructionType_int,
                InstructionType_into,
                InstructionType_iret
            };
        
            u8 op = bytes[0] & 0x03;
            decoded.type = mnemonics[op];
            
            if (op == 1) {
                decoded.flags |= InstFlags_ExtractData;
                decoded.op_encoding = OP_ENCODING_IMM;
                byte_count += 1;
            } else {
                capture_instruction(address, "%s\n", instruction_strings[decoded.type]);
                i += 1;
                goto finish_instruction;
            }
        
        } else if (bytes[0] >= 0xD0 && bytes[0] <= 0xD3) { 
            InstructionType mnemonics[] = {
                InstructionType_rol,
                InstructionType_ror,
                InstructionType_rcl,
                InstructionType_rcr,
                InstructionType_shl, // "sal"
                InstructionType_shr,
                InstructionType_Undefined,
                InstructionType_sar
            };
            
            decoded.type = mnemonics[(bytes[1] >> 3) & 0x07];
            decoded.op_encoding = OP_ENCODING_RM;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            is_bit_shift = true;
            bit_shift_type = bytes[0] & 0x02;

            byte_count += 2;
        
        } else if ((bytes[0] & 0xFC) == 0xD4) {
            InstructionType mnemonics[] = {
                InstructionType_aam,
                InstructionType_aad,
                InstructionType_Undefined,
                InstructionType_xlat,
            };
            
            u8 index = bytes[0] & 0x03;
            decoded.type = mnemonics[index];
            capture_instruction(address, "%s\n", instruction_strings[decoded.type]);
            
            if (index >= 2) {
                i += 1;
            } else {
                i += 2;
            }
            
            goto finish_instruction;
            
        } else if ((bytes[0] & 0xFC) == 0xE0) {
            const char* mnemonics[] = {
                "loopnz",
                "loopz",
                "loop",
                "jcxz",
            };
        
            i += 2; 
            capture_jump_instruction(address, i + (char)bytes[1], mnemonics[bytes[0] & 0x03]);
            goto finish_instruction;
        
        } else if (bytes[0] >= 0xE4 && bytes[0] <= 0xE7) {
            decoded.type = (bytes[0] & 0x2) ? InstructionType_out : InstructionType_in;
            decoded.op_encoding = OP_ENCODING_IMM_ACC;
            
            decoded.flags |= InstFlags_ExtractData;
            
            byte_count += 1;
            
        } else if ((bytes[0] & 0xFE) == 0xE8) {
            decoded.type = (bytes[0] & 0x01) ? InstructionType_jmp : InstructionType_call;
            short disp = bytes[1] | (bytes[2] << 8);
            short next_ip = address + 3 + disp;
            
            capture_instruction(address, "%s %ld\n", instruction_strings[decoded.type], next_ip);
            i += 3;
            goto finish_instruction;
            
        } else if (bytes[0] == 0xEA) {
            u16 ip = bytes[1] | (bytes[2] << 8);
            u16 cs = bytes[3] | (bytes[4] << 8);

            capture_instruction(address, "jmp %lu:%lu\n", cs, ip);
            i += 5;
            goto finish_instruction;
            
        } else if (bytes[0] == 0xEB) {
            capture_instruction(address, "jmp %lld\n", (char)bytes[1]);
            i += 2;
            goto finish_instruction;
            
        } else if (bytes[0] >= 0xEC && bytes[0] <= 0xEF) {
            dir = bytes[0] & 0x02;
            decoded.type = dir ? InstructionType_out : InstructionType_in;

            char* reg = (decoded.flags & InstFlags_RegisterWord) ? "ax" : "al";
            if (dir) {
                capture_instruction(address, "%s dx, %s\n", instruction_strings[decoded.type], reg);
            } else {
                capture_instruction(address, "%s %s, dx\n", instruction_strings[decoded.type], reg);
            }
            
            i += 1;
            goto finish_instruction;
            
        } else if (bytes[0] == 0xF3) {
            if ((bytes[1] & 0xFE) == 0xAA) {
                decoded.type = InstructionType_stos;
            } else if (bytes[1] & 0x08) {
                decoded.type = (bytes[1] & 0x02) ? InstructionType_scas : InstructionType_lods;
            } else {
                decoded.type = (bytes[1] & 0x02) ? InstructionType_cmps : InstructionType_movs;
            }
            
            if ((bytes[1] & 0x01) != 0) {
                decoded.flags |= InstFlags_RegisterWord;
            } else {
                decoded.flags &= ~InstFlags_RegisterWord;
            }
            capture_instruction(address, "rep %s%s\n", instruction_strings[decoded.type], (decoded.flags & InstFlags_RegisterWord) ? "w" : "b");
            
            i += 2;
            goto finish_instruction;
        
        } else if ((bytes[0] & 0xFE) == 0xF4) {
            decoded.type = (bytes[0] & 0x01) ? InstructionType_cmc : InstructionType_hlt;
            capture_instruction(address, "%s\n", instruction_strings[decoded.type]);
            i += 1;
            goto finish_instruction;
        
        } else if ((bytes[0] & 0xFE) == 0xF6) {
            InstructionType mnemonics[] = {
                InstructionType_test,
                InstructionType_Undefined,
                InstructionType_not,
                InstructionType_neg,
                InstructionType_mul,
                InstructionType_imul,
                InstructionType_div,
                InstructionType_idiv,
            };
            
            u8 op = (bytes[1] >> 3) & 0x07;
            
            if (op == 0) {
                decoded.flags |= InstFlags_ExtractData;
                if (decoded.flags & InstFlags_RegisterWord) {
                    decoded.flags |= InstFlags_ExtractWord;
                }
                decoded.op_encoding = OP_ENCODING_IMM_RM;
            } else {
                decoded.op_encoding = OP_ENCODING_RM;
            }
            
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = mnemonics[op];
            byte_count += 2;
        
        } else if (bytes[0] >= 0xF8 && bytes[0] <= 0xFD) {
            u8 op = bytes[0] & 0x07;
            
            InstructionType mnemonics[] = {
                InstructionType_clc,
                InstructionType_stc,
                InstructionType_cli,
                InstructionType_sti,
                InstructionType_cld,
                InstructionType_std
            };
            
            decoded.type = mnemonics[op];
            capture_instruction(address, "%s\n", instruction_strings[decoded.type]);
            i += 1;
            goto finish_instruction;
        
        } else if ((bytes[0] & 0xFE) == 0xFE) {            
            InstructionType mnemonics[] = {
                InstructionType_inc,
                InstructionType_dec,
                InstructionType_call,
                InstructionType_call_far,
                InstructionType_jmp,
                InstructionType_jmp_far,
                InstructionType_push
            };
            
            decoded.op_encoding = OP_ENCODING_RM;
            decoded.flags |= InstFlags_DecodeRegisterMemory;
            decoded.type = mnemonics[(bytes[1] >> 3) & 0x07]; 
            byte_count += 2;
        }
        
        // nocheckin
        if (decoded.flags & InstFlags_DecodeRegisterMemory) {
            if (mode == MODE_REGISTER) {
                const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte;
                sb_appendf(&address_operand_sb, reg_table[decoded.rm_encoding]); 
            } else {
                short displacement = 0;
                bool use_effective_address = true;
                
                if (mode == MODE_MEMORY_NO_DISPLACEMENT && decoded.rm_encoding == 6) {
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
                
                if (decoded.flags & InstFlags_UseSegmentOverride) {
                   sb_appendf(&address_operand_sb, "%s:", segment_register_strings[decoded.segment_register]);
                }
                
                sb_appendf(&address_operand_sb, "[");
                
                if (use_effective_address) {
                    sb_appendf(&address_operand_sb, effective_address_table[decoded.rm_encoding]);
                }
                
                if (displacement != 0) {
                    sb_appendf(&address_operand_sb, " + %hd", displacement);
                }
                
                sb_appendf(&address_operand_sb, "]");
            }
        }
        
        u16 data = 0;
        if (decoded.flags & InstFlags_ExtractData) {
            byte_count += extract_encoded_data(bytes, byte_count, decoded.flags & InstFlags_ExtractWord, decoded.flags & InstFlags_UseSignedImmediate, &data);
        }
        
        switch (decoded.op_encoding) {
            case OP_ENCODING_SEG: {
                decoded.segment_register = (SegmentRegister)((bytes[1] >> 3) & 0x03);                   
                i += byte_count;
                goto finish_instruction;
            } break;
            
            case OP_ENCODING_IMM: {
                const char* size_label = (decoded.flags & InstFlags_ExtractWord) ? "word" : "byte";
                capture_instruction(address, "%s %s %hu\n", instruction_strings[decoded.type], size_label, data);
                i += byte_count;
                goto finish_instruction;
            } break;
            
            case OP_ENCODING_RM: {            
                const char* size_label = (decoded.flags & InstFlags_RegisterWord) ? "word" : "byte";
                
                if (is_bit_shift) {
                    capture_instruction(address, "%s %s %s, %s\n", instruction_strings[decoded.type], size_label, address_operand, bit_shift_type ? "cl" : "1");
                } else {
                    capture_instruction(address, "%s %s %s\n", instruction_strings[decoded.type], size_label, address_operand);
                }
                
                i += byte_count;
                goto finish_instruction;
            } break;
                
            case OP_ENCODING_RM_R: {
                decoded.reg_encoding = (bytes[1] & 0x38) >> 3;                
                i += byte_count;
                goto finish_instruction;
            } break;
            
            case OP_ENCODING_IMM_RM: {
                const char* size_label = (decoded.flags & InstFlags_ExtractWord) ? "word" : "byte";
                capture_instruction(address, "%s %s, %s %hu\n", instruction_strings[decoded.type], address_operand, size_label, data);
    
                i += byte_count;
                goto finish_instruction;
            } break;
          
            case OP_ENCODING_IMM_ACC: {
                const char* size_label = (decoded.flags & InstFlags_ExtractWord) ? "word" : "byte";
                const char* reg = (decoded.flags & InstFlags_RegisterWord) ? "ax" : "al";
                
                if (dir) {
                    capture_instruction(address, "%s %s %hu, %s\n", instruction_strings[decoded.type], size_label, data, reg);
                } else {
                    capture_instruction(address, "%s %s, %s %hu\n", instruction_strings[decoded.type], reg, size_label, data);
                }
            
                i += byte_count;    
                goto finish_instruction;
            } break;
        }
        
        fputs("Unable to decode byte: ", stderr);
        print_byte(bytes[0], stderr);
        fputc('\n', stderr);
        ERROR_ABORT();
    
        // nocheckin: temporary as we refactor the decoder into a better state.
        finish_instruction:
        
        if (program_state.exec_enabled) {
            switch (decoded.op_encoding) {
                case OP_ENCODING_SEG: {    
                    u16 previous_value;
                    u16* dest_register;
                    u16* src_register;
                    
                    Register rm_reg = (Register)(decoded.rm_encoding & 0x03);
                    
                    if (dir) {
                        dest_register = &segment_registers[decoded.segment_register]; 
                        src_register = &registers[rm_reg]; 
                    } else {
                        dest_register = &registers[rm_reg];
                        src_register = &segment_registers[decoded.segment_register]; 
                    }
                    
                    previous_value = *dest_register;
                    *dest_register = *src_register;

                    const char* sr_string = segment_register_strings[decoded.segment_register];
                    const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte;
                    const char* reg_string = reg_table[decoded.rm_encoding];
                    const char* dest = dir ? sr_string : reg_string;
                    const char* source = dir ? reg_string : sr_string;

                    printf("%s %s, %s ; %s:0x%01hx->0x%01hx\n", instruction_strings[decoded.type], dest, source, dest, previous_value, *dest_register);
                } break;
                
                case OP_ENCODING_RM_R: {
                    u8 dest_encoding = dir ? decoded.reg_encoding : decoded.rm_encoding;
                    u8 src_encoding  = dir ? decoded.rm_encoding : decoded.reg_encoding;

                    Register dest_reg = (Register)(dest_encoding & 0x03);
                    Register src_reg = (Register)(src_encoding & 0x03);

                    switch (decoded.type) {
                        case InstructionType_mov: {
                            u16 previous_value = registers[dest_reg];
                        
                            if (decoded.flags & InstFlags_RegisterWord) {
                                registers[dest_reg] = registers[src_reg];
                            } else {
                                u16 src_value = 0;
                                if (src_encoding <= 3) {
                                    src_value = registers[src_reg] & 0x00FF;
                                } else {
                                    src_value = registers[src_reg] >> 8;
                                }
                                
                                if (dest_encoding <= 3) {
                                    registers[dest_reg] = src_value | (registers[dest_reg] & 0xFF00); 
                                } else {
                                    registers[dest_reg] = (src_value << 8) | (registers[dest_reg] & 0x00FF); 
                                }
                            }
                        
                            const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte; 
                            const char* reg_operand = reg_table[decoded.reg_encoding];
                            const char* rm_operand = reg_table[decoded.rm_encoding];
                            const char* dest = dir ? reg_operand : rm_operand;
                            const char* source = dir ? rm_operand : reg_operand;
                                    
                            printf("mov %s, %s ; %s:0x%01hx->0x%01hx\n", dest, source, register_map_word[dest_reg], previous_value, registers[dest_reg]);
                        } break;
                        
                        // nocheckin
                        //default: not_implemented();
                    }
                } break;
                
                case OP_ENCODING_IMM_RM: {
                    Register reg = (Register)(decoded.rm_encoding & 0x03);
                    u16 previous_value = registers[reg];
                    
                    if (decoded.flags & InstFlags_RegisterWord) {
                        registers[reg] = data;
                    } else if (decoded.rm_encoding <= 3) {
                        // Set high bits of register.
                        registers[reg] = (data << 8) | (registers[reg] & 0x00FF);
                    } else {
                        // Set low bits of register.
                        registers[reg] = data | (registers[reg] & 0xFF00);
                    }
                    
                    const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte; 
                    const char* reg_label = reg_table[reg];
                    
                    printf("mov %s, %hu ; %s:0x%01hx->0x%01hx\n", reg_label, data, register_map_word[reg], previous_value, registers[reg]);
                } break;
            }
        } else {
            // nocheckin: right now we do not capture the effective address in the decoded instruction, so we have to use the crappy address_operand string.
            switch (decoded.op_encoding) {
                case OP_ENCODING_SEG: {
                    const char* sr_string = segment_register_strings[decoded.segment_register];
                    const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte;
                    const char* reg_string = address_operand;
                    const char* dest = dir ? sr_string : reg_string;
                    const char* source = dir ? reg_string : sr_string;
                    
                    capture_instruction(address, "%s %s, %s\n", instruction_strings[decoded.type], dest, source);
                } break;
                
                case OP_ENCODING_RM_R: {
                    const char** reg_table = (decoded.flags & InstFlags_RegisterWord) ? register_map_word : register_map_byte; 
                    const char* reg_operand = reg_table[decoded.reg_encoding];
                    const char* dest   = dir ? reg_operand : address_operand;
                    const char* source = dir ? address_operand : reg_operand;
                    
                    capture_instruction(address, "%s %s, %s\n", instruction_strings[decoded.type], dest, source);
                } break;
            }
        }
    }
    
    // nocheckin:
    // DecodeInstruction instruction = {};
    // while (NextInstruction(&instruction)) {
    //  
    // }
    
    if (program_state.exec_enabled) {
        printf("\nFinal registers:\n");
        for (int i = 0; i < 8; i++) {
            printf("      %s: 0x%04hx (%hu)\n", register_map_word[i], registers[i], registers[i]);
        }
        for (int i = 0; i < 4; i++) {
            printf("      %s: 0x%04hx (%hu)\n", segment_register_strings[i], segment_registers[i], segment_registers[i]);
        }
        printf("\n");
    }
    
    // Labels
    int label_counter = 0;
    int label_addresses[1024];
    memset(label_addresses, 0, 1024 * sizeof(int)); 
    
    for (int i = 0; i < instruction_count; i++) {
        if (!instructions[i].is_jump) {
            continue;
        }
    
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