#include <serialization/BinaryReader.h>

bool BinaryReader::ReadBytes(char* buffer, std::streamsize count)
{
    if (count < 0) return false;
    if (count == 0) return true;

    Stream.read(buffer, count);
    return Stream.good();
}