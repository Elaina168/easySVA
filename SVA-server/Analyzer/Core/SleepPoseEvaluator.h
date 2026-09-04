#ifndef ANALYZER_SLEEPPOSEEVALUATOR_H
#define ANALYZER_SLEEPPOSEEVALUATOR_H

#include "PoseTypes.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SVAAnalyzer
{
    struct DetectObject;

    enum class SleepPoseState
    {
        Normal = 0,
        Suspect = 1,
        Sleep = 2,
        Recover = 3,
    };

    const char *sleepPoseStateName(SleepPoseState state);

    struct SleepPoseConfig
    {
        float keypointConfidence = 0.35f;
        int64_t trackerMaxMissingMs = 1000;
        int64_t confirmWindowMs = 15000;
        float sleepPositiveRatio = 0.80f;
        float minimumValidRatio = 0.60f;
        int64_t recoveryMs = 2000;
        float headHeightRatioMax = 0.45f;
        float headSideRatioMin = 0.30f;
        float headArmDistanceRatioMax = 0.75f;
        float torsoAngleDegMin = 25.0f;
        float shoulderTiltDegMin = 15.0f;
        int64_t motionWindowMs = 2000;
        float headMotionRatioMax = 0.15f;
    };

    struct SleepPoseFeatures
    {
        bool valid = false;
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
    };

    struct SleepPoseDecision
    {
        SleepPoseState state = SleepPoseState::Normal;
        SleepPoseState previousState = SleepPoseState::Normal;
        bool transitioned = false;
        bool alert = false;
        bool candidate = false;
        float sleepScore = 0.0f;
        float validRatio = 0.0f;
        float positiveRatio = 0.0f;
        std::optional<float> headMotionRatio;
        std::string reason;
    };

    struct SleepPoseObservation
    {
        int64_t timestampMs = 0;
        bool valid = false;
        bool candidate = false;
        float score = 0.0f;
        std::optional<float> headX;
        std::optional<float> headY;
        std::optional<float> shoulderWidth;
    };

    struct SleepPoseTrackContext
    {
        bool initialized = false;
        SleepPoseState state = SleepPoseState::Normal;
        int64_t stateSinceMs = 0;
        int64_t lastSeenMs = 0;
        std::optional<int64_t> negativeSinceMs;
        std::deque<SleepPoseObservation> observations;
    };

    struct SleepPoseStreamContext
    {
        std::string streamCode;
        std::string controlCode;
        std::unordered_map<int, SleepPoseTrackContext> tracks;
    };

    class SleepPoseProcessor
    {
    public:
        static SleepPoseFeatures extractFeatures(const PoseKeypoints &keypoints,
                                                 const SleepPoseConfig &config);

        static SleepPoseDecision updateTrack(SleepPoseTrackContext &context,
                                             int64_t timestampMs,
                                             const SleepPoseFeatures &features,
                                             const SleepPoseConfig &config);

        static void updateStream(SleepPoseStreamContext &context,
                                 std::vector<DetectObject *> &detects,
                                 int64_t timestampMs,
                                 const SleepPoseConfig &config);

        static void expireMissing(SleepPoseStreamContext &context,
                                  int64_t timestampMs,
                                  const std::unordered_set<int> &visibleTrackIds,
                                  int64_t toleranceMs);
    };
}

#endif // ANALYZER_SLEEPPOSEEVALUATOR_H
