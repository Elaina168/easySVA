#include "AlgorithmOnYoloPose.h"
#include "SleepPoseEvaluator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <opencv2/opencv.hpp>
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
    if (argc != 4)
    {
        std::fprintf(stderr, "Usage: %s <pose.onnx> <input.mp4> <annotated.mp4>\n", argv[0]);
        return 2;
    }

    const std::string modelPath = argv[1];
    const std::string sourcePath = argv[2];
    const std::string outputPath = argv[3];

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
        std::printf("frames=%lld detections=%lld sleep_alerts=%lld first_sleep_alert_ms=%lld seconds=%.3f throughput_fps=%.3f output=%s\n",
                    static_cast<long long>(frameCount),
                    static_cast<long long>(detectionCount),
                    static_cast<long long>(sleepAlertCount),
                    static_cast<long long>(firstSleepAlertMs),
                    elapsedSeconds,
                    throughput,
                    outputPath.c_str());
        return 0;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "Pose smoke test failed: %s\n", e.what());
        return 6;
    }
}
