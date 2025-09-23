#include <filesystem>
#include <fstream>

#include "instructions.hpp"

constexpr bool WRITE_MODE = true;

// Takes in a fully initialized state and runs bytecode.
void execute(run_state& state) {
    // Execute thread 0
    execute_thread(state, state.get_thread(0));

    // Yield for other threads to finish.
    while (!state.are_threads_depleted()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    thread_safe_print("Execution finished on all threads.\n");
}

// Assumes the chunk is already initialized. Parses the first few instructions and initializes the constant table.
// At this point, error checks are no more. Assume the compiler wrote everything correctly
bool load_constants(run_state_initializer& init) {
    t_literal_id literal_count = _call_mergel_16(init.chunk, init.ip);

    for (t_literal_id i = 0; i < literal_count; i++) {
        uint8_t literal_size = init.next();
        t_register_value binary;

        switch (literal_size) {
            case 1:
                binary = init.next();
                break;
            case 2:
                binary = _call_mergel_16(init.chunk, init.ip);
                break;
            case 4:
                binary = _call_mergel_32(init.chunk, init.ip);
                break;
            case 8:
                binary = _call_mergel_64(init.chunk, init.ip);
                break;
            default:
                binary = 0;
                break;
        }

        init.literal_list.emplace_back(binary);
    }

    return true;
}

bool open_file(run_state_initializer& init, const std::string& path) {
    if (!std::filesystem::exists(path)) {
        thread_safe_print('\'' + path + "' is not a valid file.\n");
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        thread_safe_print("Failed to open file.\n");
        return false;
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    t_chunk chunk(size);

    if (!file.read(reinterpret_cast<char*>(chunk.data()), size)) {
        thread_safe_print("Failed to read file.\n");
        return false;
    } 

    init.chunk = chunk;

    return true;
}

// Does not return whether or not the execution was a success.
// Only returns whether or not constant and file loading was a success.
bool run(const std::string& path) {    
    run_state_initializer init;

    if (!open_file(init, path))
        return false;

    // Load static memory
    init.static_memory_size = _call_mergel_32(init.chunk, init.ip);

    // <-IP-> +++++++->CONSTANTS<-++++++++++++++->BC<-+++++++

    if (!load_constants(init))
        return false;

    // +++++++->CONSTANTS<-<-IP->++++++++++++++->BC<-+++++++

    run_state state(init);
    state.spawn_thread(init.ip); // Spawn main thread after constant reading.

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

    // Bytecode
    #define _8(val) write_8(b, val);
    #define _16(val) write_16(b, val);
    #define _32(val) write_32(b, val);
    #define _64(val) write_64(b, val);

    _32(0)          // static memory size

    _16(2)          // program has 2 literals in constant pool
    _8(4) _32(5)    // first literal: 4 bytes, 32 bit integer (5)
    _8(4) _32(3)    // second literal: same thing, number 3

    _8(OP_LOAD) _8(0) _16(0)    // copy literal 0 to reg 0
    _8(OP_LOAD) _8(1) _16(1)    // copy literal 1 to reg 1

    _8(OP_B_ADD) _8(VAL_I32) _8(2) _8(0) _8(1)  // reg2 = reg0 + reg 1

    _8(OP_MALLOC) _8(3) _8(4)                   // reg3 = address of the memory allocation of (reg 4) bytes
    _8(OP_MWRITE) _8(3) _8(2) _8(4)             // write the first 4 bytes (arg 3) of register 2 (arg 2) to address (reg3) (arg 1)

    _8(OP_MREAD) _8(3) _8(4) _8(4)              // read the first 4 bytes (arg 3) from address (reg3), and store reading to reg4 (arg 2)

    _8(OP_OUT) _8(VAL_I32) _8(4)                // output our retrieved value from the heap

    _8(OP_RETURN)                               // end the program
    
    #undef B8
    #undef B16
    #undef B32
    #undef B64

    std::ofstream write_file("test.lch", std::ios::binary); // .lican-chunk
    write_file.write(b.c_str(), b.length());
    write_file.close();

    return !run("test.lch");
}
