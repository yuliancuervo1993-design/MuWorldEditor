#include "WorldModelReader.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace Silver::WorldEditor
{
std::span<const std::uint8_t> WorldModelReader::Take(std::size_t count)
{
    if (count > Remaining()) throw std::invalid_argument("BMD truncado: faltan datos.");
    const auto result = bytes_.subspan(cursor_, count);
    cursor_ += count;
    return result;
}
std::uint32_t WorldModelReader::Unsigned(std::size_t count)
{
    if (count > sizeof(std::uint32_t)) throw std::invalid_argument("Lectura BMD fuera de rango.");
    std::uint32_t result = 0;
    const auto value = Take(count);
    for (std::size_t index = 0; index < count; ++index)
        result |= static_cast<std::uint32_t>(value[index]) << (index * 8);
    return result;
}
int WorldModelReader::SignedShort()
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(Unsigned(2)));
}
int WorldModelReader::Count(int maximum)
{
    const auto count = SignedShort();
    if (count < 0 || count > maximum) throw std::invalid_argument("Conteo BMD fuera del limite de inspeccion.");
    return count;
}
bool WorldModelReader::Boolean()
{
    const auto value = Unsigned(1);
    if (value > 1) throw std::invalid_argument("Booleano BMD no valido.");
    return value != 0;
}
float WorldModelReader::Float()
{
    constexpr float MaximumMagnitude = 1.0e20f;
    const auto value = std::bit_cast<float>(Unsigned(4));
    if (!std::isfinite(value) || std::abs(value) > MaximumMagnitude)
        throw std::invalid_argument("BMD con NaN, infinito o coordenadas fuera de rango.");
    return value;
}
std::string WorldModelReader::Name()
{
    constexpr std::size_t NameBytes = 32;
    const auto bytes = Take(NameBytes);
    const auto end = std::find(bytes.begin(), bytes.end(), std::uint8_t{0});
    return std::string(bytes.begin(), end);
}
}
