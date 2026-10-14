// physics.trace and physics.overlap through the authored query dispatcher, so every
// answer is checked against the declared result.

#include <authored/AuthoredQueryDispatcher.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>

#include <gtest/gtest.h>

#include <optional>

namespace
{
const EntityId kBox{ 7, 1 };
const EntityId kNear{ 8, 1 };

AuthoredValue Vector(double x, double y, double z)
{
    AuthoredVectorValue vector;
    vector.Length = 3;
    vector.Components[0] = x;
    vector.Components[1] = y;
    vector.Components[2] = z;
    return AuthoredValue::Vector(vector);
}

class PhysicsAuthoredQueryTest : public ::testing::Test
{
protected:
    PhysicsAuthoredQueryTest()
    {
        Box(kBox, Vec3d::Zero());
        Box(kNear, Vec3d(0.0f, 0.0f, 3.0f));
        AuthoredQueryRegistrationScope scope(Registry, "engine");
        DeclarePhysicsQueries(scope);
        EXPECT_TRUE(scope.Commit());
        Trace = Dispatcher.Bind<&EvaluatePhysicsTrace>(Registry.Find(kPhysicsTraceQuery), Queries);
        Overlap = Dispatcher.Bind<&EvaluatePhysicsOverlap>(Registry.Find(kPhysicsOverlapQuery), Queries);
    }

    void Box(EntityId entity, Vec3d at)
    {
        BodyDesc box;
        box.Shape = CollisionShape::MakeBox(Vec3d(1.0f, 1.0f, 1.0f));
        box.Position = at;
        box.Motion = BodyMotion::Static;
        box.Layer = CollisionLayer::Static;
        box.UserData = PackEntity(entity);
        (void)Physics.AddBody(box);
    }

    AuthoredQueryStatus Ask(std::string_view query, std::span<const AuthoredValue> arguments)
    {
        return Dispatcher.Evaluate(Registry.Resolve(query), arguments, Result);
    }

    PhysicsWorld Physics;
    PhysicsQueries Queries{ Physics };
    AuthoredQueryRegistry Registry;
    AuthoredQueryDispatcher Dispatcher{ Registry };
    AuthoredQueryBindingToken Trace;
    AuthoredQueryBindingToken Overlap;
    AuthoredValue Result;
};
}

TEST_F(PhysicsAuthoredQueryTest, ATraceAnswersWhatItStruckAndWhere)
{
    const AuthoredValue arguments[] = { Vector(0, 5, 0), Vector(0, -1, 0), AuthoredValue::Float(10.0), {} };
    ASSERT_EQ(Ask(kPhysicsTraceQuery, arguments), AuthoredQueryStatus::Value);
    ASSERT_EQ(Result.Children().size(), 4u);
    bool hit = false;
    EntityId entity;
    AuthoredVectorValue point;
    ASSERT_TRUE(Result.Children()[0].TryGetBool(hit));
    ASSERT_TRUE(Result.Children()[1].TryGetEntity(entity));
    ASSERT_TRUE(Result.Children()[2].TryGetVector(point));
    EXPECT_TRUE(hit);
    EXPECT_EQ(entity, kBox);
    EXPECT_NEAR(point.Components[1], 1.0, 0.05);
}

TEST_F(PhysicsAuthoredQueryTest, AMissIsAnAnswerNotAnAbsence)
{
    const AuthoredValue arguments[] = { Vector(20, 5, 0), Vector(0, -1, 0), AuthoredValue::Float(10.0), {} };
    ASSERT_EQ(Ask(kPhysicsTraceQuery, arguments), AuthoredQueryStatus::Value);
    bool hit = true;
    ASSERT_TRUE(Result.Children()[0].TryGetBool(hit));
    EXPECT_FALSE(hit);
}

TEST_F(PhysicsAuthoredQueryTest, ATracePassesThroughTheEntityItIgnores)
{
    const AuthoredValue arguments[] = { Vector(0, 0, -5), Vector(0, 0, 1), AuthoredValue::Float(20.0),
                                        AuthoredValue::Entity(kBox) };
    ASSERT_EQ(Ask(kPhysicsTraceQuery, arguments), AuthoredQueryStatus::Value);
    EntityId entity;
    ASSERT_TRUE(Result.Children()[1].TryGetEntity(entity));
    EXPECT_EQ(entity, kNear);
}

TEST_F(PhysicsAuthoredQueryTest, AnOverlapListsEveryEntityButTheIgnoredOne)
{
    const AuthoredValue everyone[] = { Vector(0, 0, 1.5), AuthoredValue::Float(2.0), {} };
    ASSERT_EQ(Ask(kPhysicsOverlapQuery, everyone), AuthoredQueryStatus::Value);
    EXPECT_EQ(Result.Children().size(), 2u);

    const AuthoredValue others[] = { Vector(0, 0, 1.5), AuthoredValue::Float(2.0), AuthoredValue::Entity(kBox) };
    ASSERT_EQ(Ask(kPhysicsOverlapQuery, others), AuthoredQueryStatus::Value);
    ASSERT_EQ(Result.Children().size(), 1u);
    EntityId entity;
    ASSERT_TRUE(Result.Children()[0].TryGetEntity(entity));
    EXPECT_EQ(entity, kNear);
}

TEST_F(PhysicsAuthoredQueryTest, ANegativeRangeIsRefused)
{
    const AuthoredValue arguments[] = { Vector(0, 5, 0), Vector(0, -1, 0), AuthoredValue::Float(-1.0), {} };
    EXPECT_EQ(Ask(kPhysicsTraceQuery, arguments), AuthoredQueryStatus::InvalidArguments);
}
