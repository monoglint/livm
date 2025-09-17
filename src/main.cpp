#include "common.hpp"

constexpr bool WRITE_MODE = true;

constexpr bool CHRONO_MODE = true;
constexpr uint64_t CHRONO_REPEAT = 100000000;

inline void _run_instructions(run_state& state) {
    while (!state.at_eof() && state.call_stack.size() > 0) {
        instruction_jump_table[state.next()](state);
    }
}

// Takes in a fully initialized state and runs bytecode.
void execute(run_state& state) {
    state.call_stack.emplace_back(call_stack_frame(0, 0));

    if constexpr (CHRONO_MODE) {
        auto start = std::chrono::high_resolution_clock::now();
        t_chunk_pos ip_marker = state.ip;
        for (int _ = 0; _ < CHRONO_REPEAT; _++) {
            state.ip = ip_marker;
            _run_instructions(state);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "Execution time (avg): " << diff.count() / CHRONO_REPEAT << "ns\n";
    }
    else {
        _run_instructions(state);
    }

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
    if constexpr (!WRITE_MODE) {
        if (argc != 2) {
            std::cout << "Expected a path to bytecode.\n";
            return 1;
        }

        return !run(argv[1]);
    }

    std::string b;

    using namespace str_util;

    // Literal count
    write_16(b, 2);

    write_8(b, 4);
    write_32(b, bit_util::bit_cast<float, uint32_t>(float(-52)));

    write_8(b, 4);
    write_32(b, bit_util::bit_cast<float, uint32_t>(float(24)));

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

    // write_8(b, OP_OUT);
    // write_8(b, VAL_F32);
    // write_8(b, 2);

    write_8(b, OP_RETURN);
    write_8(b, OP_EOF);

    std::ofstream write_file("test.lch", std::ios::binary); // .lican-chunk
    write_file.write(b.c_str(), b.length());
    write_file.close();

    return !run("test.lch");
}
