#include <math/Transform3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	constexpr size_t DefaultTransformCount = 2'000;
	constexpr size_t DefaultWarmupIterations = 32;
	constexpr size_t DefaultMeasuredIterations = 300;
	constexpr size_t DefaultBranchingFactor = 4;
	constexpr double ValidationEpsilon = 1e-4;
	constexpr uint32_t NullParent = UINT32_MAX;

	using Clock = std::chrono::steady_clock;

	struct RunConfig
	{
		size_t TransformCount = DefaultTransformCount;
		size_t WarmupIterations = DefaultWarmupIterations;
		size_t MeasuredIterations = DefaultMeasuredIterations;
		size_t BranchingFactor = DefaultBranchingFactor;
	};

	struct SampleStats
	{
		double TotalUs = 0.0;
		double MinUs = 0.0;
		double MaxUs = 0.0;
		double MeanUs = 0.0;
		double MedianUs = 0.0;
		double P95Us = 0.0;
		double StdDevUs = 0.0;
		double NsPerTransform = 0.0;
		double TransformsPerSecond = 0.0;
	};

	struct BenchmarkResult
	{
		std::string Name;
		double SetupUs = 0.0;
		double Checksum = 0.0;
		size_t TransformCount = 0;
		size_t NodeAllocations = 0;
		size_t ContiguousTransformPayloadBytes = 0;
		size_t ChildPointerStorageBytes = 0;
		size_t ParentIndexStorageBytes = 0;
		SampleStats Propagation;
	};

	size_t ParseSizeArgument(const char* value, const char* label)
	{
		char* end = nullptr;
		unsigned long long parsed = std::strtoull(value, &end, 10);
		if (end == value || *end != '\0' || parsed == 0)
		{
			throw std::invalid_argument(std::string("Invalid ") + label + ": " + value);
		}
		return static_cast<size_t>(parsed);
	}

	RunConfig ParseArguments(int argc, char** argv)
	{
		RunConfig config;
		if (argc > 1)
			config.TransformCount = ParseSizeArgument(argv[1], "transform count");
		if (argc > 2)
			config.MeasuredIterations = ParseSizeArgument(argv[2], "measured iteration count");
		if (argc > 3)
			config.WarmupIterations = ParseSizeArgument(argv[3], "warmup iteration count");
		if (argc > 4)
			config.BranchingFactor = ParseSizeArgument(argv[4], "branching factor");
		if (argc > 5)
		{
			throw std::invalid_argument(
				"Usage: TransformHierarchyStressTest [transform_count] "
				"[measured_iterations] [warmup_iterations] [branching_factor]");
		}
		return config;
	}

	size_t ParentIndexFor(size_t index, size_t branchingFactor)
	{
		return (index - 1) / branchingFactor;
	}

	Transform3f MakeLocalTransform(size_t index)
	{
		const float x = (static_cast<float>(index % 17) - 8.0f) * 0.05f;
		const float y = (static_cast<float>((index * 7) % 19) - 9.0f) * 0.035f;
		const float z = (static_cast<float>((index * 11) % 13) - 6.0f) * 0.025f;
		const float angle = (static_cast<float>(index % 31) - 15.0f) * 0.0015f;
		const float scaleJitter = static_cast<float>(index % 5) * 0.0005f;

		return Transform3f(
			Vec3(x, y, z),
			Quatf::FromAxisAngle(Vec3(0.25f, 1.0f, 0.5f), angle),
			Vec3(1.0f + scaleJitter, 1.0f + scaleJitter * 0.5f, 1.0f + scaleJitter * 0.25f));
	}

	void SetRootFrame(Transform3f& transform, size_t frame)
	{
		const float offset = static_cast<float>((frame % 997) + 1) * 0.0001f;
		transform.Position.X = -0.4f + offset;
		transform.Position.Y = 0.2f - offset * 0.5f;
	}

	void SetRootFrameLocal(Vec3& position, Quatf& /*rotation*/, Vec3& /*scale*/, size_t frame)
	{
		const float offset = static_cast<float>((frame % 997) + 1) * 0.0001f;
		position.X = -0.4f + offset;
		position.Y = 0.2f - offset * 0.5f;
	}

	double ElapsedMicroseconds(Clock::time_point start, Clock::time_point end)
	{
		return std::chrono::duration<double, std::micro>(end - start).count();
	}

	double TransformChecksum(const Transform3f& transform)
	{
		return static_cast<double>(transform.Position.X) * 0.31
			+ static_cast<double>(transform.Position.Y) * 0.37
			+ static_cast<double>(transform.Position.Z) * 0.41
			+ static_cast<double>(transform.Rotation.X) * 0.43
			+ static_cast<double>(transform.Rotation.Y) * 0.47
			+ static_cast<double>(transform.Rotation.Z) * 0.53
			+ static_cast<double>(transform.Rotation.W) * 0.59
			+ static_cast<double>(transform.Scale.X) * 0.61
			+ static_cast<double>(transform.Scale.Y) * 0.67
			+ static_cast<double>(transform.Scale.Z) * 0.71;
	}

	SampleStats CalculateStats(const std::vector<double>& samplesUs, size_t transformCount)
	{
		SampleStats stats;
		if (samplesUs.empty())
			return stats;

		stats.TotalUs = std::accumulate(samplesUs.begin(), samplesUs.end(), 0.0);
		stats.MinUs = *std::min_element(samplesUs.begin(), samplesUs.end());
		stats.MaxUs = *std::max_element(samplesUs.begin(), samplesUs.end());
		stats.MeanUs = stats.TotalUs / static_cast<double>(samplesUs.size());

		std::vector<double> sorted = samplesUs;
		std::sort(sorted.begin(), sorted.end());
		stats.MedianUs = sorted[sorted.size() / 2];
		const size_t p95Index = std::min(
			sorted.size() - 1,
			static_cast<size_t>(std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1);
		stats.P95Us = sorted[p95Index];

		double variance = 0.0;
		for (double sample : samplesUs)
		{
			const double delta = sample - stats.MeanUs;
			variance += delta * delta;
		}
		variance /= static_cast<double>(samplesUs.size());
		stats.StdDevUs = std::sqrt(variance);

		stats.NsPerTransform = (stats.MeanUs * 1'000.0) / static_cast<double>(transformCount);
		stats.TransformsPerSecond =
			static_cast<double>(transformCount) / (stats.MeanUs / 1'000'000.0);
		return stats;
	}

	template <typename AdvanceFn, typename PropagateFn>
	SampleStats MeasurePropagation(
		const RunConfig& config,
		AdvanceFn&& advance,
		PropagateFn&& propagate)
	{
		for (size_t i = 0; i < config.WarmupIterations; ++i)
		{
			advance(i);
			propagate();
		}

		std::vector<double> samplesUs;
		samplesUs.reserve(config.MeasuredIterations);

		for (size_t i = 0; i < config.MeasuredIterations; ++i)
		{
			advance(config.WarmupIterations + i);

			std::atomic_signal_fence(std::memory_order_seq_cst);
			const auto start = Clock::now();
			propagate();
			std::atomic_signal_fence(std::memory_order_seq_cst);
			const auto end = Clock::now();

			samplesUs.push_back(ElapsedMicroseconds(start, end));
		}

		return CalculateStats(samplesUs, config.TransformCount);
	}

	class TraditionalSceneNode
	{
	public:
		explicit TraditionalSceneNode(Transform3f localTransform)
			: LocalTransform(localTransform)
			, WorldTransform(Transform3f::Identity())
		{
		}

		TraditionalSceneNode& EmplaceChild(Transform3f localTransform)
		{
			Children.push_back(std::make_unique<TraditionalSceneNode>(localTransform));
			return *Children.back();
		}

		void ResolveWorldRecursive(const Transform3f& parentWorld)
		{
			WorldTransform = parentWorld * LocalTransform;
			for (const auto& child : Children)
			{
				child->ResolveWorldRecursive(WorldTransform);
			}
		}

		void ResolveWorldIterative(const Transform3f& rootParentWorld)
		{
			WorldTransform = rootParentWorld * LocalTransform;

			std::vector<TraditionalSceneNode*> stack;
			stack.reserve(Children.size());
			stack.push_back(this);

			while (!stack.empty())
			{
				TraditionalSceneNode* node = stack.back();
				stack.pop_back();

				for (auto it = node->Children.rbegin(); it != node->Children.rend(); ++it)
				{
					TraditionalSceneNode* child = it->get();
					child->WorldTransform = node->WorldTransform * child->LocalTransform;
					stack.push_back(child);
				}
			}
		}

		double AccumulateChecksumRecursive() const
		{
			double checksum = TransformChecksum(WorldTransform);
			for (const auto& child : Children)
			{
				checksum += child->AccumulateChecksumRecursive();
			}
			return checksum;
		}

		size_t ChildPointerStorageBytesRecursive() const
		{
			size_t bytes = Children.capacity() * sizeof(std::unique_ptr<TraditionalSceneNode>);
			for (const auto& child : Children)
			{
				bytes += child->ChildPointerStorageBytesRecursive();
			}
			return bytes;
		}

		Transform3f LocalTransform;
		Transform3f WorldTransform;
		std::vector<std::unique_ptr<TraditionalSceneNode>> Children;
	};

	struct TraditionalFixture
	{
		TraditionalFixture(size_t count, size_t branchingFactor)
			: Root(std::make_unique<TraditionalSceneNode>(MakeLocalTransform(0)))
		{
			Nodes.reserve(count);
			Nodes.push_back(Root.get());

			for (size_t i = 1; i < count; ++i)
			{
				TraditionalSceneNode* parent = Nodes[ParentIndexFor(i, branchingFactor)];
				TraditionalSceneNode& child = parent->EmplaceChild(MakeLocalTransform(i));
				Nodes.push_back(&child);
			}
		}

		void Advance(size_t frame)
		{
			SetRootFrame(Root->LocalTransform, frame);
		}

		void PropagateRecursive()
		{
			Root->ResolveWorldRecursive(Transform3f::Identity());
		}

		void PropagateIterative()
		{
			Root->ResolveWorldIterative(Transform3f::Identity());
		}

		double Checksum() const
		{
			return Root->AccumulateChecksumRecursive();
		}

		std::unique_ptr<TraditionalSceneNode> Root;
		std::vector<TraditionalSceneNode*> Nodes;
	};

	struct DataOrientedSoAFixture
	{
		DataOrientedSoAFixture(size_t count, size_t branchingFactor)
		{
			LocalPositions.reserve(count);
			LocalRotations.reserve(count);
			LocalScales.reserve(count);
			WorldPositions.reserve(count);
			WorldRotations.reserve(count);
			WorldScales.reserve(count);
			ParentIndices.reserve(count);

			for (size_t i = 0; i < count; ++i)
			{
				Transform3f local = MakeLocalTransform(i);
				LocalPositions.push_back(local.Position);
				LocalRotations.push_back(local.Rotation);
				LocalScales.push_back(local.Scale);

				WorldPositions.emplace_back(0.0f, 0.0f, 0.0f);
				WorldRotations.push_back(Quatf::Identity());
				WorldScales.emplace_back(1.0f, 1.0f, 1.0f);

				ParentIndices.push_back(i == 0
					? NullParent
					: static_cast<uint32_t>(ParentIndexFor(i, branchingFactor)));
			}
		}

		void Advance(size_t frame)
		{
			SetRootFrameLocal(LocalPositions[0], LocalRotations[0], LocalScales[0], frame);
		}

		void PropagateLinear()
		{
			if (LocalPositions.empty())
				return;

			WorldPositions[0] = LocalPositions[0];
			WorldRotations[0] = LocalRotations[0];
			WorldScales[0] = LocalScales[0];

			for (size_t i = 1; i < LocalPositions.size(); ++i)
			{
				const uint32_t parentIndex = ParentIndices[i];
				const Transform3f parentWorld(
					WorldPositions[parentIndex],
					WorldRotations[parentIndex],
					WorldScales[parentIndex]);
				const Transform3f local(LocalPositions[i], LocalRotations[i], LocalScales[i]);
				const Transform3f world = parentWorld * local;

				WorldPositions[i] = world.Position;
				WorldRotations[i] = world.Rotation;
				WorldScales[i] = world.Scale;
			}
		}

		Transform3f GetWorldTransform(size_t index) const
		{
			return Transform3f(WorldPositions[index], WorldRotations[index], WorldScales[index]);
		}

		double Checksum() const
		{
			double checksum = 0.0;
			for (size_t i = 0; i < WorldPositions.size(); ++i)
			{
				checksum += TransformChecksum(GetWorldTransform(i));
			}
			return checksum;
		}

		size_t TransformPayloadBytes() const
		{
			return (LocalPositions.capacity() + LocalScales.capacity()
				+ WorldPositions.capacity() + WorldScales.capacity()) * sizeof(Vec3)
				+ (LocalRotations.capacity() + WorldRotations.capacity()) * sizeof(Quatf);
		}

		std::vector<Vec3> LocalPositions;
		std::vector<Quatf> LocalRotations;
		std::vector<Vec3> LocalScales;
		std::vector<Vec3> WorldPositions;
		std::vector<Quatf> WorldRotations;
		std::vector<Vec3> WorldScales;
		std::vector<uint32_t> ParentIndices;
	};

	void ValidateEquivalentWorlds(
		const TraditionalFixture& traditional,
		const DataOrientedSoAFixture& dataOriented)
	{
		if (traditional.Nodes.size() != dataOriented.WorldPositions.size())
			throw std::runtime_error("Fixture transform counts do not match.");

		for (size_t i = 0; i < traditional.Nodes.size(); ++i)
		{
			const Transform3f& traditionalWorld = traditional.Nodes[i]->WorldTransform;
			const Transform3f dataWorld = dataOriented.GetWorldTransform(i);
			if (!traditionalWorld.NearlyEquals(dataWorld, static_cast<float>(ValidationEpsilon)))
			{
				throw std::runtime_error("Traditional and data-oriented world transforms diverged.");
			}
		}
	}

	void PrintResult(const BenchmarkResult& result)
	{
		std::cout << "\n" << result.Name << "\n";
		std::cout << "  setup_us: " << result.SetupUs << "\n";
		std::cout << "  transforms: " << result.TransformCount << "\n";
		std::cout << "  checksum: " << result.Checksum << "\n";
		std::cout << "  node_allocations: " << result.NodeAllocations << "\n";
		std::cout << "  contiguous_transform_payload_bytes: "
				  << result.ContiguousTransformPayloadBytes << "\n";
		std::cout << "  child_pointer_storage_bytes: "
				  << result.ChildPointerStorageBytes << "\n";
		std::cout << "  parent_index_storage_bytes: "
				  << result.ParentIndexStorageBytes << "\n";
		std::cout << "  propagation_total_us: " << result.Propagation.TotalUs << "\n";
		std::cout << "  propagation_min_us: " << result.Propagation.MinUs << "\n";
		std::cout << "  propagation_mean_us: " << result.Propagation.MeanUs << "\n";
		std::cout << "  propagation_median_us: " << result.Propagation.MedianUs << "\n";
		std::cout << "  propagation_p95_us: " << result.Propagation.P95Us << "\n";
		std::cout << "  propagation_max_us: " << result.Propagation.MaxUs << "\n";
		std::cout << "  propagation_stddev_us: " << result.Propagation.StdDevUs << "\n";
		std::cout << "  propagation_ns_per_transform: "
				  << result.Propagation.NsPerTransform << "\n";
		std::cout << "  propagation_transforms_per_second: "
				  << result.Propagation.TransformsPerSecond << "\n";
	}

	void PrintCsvRow(const BenchmarkResult& result)
	{
		std::cout
			<< "csv,"
			<< result.Name << ","
			<< result.TransformCount << ","
			<< result.SetupUs << ","
			<< result.Propagation.TotalUs << ","
			<< result.Propagation.MinUs << ","
			<< result.Propagation.MeanUs << ","
			<< result.Propagation.MedianUs << ","
			<< result.Propagation.P95Us << ","
			<< result.Propagation.MaxUs << ","
			<< result.Propagation.StdDevUs << ","
			<< result.Propagation.NsPerTransform << ","
			<< result.Propagation.TransformsPerSecond << ","
			<< result.Checksum << ","
			<< result.NodeAllocations << ","
			<< result.ContiguousTransformPayloadBytes << ","
			<< result.ChildPointerStorageBytes << ","
			<< result.ParentIndexStorageBytes
			<< "\n";
	}
}

int main(int argc, char** argv)
{
	try
	{
		const RunConfig config = ParseArguments(argc, argv);

		std::cout << std::fixed << std::setprecision(3);
		std::cout << "Transform hierarchy propagation stress test\n";
		std::cout << "topology: breadth-first " << config.BranchingFactor << "-ary tree\n";
		std::cout << "transform_count: " << config.TransformCount << "\n";
		std::cout << "warmup_iterations: " << config.WarmupIterations << "\n";
		std::cout << "measured_iterations: " << config.MeasuredIterations << "\n";
		std::cout << "sizeof_transform3f: " << sizeof(Transform3f) << "\n";
		std::cout << "sizeof_traditional_scene_node: " << sizeof(TraditionalSceneNode) << "\n";

		const auto traditionalSetupStart = Clock::now();
		TraditionalFixture traditional(config.TransformCount, config.BranchingFactor);
		const auto traditionalSetupEnd = Clock::now();

		const auto dataSetupStart = Clock::now();
		DataOrientedSoAFixture dataSoA(config.TransformCount, config.BranchingFactor);
		const auto dataSetupEnd = Clock::now();

		traditional.Advance(0);
		dataSoA.Advance(0);
		traditional.PropagateRecursive();
		dataSoA.PropagateLinear();
		ValidateEquivalentWorlds(traditional, dataSoA);
		std::cout << "validation: equivalent world transforms\n";

		const size_t childPointerBytes = traditional.Root->ChildPointerStorageBytesRecursive();

		BenchmarkResult traditionalRecursive;
		traditionalRecursive.Name = "traditional_scene_node_recursive";
		traditionalRecursive.TransformCount = config.TransformCount;
		traditionalRecursive.SetupUs = ElapsedMicroseconds(
			traditionalSetupStart,
			traditionalSetupEnd);
		traditionalRecursive.NodeAllocations = config.TransformCount;
		traditionalRecursive.ChildPointerStorageBytes = childPointerBytes;
		traditionalRecursive.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { traditional.Advance(frame); },
			[&]() { traditional.PropagateRecursive(); });
		traditionalRecursive.Checksum = traditional.Checksum();

		BenchmarkResult traditionalIterative;
		traditionalIterative.Name = "traditional_scene_node_iterative";
		traditionalIterative.TransformCount = config.TransformCount;
		traditionalIterative.SetupUs = traditionalRecursive.SetupUs;
		traditionalIterative.NodeAllocations = config.TransformCount;
		traditionalIterative.ChildPointerStorageBytes = childPointerBytes;
		traditionalIterative.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { traditional.Advance(frame); },
			[&]() { traditional.PropagateIterative(); });
		traditionalIterative.Checksum = traditional.Checksum();

		BenchmarkResult dataResult;
		dataResult.Name = "data_oriented_soa_linear";
		dataResult.TransformCount = config.TransformCount;
		dataResult.SetupUs = ElapsedMicroseconds(dataSetupStart, dataSetupEnd);
		dataResult.ContiguousTransformPayloadBytes = dataSoA.TransformPayloadBytes();
		dataResult.ParentIndexStorageBytes =
			dataSoA.ParentIndices.capacity() * sizeof(uint32_t);
		dataResult.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { dataSoA.Advance(frame); },
			[&]() { dataSoA.PropagateLinear(); });
		dataResult.Checksum = dataSoA.Checksum();

		PrintResult(traditionalRecursive);
		PrintResult(traditionalIterative);
		PrintResult(dataResult);

		std::cout << "\nCSV\n";
		std::cout
			<< "csv,name,transforms,setup_us,total_us,min_us,mean_us,median_us,"
			<< "p95_us,max_us,stddev_us,ns_per_transform,transforms_per_second,"
			<< "checksum,node_allocations,contiguous_transform_payload_bytes,"
			<< "child_pointer_storage_bytes,parent_index_storage_bytes\n";
		PrintCsvRow(traditionalRecursive);
		PrintCsvRow(traditionalIterative);
		PrintCsvRow(dataResult);
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "TransformHierarchyStressTest failed: " << ex.what() << "\n";
		return 1;
	}
}
