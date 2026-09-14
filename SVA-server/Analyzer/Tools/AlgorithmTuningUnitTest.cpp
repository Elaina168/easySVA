#include "AlgorithmTuning.h"
#include <iostream>
#include <stdexcept>

using namespace SVAAnalyzer;

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    std::string msg;
    std::vector<std::string> applied;
    int globalCalls = 0;
    int ruleCalls = 0;
    auto globals = [&] { ++globalCalls; };
    auto noTasks = [&](auto &) { ++ruleCalls; };
    require(!applyAlgorithmTuning("*", .35f, .45f, noTasks, globals, applied, msg), "无任务不能成功");
    require(globalCalls == 0, "无任务不能修改全局阈值");
    require(!applyAlgorithmTuning("task-a", .35f, -1, noTasks, globals, applied, msg), "单任务不能修改共享阈值");
    require(ruleCalls == 1 && globalCalls == 0, "拒绝请求不能产生部分更新");
    auto twoTasks = [](auto &codes) { codes = {"task-b", "task-a", "task-a"}; };
    require(applyAlgorithmTuning("*", .35f, .45f, twoTasks, globals, applied, msg), "广播应更新实际任务");
    require(applied == std::vector<std::string>({"task-a", "task-b"}) && globalCalls == 1, "回执应去重且修改全局阈值一次");
    require(applyAlgorithmTuning("task-a", -1, -1, [](auto &codes) { codes.push_back("task-a"); }, globals, applied, msg), "单任务规则更新应成功");
    require(applied.size() == 1 && globalCalls == 1, "单任务规则更新不能影响全局阈值");

    AlgorithmTuningRequest request;
    Json::Value root(Json::objectValue);
    require(!parseAlgorithmTuningRequest(root, request, msg), "空补丁应拒绝");
    root["confirmWindowSec"] = 3.0;
    root["controlCode"] = "task-a";
    require(parseAlgorithmTuningRequest(root, request, msg), "单字段补丁应解析");
    Control control;
    BehaviorRuleConfig rule;
    rule.behaviorType = "sleep";
    rule.enabled = true;
    rule.thresholdMs = 15000;
    rule.headHeightRatioMax = .42;
    rule.minimumValidRatio = .75;
    control.behaviorRules.push_back(rule);
    require(applySleepTuningPatch(control, request.rule), "已有睡岗规则应更新");
    require(control.behaviorRules[0].thresholdMs == 3000, "确认窗口单位应转换");
    require(control.behaviorRules[0].headHeightRatioMax == .42 && control.behaviorRules[0].minimumValidRatio == .75, "未提交参数应保留原值");
    control.behaviorRules[0].enabled = false;
    require(!applySleepTuningPatch(control, request.rule), "不能隐式启用规则");
    control.behaviorRules.clear();
    require(!applySleepTuningPatch(control, request.rule) && control.behaviorRules.empty(), "不能给非睡岗任务新增规则");

    root["detectionConfidence"] = .4;
    require(!parseAlgorithmTuningRequest(root, request, msg), "单任务共享阈值应在入口拒绝");
    root["controlCode"] = "*";
    require(parseAlgorithmTuningRequest(root, request, msg), "广播阈值应接受");
    root["sleepPositiveRatio"] = 1.5;
    require(!parseAlgorithmTuningRequest(root, request, msg), "越界比例应拒绝");
    root["sleepPositiveRatio"] = "0.8";
    require(!parseAlgorithmTuningRequest(root, request, msg), "错误类型应拒绝");
    root.removeMember("sleepPositiveRatio");
    root["confirmWindowMs"] = 3000;
    require(!parseAlgorithmTuningRequest(root, request, msg), "冲突时间单位应拒绝");
    std::cout << "AlgorithmTuningUnitTest passed\n";
}
