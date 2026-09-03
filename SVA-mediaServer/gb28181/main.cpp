#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "Poller/EventPoller.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "service/GbSipConfig.h"
#include "service/GbSipRequestProcessor.h"
#include "service/GbSipTransport.h"

using easy_sva::gb28181::GbSipConfig;
using easy_sva::gb28181::GbSipRequestProcessor;
using easy_sva::gb28181::GbSipTransportServer;
using toolkit::AsyncLogWriter;
using toolkit::ConsoleChannel;
using toolkit::EventPollerPool;
using toolkit::Logger;

namespace {

volatile std::sig_atomic_t exitRequested = 0;

void handleSignal(int) {
    exitRequested = 1;
}

void printUsage(const char *program) {
    std::cout << "Usage: " << program << " [--config PATH] [--check-config]\n"
              << "\n"
              << "Standalone easySVA GB28181 SIP signaling service.\n";
}

bool parseArguments(int argc,
                    char **argv,
                    std::string &configPath,
                    bool &checkOnly) {
    configPath = toolkit::exeDir() + "gb28181.ini";
    checkOnly = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return false;
        }
        if (argument == "--check-config") {
            checkOnly = true;
            continue;
        }
        if (argument == "--config" || argument == "-c") {
            if (index + 1 >= argc) {
                std::cerr << argument << " requires a path" << std::endl;
                return false;
            }
            configPath = argv[++index];
            continue;
        }
        std::cerr << "Unknown argument: " << argument << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    std::string configPath;
    bool checkOnly = false;
    if (!parseArguments(argc, argv, configPath, checkOnly)) {
        return argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") ? 0 : 2;
    }

    GbSipConfig config;
    std::string error;
    if (!GbSipConfig::load(configPath, config, &error)) {
        std::cerr << error << std::endl;
        return 2;
    }
    if (checkOnly) {
        std::cout << "GB28181 config is valid: " << configPath << std::endl;
        return 0;
    }

    try {
        Logger::Instance().add(std::make_shared<ConsoleChannel>());
        Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());
        EventPollerPool::setPoolSize(1);

        std::shared_ptr<GbSipRequestProcessor> processor(new GbSipRequestProcessor(config));
        GbSipTransportServer server;
        server.start(config, processor);

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        std::chrono::steady_clock::time_point nextSweep = std::chrono::steady_clock::now();
        while (!exitRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (std::chrono::steady_clock::now() >= nextSweep) {
                processor->sweep();
                nextSweep = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }
        server.stop();
    } catch (const std::exception &ex) {
        std::cerr << "GbSipServer failed: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
