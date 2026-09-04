#include "SleepPoseEvaluator.h"
#include "Algorithm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace SVAAnalyzer
{
    namespace
    {
        constexpr std::array<std::size_t, 5> HEAD_INDICES{{0, 1, 2, 3, 4}};
        constexpr std::array<std::size_t, 2> SHOULDER_INDICES{{5, 6}};
        constexpr std::array<std::size_t, 4> ARM_INDICES{{7, 8, 9, 10}};
        constexpr std::array<std::size_t, 2> HIP_INDICES{{11, 12}};
        constexpr float PI_F = 3.14159265358979323846f;

        bool isValidKeypoint(const PoseKeypoint &point, float minimumConfidence)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) &&
                   std::isfinite(point.confidence) && point.confidence >= minimumConfidence;
        }

        float distance(float x1, float y1, float x2, float y2)
        {
            return std::hypot(x2 - x1, y2 - y1);
        }

        float degreesFromRadians(float radians)
        {
            return radians * 180.0f / PI_F;
        }

        void transition(SleepPoseTrackContext &context,
                        SleepPoseState targetState,
                        int64_t timestampMs)
        {
            context.state = targetState;
            context.stateSinceMs = timestampMs;
            context.negativeSinceMs.reset();
        }

        void handleNegative(SleepPoseTrackContext &context,
                            int64_t timestampMs,
                            SleepPoseState targetState,
                            const SleepPoseConfig &config)
        {
            if (!context.negativeSinceMs.has_value())
            {
                context.negativeSinceMs = timestampMs;
                return;
            }
            if (timestampMs - *context.negativeSinceMs >= config.recoveryMs)
            {
                transition(context, targetState, timestampMs);
            }
        }

        std::optional<float> headMotionRatio(const SleepPoseTrackContext &context,
                                             int64_t timestampMs,
                                             const SleepPoseFeatures &features,
                                             const SleepPoseConfig &config)
        {
            if (!features.headX.has_value() || !features.headY.has_value() ||
                !features.shoulderWidth.has_value() || *features.shoulderWidth < 1.0f)
            {
                return std::nullopt;
            }

            const int64_t startMs = timestampMs - config.motionWindowMs;
            const SleepPoseObservation *reference = nullptr;
            for (const SleepPoseObservation &observation : context.observations)
            {
                if (observation.valid && observation.timestampMs >= startMs &&
                    observation.headX.has_value() && observation.headY.has_value())
                {
                    reference = &observation;
                    break;
                }
            }
            if (!reference || timestampMs - reference->timestampMs < 500)
            {
                return std::nullopt;
            }
            return distance(*reference->headX,
                            *reference->headY,
                            *features.headX,
                            *features.headY) /
                   *features.shoulderWidth;
        }

        struct CandidateAssessment
        {
            bool candidate = false;
            float score = 0.0f;
            std::optional<float> motionRatio;
            std::string reason;
        };

        CandidateAssessment assessCandidate(const SleepPoseTrackContext &context,
                                             int64_t timestampMs,
                                             const SleepPoseFeatures &features,
                                             const SleepPoseConfig &config)
        {
            CandidateAssessment assessment;
            if (!features.valid || !features.headHeightRatio.has_value())
            {
                assessment.reason = features.invalidReason;
                return assessment;
            }

            assessment.motionRatio = headMotionRatio(context, timestampMs, features, config);
            const bool headLow = *features.headHeightRatio <= config.headHeightRatioMax;
            const bool sideLean = features.headSideRatio.has_value() &&
                                  *features.headSideRatio >= config.headSideRatioMin;
            const bool armSupport = features.headArmDistanceRatio.has_value() &&
                                    *features.headArmDistanceRatio <= config.headArmDistanceRatioMax;
            const bool torsoForward = features.torsoAngleDeg.has_value() &&
                                      *features.torsoAngleDeg >= config.torsoAngleDegMin;
            const bool shoulderTilt = features.shoulderAngleDeg.has_value() &&
                                      std::abs(*features.shoulderAngleDeg) >= config.shoulderTiltDegMin;
            const bool lowMotion = assessment.motionRatio.has_value() &&
                                   *assessment.motionRatio <= config.headMotionRatioMax;

            const std::array<std::pair<const char *, bool>, 5> supports{{
                {"side_lean", sideLean},
                {"head_near_arm", armSupport},
                {"torso_forward", torsoForward},
                {"shoulder_tilt", shoulderTilt},
                {"low_motion", lowMotion},
            }};
            int activeSupportCount = 0;
            for (const auto &support : supports)
            {
                if (support.second)
                {
                    ++activeSupportCount;
                }
            }
            assessment.candidate = headLow && activeSupportCount > 0;

            constexpr float margin = 0.35f;
            const float headStrength = std::max(
                0.0f,
                std::min(1.0f,
                         (config.headHeightRatioMax + margin - *features.headHeightRatio) /
                             (2.0f * margin)));
            const float supportStrength = static_cast<float>(activeSupportCount) /
                                          static_cast<float>(supports.size());
            assessment.score = 0.60f * headStrength + 0.40f * supportStrength;

            assessment.reason = "head_not_low";
            if (headLow)
            {
                assessment.reason = "head_low";
                if (activeSupportCount == 0)
                {
                    assessment.reason += "+no_supporting_evidence";
                }
                else
                {
                    for (const auto &support : supports)
                    {
                        if (support.second)
                        {
                            assessment.reason += "+";
                            assessment.reason += support.first;
                        }
                    }
                }
            }
            return assessment;
        }

        void trimHistory(SleepPoseTrackContext &context,
                         int64_t timestampMs,
                         const SleepPoseConfig &config)
        {
            const int64_t keepMs = std::max(config.confirmWindowMs * 2,
                                            config.motionWindowMs * 2);
            const int64_t oldestMs = timestampMs - keepMs;
            while (!context.observations.empty() &&
                   context.observations.front().timestampMs < oldestMs)
            {
                context.observations.pop_front();
            }
        }

        std::pair<float, float> windowRatios(const SleepPoseTrackContext &context,
                                             int64_t timestampMs,
                                             const SleepPoseConfig &config)
        {
            const int64_t startMs = timestampMs - config.confirmWindowMs;
            int windowCount = 0;
            int validCount = 0;
            int positiveCount = 0;
            for (const SleepPoseObservation &observation : context.observations)
            {
                if (observation.timestampMs < startMs)
                {
                    continue;
                }
                ++windowCount;
                if (observation.valid)
                {
                    ++validCount;
                    if (observation.candidate)
                    {
                        ++positiveCount;
                    }
                }
            }
            const float validRatio = windowCount > 0
                                         ? static_cast<float>(validCount) / static_cast<float>(windowCount)
                                         : 0.0f;
            const float positiveRatio = validCount > 0
                                            ? static_cast<float>(positiveCount) / static_cast<float>(validCount)
                                            : 0.0f;
            return {validRatio, positiveRatio};
        }

        SleepPoseAnalysis buildAnalysis(const SleepPoseFeatures &features,
                                        const SleepPoseDecision &decision)
        {
            SleepPoseAnalysis analysis;
            analysis.evaluated = true;
            analysis.featuresValid = features.valid;
            analysis.invalidReason = features.invalidReason;
            analysis.validKeypointCount = features.validKeypointCount;
            analysis.headX = features.headX;
            analysis.headY = features.headY;
            analysis.shoulderCenterX = features.shoulderCenterX;
            analysis.shoulderCenterY = features.shoulderCenterY;
            analysis.shoulderWidth = features.shoulderWidth;
            analysis.headHeightRatio = features.headHeightRatio;
            analysis.headSideRatio = features.headSideRatio;
            analysis.shoulderAngleDeg = features.shoulderAngleDeg;
            analysis.headArmDistanceRatio = features.headArmDistanceRatio;
            analysis.torsoAngleDeg = features.torsoAngleDeg;
            analysis.headMotionRatio = decision.headMotionRatio;
            analysis.candidate = decision.candidate;
            analysis.sleepScore = decision.sleepScore;
            analysis.validRatio = decision.validRatio;
            analysis.positiveRatio = decision.positiveRatio;
            analysis.state = sleepPoseStateName(decision.state);
            analysis.transitioned = decision.transitioned;
            analysis.alert = decision.alert;
            analysis.evidence = decision.reason;
            return analysis;
        }
    }

    const char *sleepPoseStateName(SleepPoseState state)
    {
        switch (state)
        {
        case SleepPoseState::Normal:
            return "NORMAL";
        case SleepPoseState::Suspect:
            return "SUSPECT";
        case SleepPoseState::Sleep:
            return "SLEEP";
        case SleepPoseState::Recover:
            return "RECOVER";
        }
        return "NORMAL";
    }

    SleepPoseFeatures SleepPoseProcessor::extractFeatures(const PoseKeypoints &keypoints,
                                                          const SleepPoseConfig &config)
    {
        SleepPoseFeatures features;
        std::array<bool, COCO_POSE_KEYPOINT_COUNT> valid{};
        for (std::size_t index = 0; index < keypoints.size(); ++index)
        {
            valid[index] = isValidKeypoint(keypoints[index], config.keypointConfidence);
            if (valid[index])
            {
                ++features.validKeypointCount;
            }
        }

        if (!valid[SHOULDER_INDICES[0]] || !valid[SHOULDER_INDICES[1]])
        {
            features.invalidReason = "both shoulders are required";
            return features;
        }

        float headWeightSum = 0.0f;
        float weightedHeadX = 0.0f;
        float weightedHeadY = 0.0f;
        for (const std::size_t index : HEAD_INDICES)
        {
            if (!valid[index])
            {
                continue;
            }
            headWeightSum += keypoints[index].confidence;
            weightedHeadX += keypoints[index].x * keypoints[index].confidence;
            weightedHeadY += keypoints[index].y * keypoints[index].confidence;
        }
        if (headWeightSum <= 0.0f)
        {
            features.invalidReason = "at least one head keypoint is required";
            return features;
        }

        const PoseKeypoint &leftShoulder = keypoints[SHOULDER_INDICES[0]];
        const PoseKeypoint &rightShoulder = keypoints[SHOULDER_INDICES[1]];
        const float shoulderWidth = distance(leftShoulder.x,
                                             leftShoulder.y,
                                             rightShoulder.x,
                                             rightShoulder.y);
        if (shoulderWidth < 1.0f)
        {
            features.invalidReason = "shoulder width is too small";
            return features;
        }

        const float shoulderCenterX = (leftShoulder.x + rightShoulder.x) * 0.5f;
        const float shoulderCenterY = (leftShoulder.y + rightShoulder.y) * 0.5f;
        const float headX = weightedHeadX / headWeightSum;
        const float headY = weightedHeadY / headWeightSum;
        features.headX = headX;
        features.headY = headY;
        features.shoulderCenterX = shoulderCenterX;
        features.shoulderCenterY = shoulderCenterY;
        features.shoulderWidth = shoulderWidth;
        features.headHeightRatio = (shoulderCenterY - headY) / shoulderWidth;
        features.headSideRatio = std::abs(headX - shoulderCenterX) / shoulderWidth;
        features.shoulderAngleDeg = degreesFromRadians(
            std::atan2(rightShoulder.y - leftShoulder.y,
                       rightShoulder.x - leftShoulder.x));

        float minimumArmDistance = std::numeric_limits<float>::max();
        bool hasArm = false;
        for (const std::size_t index : ARM_INDICES)
        {
            if (valid[index])
            {
                minimumArmDistance = std::min(
                    minimumArmDistance,
                    distance(headX, headY, keypoints[index].x, keypoints[index].y));
                hasArm = true;
            }
        }
        if (hasArm)
        {
            features.headArmDistanceRatio = minimumArmDistance / shoulderWidth;
        }

        if (valid[HIP_INDICES[0]] && valid[HIP_INDICES[1]])
        {
            const PoseKeypoint &leftHip = keypoints[HIP_INDICES[0]];
            const PoseKeypoint &rightHip = keypoints[HIP_INDICES[1]];
            const float hipCenterX = (leftHip.x + rightHip.x) * 0.5f;
            const float hipCenterY = (leftHip.y + rightHip.y) * 0.5f;
            const float torsoDx = hipCenterX - shoulderCenterX;
            const float torsoDy = hipCenterY - shoulderCenterY;
            if (std::hypot(torsoDx, torsoDy) >= 1.0f)
            {
                features.torsoAngleDeg = degreesFromRadians(
                    std::atan2(std::abs(torsoDx), std::abs(torsoDy)));
            }
        }

        features.valid = true;
        return features;
    }

    SleepPoseDecision SleepPoseProcessor::updateTrack(SleepPoseTrackContext &context,
                                                      int64_t timestampMs,
                                                      const SleepPoseFeatures &features,
                                                      const SleepPoseConfig &config)
    {
        if (!context.initialized)
        {
            context.initialized = true;
            context.stateSinceMs = timestampMs;
            context.lastSeenMs = timestampMs;
        }
        context.lastSeenMs = timestampMs;
        const SleepPoseState previousState = context.state;
        const CandidateAssessment assessment = assessCandidate(context,
                                                               timestampMs,
                                                               features,
                                                               config);

        SleepPoseObservation observation;
        observation.timestampMs = timestampMs;
        observation.valid = features.valid;
        observation.candidate = assessment.candidate;
        observation.score = assessment.score;
        observation.headX = features.headX;
        observation.headY = features.headY;
        observation.shoulderWidth = features.shoulderWidth;
        context.observations.push_back(observation);
        trimHistory(context, timestampMs, config);
        const auto ratios = windowRatios(context, timestampMs, config);

        bool alert = false;
        if (features.valid)
        {
            switch (context.state)
            {
            case SleepPoseState::Normal:
                if (assessment.candidate)
                {
                    transition(context, SleepPoseState::Suspect, timestampMs);
                }
                break;
            case SleepPoseState::Suspect:
                if (assessment.candidate)
                {
                    context.negativeSinceMs.reset();
                    const int64_t elapsedMs = timestampMs - context.stateSinceMs;
                    if (elapsedMs >= config.confirmWindowMs &&
                        ratios.first >= config.minimumValidRatio &&
                        ratios.second >= config.sleepPositiveRatio)
                    {
                        transition(context, SleepPoseState::Sleep, timestampMs);
                        alert = true;
                    }
                }
                else
                {
                    handleNegative(context, timestampMs, SleepPoseState::Normal, config);
                }
                break;
            case SleepPoseState::Sleep:
                if (assessment.candidate)
                {
                    context.negativeSinceMs.reset();
                }
                else
                {
                    transition(context, SleepPoseState::Recover, timestampMs);
                    context.negativeSinceMs = timestampMs;
                }
                break;
            case SleepPoseState::Recover:
                if (assessment.candidate)
                {
                    transition(context, SleepPoseState::Sleep, timestampMs);
                }
                else
                {
                    handleNegative(context, timestampMs, SleepPoseState::Normal, config);
                }
                break;
            }
        }

        SleepPoseDecision decision;
        decision.state = context.state;
        decision.previousState = previousState;
        decision.transitioned = context.state != previousState;
        decision.alert = alert;
        decision.candidate = assessment.candidate;
        decision.sleepScore = assessment.score;
        decision.validRatio = ratios.first;
        decision.positiveRatio = ratios.second;
        decision.headMotionRatio = assessment.motionRatio;
        decision.reason = features.valid ? assessment.reason : features.invalidReason;
        return decision;
    }

    void SleepPoseProcessor::updateStream(SleepPoseStreamContext &context,
                                          std::vector<DetectObject *> &detects,
                                          int64_t timestampMs,
                                          const SleepPoseConfig &config)
    {
        std::unordered_set<int> visibleTrackIds;
        for (DetectObject *detect : detects)
        {
            if (!detect || !detect->hasPose || detect->trackId < 0)
            {
                continue;
            }
            visibleTrackIds.insert(detect->trackId);
            SleepPoseTrackContext &trackContext = context.tracks[detect->trackId];
            const SleepPoseFeatures features = extractFeatures(detect->keypoints, config);
            const SleepPoseDecision decision = updateTrack(trackContext,
                                                           timestampMs,
                                                           features,
                                                           config);
            detect->sleepPose = buildAnalysis(features, decision);
        }
        expireMissing(context,
                      timestampMs,
                      visibleTrackIds,
                      config.trackerMaxMissingMs);
    }

    void SleepPoseProcessor::expireMissing(SleepPoseStreamContext &context,
                                           int64_t timestampMs,
                                           const std::unordered_set<int> &visibleTrackIds,
                                           int64_t toleranceMs)
    {
        for (auto it = context.tracks.begin(); it != context.tracks.end();)
        {
            if (visibleTrackIds.find(it->first) == visibleTrackIds.end() &&
                timestampMs - it->second.lastSeenMs > toleranceMs)
            {
                it = context.tracks.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
