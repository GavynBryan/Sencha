#include <gtest/gtest.h>
#include <logging/LoggingProvider.h>
#include <logging/ConsoleLogSink.h>
#include <service/ServiceHost.h>
#include <service/ServiceProvider.h>

// --- Test sink that captures messages ---

struct LogEntry
{
	LogLevel Level;
	std::string Category;
	std::string Message;
};

class TestLogSink : public ILogSink
{
public:
	void Write(LogLevel level, std::string_view category, std::string_view message) override
	{
		if (level < MinLevel) return;
		Entries.push_back({ level, std::string(category), std::string(message) });
	}

	std::vector<LogEntry> Entries;
};

// --- Dummy types for template-based logger names ---

class AlphaSystem {};
class BetaSystem {};

// --- LoggingProvider tests ---

TEST(LoggingProvider, GetLoggerReturnsSameInstanceForSameType)
{
	LoggingProvider provider;
	provider.AddSink<TestLogSink>();

	auto& loggerA = provider.GetLogger<AlphaSystem>();
	auto& loggerB = provider.GetLogger<AlphaSystem>();
	EXPECT_EQ(&loggerA, &loggerB);
}

TEST(LoggingProvider, GetLoggerReturnsDifferentInstancesForDifferentTypes)
{
	LoggingProvider provider;
	provider.AddSink<TestLogSink>();

	auto& loggerA = provider.GetLogger<AlphaSystem>();
	auto& loggerB = provider.GetLogger<BetaSystem>();
	EXPECT_NE(&loggerA, &loggerB);
}

TEST(LoggingProvider, LoggerCategoryContainsTypeName)
{
	LoggingProvider provider;
	provider.AddSink<TestLogSink>();

	auto& logger = provider.GetLogger<AlphaSystem>();
	EXPECT_NE(logger.GetCategory().find("AlphaSystem"), std::string::npos);
}

TEST(LoggingProvider, LoggerWritesToSink)
{
	LoggingProvider provider;
	auto& sink = provider.AddSink<TestLogSink>();

	auto& logger = provider.GetLogger<AlphaSystem>();
	logger.Info("hello");

	ASSERT_EQ(sink.Entries.size(), 1u);
	EXPECT_EQ(sink.Entries[0].Level, LogLevel::Info);
	EXPECT_EQ(sink.Entries[0].Message, "hello");
}

TEST(LoggingProvider, LoggerWritesToMultipleSinks)
{
	LoggingProvider provider;
	auto& sinkA = provider.AddSink<TestLogSink>();
	auto& sinkB = provider.AddSink<TestLogSink>();

	auto& logger = provider.GetLogger<AlphaSystem>();
	logger.Error("oops");

	EXPECT_EQ(sinkA.Entries.size(), 1u);
	EXPECT_EQ(sinkB.Entries.size(), 1u);
}

TEST(LoggingProvider, SetMinLevelFiltersMessages)
{
	LoggingProvider provider;
	auto& sink = provider.AddSink<TestLogSink>();
	provider.SetMinLevel(LogLevel::Warning);

	auto& logger = provider.GetLogger<AlphaSystem>();
	logger.Debug("skip me");
	logger.Info("skip me too");
	logger.Warn("keep me");
	logger.Error("keep me too");

	ASSERT_EQ(sink.Entries.size(), 2u);
	EXPECT_EQ(sink.Entries[0].Level, LogLevel::Warning);
	EXPECT_EQ(sink.Entries[1].Level, LogLevel::Error);
}

TEST(LoggingProvider, FormatStringSupport)
{
	LoggingProvider provider;
	auto& sink = provider.AddSink<TestLogSink>();

	auto& logger = provider.GetLogger<AlphaSystem>();
	logger.Info("loaded {} textures in {:.1f}ms", 42, 3.14);

	ASSERT_EQ(sink.Entries.size(), 1u);
	EXPECT_EQ(sink.Entries[0].Message, "loaded 42 textures in 3.1ms");
}

TEST(LoggingProvider, AllLogLevelsWork)
{
	LoggingProvider provider;
	auto& sink = provider.AddSink<TestLogSink>();

	auto& logger = provider.GetLogger<AlphaSystem>();
	logger.Debug("d");
	logger.Info("i");
	logger.Warn("w");
	logger.Error("e");
	logger.Critical("c");

	ASSERT_EQ(sink.Entries.size(), 5u);
	EXPECT_EQ(sink.Entries[0].Level, LogLevel::Debug);
	EXPECT_EQ(sink.Entries[1].Level, LogLevel::Info);
	EXPECT_EQ(sink.Entries[2].Level, LogLevel::Warning);
	EXPECT_EQ(sink.Entries[3].Level, LogLevel::Error);
	EXPECT_EQ(sink.Entries[4].Level, LogLevel::Critical);
}

// --- ServiceHost integration tests ---

TEST(LoggingProvider, AccessibleViaServiceHost)
{
	ServiceHost host;
	auto& sink = host.GetLoggingProvider().AddSink<TestLogSink>();

	auto& logger = host.GetLoggingProvider().GetLogger<BetaSystem>();
	logger.Info("from host");

	ASSERT_EQ(sink.Entries.size(), 1u);
	EXPECT_EQ(sink.Entries[0].Message, "from host");
}

// --- ServiceProvider integration tests ---

TEST(LoggingProvider, GetLoggerViaServiceProvider)
{
	ServiceHost host;
	auto& sink = host.GetLoggingProvider().AddSink<TestLogSink>();

	ServiceProvider provider(host);
	auto& logger = provider.GetLogger<BetaSystem>();
	logger.Warn("from provider");

	ASSERT_EQ(sink.Entries.size(), 1u);
	EXPECT_EQ(sink.Entries[0].Level, LogLevel::Warning);
	EXPECT_EQ(sink.Entries[0].Message, "from provider");
	EXPECT_NE(sink.Entries[0].Category.find("BetaSystem"), std::string::npos);
}

TEST(LoggingProvider, ServiceProviderAndHostReturnSameLogger)
{
	ServiceHost host;
	host.GetLoggingProvider().AddSink<TestLogSink>();

	ServiceProvider provider(host);
	auto& fromHost     = host.GetLoggingProvider().GetLogger<AlphaSystem>();
	auto& fromProvider = provider.GetLogger<AlphaSystem>();
	EXPECT_EQ(&fromHost, &fromProvider);
}
