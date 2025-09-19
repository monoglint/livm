#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <vector>
#include <memory>
#include <variant>
#include <array>
#include <string>
#include <bitset>

#include "util.hpp"

/*

CHUNK
    t_literal_id_MAX BYTES - Number of literals

        LITERAL
            1 byte  - Literal Size (bytes)
            x bytes - Literal binary

    x bytes - Opcodes

    1 byte - EOF - Mostly just so we can return something rather than undefined behavior if we move past EOF.
*/

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

enum opcode {
                           // Assume args are unsigned unless explicitly said otherwise.
                           // ARGS -> SIDE EFFECTS

    OP_EOF, // Must be index 0

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

    // Push the contents of the given register into the stack.
    OP_PUSH_LOCAL,           // SOURCE_REG: 8 

    // Copy a value from the local stack.
    OP_COPY_LOCAL,           // REG: 8, LOCAL_INDEX: T_LOCAL_ID_MAX

    // Push a new value to the stack frame.
    // Note: IP_OFFSET is based on the instruction location, not the ending arg byte of the instruction.
    OP_CALL,               // IP_OFFSET: i32, REG_RETURN_LOC: 8, ARG_COUNT: 8, REG_ARG: 8...?
                           // If REG_RETURN_LOC is 0, then no return value. Otherwise, the return reg is REG_RETURN_LOC - 1

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
    run_thread(const t_chunk& chunk, const t_chunk_pos start_pos, const t_chunk_pos return_address = 0, const t_chunk_pos return_value_reg = 0)
        : chunk(chunk), ip(start_pos) {
            init(start_pos, return_address, return_value_reg);
        }

    const t_chunk& chunk;

    t_chunk_pos ip = 0;

    // Initialize the thread to be execution-ready. This includes creating a default entry point function.
    // Can be called after clean_up()
    inline void init(const t_chunk_pos start_pos, const t_chunk_pos return_address = 0, const t_chunk_pos return_value_reg = 0) {
        ip = start_pos;
        _call_stack.emplace_back(return_address, return_value_reg);

        _is_cleaned = false;
    }

    // Clean up the thread when it is done running to save memory. Marking this as clean will make it available for use if another thread is made.
    inline void clean_up() {
        if (_is_cleaned)
            return;

        _is_cleaned = true;

        ip = 0;
        _call_stack.clear();
    }

    // When called, the run thread
    inline bool is_active() {
        if (!at_eof() && _call_stack.size() != 0)
            return true;

        clean_up();
        return false;
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
        return ip >= chunk.size() - 1;
    }

    inline call_frame& top_frame() {
        return _call_stack.back();
    }

    inline call_frame& emplace_call_frame(call_frame& frame) {
        return _call_stack.emplace_back(std::move(frame));
    }

    inline call_frame& emplace_call_frame(call_frame&& frame) {
        return _call_stack.emplace_back(std::move(frame));
    }

    inline void pop_call_frame() {
        _call_stack.pop_back();

        if (_call_stack.empty())
            clean_up();
    }
    
    inline call_frame& get_call_stack_index(const t_call_frame_id index) {
        return _call_stack[index];
    }

    inline call_frame& get_call_stack_index_by_deepness(const t_call_frame_id deepness) {
        return _call_stack[_call_stack.size() - 1 - deepness];
    }
private:
    t_call_stack _call_stack;
    bool _is_cleaned = false;
};

using t_thread_list = std::vector<run_thread>;
using t_thread_id = uint8_t;

struct run_state {
    t_chunk chunk;

    t_literal_list literal_list;
    std::vector<run_thread> thread_list;

    inline t_register_value lit_copy_from(const t_literal_id literal) const {
        return literal_list[literal];
    }

    // Creates or reycles a thread in the thread list.
    inline run_thread& new_thread(const t_chunk_pos start_pos) {
        for (auto& thread : thread_list) {
            if (!thread.is_active()) {
                thread.init(start_pos);
                return thread;
            }
        }

        return thread_list.emplace_back(chunk, start_pos);
    }
};

static inline uint16_t _call_mergel_16(run_thread& thread) {
    uint8_t b0 = thread.next();
    uint8_t b1 = thread.next();
 
    return bit_util::mergel_16(b0, b1);
}

static inline uint32_t _call_mergel_32(run_thread& thread) {
    uint8_t b0 = thread.next();
    uint8_t b1 = thread.next();
    uint8_t b2 = thread.next();
    uint8_t b3 = thread.next();

    return bit_util::mergel_32(b0, b1, b2, b3);
}

static inline uint64_t _call_mergel_64(run_thread& thread) {
    uint8_t b0 = thread.next();
    uint8_t b1 = thread.next();
    uint8_t b2 = thread.next();
    uint8_t b3 = thread.next();
    uint8_t b4 = thread.next();
    uint8_t b5 = thread.next();
    uint8_t b6 = thread.next();
    uint8_t b7 = thread.next();

    return bit_util::mergel_64(b0, b1, b2, b3, b4, b5, b6, b7);
}

void instr_eof(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_out(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_copy(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_add(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_sub(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_mul(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_div(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_more(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_less(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_binary_equal(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_loc_push(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_loc_copy(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_call(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_return(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_i8(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_i16(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_jump_if_false(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_unary_not(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_unary_neg(run_state& state, run_thread& thread, call_frame& top_frame);

// Functions must carry the same order as their enum equiv
void (*const instruction_jump_table[])(run_state&, run_thread&, call_frame&) = { 
    instr_eof, 
    instr_out, 
    instr_copy, 
    instr_binary_add, 
    instr_binary_sub, 
    instr_binary_mul, 
    instr_binary_div,
    instr_binary_more,
    instr_binary_less,
    instr_binary_equal, 
    instr_loc_push, 
    instr_loc_copy, 
    instr_call, 
    instr_return, 
    instr_jump_i8, 
    instr_jump_i16,
    instr_jump_if_false,
    instr_unary_not,
    instr_unary_neg,
};

static inline void execute_thread(run_state& state, t_thread_id thread_id) {
    run_thread& thread = state.thread_list[thread_id];

    while (thread.is_active()) {
        instruction_jump_table[thread.next()](state, thread, thread.top_frame());
    }
}