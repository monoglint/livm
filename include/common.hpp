#pragma once

#include <iostream>
#include <sstream>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <mutex>
#include <thread>
#include <bitset>
#include <set>
#include <memory>

#include "util.hpp"

void thread_safe_print(const std::string& string);

/*

CHUNK
    t_literal_id_MAX BYTES - Number of literals

        LITERAL
            1 byte  - Literal Size (bytes)
            x bytes - Literal binary

    x bytes - Opcodes

    1 byte - EOF - Mostly just so we can return something rather than undefined behavior if we move past EOF.
*/

/*

WARNING
This is "softcoded" with some considerations to keep in mind.
If any types here are changed, ensure that the method of reading bytes is updated in 'instructions.cpp'
*/

// By index, not size
constexpr auto THREAD_POOL_MAX = 64;
constexpr auto LOCAL_STACK_MAX = UINT8_MAX;
constexpr auto REGISTER_COUNT = UINT8_MAX;
constexpr auto LOCAL_LIST_MAX = UINT8_MAX;

// A chunk is a segment of bytecode. In this case it is what the ip will be swimming through.
using t_chunk = std::vector<uint8_t>;
using t_chunk_pos = uint32_t;

using t_register_value = uint64_t; // How many bytes a register takes up.
using t_register_list = std::array<t_register_value, REGISTER_COUNT + 1>;
using t_register_id = uint8_t;

// Note: This is not a literal type!! This is just the index of the literal in the constant pool/literal list.
using t_literal_list = std::vector<t_register_value>;
using t_literal_id = uint16_t;

using t_local_stack = std::vector<t_register_value>;
using t_local_id = uint16_t;

using t_heap = std::vector<uint8_t>;
using t_heap_address = uint32_t;

enum opcode {
                           // Assume args are unsigned unless explicitly said otherwise.
                           // ARGS -> SIDE EFFECTS

    OP_OUT,                // VALUE_TYPE: 8, SOURCE_REG: 8

    // Load a constant into a register.
    OP_COPY,               // REG: 8, LITERAL_ID: 16 -> REG = LITERAL == ARRAY ? &LITERAL_COPY : LITERAL_COPY

    OP_B_ADD,                // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_SUB,                // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_MUL,                // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_DIV,                // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_MORE,               // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_LESS,               // OPR_TYPE: 8, TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8
    OP_B_EQUAL,              // TARGET_REG: 8, OP_REG0: 8, OP_REG1: 8

    // really just a fun c reference, could just be alloc
    OP_MALLOC,               // TARGET_REG (stores location in heap of malloced data): 8, DATA_SIZE: 8
    OP_MFREE,                // POINTER_REG: 8, DATA_SIZE: 8 
    OP_MWRITE,               // POINTER_REG: 8, SOURCE_REG: 8, SIZE: 8
    OP_MREAD,                // POINTER_REG: 8, TARGET_REG: 8, SIZE: 8

    // Push the contents of the given register into the stack.
    OP_PUSH_LOCAL,           // SOURCE_REG: 8 

    // Copy a value from the local stack.
    OP_COPY_LOCAL,           // REG: 8, LOCAL_INDEX: T_LOCAL_ID_MAX

    // Push a new value to the stack frame.
    // Note: IP_OFFSET is based on the instruction location, not the ending arg byte of the instruction.
    OP_CALL,               // IP_OFFSET: i32, REG_RETURN_LOC: 8, ARG_COUNT: 8, REG_ARG: 8...?
                           // If REG_RETURN_LOC is 0, then no return value. Otherwise, the return reg is REG_RETURN_LOC - 1

    OP_DESYNC,             // IP_OFFSET: i32, ARG_COUNT: 8, REG_ARG: 8..?

    OP_RETURN,             // REG_RETURN_VAL: 8
                           // If caller expects no return value, this should just default to zero.
                           // Otherwise, the - 1 rule is not applied since it is not necessary.

    OP_JUMP_I8,            // IP_OFFSET: i8
    OP_JUMP_I16,           // IP_OFFSET: i16

    OP_JUMP_IF_FALSE,      // IP_OFFSET: i16, SOURCE_REG: 8

    OP_U_NOT,             // TARGET_REG: 8, SOURCE_REG: 8
    OP_U_NEG,              // TARGET_REG: 8, SOURCE_REG: 8
};

// Used for some opcode arguments.
enum value_type : uint8_t {
    VAL_NIL,
    VAL_PTR,
    VAL_BOOL,
    VAL_U8,
    VAL_U16,
    VAL_U32,
    VAL_U64,
    VAL_I8,
    VAL_I16,
    VAL_I32,
    VAL_I64,
    VAL_F32,
    VAL_F64,
};

struct call_frame {
    call_frame(const t_chunk_pos return_address, const t_chunk_pos return_value_reg)
        : return_address(return_address), return_value_reg(return_value_reg) {}

    t_register_list register_list;
    t_local_stack local_stack;
    
    const t_chunk_pos return_address;
    const t_register_id return_value_reg;

    inline void reg_copy_to(const t_register_id reg, const t_register_value value) {
        register_list[reg] = value;
    }

    inline const t_register_value reg_copy_from(const t_register_id reg) const {
        return register_list[reg];
    }
};

using t_call_stack = std::vector<call_frame>;
using t_call_frame_id = uint8_t;

// Execution of the thread must be done externally due to some declaration limitations.
struct run_thread {
    run_thread(const t_chunk& chunk)
        : chunk(chunk) {}

    const t_chunk& chunk;
    t_call_stack _call_stack;

    t_chunk_pos ip = 0;

    // Initialize the thread to be execution-ready. This includes creating a default entry point function.
    // Can be called after clean_up()
    inline void init(const t_chunk_pos start_pos) {
        std::lock_guard<std::mutex> lock(_empty_mutex);

        ip = start_pos;
        _call_stack.emplace_back(0, 0);

        _is_empty = false;
    }

    // Clean up the thread when it is done running to save memory. Marking this as clean will make it available for use if another thread is made.
    inline void clean_up() {
        std::lock_guard<std::mutex> lock(_empty_mutex);

        if (_is_empty)
            return;

        _is_empty = true;

        ip = 0;
        _call_stack.clear();
    }

    inline bool is_active() {
        std::lock_guard<std::mutex> lock(_empty_mutex);
        return !_is_empty;
    }

    inline uint8_t now() const {
        if (at_eof())
            return 0;
        
        return chunk[ip];
    }

    inline uint8_t next() {
        if (ip >= chunk.size())
            return 0;

        return chunk[ip++];
    }

    inline bool at_eof() const {
        return ip >= chunk.size();
    }

    inline call_frame& top_frame() {
        return _call_stack.back();
    }
private:
    bool _is_empty = false;
    std::mutex _empty_mutex;
};

using p_run_thread = std::unique_ptr<run_thread>;

using t_thread_pool = std::vector<p_run_thread>;
using t_thread_id = uint8_t;

struct run_state_initializer {
    t_chunk chunk;
    t_literal_list literal_list;

    t_chunk_pos ip = 0;

    // Use for traversing chunk to get literals.
    inline uint8_t next() {
        if (ip >= chunk.size())
            return 0;

        return chunk[ip++];
    }
};

struct run_state {
    run_state(run_state_initializer& initializer)
        : chunk(std::move(initializer.chunk)), literal_list(initializer.literal_list) {
            _thread_pool.reserve(THREAD_POOL_MAX);
        }
    
    const t_chunk chunk;
    const t_literal_list literal_list;

    inline t_register_value lit_copy_from(const t_literal_id literal) const {
        return literal_list[literal];
    }

    inline bool are_threads_depleted() {
        std::lock_guard<std::mutex> lock(_thread_pool_mutex);

        for (p_run_thread& thread : _thread_pool) {
            if (thread->is_active())
                return false;
        }

        return true;
    }

    // Can potentially recycle a previously finished thread just for memory efficiency.
    run_thread& spawn_thread(const t_chunk_pos start_pos);

    inline run_thread& get_thread(const t_thread_id thread_id) {
        std::lock_guard<std::mutex> lock(_thread_pool_mutex);
        return *_thread_pool[thread_id];
    }

    t_heap_address malloc(const uint8_t size);
    void mfree(const t_heap_address address, const uint8_t size);

    // 'bytes' is the number of bytes in the data that should be appended into the address space.
    void mwrite(const t_heap_address address, const t_register_value value, const uint8_t bytes);

    // Limit size from 0-7 to fit in t_register value
    t_register_value mread(const t_heap_address address, const uint8_t size);

private:
    struct _heap_selection {
        _heap_selection(const t_heap_address address, const uint8_t size)
            : address(address), size(size) {};

        t_heap_address address;
        uint8_t size;

        bool operator<(const _heap_selection& other) const {
            return address < other.address;
        }
    };

    t_heap _heap;
    std::mutex _heap_mutex;

    std::set<_heap_selection> _free_heap_space_set;
    std::mutex _free_heap_space_set_mutex;

    t_thread_pool _thread_pool;
    std::mutex _thread_pool_mutex;
};

static inline uint16_t _call_mergel_16(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
 
    return bit_util::mergel_16(b0, b1);
}

static inline uint32_t _call_mergel_32(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
    uint8_t b2 = chunk[ip++];
    uint8_t b3 = chunk[ip++];

    return bit_util::mergel_32(b0, b1, b2, b3);
}

static inline uint64_t _call_mergel_64(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
    uint8_t b2 = chunk[ip++];
    uint8_t b3 = chunk[ip++];
    uint8_t b4 = chunk[ip++];
    uint8_t b5 = chunk[ip++];
    uint8_t b6 = chunk[ip++];
    uint8_t b7 = chunk[ip++];

    return bit_util::mergel_64(b0, b1, b2, b3, b4, b5, b6, b7);
}

void instr_out(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_copy(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_add(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_sub(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_mul(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_div(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_more(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_less(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_equal(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_malloc(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_mfree(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_mwrite(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_mread(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_loc_push(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_loc_copy(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_call(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_desync(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_return(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_i8(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_i16(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_if_false(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_unary_not(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_unary_neg(run_state& state, run_thread& thread, call_frame& top_frame);

// Functions must carry the same order as their enum equiv
void (*const instruction_jump_table[])(run_state&, run_thread&, call_frame&) = { 
    instr_out, 
    instr_copy, 
    instr_binary_add, 
    instr_binary_sub, 
    instr_binary_mul, 
    instr_binary_div,
    instr_binary_more,
    instr_binary_less,
    instr_binary_equal, 
    instr_malloc,
    instr_mfree,
    instr_mwrite,
    instr_mread,
    instr_loc_push, 
    instr_loc_copy, 
    instr_call, 
    instr_desync,
    instr_return, 
    instr_jump_i8, 
    instr_jump_i16,
    instr_jump_if_false,
    instr_unary_not,
    instr_unary_neg,
};

void execute_thread(run_state& state, run_thread& thread);