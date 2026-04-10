#include <serialization/BinaryWriter.h>

bool BinaryWriter::WriteBytes(const char* buffer, std::streamsize count)
{
    if (count < 0) return false;
    if (count == 0) return true;

    Stream.write(buffer, count);
    return !Stream.fail();
}