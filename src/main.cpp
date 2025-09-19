#include "common.hpp"

constexpr bool WRITE_MODE = true;

constexpr bool CHRONO_MODE = false;
constexpr uint64_t CHRONO_REPEAT = 3;

// Takes in a fully initialized state and runs bytecode.
void execute(run_state& state) {
    // Execute thread 0
    execute_thread(state, 0);

    std::cout << "Execution complete.\n";
}

// Assumes the chunk is already initialized. Parses the first few instructions and initializes the constant table.
// At this point, error checks are no more. Assume the compiler wrote everything correctly
bool load_constants(run_state& state) {
    run_thread& main_thread = state.thread_list[0];
    t_literal_id literal_count = _call_mergel_16(main_thread);

    for (t_literal_id i = 0; i < literal_count; i++) {
        uint8_t literal_size = main_thread.next();
        t_register_value binary;

        switch (literal_size) {
            case 1:
                binary = main_thread.next();
                break;
            case 2:
                binary = _call_mergel_16(main_thread);
                break;
            case 4:
                binary = _call_mergel_32(main_thread);
                break;
            case 8:
                binary = _call_mergel_64(main_thread);
                break;
            default:
                binary = 0;
                break;
        }

        state.literal_list.emplace_back(binary);
    }

    return true;
}

void init_state(run_state& state) {
    // Initialize main thread
    state.new_thread(0);
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

    init_state(state);

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
    write_16(b, 1);

    write_8(b, 1);
    write_8(b, false);

    write_8(b, OP_COPY);
    write_8(b, 0);
    write_16(b, 0);

    write_8(b, OP_U_NOT);
    write_8(b, 1);
    write_8(b, 0);

    write_8(b, OP_OUT);
    write_8(b, VAL_BOOL);
    write_8(b, 0);

    write_8(b, OP_OUT);
    write_8(b, VAL_BOOL);
    write_8(b, 1);

    write_8(b, OP_RETURN);
    write_8(b, OP_EOF);

    std::ofstream write_file("test.lch", std::ios::binary); // .lican-chunk
    write_file.write(b.c_str(), b.length());
    write_file.close();

    return !run("test.lch");
}
