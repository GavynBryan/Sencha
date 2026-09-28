#include "CandidateOperationSupport.h"

void DeclareBuiltInCandidateOperations(CandidateMeasureRegistrationScope& measures,
                                       CandidateGeneratorRegistrationScope& generators)
{
    using namespace candidate_operation;
    (void)generators.Declare(PointsGenerator());
    (void)generators.Declare(EntitiesGenerator());
    (void)generators.Declare(RingGenerator());
    (void)generators.Declare(GridGenerator());
    (void)generators.Declare(ReachableGenerator());

    (void)measures.Declare(DistanceMeasure());
    (void)measures.Declare(HeightMeasure());
    (void)measures.Declare(FacingMeasure());
    (void)measures.Declare(EntityTagsMeasure());
    (void)measures.Declare(ReachableMeasure());
    (void)measures.Declare(TravelCostMeasure());
    (void)measures.Declare(VisibilityMeasure());
    (void)measures.Declare(RouteVisibilityMeasure());
    (void)measures.Declare(AuthoredQueryMeasure());
}
