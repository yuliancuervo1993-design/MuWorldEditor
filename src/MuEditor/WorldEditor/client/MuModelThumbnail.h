#pragma once
#ifdef _EDITOR
#include "../ui/WorldModelThumbnail.h"
#include <vector>
class BMD;
namespace Silver::WorldEditor
{
std::vector<WorldModelThumbnail::Triangle> BuildModelThumbnail(const BMD& model);
}
#endif
