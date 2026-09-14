#pragma once

#include "Control.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace SVAAnalyzer
{
    // 热调请求是局部补丁；未提交的值不能覆盖运行任务原有配置。
    struct AlgorithmTuningRequest
    {
        std::string controlCode = "*";
        BehaviorRuleConfig rule;
        float scoreThreshold = -1.0f;
        float nmsThreshold = -1.0f;

        AlgorithmTuningRequest()
        {
            rule.keypointConfidence = 0;
            rule.thresholdMs = 0;
            rule.sleepPositiveRatio = 0;
            rule.minimumValidRatio = 0;
            rule.recoveryMs = 0;
            rule.headHeightRatioMax = 0;
            rule.headSideRatioMin = 0;
            rule.headArmDistanceRatioMax = 0;
            rule.torsoAngleDegMin = 0;
            rule.shoulderTiltDegMin = 0;
            rule.motionWindowMs = 0;
            rule.headMotionRatioMax = 0;
        }
    };

    inline bool parseAlgorithmTuningRequest(const Json::Value &root,
                                            AlgorithmTuningRequest &request,
                                            std::string &msg)
    {
        request = AlgorithmTuningRequest();
        if (!root.isObject() || (root.isMember("controlCode") && !root["controlCode"].isString()))
        {
            msg = "request must be an object with a string controlCode";
            return false;
        }
        if (root.isMember("controlCode")) request.controlCode = root["controlCode"].asString();
        if (request.controlCode.empty())
        {
            msg = "controlCode must not be empty";
            return false;
        }
        struct Range { const char *key; double min; double max; bool integer; };
        const Range ranges[] = {
            {"detectionConfidence", 0.1, 0.9, false}, {"nmsThreshold", 0.1, 0.9, false},
            {"keypointConfidence", 0.1, 0.9, false}, {"confirmWindowSec", 1, 60, false},
            {"confirmWindowMs", 1000, 60000, true}, {"sleepPositiveRatio", 0.5, 1, false},
            {"headHeightRatioMax", 0.3, 0.7, false}, {"headArmDistanceRatioMax", 0.3, 1.2, false},
            {"headMotionRatioMax", 0.05, 0.6, false}, {"recoveryMs", 500, 8000, true},
            {"torsoAngleDegMin", 5, 60, false}, {"shoulderTiltDegMin", 5, 45, false},
            {"motionWindowMs", 500, 6000, true}
        };
        bool hasParameter = false;
        for (const auto &range : ranges)
        {
            if (!root.isMember(range.key)) continue;
            const auto &value = root[range.key];
            if (!value.isNumeric() || !std::isfinite(value.asDouble()) ||
                value.asDouble() < range.min || value.asDouble() > range.max ||
                (range.integer && std::floor(value.asDouble()) != value.asDouble()))
            {
                msg = std::string("invalid parameter: ") + range.key;
                return false;
            }
            hasParameter = true;
        }
        if (!hasParameter || (root.isMember("confirmWindowSec") && root.isMember("confirmWindowMs")))
        {
            msg = "provide tuning parameters and only one confirmation window unit";
            return false;
        }
        if (root.isMember("detectionConfidence")) request.scoreThreshold = root["detectionConfidence"].asFloat();
        if (root.isMember("nmsThreshold")) request.nmsThreshold = root["nmsThreshold"].asFloat();
        if (request.controlCode != "*" && (request.scoreThreshold > 0 || request.nmsThreshold > 0))
        {
            msg = "detectionConfidence and nmsThreshold are global; use controlCode=*";
            return false;
        }
        auto &rule = request.rule;
        if (root.isMember("keypointConfidence")) rule.keypointConfidence = root["keypointConfidence"].asDouble();
        if (root.isMember("confirmWindowSec")) rule.thresholdMs = static_cast<int64_t>(root["confirmWindowSec"].asDouble() * 1000);
        if (root.isMember("confirmWindowMs")) rule.thresholdMs = root["confirmWindowMs"].asInt64();
        if (root.isMember("sleepPositiveRatio")) rule.sleepPositiveRatio = root["sleepPositiveRatio"].asDouble();
        if (root.isMember("headHeightRatioMax")) rule.headHeightRatioMax = root["headHeightRatioMax"].asDouble();
        if (root.isMember("headArmDistanceRatioMax")) rule.headArmDistanceRatioMax = root["headArmDistanceRatioMax"].asDouble();
        if (root.isMember("headMotionRatioMax")) rule.headMotionRatioMax = root["headMotionRatioMax"].asDouble();
        if (root.isMember("recoveryMs")) rule.recoveryMs = root["recoveryMs"].asInt64();
        if (root.isMember("torsoAngleDegMin")) rule.torsoAngleDegMin = root["torsoAngleDegMin"].asDouble();
        if (root.isMember("shoulderTiltDegMin")) rule.shoulderTiltDegMin = root["shoulderTiltDegMin"].asDouble();
        if (root.isMember("motionWindowMs")) rule.motionWindowMs = root["motionWindowMs"].asInt64();
        return true;
    }

    inline bool applySleepTuningPatch(Control &control, const BehaviorRuleConfig &patch)
    {
        bool updated = false;
        for (auto &rule : control.behaviorRules)
        {
            // 不给非睡岗布控隐式新增规则，也不重新启用已关闭的规则。
            if (rule.behaviorType != "sleep" || !rule.enabled) continue;
            if (patch.keypointConfidence > 0) rule.keypointConfidence = patch.keypointConfidence;
            if (patch.thresholdMs > 0) rule.thresholdMs = patch.thresholdMs;
            if (patch.sleepPositiveRatio > 0) rule.sleepPositiveRatio = patch.sleepPositiveRatio;
            if (patch.recoveryMs > 0) rule.recoveryMs = patch.recoveryMs;
            if (patch.headHeightRatioMax > 0) rule.headHeightRatioMax = patch.headHeightRatioMax;
            if (patch.headArmDistanceRatioMax > 0) rule.headArmDistanceRatioMax = patch.headArmDistanceRatioMax;
            if (patch.torsoAngleDegMin > 0) rule.torsoAngleDegMin = patch.torsoAngleDegMin;
            if (patch.shoulderTiltDegMin > 0) rule.shoulderTiltDegMin = patch.shoulderTiltDegMin;
            if (patch.motionWindowMs > 0) rule.motionWindowMs = patch.motionWindowMs;
            if (patch.headMotionRatioMax > 0) rule.headMotionRatioMax = patch.headMotionRatioMax;
            updated = true;
        }
        return updated;
    }

    // 调用方持有任务容器锁；先获得任务更新结果，再允许修改共享模型阈值。
    template <typename UpdateRules, typename UpdateGlobal>
    bool applyAlgorithmTuning(const std::string &controlCode, float score, float nms,
                              UpdateRules updateRules, UpdateGlobal updateGlobal,
                              std::vector<std::string> &updatedControls, std::string &msg)
    {
        updatedControls.clear();
        if (controlCode != "*" && (score > 0 || nms > 0))
        {
            msg = "shared model thresholds require controlCode=*";
            return false;
        }
        updateRules(updatedControls);
        std::sort(updatedControls.begin(), updatedControls.end());
        updatedControls.erase(std::unique(updatedControls.begin(), updatedControls.end()), updatedControls.end());
        if (updatedControls.empty())
        {
            msg = "no matching active sleep control found to update";
            return false;
        }
        if (score > 0 || nms > 0) updateGlobal();
        msg = "algorithm config updated for active sleep controls";
        return true;
    }
}
