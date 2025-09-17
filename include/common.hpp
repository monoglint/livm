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

constexpr auto CALL_STACK_MAX = UINT8_MAX;
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
using t_literal_id = uint16_t;

using t_local_id = uint16_t;

enum opcode {
                           // Assume args are unsigned unless explicitly said otherwise.
                           // ARGS -> SIDE EFFECTS

    OP_EOF, // Must be index 0

    OP_OUT,                // VALUE_TYPE: 8, SOURCE_REG: 8

    // Load a constant into a register.
    OP_COPY,               // REG: 8, LITERAL_ID: 16 -> REG = LITERAL == ARRAY ? &LITERAL_COPY : LITERAL_COPY

    // Add the value of two integer registers and store them in the third.
    OP_ADD,                // ADD_TYPE: 8, SUM_REG: 8, OP_REG0: 8, OP_REG1: 8

    // Push the contents of the given register into the stack.
    OP_LOC_PUSH,           // SOURCE_REG: 8 

    // Copy a value from the local stack.
    OP_LOC_COPY,           // REG: 8, LOCAL_INDEX: T_LOCAL_ID_MAX

    // Push a new value to the stack frame.
    // Note: IP_OFFSET is based on the instruction location, not the ending arg byte of the instruction.
    OP_CALL,               // IP_OFFSET: i32, REG_RETURN_LOC: 8, ARG_COUNT: 8, REG_ARG: 8...?
                           // If REG_RETURN_LOC is 0, then no return value. Otherwise, the return reg is REG_RETURN_LOC - 1

    OP_RETURN,             // REG_RETURN_VAL: 8
                           // If caller expects no return value, this should just default to zero.
                           // Otherwise, the - 1 rule is not applied since it is not necessary.

    OP_JUMP_I8,            // IP_OFFSET: i8
    OP_JUMP_I16,           // IP_OFFSET: i16

    JUMP_IF_FALSE,         // IP_OFFSET: i16

    OP_CMP_MORE,           // TARGET_REG: 8, REG0: 8, REG1: 8
    OP_CMP_LESS,           // TARGET_REG: 8, REG0: 8, REG1: 8
    OP_CMP_EQUAL,          // TARGET_REG: 8, REG0: 8, REG1: 8
    OP_FLIP,               // TARGET_REG: 8, SOURCE_REG: 8
};

// Used for some opcode arguments.
enum value_type : uint8_t {
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

struct call_stack_frame {
    call_stack_frame(const t_chunk_pos return_address, const t_chunk_pos return_value_register)
        : return_address(return_address), return_value_register(return_value_register) {}

    t_register_list register_list;

    std::vector<t_register_value> local_stack;
    
    const t_chunk_pos return_address;
    const t_register_id return_value_register;

    inline void reg_copy_to(const t_register_id reg, const t_register_value& binary) {
        register_list[reg] = binary;
    }

    inline const t_register_value reg_copy_from(const t_register_id reg) const {
        return register_list[reg];
    }

    inline void reg_emplace_to(const t_register_id reg, const t_register_value new_value) {
        register_list[reg] = new_value;
    }
};

using t_call_stack = std::array<call_stack_frame, CALL_STACK_MAX + 1>;
using t_call_stack_frame_id = uint16_t;

struct run_state {
    t_chunk chunk;

    std::vector<t_register_value> literal_list;
    std::vector<call_stack_frame> call_stack;

    inline t_register_value lit_copy_from(const t_literal_id literal) const {
        return literal_list[literal];
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

    inline call_stack_frame& top_frame() {
        return call_stack.back();
    }

    t_chunk_pos ip = 0;
};

static inline uint16_t _call_mergel_16(run_state& state) {
    uint8_t b0 = state.next();
    uint8_t b1 = state.next();
 
    return bit_util::mergel_16(b0, b1);
}

static inline uint32_t _call_mergel_32(run_state& state) {
    uint8_t b0 = state.next();
    uint8_t b1 = state.next();
    uint8_t b2 = state.next();
    uint8_t b3 = state.next();

    return bit_util::mergel_32(b0, b1, b2, b3);
}

static inline uint64_t _call_mergel_64(run_state& state) {
    uint8_t b0 = state.next();
    uint8_t b1 = state.next();
    uint8_t b2 = state.next();
    uint8_t b3 = state.next();
    uint8_t b4 = state.next();
    uint8_t b5 = state.next();
    uint8_t b6 = state.next();
    uint8_t b7 = state.next();

    return bit_util::mergel_64(b0, b1, b2, b3, b4, b5, b6, b7);
}

void instr_eof(run_state& state);
void instr_out(run_state& state);
void instr_copy(run_state& state);
void instr_add(run_state& state);
void instr_loc_push(run_state& state);
void instr_loc_copy(run_state& state);
void instr_call(run_state& state);
void instr_return(run_state& state);
void instr_jump_i8(run_state& state);
void instr_jump_i16(run_state& state);

// Functions must carry the same order as their enum equiv
void (*const instruction_jump_table[])(run_state&) = { instr_eof, instr_out, instr_copy, instr_add, instr_loc_push, instr_loc_copy, instr_call, instr_return, instr_jump_i8, instr_jump_i16 };