#ifndef ANALYZER_POSETYPES_H
#define ANALYZER_POSETYPES_H

#include <array>
#include <cstddef>

namespace SVAAnalyzer
{
    constexpr std::size_t COCO_POSE_KEYPOINT_COUNT = 17;

    struct PoseKeypoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float confidence = 0.0f;
    };

    using PoseKeypoints = std::array<PoseKeypoint, COCO_POSE_KEYPOINT_COUNT>;
}

#endif // ANALYZER_POSETYPES_H
