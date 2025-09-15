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
#define PRINT_ENTRY_POINT_REGISTERS 1

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

using t_register_binary = uint64_t; // How many bytes a register takes up.
using t_register_list = std::array<t_register_binary, REGISTER_COUNT + 1>;
using t_register_id = uint8_t;

// Note: This is not a literal type!! This is just the index of the literal in the constant pool/literal list.
using t_literal_id = uint16_t;

using t_local_id = uint16_t;

struct call_stack_frame {
    call_stack_frame(const t_chunk_pos return_address, const t_chunk_pos return_value_register)
        : return_address(return_address), return_value_register(return_value_register) {
        for (int i = 0; i <= REGISTER_COUNT; i++) {
            register_list[i] = 0;
        }
    }

    t_register_list register_list;

    std::vector<t_register_binary> local_stack;
    
    const t_chunk_pos return_address;
    const t_register_id return_value_register;

    inline void reg_copy_to(const t_register_id reg, const t_register_binary& binary) {
        register_list[reg] = binary;
    }

    inline const t_register_binary reg_copy_from(const t_register_id reg) const {
        return register_list[reg];
    }

    inline void reg_emplace_to(const t_register_id reg, const t_register_binary new_value) {
        register_list[reg] = new_value;
    }
};

using t_call_stack = std::array<call_stack_frame, CALL_STACK_MAX + 1>;
using t_call_stack_frame_id = uint16_t;

enum opcode {
                           // ARGS -> SIDE EFFECST

    OP_EOF, // Must be index 0

    // Load a constant into a register.
    OP_COPY,               // REG: 8, LITERAL_ID: 16 -> REG = LITERAL == ARRAY ? &LITERAL_COPY : LITERAL_COPY

    // Add the value of two integer registers and store them in the third.
    OP_ADD,               // ADD_TYPE: 8, SUM_REG: 8, OP_REG0: 8, OP_REG1: 8

    // Push the contents of the given register into the stack.
    OP_LPUSH,              // SOURCE_REG: 8 

    // Copy a value from the local stack.
    OP_LCOPY,              // REG: 8, LOCAL_INDEX: T_LOCAL_ID_MAX

    // Push a new value to the stack frame.
    OP_CALL,               // GOTO_LOC: SIZE_T, REG_RETURN_LOC: 8, ARG_COUNT: 8, REG_ARG: 8...
                           // If REG_RETURN_LOC is 0, then no return value. Otherwise, the return reg is REG_RETURN_LOC - 1

    OP_RETURN,             // REG_RETURN_VAL: 8
                           // If caller expects no return value, this should just default to zero.
                           // Otherwise, the - 1 rule is not applied since it is not necessary.

    OP_GOTO,               // GOTO_LOC: SIZE_T
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

/*
Making a local variable
OP_COPY 0 0
OP_LPUSH 0

Making a local variable that is a table.

literals: [5i, 3i, 6i, -4i]
OP_COPY 0 0
OP_LPUSH 0
OP_COPY 0 1
OP_LPUSH 0
OP_COPY 0 2
OP_LPUSH 0
OP_COPY 0 3
OP_LPUSH 0

*/

struct run_state {
    t_chunk chunk;

    std::vector<t_register_binary> literal_list;
    std::vector<call_stack_frame> call_stack;

    inline t_register_binary lit_copy_from(const t_literal_id literal) const {
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
static inline t_register_binary _binary_add(const t_register_binary operand0, const t_register_binary operand1) {
    return bit_util::bit_cast<T, t_register_binary>(bit_util::bit_cast<t_register_binary, T>(operand0) + bit_util::bit_cast<t_register_binary, T>(operand1));
}

// Takes in a fully initialized state and runs bytecode.
void execute(run_state& state) {
    state.call_stack.emplace_back(call_stack_frame(0, 0));

    auto start = std::chrono::high_resolution_clock::now();
    t_chunk_pos ip_marker = state.ip;

#if CHRONO_MODE
    for (int _ = 0; _ < 50; _++) {
        state.ip = ip_marker;
#endif
        while (!state.at_eof()) {
            uint8_t instruction = state.next();
            call_stack_frame& top_frame = state.call_stack.back();

            switch (instruction) {
                case OP_EOF:
                    break;

                case OP_COPY: {
                    const t_register_id reg_target = state.next();
                    const t_literal_id literal_id = _call_mergel_16(state);

                    top_frame.reg_copy_to(reg_target, state.lit_copy_from(literal_id));
                    break;
                }

                case OP_ADD: {
                    const value_type type = static_cast<value_type>(state.next());
                    const t_register_id reg_target = state.next();
                    const t_register_binary& operand0 = top_frame.reg_copy_from(state.next());
                    const t_register_binary& operand1 = top_frame.reg_copy_from(state.next());

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

                case OP_LPUSH:
                    top_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
                    break;
                
                case OP_LCOPY: {
                    const t_register_binary& target = state.next();
                    top_frame.reg_copy_to(target, top_frame.local_stack[state.next()]);
                    break;
                }   

                case OP_CALL: {
                    const t_chunk_pos goto_position = _call_mergel_32(state);
                    const t_register_id return_value_register = state.next();
                    const uint8_t argument_count = state.next();

                    call_stack_frame new_stack_frame(state.ip + argument_count, return_value_register);

                    for (int i = 0; i < argument_count; i++) {
                        new_stack_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
                    }

                    state.call_stack.emplace_back(new_stack_frame);

                    state.ip = goto_position;

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

                case OP_GOTO:
                    state.ip = _call_mergel_64(state);
                    break;

                default:
                    throw std::runtime_error("Responded to invalid opcode: " + std::to_string(instruction));
            }
        }

#if CHRONO_MODE
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "Execution time (avg): " << diff.count() / 50 << "ns\n";
#endif

    std::cout << "Execution complete.\n";

#if PRINT_ENTRY_POINT_REGISTERS
    std::cout << "Modified registers in entry-point stack frame:\n";
    for (int id = 0; id < state.call_stack.back().register_list.size(); id++) {
        const t_register_binary register_value = state.call_stack.back().reg_copy_from(id);
        
        if (register_value != 0)
            std::cout << std::to_string(id) << ":\t" << std::bitset<64>(state.call_stack.back().reg_copy_from(id)) << '\n';
    }
#endif
}

// Assumes the chunk is already initialized. Parses the first few instructions and initializes the constant table.
// At this point, error checks are no more. Assume the compiler wrote everything correctly
bool load_constants(run_state& state) {
    t_literal_id literal_count = _call_mergel_16(state);

    for (t_literal_id i = 0; i < literal_count; i++) {
        uint8_t literal_size = state.next();
        t_register_binary binary;

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

    // Main
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

    // Eof
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