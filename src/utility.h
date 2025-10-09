#include <stdio.h>
#include <stdarg.h>
#include <cstdint>
#include <errno.h>
#include <string.h>

#define u8  uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

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

struct StringBuilder {
    char* buffer;
    u32 length;
};

void sb_appendf(StringBuilder* builder, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    builder->length += vsprintf(builder->buffer + builder->length, fmt, ap);
    va_end(ap);
}

void bubble_sort(int arr[], int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

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