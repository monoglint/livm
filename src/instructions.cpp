#include <utility>
#include "common.hpp"

inline constexpr auto _binary_add = [](auto a, auto b) { return a + b; };
inline constexpr auto _binary_sub = [](auto a, auto b) { return a - b; };
inline constexpr auto _binary_mul = [](auto a, auto b) { return a * b; };
inline constexpr auto _binary_div = [](auto a, auto b) { return a / b; };
inline constexpr auto _binary_equal_to = [](auto a, auto b) { return a == b; };
inline constexpr auto _binary_more_than = [](auto a, auto b) { return a > b; };

template <typename T, typename OP>
static inline t_register_value _binary_op(const t_register_value operand0, const t_register_value operand1, OP op) {
    return bit_util::bit_cast<T, t_register_value>(op(bit_util::bit_cast<t_register_value, T>(operand0), bit_util::bit_cast<t_register_value, T>(operand1))); 
}

void instr_eof(run_state& state) {

}

void instr_out(run_state& state) {
    const value_type type = static_cast<value_type>(state.next());
    const t_register_value reg_target_data = state.top_frame().reg_copy_from(state.next());
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
}

void instr_copy(run_state& state) {
    const t_register_id reg_target = state.next();
    const t_literal_id literal_id = _call_mergel_16(state);

    state.top_frame().reg_copy_to(reg_target, state.lit_copy_from(literal_id));
}

void instr_add(run_state& state) {
    const value_type type = static_cast<value_type>(state.next());
    const t_register_id reg_target = state.next();
    const t_register_value& operand0 = state.top_frame().reg_copy_from(state.next());
    const t_register_value& operand1 = state.top_frame().reg_copy_from(state.next());

    uint64_t result;

    switch (type) {
        case VAL_U8:  result = _binary_op<uint8_t>(operand0, operand1, _binary_add); break;
        case VAL_U16: result = _binary_op<uint16_t>(operand0, operand1, _binary_add); break;
        case VAL_U32: result = _binary_op<uint32_t>(operand0, operand1, _binary_add); break;
        case VAL_U64: result = _binary_op<uint64_t>(operand0, operand1, _binary_add); break;
        case VAL_I8:  result = _binary_op<int8_t>(operand0, operand1, _binary_add); break;
        case VAL_I16: result = _binary_op<int16_t>(operand0, operand1, _binary_add); break;
        case VAL_I32: result = _binary_op<int32_t>(operand0, operand1, _binary_add); break;
        case VAL_I64: result = _binary_op<int64_t>(operand0, operand1, _binary_add); break;
        case VAL_F32: result = _binary_op<float>(operand0, operand1, _binary_add); break;
        case VAL_F64: result = _binary_op<double>(operand0, operand1, _binary_add); break;
    }

    state.top_frame().reg_emplace_to(reg_target, result);
}

void instr_loc_push(run_state& state) {
    state.top_frame().local_stack.emplace_back(state.top_frame().reg_copy_from(state.next()));
}

void instr_loc_copy(run_state& state) {
    const t_register_value& target = state.next();
    const t_local_id local_index = _call_mergel_16(state);
    state.top_frame().reg_copy_to(target, state.top_frame().local_stack[local_index]);
}

void instr_call(run_state& state) {
    const int32_t jump_distance = bit_util::bit_cast<uint32_t, int32_t>(_call_mergel_32(state));
    const t_register_id return_value_register = state.next();
    const uint8_t argument_count = state.next();

    call_stack_frame new_stack_frame(state.ip + argument_count, return_value_register);

    for (int i = 0; i < argument_count; i++) {
        new_stack_frame.local_stack.emplace_back(state.top_frame().reg_copy_from(state.next()));
    }

    state.call_stack.emplace_back(new_stack_frame);

    // (-6 - argument_count) accounts for ensuring that the jump is relative to the instruction opcode and not any of its arguments.
    state.ip += -6 - argument_count + jump_distance;
}

void instr_return(run_state& state) {
    // Write return value.
    if (state.top_frame().return_value_register > 0) {
        state.call_stack[state.call_stack.size() - 2].reg_emplace_to(state.top_frame().return_value_register - 1, state.top_frame().reg_copy_from(state.next()));
    }  

    state.ip = state.top_frame().return_address;
    state.call_stack.pop_back();
}

void instr_jump_i8(run_state& state) {
    state.ip += bit_util::bit_cast<uint8_t, int8_t>(state.next()) - 2;
}

void instr_jump_i16(run_state& state) {
    state.ip += bit_util::bit_cast<uint16_t, int16_t>(_call_mergel_16(state)) - 3;
}