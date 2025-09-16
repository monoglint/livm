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

#define CHRONO_MODE 0
constexpr uint64_t CHRONO_REPEAT = 100000000;

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

/*

===WARNING===
THESE ARE NOT SOFTCODED

CHANGING A TYPE BELOW CAN MESS UP THE PROGRAM.

*/

// A chunk is a segment of bytecode. In this case it is what the ip will be swimming through.
using t_chunk = std::vector<uint8_t>;
using t_chunk_pos = uint32_t;

using t_register_value = uint64_t; // How many bytes a register takes up.
using t_register_list = std::array<t_register_value, REGISTER_COUNT + 1>;
using t_register_id = uint8_t;

// Note: This is not a literal type!! This is just the index of the literal in the constant pool/literal list.
using t_literal_id = uint16_t;

using t_local_id = uint16_t;

struct call_stack_frame {
    call_stack_frame(const t_chunk_pos return_address, const t_chunk_pos return_value_register)
        : return_address(return_address), return_value_register(return_value_register) {
        local_stack.reserve(16); // A good amount of local variables we can assume the average function might have.
        for (int i = 0; i <= REGISTER_COUNT; i++) {
            register_list[i] = 0;
        }
    }

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

enum opcode {
                           // Assume args are unsigned unless explicitly said otherwise.
                           // ARGS -> SIDE EFFECST

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

template <typename T>
static inline t_register_value _binary_add(const t_register_value operand0, const t_register_value operand1) {
    return bit_util::bit_cast<T, t_register_value>(bit_util::bit_cast<t_register_value, T>(operand0) + bit_util::bit_cast<t_register_value, T>(operand1));
}

// Takes in a fully initialized state and runs bytecode.
void execute(run_state& state) {
    state.call_stack.emplace_back(call_stack_frame(0, 0));

#if CHRONO_MODE
    auto start = std::chrono::high_resolution_clock::now();
    t_chunk_pos ip_marker = state.ip;
    for (int _ = 0; _ < CHRONO_REPEAT; _++) {
        state.ip = ip_marker;
#endif
        while (!state.at_eof() && state.call_stack.size() > 0) {
            uint8_t instruction = state.next();
            call_stack_frame& top_frame = state.call_stack.back();

            switch (instruction) {
                case OP_EOF:
                    break;

                case OP_OUT: {
                    const value_type type = static_cast<value_type>(state.next());
                    const t_register_value reg_target_data = top_frame.reg_copy_from(state.next());
                    std::string buffer;

                    switch (type) {
                        case VAL_U8:  buffer = std::to_string(bit_util::bit_cast<t_register_value, uint8_t>(reg_target_data)); break;
                        case VAL_U16: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint16_t>(reg_target_data)); break;
                        case VAL_U32: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint32_t>(reg_target_data)); break;
                        case VAL_U64: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint64_t>(reg_target_data)); break;
                        case VAL_I8:  buffer = std::to_string(bit_util::bit_cast<t_register_value, uint8_t>(reg_target_data)); break;
                        case VAL_I16: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint16_t>(reg_target_data)); break;
                        case VAL_I32: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint32_t>(reg_target_data)); break;
                        case VAL_I64: buffer = std::to_string(bit_util::bit_cast<t_register_value, uint64_t>(reg_target_data)); break;
                        case VAL_F32: buffer = std::to_string(bit_util::bit_cast<t_register_value, float>(reg_target_data)); break;
                        case VAL_F64: buffer = std::to_string(bit_util::bit_cast<t_register_value, double>(reg_target_data)); break;
                    }
                    
                    std::cout << "RUNTIME VALUE: " << buffer << '\n';

                    break;                    
                }

                case OP_COPY: {
                    const t_register_id reg_target = state.next();
                    const t_literal_id literal_id = _call_mergel_16(state);

                    top_frame.reg_copy_to(reg_target, state.lit_copy_from(literal_id));
                    break;
                }

                case OP_ADD: {
                    const value_type type = static_cast<value_type>(state.next());
                    const t_register_id reg_target = state.next();
                    const t_register_value& operand0 = top_frame.reg_copy_from(state.next());
                    const t_register_value& operand1 = top_frame.reg_copy_from(state.next());

                    uint64_t result;

                    switch (type) {
                        case VAL_U8:  result = _binary_add<uint8_t>(operand0, operand1); break;
                        case VAL_U16: result = _binary_add<uint16_t>(operand0, operand1); break;
                        case VAL_U32: result = _binary_add<uint32_t>(operand0, operand1); break;
                        case VAL_U64: result = _binary_add<uint64_t>(operand0, operand1); break;
                        case VAL_I8:  result = _binary_add<int8_t>(operand0, operand1); break;
                        case VAL_I16: result = _binary_add<int16_t>(operand0, operand1); break;
                        case VAL_I32: result = _binary_add<int32_t>(operand0, operand1); break;
                        case VAL_I64: result = _binary_add<int64_t>(operand0, operand1); break;
                        case VAL_F32: result = _binary_add<float>(operand0, operand1); break;
                        case VAL_F64: result = _binary_add<double>(operand0, operand1); break;
                    }

                    top_frame.reg_emplace_to(reg_target, result);

                    break;
                }

                case OP_LOC_PUSH:
                    top_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
                    break;
                
                case OP_LOC_COPY: {
                    const t_register_value& target = state.next();
                    const t_local_id local_index = _call_mergel_16(state);
                    top_frame.reg_copy_to(target, top_frame.local_stack[local_index]);
                    break;
                }   

                case OP_CALL: {
                    const int32_t jump_distance = bit_util::bit_cast<uint32_t, int32_t>(_call_mergel_32(state));
                    const t_register_id return_value_register = state.next();
                    const uint8_t argument_count = state.next();

                    call_stack_frame new_stack_frame(state.ip + argument_count, return_value_register);

                    for (int i = 0; i < argument_count; i++) {
                        new_stack_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
                    }

                    state.call_stack.emplace_back(new_stack_frame);

                    state.ip += -6 - argument_count + jump_distance;

                    break;
                }

                case OP_RETURN: {
                    // Write return value.
                    if (top_frame.return_value_register > 0) {
                        state.call_stack[state.call_stack.size() - 2].reg_emplace_to(top_frame.return_value_register - 1, top_frame.reg_copy_from(state.next()));
                    }  

                    state.ip = top_frame.return_address;
                    state.call_stack.pop_back();
                    
                    break;
                }

                case OP_JUMP_I8:
                    state.ip += bit_util::bit_cast<uint8_t, int8_t>(state.next()) - 2;
                    break;

                case OP_JUMP_I16:
                    state.ip += bit_util::bit_cast<uint16_t, int16_t>(_call_mergel_16(state)) - 3;
                    break;

                default:
                    std::cout << "PANIC - Invalid opcode.\n";
                    std::exit(1);
            }
        }

#if CHRONO_MODE
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "Execution time (avg): " << diff.count() / CHRONO_REPEAT << "ns\n";
#endif

    std::cout << "Execution complete.\n";

    if (state.call_stack.size() != 0)
        std::cout << "Note - Not all call stacks were exited before runtime ceased.\n";
}

// Assumes the chunk is already initialized. Parses the first few instructions and initializes the constant table.
// At this point, error checks are no more. Assume the compiler wrote everything correctly
bool load_constants(run_state& state) {
    t_literal_id literal_count = _call_mergel_16(state);

    for (t_literal_id i = 0; i < literal_count; i++) {
        uint8_t literal_size = state.next();
        t_register_value binary;

        switch (literal_size) {
            case 1:
                binary = state.next();
                break;
            case 2:
                binary = _call_mergel_16(state);
                break;
            case 4:
                binary = _call_mergel_32(state);
                break;
            case 8:
                binary = _call_mergel_64(state);
                break;
            default:
                binary = 0;
                break;
        }

        state.literal_list.emplace_back(binary);
    }

    return true;
}

bool open_file(run_state& state, const std::string& path) {
    if (!std::filesystem::exists(path)) {
        std::cout << '\'' << path << "' is not a valid file.\n";
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::cout << "Failed to open file.\n";
        return false;
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    t_chunk chunk(size);

    if (!file.read(reinterpret_cast<char*>(chunk.data()), size)) {
        std::cout << "Failed to read file.\n";
        return false;
    } 

    state.chunk = chunk;

    return true;
}

// Does not return whether or not the execution was a success.
// Only returns whether or not constant and file loading was a success.
bool run(const std::string& path) {    
    run_state state;

    if (!open_file(state, path))
        return false;

    // <-IP-> +++++++->CONSTANTS<-++++++++++++++->BC<-+++++++

    if (!load_constants(state))
        return false;

    // +++++++->CONSTANTS<-<-IP->++++++++++++++->BC<-+++++++

    execute(state);

    // +++++++->CONSTANTS<-++++++++++++++->BC<-+++++++<-IP->

    return true;
}

int main(int argc, char* argv[]) {
    std::string b;

    using namespace str_util;

    // Literal count
    write_16(b, 3);

    write_8(b, 4);
    write_32(b, bit_util::bit_cast<float, uint32_t>(float(-52)));

    write_8(b, 4);
    write_32(b, bit_util::bit_cast<float, uint32_t>(float(24)));

    write_8(b, 4);
    write_32(b, bit_util::bit_cast<float, uint32_t>(float(63)));

    // MAIN
    write_8(b, OP_CALL); /* 17 */
    write_32(b, 8); // Jump ahead 8 places to start function.
    write_8(b, 0);
    write_8(b, 0);

    // Jump to end of program after function call
    write_8(b, OP_JUMP_I8);
    write_8(b, 34);

    // FUNCTION
        write_8(b, OP_COPY);
        write_8(b, 0);
        write_16(b, 0);

        write_8(b, OP_COPY);
        write_8(b, 1);
        write_16(b, 1);

        write_8(b, OP_ADD);
        write_8(b, VAL_F32);
        write_8(b, 2);
        write_8(b, 0);
        write_8(b, 1);

        write_8(b, OP_COPY);
        write_8(b, 3);
        write_16(b, 2);

        write_8(b, OP_ADD);
        write_8(b, VAL_F32);
        write_8(b, 4);
        write_8(b, 2);
        write_8(b, 3);

        write_8(b, OP_LOC_PUSH);
        write_8(b, 4);

        write_8(b, OP_LOC_COPY);
        write_8(b, 5);
        write_16(b, 0);  // Access local index 0

        write_8(b, OP_OUT);
        write_8(b, VAL_F32);
        write_8(b, 5);

        write_8(b, OP_RETURN);

    // Eof
    write_8(b, OP_RETURN);
    write_8(b, OP_EOF);

    std::ofstream write_file("test.lch", std::ios::binary); // .lican-chunk
    write_file.write(b.c_str(), b.length());
    write_file.close();

    return !run("test.lch");

    // if (argc != 2) {
    //     std::cout << "Expected a path to bytecode.\n";
    //     return 1;
    // }

    // return !run(argv[1]);
}