#include "AvPullStream.h"
#include "Worker.h"
#include "Scheduler.h"
#include <iostream>
#include <cassert>

namespace SVAAnalyzer
{
    bool Worker::getState() { return false; }
    void Worker::remove() {}
    Config *Scheduler::getConfig() { return nullptr; }
}

int main()
{
    std::cout << "=== Running StreamUrlUnitTest ===" << std::endl;

    // Test 1: Full gb28181:// URL with host and port
    std::string url1 = "gb28181://127.0.0.1:9994/live/stream1";
    std::string norm1 = SVAAnalyzer::AvPullStream::normalizeStreamUrl(url1, 9994);
    std::cout << "Test 1: " << url1 << " -> " << norm1 << std::endl;
    assert(norm1 == "rtsp://127.0.0.1:9994/live/stream1");

    // Test 2: gb:// URL without port (should fill defaultRtspPort)
    std::string url2 = "gb://127.0.0.1/live/test_stream";
    std::string norm2 = SVAAnalyzer::AvPullStream::normalizeStreamUrl(url2, 9994);
    std::cout << "Test 2: " << url2 << " -> " << norm2 << std::endl;
    assert(norm2 == "rtsp://127.0.0.1:9994/live/test_stream");

    // Test 3: Standard rtsp:// URL (should remain unchanged)
    std::string url3 = "rtsp://192.168.1.50:554/live/main";
    std::string norm3 = SVAAnalyzer::AvPullStream::normalizeStreamUrl(url3, 9994);
    std::cout << "Test 3: " << url3 << " -> " << norm3 << std::endl;
    assert(norm3 == "rtsp://192.168.1.50:554/live/main");

    // Test 4: rtmp:// or http-flv URL (should remain unchanged)
    std::string url4 = "http://127.0.0.1:9992/live/stream.flv";
    std::string norm4 = SVAAnalyzer::AvPullStream::normalizeStreamUrl(url4, 9994);
    std::cout << "Test 4: " << url4 << " -> " << norm4 << std::endl;
    assert(norm4 == "http://127.0.0.1:9992/live/stream.flv");

    // Test 5: Custom default RTSP port
    std::string url5 = "gb28181://192.168.2.10/rtp/session_abc";
    std::string norm5 = SVAAnalyzer::AvPullStream::normalizeStreamUrl(url5, 554);
    std::cout << "Test 5: " << url5 << " -> " << norm5 << std::endl;
    assert(norm5 == "rtsp://192.168.2.10:554/rtp/session_abc");

    std::cout << "All StreamUrlUnitTest cases PASSED!" << std::endl;
    return 0;
}
