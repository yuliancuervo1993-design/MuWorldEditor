#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
namespace Silver::WorldEditor
{
// Bounded little-endian reader for the disk layout, not native C++ structs.
class WorldModelReader
{
public:
    explicit WorldModelReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::span<const std::uint8_t> Take(std::size_t count);
    std::uint32_t Unsigned(std::size_t count);
    int SignedShort();
    int Count(int maximum);
    bool Boolean();
    float Float();
    std::string Name();
    std::size_t Remaining() const { return bytes_.size() - cursor_; }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
};
}
