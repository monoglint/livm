#pragma once

#include <cstdint>

#include "core.hpp"

enum opcode {
// ================================================================================ \\
    INSTRUCTION         ARGS                                    DESCRIPTION
// ================================================================================ \\

    OP_OUT,          // TYPE: 8, A: REG
    OP_LOAD,         // A: REG, LIT_ID: 16                      Load a constant into (A).
    OP_B_ADD,        // TYPE: 8, A: REG, B: REG, C: REG         Add (B) and (C) with type TYPE, store result in (A).
    OP_B_SUB,        // TYPE: 8, A: REG, B: REG, C: REG
    OP_B_MUL,        // TYPE: 8, A: REG, B: REG, C: REG
    OP_B_DIV,        // TYPE: 8, A: REG, B: REG, C: REG
    OP_B_MORE,       // TYPE: 8, A: REG, B: REG, C: REG
    OP_B_LESS,       // TYPE: 8, A: REG, B: REG, C: REG
    OP_B_EQUAL,      // A: REG, B: REG, C: REG                  Evaluate whether (B) and (C) have matching binary, store result in (A).

    OP_MALLOC,       // A: REG, B: REG                          Allocates (B) bytes of memory. Stores address in (A).
    OP_MFREE,        // A: REG, B: REG                          Frees address (A) for (B) bytes afterwards. 
    OP_MWRITE,       // A: REG, B: REG, C: REG                  Writes first (C) bytes of (B), stores in heap address (A).
    OP_MREAD,        // A: REG, B: REG, C: REG                  Reads (C) bytes of (B), stores in (A).

    OP_PUSH_LOCAL,   // A: REG                                  Push (A) onto the local stack.
    OP_COPY_LOCAL,   // A: REG, I: 16                           Copy local index I into (A).

    OP_CALL,         // OFFSET: i32, A: REG, ARGS: 8, B: REG... Opens a new call frame,
                     //                                         moves ip by OFFSET,
                     //                                         loads ARGS registers (B...) into the local stack of the new call frame.
                     //                                         If A > 0, value is written to (A - 1) when function is complete.

    OP_DESYNC,       // OFFSET: i32, ARGS: 8, A: ARG: 8...      Detaches new thread,
                     //                                         sets thread ip to current ip + OFFSET
                     //                                         loads ARGS registers (A...) into local stack of thread's main call frame.  

    OP_RETURN,       // A: REG?                                 Closes current call stack, writes A to X register of lower call stack if a return register was specified.

    OP_JUMP_I8,      // OFFSET: i8
    OP_JUMP_I16,     // OFFSET: i16

    OP_JUMP_IF_FALSE,// OFFSET: i16, A: REG                     Jumps IP by offset if A is falsey.

    OP_U_NOT,        // A: REG, B: REG                          Flips little bit of B, writes to A.
    OP_U_NEG,        // A: REG, B: REG                          Flips sign bit of B, writes to A.
};

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

void instr_out(run_state& state, run_thread& thread, call_frame& top_frame);
void instr_load(run_state& state, run_thread& thread, call_frame& top_frame);
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
    instr_load, 
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
    instr_unary_neg
};

void execute_thread(run_state& state, run_thread& thread);