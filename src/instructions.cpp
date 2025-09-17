#include <utility>
#include "common.hpp"

inline constexpr auto _typed_binary_add = [](auto a, auto b) { return a + b; };
inline constexpr auto _typed_binary_sub = [](auto a, auto b) { return a - b; };
inline constexpr auto _typed_binary_mul = [](auto a, auto b) { return a * b; };
inline constexpr auto _typed_binary_div = [](auto a, auto b) { return a / b; };
inline constexpr auto _typed_binary_more = [](auto a, auto b) { return a > b; };
inline constexpr auto _typed_binary_less = [](auto a, auto b) { return a < b; };

template <typename T, typename OP>
static inline t_register_value _binary_op(const t_register_value operand0, const t_register_value operand1, OP op) {
    return bit_util::bit_cast<T, t_register_value>(op(bit_util::bit_cast<t_register_value, T>(operand0), bit_util::bit_cast<t_register_value, T>(operand1))); 
}

template <typename T, typename OP>
static inline t_register_value _unary_op(const t_register_value operand0, OP op) {
    return bit_util::bit_cast<T, t_register_value>(op(bit_util::bit_cast<t_register_value, T>(operand0))); 
}

void instr_eof(run_state& state, call_stack_frame& top_frame) {

}

void instr_out(run_state& state, call_stack_frame& top_frame) {
    const value_type type = static_cast<value_type>(state.next());
    const t_register_id target_reg = state.next();

    const t_register_value source_reg_value = top_frame.reg_copy_from(target_reg);
    std::string buffer;

    switch (type) {
        case VAL_NIL:   buffer = "NIL"; break;
        case VAL_BOOL:  buffer = source_reg_value == 0ULL ? "FALSE" : "TRUE"; break;
        case VAL_U8:    buffer = std::to_string(bit_util::bit_cast<t_register_value, uint8_t>(source_reg_value)); break;
        case VAL_U16:   buffer = std::to_string(bit_util::bit_cast<t_register_value, uint16_t>(source_reg_value)); break;
        case VAL_U32:   buffer = std::to_string(bit_util::bit_cast<t_register_value, uint32_t>(source_reg_value)); break;
        case VAL_U64:   buffer = std::to_string(bit_util::bit_cast<t_register_value, uint64_t>(source_reg_value)); break;
        case VAL_I8:    buffer = std::to_string(bit_util::bit_cast<t_register_value, int8_t>(source_reg_value)); break;
        case VAL_I16:   buffer = std::to_string(bit_util::bit_cast<t_register_value, int16_t>(source_reg_value)); break;
        case VAL_I32:   buffer = std::to_string(bit_util::bit_cast<t_register_value, int32_t>(source_reg_value)); break;
        case VAL_I64:   buffer = std::to_string(bit_util::bit_cast<t_register_value, int64_t>(source_reg_value)); break;
        case VAL_F32:   buffer = std::to_string(bit_util::bit_cast<t_register_value, float>(source_reg_value)); break;
        case VAL_F64:   buffer = std::to_string(bit_util::bit_cast<t_register_value, double>(source_reg_value)); break;
    }

    buffer += " (" + std::bitset<64>(source_reg_value).to_string() + ")";
    
    std::cout << "RUNTIME VALUE: " << buffer << '\n';
}

void instr_copy(run_state& state, call_stack_frame& top_frame) {
    const t_register_id target_reg = state.next();
    const t_literal_id literal_to_copy = _call_mergel_16(state);

    top_frame.reg_copy_to(target_reg, state.lit_copy_from(literal_to_copy));
}

template <typename FUNC>
inline void typed_binary_instr(run_state& state, call_stack_frame& top_frame, FUNC func) {
    const value_type type = static_cast<value_type>(state.next());
    const t_register_id target_reg = state.next();
    const t_register_value& operand0 = top_frame.reg_copy_from(state.next());
    const t_register_value& operand1 = top_frame.reg_copy_from(state.next());

    uint64_t result;

    switch (type) {
        case VAL_U8:  result = _binary_op<uint8_t>(operand0, operand1, func); break;
        case VAL_U16: result = _binary_op<uint16_t>(operand0, operand1, func); break;
        case VAL_U32: result = _binary_op<uint32_t>(operand0, operand1, func); break;
        case VAL_U64: result = _binary_op<uint64_t>(operand0, operand1, func); break;
        case VAL_I8:  result = _binary_op<int8_t>(operand0, operand1, func); break;
        case VAL_I16: result = _binary_op<int16_t>(operand0, operand1, func); break;
        case VAL_I32: result = _binary_op<int32_t>(operand0, operand1, func); break;
        case VAL_I64: result = _binary_op<int64_t>(operand0, operand1, func); break;
        case VAL_F32: result = _binary_op<float>(operand0, operand1, func); break;
        case VAL_F64: result = _binary_op<double>(operand0, operand1, func); break;
    }

    top_frame.reg_emplace_to(target_reg, result);
}

void instr_binary_add(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_add);
}

void instr_binary_sub(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_sub);
}

void instr_binary_mul(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_mul);
}

void instr_binary_div(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_div);
}

void instr_binary_more(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_more);
}

void instr_binary_less(run_state& state, call_stack_frame& top_frame) {
    typed_binary_instr(state, top_frame, _typed_binary_less);
}

void instr_binary_equal(run_state& state, call_stack_frame& top_frame) {
    const t_register_id target_reg = state.next();
    const t_register_value operand0 = top_frame.reg_copy_from(state.next());
    const t_register_value operand1 = top_frame.reg_copy_from(state.next());

    top_frame.reg_copy_to(target_reg, operand0 == operand1 ? 1ULL : 0ULL);
}

void instr_loc_push(run_state& state, call_stack_frame& top_frame) {
    top_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
}

void instr_loc_copy(run_state& state, call_stack_frame& top_frame) {
    const t_register_value& target_reg = state.next();
    const t_local_id local_index = _call_mergel_16(state);
    top_frame.reg_copy_to(target_reg, top_frame.local_stack[local_index]);
}

void instr_call(run_state& state, call_stack_frame& top_frame) {
    const int32_t jump_distance = bit_util::bit_cast<uint32_t, int32_t>(_call_mergel_32(state));
    const t_register_id return_value_reg = state.next();
    const uint8_t argument_count = state.next();

    call_stack_frame new_stack_frame(state.ip + argument_count, return_value_reg);

    for (int i = 0; i < argument_count; i++) {
        new_stack_frame.local_stack.emplace_back(top_frame.reg_copy_from(state.next()));
    }

    state.call_stack.emplace_back(new_stack_frame);

    // (-6 - argument_count) accounts for ensuring that the jump is relative to the instruction opcode and not any of its arguments.
    state.ip += -6 - argument_count + jump_distance;
}

void instr_return(run_state& state, call_stack_frame& top_frame) {
    // Write return value.
    if (top_frame.return_value_reg > 0) {
        state.call_stack[state.call_stack.size() - 2].reg_emplace_to(top_frame.return_value_reg - 1, top_frame.reg_copy_from(state.next()));
    }  

    state.ip = top_frame.return_address;
    state.call_stack.pop_back();
}

void instr_jump_i8(run_state& state, call_stack_frame& top_frame) {
    state.ip += bit_util::bit_cast<uint8_t, int8_t>(state.next()) - 2;
}

void instr_jump_i16(run_state& state, call_stack_frame& top_frame) {
    state.ip += bit_util::bit_cast<uint16_t, int16_t>(_call_mergel_16(state)) - 3;
}

void instr_jump_if_false(run_state& state, call_stack_frame& top_frame) {

}

void instr_unary_not(run_state& state, call_stack_frame& top_frame) {
    const t_register_id target_reg = state.next();
    const t_register_id source_reg = state.next();

    const t_register_value write_data = top_frame.reg_copy_from(source_reg) ^ 1ULL;

    top_frame.reg_copy_to(target_reg, write_data);
}

void instr_unary_neg(run_state& state, call_stack_frame& top_frame) {
    const t_register_id target_reg = state.next();
    const t_register_id source_reg = state.next();

    const t_register_value write_data = top_frame.reg_copy_from(source_reg) ^ (1ULL << 63);

    top_frame.reg_copy_to(target_reg, write_data);
}