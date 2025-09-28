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
#define OPCODE_MOV_IMMEDIATE_TO_REGISTER 0xB0

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

    printf("bits 16\n");

    for (int i = 0; i < file.size;) {
        u8* bytes = &file.buffer[i];
        
        if ((bytes[0] & 0b11111100) == OPCODE_MOV_REGISTER_OR_MEMORY_TO_FROM_REGISTER) {

            u8 word_mask   = 0x01;
            u8 dir_mask    = 0x02;
            u8 reg_mask    = 0x38;
            u8 rm_mask     = 0x07;
    
            const char** reg_table = (bytes[0] & word_mask) ? register_map_word : register_map_byte; 
            
            u8 dir  = bytes[0] & dir_mask;
            u8 mode = bytes[1] >> 6;
            u8 reg  = (bytes[1] & reg_mask) >> 3; 
            u8 rm   = bytes[1] & rm_mask;
            
            const char* effective_address = effective_address_table[rm];

            if (mode == MODE_REGISTER) {
                const char* r1 = reg_table[reg];
                const char* r2 = reg_table[rm];
                
                const char* dest   = dir ? r1 : r2;
                const char* source = dir ? r2 : r1;
            
                printf("mov %s, %s\n", dest, source);
                
            } else {
                short displacement = 0;
                char address_operand[32];
            
                if (mode == MODE_MEMORY_NO_DISPLACEMENT && rm == 6) {
                    not_implemented();
                
                } else {
                    if (mode == MODE_MEMORY_8_BIT_DISPLACEMENT) {
                        u8 sign = bytes[2] & 0x80;
                        if (sign) {
                            displacement = 0xFF00;
                        }
                        displacement |= bytes[2];
                        i += 1;
                    
                    } else if (mode == MODE_MEMORY_16_BIT_DISPLACEMENT) {
                        displacement = bytes[2] | (bytes[3] << 8);
                        i += 2;
                    }                    
                }
                
                if (displacement == 0) {
                    sprintf(address_operand, "[%s]", effective_address);
                } else {
                    sprintf(address_operand, "[%s + %hd]", effective_address, displacement);
                }
                
                const char* reg_operand = reg_table[reg];
                const char* dest   = dir ? reg_operand : address_operand;
                const char* source = dir ? address_operand : reg_operand;
                
                printf("mov %s, %s\n", dest, source);
            }
            
            i += 2;
            continue;
                        
        } else if ((bytes[0] & 0b11110000) == OPCODE_MOV_IMMEDIATE_TO_REGISTER) {
            u8 reg  = bytes[0] & 0x07;
            u8 word = bytes[0] & 0x08;

            const char** reg_table = word ? register_map_word : register_map_byte; 
            printf("mov %s, ", reg_table[reg]);
            
            if (word) {
                u16 data = bytes[1] | (bytes[2] << 8);
                printf("%hu\n", data);
                i += 3;
                
            } else {
                u8 data = bytes[1];
                printf("%hhu\n", data);
                i += 2;
            }
            
            continue;
        }
        
        fputs("Unable to decode byte: ", stderr);
        print_byte(bytes[0], stderr);
        fputc('\n', stderr);
        ERROR_ABORT();
    }

    return 0;
}