#include <entity/EntityHandle.h>
#include <math/geometry/3d/Transform3d.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformPropagationSystem.h>
#include <transform/TransformStore.h>

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
		double BatchEmplaceUs = 0.0;
		double BatchRemoveUs = 0.0;
		double HierarchyUnregisterUs = 0.0;
		double Checksum = 0.0;
		size_t TransformCount = 0;
		size_t NodeAllocations = 0;
		size_t ContiguousTransformPayloadBytes = 0;
		size_t ChildPointerStorageBytes = 0;
		size_t ParentIndexStorageBytes = 0;
		SampleStats Propagation;
		SampleStats RebuildAndPropagate;  // Propagate() when cache is dirty each iteration.
	};

	enum class ProductionBatchMode
	{
		Scalar,
		Bulk
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
			Vec3d(x, y, z),
			Quatf::FromAxisAngle(Vec3d(0.25f, 1.0f, 0.5f), angle),
			Vec3d(1.0f + scaleJitter, 1.0f + scaleJitter * 0.5f, 1.0f + scaleJitter * 0.25f));
	}

	void SetRootFrame(Transform3f& transform, size_t frame)
	{
		const float offset = static_cast<float>((frame % 997) + 1) * 0.0001f;
		transform.Position.X = -0.4f + offset;
		transform.Position.Y = 0.2f - offset * 0.5f;
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

	// Contiguous AoS layout: locals and worlds are each a single std::vector<Transform3f>,
	// and nodes are laid out parent-before-child, so propagation is a single forward sweep
	// with no allocations, no pointer chasing, and no child-list bookkeeping.
	//
	// A split SoA layout (separate arrays for position/rotation/scale) would be a poor fit
	// here: transform composition reads every component of each element together, and the
	// dependency on the parent's just-written world blocks cross-element vectorisation, so
	// splitting would just multiply stream count and defeat the prefetcher.
	struct DataOrientedFixture
	{
		DataOrientedFixture(size_t count, size_t branchingFactor)
		{
			Locals.reserve(count);
			Worlds.reserve(count);
			ParentIndices.reserve(count);

			for (size_t i = 0; i < count; ++i)
			{
				Locals.push_back(MakeLocalTransform(i));
				Worlds.push_back(Transform3f::Identity());
				ParentIndices.push_back(i == 0
					? NullParent
					: static_cast<uint32_t>(ParentIndexFor(i, branchingFactor)));
			}
		}

		void Advance(size_t frame)
		{
			SetRootFrame(Locals[0], frame);
		}

		void Propagate()
		{
			if (Locals.empty())
				return;

			Worlds[0] = Locals[0];
			for (size_t i = 1; i < Locals.size(); ++i)
			{
				Worlds[i] = Worlds[ParentIndices[i]] * Locals[i];
			}
		}

		double Checksum() const
		{
			double checksum = 0.0;
			for (const Transform3f& world : Worlds)
				checksum += TransformChecksum(world);
			return checksum;
		}

		size_t TransformPayloadBytes() const
		{
			return (Locals.capacity() + Worlds.capacity()) * sizeof(Transform3f);
		}

		std::vector<Transform3f> Locals;
		std::vector<Transform3f> Worlds;
		std::vector<uint32_t> ParentIndices;
	};

	// Production-path fixture: drives the real TransformPropagationSystem against
	// the real entity-indexed TransformStore<Transform3f> + hierarchy services.
	//
	// Used to confirm that the production path matches the hand-written
	// contiguous fixture in both correctness and performance.
	struct ProductionPropagationFixture
	{
		using Hierarchy3f = TransformHierarchyService;
		using PropagationOrder3f = TransformPropagationOrderService;
		using Propagation3f = TransformPropagationSystem<Transform3f>;

		Hierarchy3f Hierarchy;
		PropagationOrder3f PropagationOrder;
		TransformStore<Transform3f> Transforms;
		Propagation3f Propagation;

		std::vector<EntityHandle> Keys;
		double BatchEmplaceUs = 0.0;
		double BatchRemoveUs = 0.0;
		double HierarchyUnregisterUs = 0.0;
		ProductionBatchMode BatchMode = ProductionBatchMode::Bulk;

		ProductionPropagationFixture(
			size_t count,
			size_t branchingFactor,
			ProductionBatchMode batchMode)
			: Transforms(PropagationOrder)
			, Propagation(Transforms, Hierarchy, PropagationOrder)
			, BatchMode(batchMode)
		{
			Transforms.GetItems();
			Keys.reserve(count);

			const auto emplaceStart = Clock::now();
			for (size_t i = 0; i < count; ++i)
			{
				EntityHandle entity{ static_cast<EntityId>(i + 1), 1 };
				Keys.push_back(entity);
				Transforms.Add(entity, MakeLocalTransform(i));
				Hierarchy.Register(entity);
			}
			const auto emplaceEnd = Clock::now();
			BatchEmplaceUs = ElapsedMicroseconds(emplaceStart, emplaceEnd);

			for (size_t i = 1; i < count; ++i)
			{
				Hierarchy.SetParent(Keys[i], Keys[ParentIndexFor(i, branchingFactor)]);
			}
		}

		void Advance(size_t frame)
		{
			Transform3f* root = Transforms.TryGetLocalMutable(Keys[0]);
			if (root)
				SetRootFrame(*root, frame);
		}

		void PropagateTick()
		{
			Propagation.Propagate();
		}

		double Checksum() const
		{
			double checksum = 0.0;
			for (const TransformComponent<Transform3f>& component : Transforms.GetItems())
				checksum += TransformChecksum(component.World);
			return checksum;
		}

		size_t TransformPayloadBytes() const
		{
			return Transforms.Count() * sizeof(TransformComponent<Transform3f>);
		}

		void RemoveAll()
		{
			const auto hierarchyStart = Clock::now();
			for (EntityHandle key : Keys)
				Hierarchy.Unregister(key);
			const auto hierarchyEnd = Clock::now();

			const auto removeStart = Clock::now();
			for (EntityHandle key : Keys)
				Transforms.Remove(key);
			const auto removeEnd = Clock::now();

			HierarchyUnregisterUs = ElapsedMicroseconds(hierarchyStart, hierarchyEnd);
			BatchRemoveUs = ElapsedMicroseconds(removeStart, removeEnd);

			if (Transforms.Count() != 0 || Hierarchy.Count() != 0)
				throw std::runtime_error("Production removal did not empty the fixture.");
		}
	};

	void ValidateEquivalentWorlds(
		const TraditionalFixture& traditional,
		const DataOrientedFixture& dataOriented)
	{
		if (traditional.Nodes.size() != dataOriented.Worlds.size())
			throw std::runtime_error("Fixture transform counts do not match.");

		for (size_t i = 0; i < traditional.Nodes.size(); ++i)
		{
			const Transform3f& traditionalWorld = traditional.Nodes[i]->WorldTransform;
			const Transform3f& dataWorld = dataOriented.Worlds[i];
			if (!traditionalWorld.NearlyEquals(dataWorld, static_cast<float>(ValidationEpsilon)))
			{
				throw std::runtime_error("Traditional and data-oriented world transforms diverged.");
			}
		}
	}

	void ValidateProductionMatchesTraditional(
		const TraditionalFixture& traditional,
		const ProductionPropagationFixture& production)
	{
		if (traditional.Nodes.size() != production.Keys.size())
			throw std::runtime_error("Fixture transform counts do not match.");

		for (size_t i = 0; i < traditional.Nodes.size(); ++i)
		{
			const Transform3f& traditionalWorld = traditional.Nodes[i]->WorldTransform;
			const Transform3f* productionWorld = production.Transforms.TryGetWorld(production.Keys[i]);
			if (!productionWorld
				|| !traditionalWorld.NearlyEquals(*productionWorld, static_cast<float>(ValidationEpsilon)))
			{
				throw std::runtime_error("Traditional and production-system world transforms diverged.");
			}
		}
	}

	// Measures the cost of Propagate() when the propagation cache is forced dirty
	// on every iteration. Registers a sentinel key that has no entry in any
	// TransformStore: this bumps the hierarchy version (triggering a full rebuild on the
	// next Propagate) but is silently skipped by the BFS, so results stay valid.
	// The sentinel entity uses an id well above any real benchmark entity.
	SampleStats MeasureRebuildCost(
		const RunConfig& config,
		ProductionPropagationFixture& fixture)
	{
		constexpr EntityHandle SentinelKey{ 0xFFFFu, 1 };

		// Warmup: ensure the cache is hot before measuring.
		for (size_t i = 0; i < config.WarmupIterations; ++i)
		{
			fixture.Hierarchy.Register(SentinelKey);
			fixture.Hierarchy.Unregister(SentinelKey);
			fixture.PropagateTick();
		}

		std::vector<double> samplesUs;
		samplesUs.reserve(config.MeasuredIterations);

		for (size_t i = 0; i < config.MeasuredIterations; ++i)
		{
			// Dirty the hierarchy version so the next Propagate must rebuild.
			fixture.Hierarchy.Register(SentinelKey);
			fixture.Hierarchy.Unregister(SentinelKey);

			std::atomic_signal_fence(std::memory_order_seq_cst);
			const auto start = Clock::now();
			fixture.PropagateTick();
			std::atomic_signal_fence(std::memory_order_seq_cst);
			const auto end = Clock::now();

			samplesUs.push_back(ElapsedMicroseconds(start, end));
		}

		return CalculateStats(samplesUs, fixture.Keys.size());
	}

	void PrintStats(const char* prefix, const SampleStats& stats)
	{
		std::cout << "  " << prefix << "_mean_us: " << stats.MeanUs << "\n";
		std::cout << "  " << prefix << "_median_us: " << stats.MedianUs << "\n";
		std::cout << "  " << prefix << "_p95_us: " << stats.P95Us << "\n";
		std::cout << "  " << prefix << "_min_us: " << stats.MinUs << "\n";
		std::cout << "  " << prefix << "_max_us: " << stats.MaxUs << "\n";
		std::cout << "  " << prefix << "_stddev_us: " << stats.StdDevUs << "\n";
		std::cout << "  " << prefix << "_ns_per_transform: " << stats.NsPerTransform << "\n";
	}

	void PrintResult(const BenchmarkResult& result)
	{
		std::cout << "\n" << result.Name << "\n";
		std::cout << "  setup_us: " << result.SetupUs << "\n";
		std::cout << "  batch_emplace_us: " << result.BatchEmplaceUs << "\n";
		std::cout << "  batch_remove_us: " << result.BatchRemoveUs << "\n";
		std::cout << "  hierarchy_unregister_us: " << result.HierarchyUnregisterUs << "\n";
		std::cout << "  transforms: " << result.TransformCount << "\n";
		std::cout << "  checksum: " << result.Checksum << "\n";
		std::cout << "  node_allocations: " << result.NodeAllocations << "\n";
		std::cout << "  contiguous_transform_payload_bytes: "
				  << result.ContiguousTransformPayloadBytes << "\n";
		std::cout << "  child_pointer_storage_bytes: "
				  << result.ChildPointerStorageBytes << "\n";
		std::cout << "  parent_index_storage_bytes: "
				  << result.ParentIndexStorageBytes << "\n";
		PrintStats("propagation", result.Propagation);
		PrintStats("rebuild_and_propagate", result.RebuildAndPropagate);
		const double rebuildCostUs = result.RebuildAndPropagate.MeanUs - result.Propagation.MeanUs;
		std::cout << "  rebuild_only_mean_us: " << rebuildCostUs << "\n";
		std::cout << "  rebuild_only_ns_per_transform: "
				  << (rebuildCostUs * 1000.0) / static_cast<double>(result.TransformCount) << "\n";
	}

	void PrintCsvRow(const BenchmarkResult& result)
	{
		std::cout
			<< "csv,"
			<< result.Name << ","
			<< result.TransformCount << ","
			<< result.SetupUs << ","
			<< result.BatchEmplaceUs << ","
			<< result.BatchRemoveUs << ","
			<< result.HierarchyUnregisterUs << ","
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
		DataOrientedFixture dataOriented(config.TransformCount, config.BranchingFactor);
		const auto dataSetupEnd = Clock::now();

		const auto productionScalarSetupStart = Clock::now();
		ProductionPropagationFixture productionScalar(
			config.TransformCount,
			config.BranchingFactor,
			ProductionBatchMode::Scalar);
		const auto productionScalarSetupEnd = Clock::now();

		const auto productionBulkSetupStart = Clock::now();
		ProductionPropagationFixture productionBulk(
			config.TransformCount,
			config.BranchingFactor,
			ProductionBatchMode::Bulk);
		const auto productionBulkSetupEnd = Clock::now();

		traditional.Advance(0);
		dataOriented.Advance(0);
		productionScalar.Advance(0);
		productionBulk.Advance(0);
		traditional.PropagateRecursive();
		dataOriented.Propagate();
		productionScalar.PropagateTick();
		productionBulk.PropagateTick();
		ValidateEquivalentWorlds(traditional, dataOriented);
		ValidateProductionMatchesTraditional(traditional, productionScalar);
		ValidateProductionMatchesTraditional(traditional, productionBulk);
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
		dataResult.Name = "data_oriented_contiguous";
		dataResult.TransformCount = config.TransformCount;
		dataResult.SetupUs = ElapsedMicroseconds(dataSetupStart, dataSetupEnd);
		dataResult.ContiguousTransformPayloadBytes = dataOriented.TransformPayloadBytes();
		dataResult.ParentIndexStorageBytes =
			dataOriented.ParentIndices.capacity() * sizeof(uint32_t);
		dataResult.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { dataOriented.Advance(frame); },
			[&]() { dataOriented.Propagate(); });
		dataResult.Checksum = dataOriented.Checksum();

		BenchmarkResult productionScalarResult;
		productionScalarResult.Name = "production_transform_propagation_system_scalar";
		productionScalarResult.TransformCount = config.TransformCount;
		productionScalarResult.SetupUs = ElapsedMicroseconds(
			productionScalarSetupStart,
			productionScalarSetupEnd);
		productionScalarResult.BatchEmplaceUs = productionScalar.BatchEmplaceUs;
		productionScalarResult.ContiguousTransformPayloadBytes =
			productionScalar.TransformPayloadBytes();
		productionScalarResult.ParentIndexStorageBytes = 0;
		productionScalarResult.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { productionScalar.Advance(frame); },
			[&]() { productionScalar.PropagateTick(); });
		productionScalarResult.RebuildAndPropagate = MeasureRebuildCost(config, productionScalar);
		productionScalarResult.Checksum = productionScalar.Checksum();

		productionScalar.RemoveAll();
		productionScalarResult.BatchRemoveUs = productionScalar.BatchRemoveUs;
		productionScalarResult.HierarchyUnregisterUs = productionScalar.HierarchyUnregisterUs;

		BenchmarkResult productionBulkResult;
		productionBulkResult.Name = "production_transform_propagation_system_bulk";
		productionBulkResult.TransformCount = config.TransformCount;
		productionBulkResult.SetupUs = ElapsedMicroseconds(
			productionBulkSetupStart,
			productionBulkSetupEnd);
		productionBulkResult.BatchEmplaceUs = productionBulk.BatchEmplaceUs;
		productionBulkResult.ContiguousTransformPayloadBytes =
			productionBulk.TransformPayloadBytes();
		productionBulkResult.ParentIndexStorageBytes = 0;
		productionBulkResult.Propagation = MeasurePropagation(
			config,
			[&](size_t frame) { productionBulk.Advance(frame); },
			[&]() { productionBulk.PropagateTick(); });
		productionBulkResult.RebuildAndPropagate = MeasureRebuildCost(config, productionBulk);
		productionBulkResult.Checksum = productionBulk.Checksum();

		productionBulk.RemoveAll();
		productionBulkResult.BatchRemoveUs = productionBulk.BatchRemoveUs;
		productionBulkResult.HierarchyUnregisterUs = productionBulk.HierarchyUnregisterUs;

		PrintResult(traditionalRecursive);
		PrintResult(traditionalIterative);
		PrintResult(dataResult);
		PrintResult(productionScalarResult);
		PrintResult(productionBulkResult);

		std::cout << "\nCSV\n";
		std::cout
			<< "csv,name,transforms,setup_us,batch_emplace_us,batch_remove_us,"
			<< "hierarchy_unregister_us,total_us,min_us,mean_us,median_us,"
			<< "p95_us,max_us,stddev_us,ns_per_transform,transforms_per_second,"
			<< "checksum,node_allocations,contiguous_transform_payload_bytes,"
			<< "child_pointer_storage_bytes,parent_index_storage_bytes\n";
		PrintCsvRow(traditionalRecursive);
		PrintCsvRow(traditionalIterative);
		PrintCsvRow(dataResult);
		PrintCsvRow(productionScalarResult);
		PrintCsvRow(productionBulkResult);
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "TransformHierarchyStressTest failed: " << ex.what() << "\n";
		return 1;
	}
}
