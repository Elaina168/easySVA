#include "AlgorithmOnYoloPose.h"
#include "SleepPoseEvaluator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iomanip>
#include <opencv2/opencv.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr float KEYPOINT_CONFIDENCE = 0.35f;
    constexpr std::array<std::pair<std::size_t, std::size_t>, 18> COCO_EDGES{{
        {0, 1}, {0, 2}, {1, 3}, {2, 4}, {3, 5}, {4, 6},
        {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10}, {5, 11},
        {6, 12}, {11, 12}, {11, 13}, {13, 15}, {12, 14}, {14, 16},
    }};

    bool isVisible(const SVAAnalyzer::PoseKeypoint &keypoint)
    {
        return keypoint.confidence >= KEYPOINT_CONFIDENCE;
    }

    std::string csvEscape(const std::string &value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
        for (const char character : value)
        {
            if (character == '"')
            {
                escaped.push_back('"');
            }
            escaped.push_back(character);
        }
        escaped.push_back('"');
        return escaped;
    }

    std::string optionalNumber(const std::optional<float> &value)
    {
        if (!value.has_value())
        {
            return {};
        }
        std::ostringstream output;
        output << std::fixed << std::setprecision(6) << *value;
        return output.str();
    }

    void writeFeatureRow(std::ofstream &output,
                         int64_t frameId,
                         int64_t timestampMs,
                         const SVAAnalyzer::DetectObject &detect)
    {
        const auto &analysis = detect.sleepPose;
        output << frameId << ','
               << timestampMs << ','
               << detect.trackId << ','
               << detect.x1 << ',' << detect.y1 << ',' << detect.x2 << ',' << detect.y2 << ','
               << (analysis.featuresValid ? 1 : 0) << ','
               << csvEscape(analysis.invalidReason) << ','
               << analysis.validKeypointCount << ','
               << optionalNumber(analysis.headX) << ','
               << optionalNumber(analysis.headY) << ','
               << optionalNumber(analysis.shoulderWidth) << ','
               << optionalNumber(analysis.headHeightRatio) << ','
               << optionalNumber(analysis.headPitchProxyDeg) << ','
               << optionalNumber(analysis.headSideRatio) << ','
               << optionalNumber(analysis.shoulderAngleDeg) << ','
               << optionalNumber(analysis.headArmDistanceRatio) << ','
               << optionalNumber(analysis.torsoAngleDeg) << ','
               << optionalNumber(analysis.headMotionRatio) << ','
               << (analysis.candidate ? 1 : 0) << ','
               << std::fixed << std::setprecision(6)
               << analysis.sleepScore << ','
               << analysis.validRatio << ','
               << analysis.positiveRatio << ','
               << csvEscape(analysis.state) << ','
               << (analysis.transitioned ? 1 : 0) << ','
               << (analysis.alert ? 1 : 0) << ','
               << csvEscape(analysis.evidence) << '\n';
    }

    void drawPose(cv::Mat &image, const SVAAnalyzer::DetectObject &detect)
    {
        cv::rectangle(image,
                      cv::Rect(detect.x1, detect.y1, detect.x2 - detect.x1, detect.y2 - detect.y1),
                      cv::Scalar(255, 100, 0),
                      2,
                      cv::LINE_AA);

        for (const auto &edge : COCO_EDGES)
        {
            const auto &start = detect.keypoints[edge.first];
            const auto &end = detect.keypoints[edge.second];
            if (isVisible(start) && isVisible(end))
            {
                cv::line(image,
                         cv::Point(cvRound(start.x), cvRound(start.y)),
                         cv::Point(cvRound(end.x), cvRound(end.y)),
                         cv::Scalar(0, 255, 0),
                         2,
                         cv::LINE_AA);
            }
        }
        for (const auto &keypoint : detect.keypoints)
        {
            if (isVisible(keypoint))
            {
                cv::circle(image,
                           cv::Point(cvRound(keypoint.x), cvRound(keypoint.y)),
                           3,
                           cv::Scalar(0, 0, 255),
                           -1,
                           cv::LINE_AA);
            }
        }

        if (detect.sleepPose.evaluated)
        {
            const cv::Scalar color = detect.sleepPose.state == "SLEEP" ||
                                             detect.sleepPose.state == "RECOVER"
                                         ? cv::Scalar(0, 0, 255)
                                         : cv::Scalar(255, 255, 0);
            const std::string label = detect.sleepPose.state +
                                      cv::format(" score=%.2f", detect.sleepPose.sleepScore);
            cv::putText(image,
                        label,
                        cv::Point(detect.x1, std::max(24, detect.y1 - 8)),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.65,
                        color,
                        2,
                        cv::LINE_AA);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5)
    {
        std::fprintf(stderr, "Usage: %s <pose.onnx> <input.mp4> <annotated.mp4> [features.csv]\n", argv[0]);
        return 2;
    }

    const std::string modelPath = argv[1];
    const std::string sourcePath = argv[2];
    const std::string outputPath = argv[3];
    const std::string featuresPath = argc == 5 ? argv[4] : std::string{};

    try
    {
        SVAAnalyzer::AlgorithmOnYoloPose algorithm(nullptr, modelPath, "on_yolo11n_pose");
        cv::VideoCapture capture(sourcePath);
        if (!capture.isOpened())
        {
            std::fprintf(stderr, "Could not open video source: %s\n", sourcePath.c_str());
            return 3;
        }

        const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
        const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        double fps = capture.get(cv::CAP_PROP_FPS);
        if (fps <= 0.0)
        {
            fps = 25.0;
        }
        cv::VideoWriter writer(outputPath,
                               cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                               fps,
                               cv::Size(width, height));
        if (!writer.isOpened())
        {
            std::fprintf(stderr, "Could not open output video: %s\n", outputPath.c_str());
            return 4;
        }

        std::ofstream featureOutput;
        if (!featuresPath.empty())
        {
            featureOutput.open(featuresPath, std::ios::out | std::ios::trunc);
            if (!featureOutput.is_open())
            {
                std::fprintf(stderr, "Could not open feature CSV: %s\n", featuresPath.c_str());
                return 7;
            }
            featureOutput << "frame_id,timestamp_ms,track_id,x1,y1,x2,y2,features_valid,invalid_reason,"
                             "valid_keypoint_count,head_x,head_y,shoulder_width,head_height_ratio,head_pitch_proxy_deg,head_side_ratio,"
                             "shoulder_angle_deg,head_arm_distance_ratio,torso_angle_deg,head_motion_ratio,candidate,"
                             "sleep_score,valid_ratio,positive_ratio,state,transitioned,alert,evidence\n";
        }

        int64_t frameCount = 0;
        int64_t detectionCount = 0;
        int64_t sleepAlertCount = 0;
        int64_t firstSleepAlertMs = -1;
        SVAAnalyzer::SleepPoseConfig sleepConfig;
        SVAAnalyzer::SleepPoseStreamContext sleepContext;
        sleepContext.streamCode = sourcePath;
        sleepContext.controlCode = "pose-smoke-test";
        const auto startedAt = std::chrono::steady_clock::now();
        cv::Mat frame;
        std::vector<SVAAnalyzer::DetectObject> detects;
        while (capture.read(frame))
        {
            if (!algorithm.objectDetect(frame, detects))
            {
                std::fprintf(stderr, "Pose inference failed at frame %lld\n", static_cast<long long>(frameCount));
                return 5;
            }

            // This standalone validator targets the current single-person material set.
            // The production Analyzer assigns persistent IDs through TemporalProcessor.
            std::vector<SVAAnalyzer::DetectObject *> detectPointers;
            detectPointers.reserve(detects.size());
            for (std::size_t index = 0; index < detects.size(); ++index)
            {
                detects[index].trackId = static_cast<int>(index + 1);
                detectPointers.push_back(&detects[index]);
            }
            const double reportedTimestampMs = capture.get(cv::CAP_PROP_POS_MSEC);
            const int64_t timestampMs = reportedTimestampMs > 0.0
                                            ? static_cast<int64_t>(reportedTimestampMs)
                                            : static_cast<int64_t>(static_cast<double>(frameCount) * 1000.0 / fps);
            SVAAnalyzer::SleepPoseProcessor::updateStream(sleepContext,
                                                          detectPointers,
                                                          timestampMs,
                                                          sleepConfig);
            for (const auto &detect : detects)
            {
                if (featureOutput.is_open())
                {
                    writeFeatureRow(featureOutput, frameCount, timestampMs, detect);
                }
                if (detect.sleepPose.alert)
                {
                    if (firstSleepAlertMs < 0)
                    {
                        firstSleepAlertMs = timestampMs;
                    }
                    ++sleepAlertCount;
                }
            }
            for (const auto &detect : detects)
            {
                drawPose(frame, detect);
            }
            detectionCount += static_cast<int64_t>(detects.size());
            ++frameCount;
            writer.write(frame);
        }

        const double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startedAt).count();
        const double throughput = elapsedSeconds > 0.0 ? static_cast<double>(frameCount) / elapsedSeconds : 0.0;
        std::printf("frames=%lld detections=%lld sleep_alerts=%lld first_sleep_alert_ms=%lld seconds=%.3f throughput_fps=%.3f output=%s features=%s\n",
                    static_cast<long long>(frameCount),
                    static_cast<long long>(detectionCount),
                    static_cast<long long>(sleepAlertCount),
                    static_cast<long long>(firstSleepAlertMs),
                    elapsedSeconds,
                    throughput,
                    outputPath.c_str(),
                    featuresPath.empty() ? "disabled" : featuresPath.c_str());
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Pose smoke test failed: %s\n", e.what());
        return 6;
    }
}
