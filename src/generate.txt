#include <string>
#include <cstdint>
#include <fstream>

int main() {
    std::string buffer = "";

    // Init Literal #
    buffer += uint16_t(2);
    
    // Literal 0
    buffer += uint8_t(1); // Literal type: u16
    buffer += uint16_t(25); // Number 25

    // Literal 1
    buffer += uint8_t(1); // Literal type: u16
    buffer += uint16_t(12); // Number 12
    
    // Bytecode
    buffer += OP_COPY; // Load
    buffer += char(0); // In register 0
    buffer += char(0); // Literal 0

    buffer += OP_COPY; // Load
    buffer += char(0); // In register 1
    buffer += char(1); // Literal 1

    buffer += OP_IADD;
    buffer += char(2); // Sum
    buffer += char(0); // Operand 0
    buffer += char(1); // Operand 1

    buffer += '\0';

    std::ofstream write_file("test.lch"); // .lican-chunk
    write_file.write(buffer.c_str(), buffer.length());
}