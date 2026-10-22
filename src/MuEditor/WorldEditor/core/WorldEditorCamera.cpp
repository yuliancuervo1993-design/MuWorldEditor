#include "WorldEditorCamera.h"
#include <cmath>
#include <stdexcept>

namespace Silver::WorldEditor
{
WorldEditorCamera WorldEditorCamera::FromClient(const float view[3][4], float px, float py,
    float cx, float cy, float width, float height, float nearPlane, float farPlane, float logicalWidth, float logicalHeight)
{
    const std::array<float, 10> parameters{px, py, cx, cy, width, height, nearPlane, farPlane, logicalWidth, logicalHeight};
    for (float value : parameters) if (!std::isfinite(value)) throw std::invalid_argument("Camara no finita.");
    if (width <= 0 || height <= 0 || px <= 0 || py <= 0 || nearPlane <= 0 || farPlane <= nearPlane || logicalWidth <= 0 || logicalHeight <= 0)
        throw std::invalid_argument("Camara no inicializada.");
    WorldEditorCamera result;
    result.Width = logicalWidth; result.Height = logicalHeight;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column) result.View[column * 4 + row] = view[row][column];
    result.View[15] = 1;
    result.Projection[0] = 2 / (width * px);
    result.Projection[5] = 2 / (height * py);
    result.Projection[8] = 1 - 2 * cx / width;
    result.Projection[9] = 2 * cy / height - 1;
    result.Projection[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    result.Projection[11] = -1;
    result.Projection[14] = -2 * farPlane * nearPlane / (farPlane - nearPlane);
    return result;
}
bool WorldEditorCamera::Project(const std::array<float, 3>& point, std::array<float, 2>& result) const
{
    std::array<float, 4> eye{}, clip{};
    for (int row = 0; row < 4; ++row)
        eye[row] = View[row] * point[0] + View[4 + row] * point[1] + View[8 + row] * point[2] + View[12 + row];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) clip[row] += Projection[column * 4 + row] * eye[column];
    if (!std::isfinite(clip[3]) || clip[3] <= 0) return false;
    result = {(clip[0] / clip[3] + 1) * Width / 2, (1 - clip[1] / clip[3]) * Height / 2};
    return std::isfinite(result[0]) && std::isfinite(result[1]);
}
}
