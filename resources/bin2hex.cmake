# Convert a binary file to a C hex array string.
# Usage: cmake -DINPUT=file.jar -DOUTPUT=hex.txt -P bin2hex.cmake
file(READ "${INPUT}" content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_content "${content}")
file(WRITE "${OUTPUT}" "${hex_content}")
