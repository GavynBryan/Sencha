#include <batch/RefBatch.h>
#include <batch/DataBatch.h>
#include <raii/RefBatchHandle.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>
#include <system/SystemHost.h>
#include <logging/ConsoleLogSink.h>
#include <logging/FileLogSink.h>
#include <iostream>

class TestLogger{};

int main()
{
    // Set up services
    ServiceHost services;
    auto& logging = services.GetLoggingProvider();
    logging.AddSink<ConsoleLogSink>();
    logging.AddSink<FileLogSink>("game.log");

    // Create a service provider for systems to use during construction
    ServiceProvider provider(services);

    // Example usage: get a logger and log some messages
    auto& logger = logging.GetLogger<TestLogger>();
    logger.Debug("This is a debug message.");
    logger.Info("This is an info message.");
    logger.Warn("This is a warning message.");
    logger.Error("This is an error message.");
    logger.Critical("This is a critical message.");

    return 0;
}
