#ifndef ANALYZER_POSETYPES_H
#define ANALYZER_POSETYPES_H

#include <array>
#include <cstddef>
#include <optional>
#include <string>

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

    struct SleepPoseAnalysis
    {
        bool evaluated = false;
        bool featuresValid = false;
        std::string invalidReason;
        int validKeypointCount = 0;
        std::optional<float> headX;
        std::optional<float> headY;
        std::optional<float> shoulderCenterX;
        std::optional<float> shoulderCenterY;
        std::optional<float> shoulderWidth;
        std::optional<float> headHeightRatio;
        std::optional<float> headSideRatio;
        std::optional<float> shoulderAngleDeg;
        std::optional<float> headArmDistanceRatio;
        std::optional<float> torsoAngleDeg;
        std::optional<float> headMotionRatio;
        bool candidate = false;
        float sleepScore = 0.0f;
        float validRatio = 0.0f;
        float positiveRatio = 0.0f;
        std::string state = "NORMAL";
        bool transitioned = false;
        bool alert = false;
        std::string evidence;
    };
}

#endif // ANALYZER_POSETYPES_H
