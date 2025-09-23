#pragma once

#include <iostream>
#include <sstream>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <mutex>
#include <thread>
#include <bitset>
#include <set>
#include <memory>

#include "util.hpp"

void thread_safe_print(const std::string& string);

/*

CHUNK
    32 bits - Static memory size
    16 bits - Number of literals

        LITERAL
            1 byte  - Literal Size (bytes)
            x bytes - Literal binary

    x bytes - Opcodes

    1 byte - EOF - Mostly just so we can return something rather than undefined behavior if we move past EOF.
*/

/*

WARNING
This is "softcoded" with some considerations to keep in mind.
If any types here are changed, ensure that the method of reading bytes is updated in 'instructions.cpp'
*/

// By index, not size
constexpr auto THREAD_POOL_MAX = 64;
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

using t_heap = std::vector<uint8_t>;
using t_heap_address = uint32_t;

using t_static_memory = std::vector<uint8_t>;
using t_static_address = uint32_t;

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
    run_thread(const t_chunk& chunk)
        : chunk(chunk) {}

    const t_chunk& chunk;
    t_call_stack _call_stack;

    t_chunk_pos ip = 0;

    // Initialize the thread to be execution-ready. This includes creating a default entry point function.
    // Can be called after clean_up()
    inline void init(const t_chunk_pos start_pos) {
        std::lock_guard<std::mutex> lock(_empty_mutex);

        ip = start_pos;
        _call_stack.emplace_back(0, 0);

        _is_empty = false;
    }

    // Clean up the thread when it is done running to save memory. Marking this as clean will make it available for use if another thread is made.
    inline void clean_up() {
        std::lock_guard<std::mutex> lock(_empty_mutex);

        if (_is_empty)
            return;

        _is_empty = true;

        ip = 0;
        _call_stack.clear();
    }

    inline bool is_active() {
        std::lock_guard<std::mutex> lock(_empty_mutex);
        return !_is_empty;
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
        return ip >= chunk.size();
    }

    inline call_frame& top_frame() {
        return _call_stack.back();
    }
private:
    bool _is_empty = false;
    std::mutex _empty_mutex;
};

using p_run_thread = std::unique_ptr<run_thread>;

using t_thread_pool = std::vector<p_run_thread>;
using t_thread_id = uint8_t;

struct run_state_initializer {
    t_chunk chunk;
    t_literal_list literal_list;
    t_static_address static_memory_size;

    t_chunk_pos ip = 0;

    // Use for traversing chunk to get literals.
    inline uint8_t next() {
        if (ip >= chunk.size())
            return 0;

        return chunk[ip++];
    }
};

struct run_state {
    run_state(run_state_initializer& initializer)
        : chunk(std::move(initializer.chunk)), literal_list(initializer.literal_list) {
            _thread_pool.reserve(THREAD_POOL_MAX);

            // We don't need a mutex. This is called before any thread is detached.
            _static_memory.reserve(initializer.static_memory_size);
        }
    
    const t_chunk chunk;
    const t_literal_list literal_list;

    inline t_register_value lit_copy_from(const t_literal_id literal) const {
        return literal_list[literal];
    }

    inline bool are_threads_depleted() {
        std::lock_guard<std::mutex> lock(_thread_pool_mutex);

        for (p_run_thread& thread : _thread_pool) {
            if (thread->is_active())
                return false;
        }

        return true;
    }

    // Can potentially recycle a previously finished thread just for memory efficiency.
    run_thread& spawn_thread(const t_chunk_pos start_pos);

    inline run_thread& get_thread(const t_thread_id thread_id) {
        std::lock_guard<std::mutex> lock(_thread_pool_mutex);
        return *_thread_pool[thread_id];
    }

    t_heap_address malloc(const uint8_t size);
    void mfree(const t_heap_address address, const uint8_t size);

    // 'bytes' is the number of bytes in the data that should be appended into the address space.
    void mwrite(const t_heap_address address, const t_register_value value, const uint8_t bytes);

    // Limit size from 0-7 to fit in t_register value
    t_register_value mread(const t_heap_address address, const uint8_t size);

    // Read and write behaves like heap, but remember, static memory is not dynamic.

    void swrite(const t_static_address address, const t_register_value value, const uint8_t bytes);
    t_register_value sread(const t_static_address address, const uint8_t size);

private:
    struct _heap_selection {
        _heap_selection(const t_heap_address address, const uint8_t size)
            : address(address), size(size) {};

        t_heap_address address;
        uint8_t size;

        // for hashing
        bool operator<(const _heap_selection& other) const {
            return address < other.address;
        }
    };

    std::vector<uint8_t> _static_memory;
    std::mutex _static_memory_mutex;

    t_heap _heap;
    std::mutex _heap_mutex;

    std::set<_heap_selection> _free_heap_space_set;
    std::mutex _free_heap_space_set_mutex;

    t_thread_pool _thread_pool;
    std::mutex _thread_pool_mutex;
};

static inline uint16_t _call_mergel_16(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
 
    return bit_util::mergel_16(b0, b1);
}

static inline uint32_t _call_mergel_32(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
    uint8_t b2 = chunk[ip++];
    uint8_t b3 = chunk[ip++];

    return bit_util::mergel_32(b0, b1, b2, b3);
}

static inline uint64_t _call_mergel_64(const t_chunk& chunk, t_chunk_pos& ip) {
    uint8_t b0 = chunk[ip++];
    uint8_t b1 = chunk[ip++];
    uint8_t b2 = chunk[ip++];
    uint8_t b3 = chunk[ip++];
    uint8_t b4 = chunk[ip++];
    uint8_t b5 = chunk[ip++];
    uint8_t b6 = chunk[ip++];
    uint8_t b7 = chunk[ip++];

    return bit_util::mergel_64(b0, b1, b2, b3, b4, b5, b6, b7);
}