#include <chrono>

#include "core.hpp"
#include "instructions.hpp"

constexpr bool CHRONO_MODE = false;
constexpr uint64_t CHRONO_REPEAT = 50;
constexpr uint64_t CHRONO_CACHE_FORGIVE = 5;

std::mutex cout_mutex;
void thread_safe_print(const std::string& string) {
    std::lock_guard<std::mutex> lock(cout_mutex);

    std::cout << string;
}

run_thread& run_state::spawn_thread(const t_chunk_pos start_pos) {
    std::lock_guard<std::mutex> lock(_thread_pool_mutex);

    if (_thread_pool.size() > THREAD_POOL_MAX)
        throw std::overflow_error("Maximum threads appended.");

    for (auto& thread : _thread_pool) {
        if (!thread->is_active()) {
            thread->init(start_pos);
            return *thread;
        }
    }

    p_run_thread& thread = _thread_pool.emplace_back(std::make_unique<run_thread>(chunk));
    thread->init(start_pos);
    return *thread;
}

t_heap_address run_state::malloc(const uint8_t size) {
    std::lock_guard<std::mutex> lock(_heap_mutex);

    // Locate a potential spot in the heap we can use first.
    for (auto it = _free_heap_space_set.begin(); it != _free_heap_space_set.end(); it++) {
        if (it->size < size)
            continue;
        
        t_heap_address start_address = it->address;
        
        if (it->size != size)
            _free_heap_space_set.emplace(it->address + size, it->size - size);
        
        _free_heap_space_set.erase(it);  

        return start_address;
    }

    t_heap_address start_address = _heap.size();
    _heap.resize(start_address + size);

    return start_address;
}

void run_state::mfree(const t_heap_address address, const uint8_t size) {
    std::lock_guard<std::mutex> lock(_free_heap_space_set_mutex);

    auto [it, inserted] = _free_heap_space_set.emplace(address, size);

    auto next = std::next(it);
    if (next != _free_heap_space_set.end() && it->address + it->size == next->address) {
        size_t new_size = it->size + next->size;
        t_heap_address new_addr = it->address;

        _free_heap_space_set.erase(next);
        _free_heap_space_set.erase(it);
        it = _free_heap_space_set.emplace(new_addr, new_size).first;
    }

    if (it != _free_heap_space_set.begin()) {
        auto prev = std::prev(it);
        if (prev->address + prev->size == it->address) {
            size_t new_size = prev->size + it->size;
            t_heap_address new_addr = prev->address;

            _free_heap_space_set.erase(it);
            _free_heap_space_set.erase(prev);
            _free_heap_space_set.emplace(new_addr, new_size);
        }
    }
}

void run_state::mwrite(const t_heap_address address, const t_register_value value, const uint8_t bytes) {
    std::lock_guard<std::mutex> lock(_heap_mutex);

    for (uint8_t i = 0; i < bytes; i++) {
        _heap[address + i] = bit_util::bit_cast<t_register_value, uint8_t>((value >> (i * 8)) & 0XFF);
    }
}

t_register_value run_state::mread(const t_heap_address address, const uint8_t size) {
    std::lock_guard<std::mutex> lock(_heap_mutex);

    t_register_value value = 0;
    
    for (uint8_t i = 0; i < size; i++) {
        value |= bit_util::bit_cast<uint8_t, t_register_value>(_heap[address + i]) << (i * 8);
    }

    return value;
}

void run_state::swrite(const t_static_address address, const t_register_value value, const uint8_t bytes) {
    std::lock_guard<std::mutex> lock(_static_memory_mutex);

    for (uint8_t i = 0; i < bytes; i++) {
        _static_memory[address + i] = bit_util::bit_cast<t_register_value, uint8_t>((value >> (i * 8)) & 0XFF);
    }
}

t_register_value run_state::sread(const t_static_address address, const uint8_t size) {
    std::lock_guard<std::mutex> lock(_static_memory_mutex);

    t_register_value value = 0;
    
    for (uint8_t i = 0; i < size; i++) {
        value |= bit_util::bit_cast<uint8_t, t_register_value>(_static_memory[address + i]) << (i * 8);
    }

    return value;
}

// Returns an int to support chrono testing
inline int direct_thread_execution(run_state& state, run_thread& thread) {
    // throw random shit at the compiler to stop optimizing #0
    volatile int sink = 0;

    while (!thread.at_eof() && !thread._call_stack.empty()) {
        instruction_jump_table[thread.next()](state, thread, thread.top_frame());
        asm volatile("" ::: "memory"); // throw random shit at the compiler to stop optimizing #1
        sink++;     // throw random shit at the compiler to stop optimizing #2
    }

    return sink;
}

void execute_thread(run_state& state, run_thread& thread) {
    if constexpr (!CHRONO_MODE)
        direct_thread_execution(state, thread);
    else {
        std::chrono::time_point start = std::chrono::high_resolution_clock::now();

        const t_chunk_pos ip_marker = thread.ip;

        for (int i = 0; i < CHRONO_REPEAT; i++) {
            if (i == CHRONO_CACHE_FORGIVE)
                start = std::chrono::high_resolution_clock::now();;

            thread.ip = ip_marker;

            do_not_optimize_away(direct_thread_execution(state, thread));
            // ^ throw random shit at the compiler to stop optimizing #3
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        thread_safe_print("Avg time (ns): " + std::to_string(diff.count() / (CHRONO_REPEAT - CHRONO_CACHE_FORGIVE)) + '\n');
    }

    thread.clean_up();
}