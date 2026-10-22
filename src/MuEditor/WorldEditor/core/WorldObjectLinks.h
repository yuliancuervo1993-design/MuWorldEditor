#pragma once
#include <span>
#include <stdexcept>

namespace Silver::WorldEditor
{
// Same doubly-linked ownership as MuMain OBJECT_BLOCK. No allocation or deletion.
template<class Object, class Block>
void ReassignBlock(Object* object, std::span<Block> blocks, int previousIndex, int nextIndex)
{
    if (!object || previousIndex < 0 || nextIndex < 0 || static_cast<std::size_t>(previousIndex) >= blocks.size()
        || static_cast<std::size_t>(nextIndex) >= blocks.size()) throw std::invalid_argument("Bloque de objeto invalido.");
    if (previousIndex == nextIndex) return;
    auto& previous = blocks[previousIndex]; auto& next = blocks[nextIndex];
    if (object->Prior) object->Prior->Next = object->Next; else previous.Head = object->Next;
    if (object->Next) object->Next->Prior = object->Prior; else previous.Tail = object->Prior;
    object->Prior = next.Tail; object->Next = nullptr;
    if (next.Tail) next.Tail->Next = object; else next.Head = object;
    next.Tail = object;
    object->Block = static_cast<decltype(object->Block)>(nextIndex);
}
}
