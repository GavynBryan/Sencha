#include <navigation/NavigationTypes.h>

const char* NavStatusName(NavStatus status)
{
    switch (status)
    {
    case NavStatus::Success: return "success";
    case NavStatus::Partial: return "partial";
    case NavStatus::NoPath: return "no path";
    case NavStatus::InvalidStart: return "invalid start";
    case NavStatus::InvalidDestination: return "invalid destination";
    case NavStatus::ZoneUnavailable: return "zone unavailable";
    case NavStatus::ProfileUnavailable: return "profile unavailable";
    case NavStatus::StaleLocation: return "stale location";
    case NavStatus::CrossZoneUnsupported: return "cross-zone unsupported";
    case NavStatus::SearchLimitReached: return "search limit reached";
    case NavStatus::OutputCapacityReached: return "output capacity reached";
    }
    return "unknown";
}

NavRouteBuffer::NavRouteBuffer(std::size_t maxSteps, std::size_t maxCorners,
                               std::size_t maxDependencies)
    : Steps_(maxSteps)
    , Corners_(maxCorners)
    , Tiles_(maxDependencies)
    , Links_(maxDependencies)
{
}

void NavRouteBuffer::Clear()
{
    Zone = ZoneId{};
    Generation = 0;
    Profile = 0;
    Cost = 0.0f;
    WalkDistance = 0.0f;
    StepCount_ = 0;
    CornerCount_ = 0;
    TileCount_ = 0;
    LinkCount_ = 0;
}

NavRouteStep* NavRouteBuffer::PushStep()
{
    if (StepCount_ >= Steps_.size())
        return nullptr;
    NavRouteStep& step = Steps_[StepCount_++];
    step = NavRouteStep{};
    return &step;
}

bool NavRouteBuffer::PushCorner(const Vec3d& corner)
{
    if (CornerCount_ >= Corners_.size())
        return false;
    Corners_[CornerCount_++] = corner;
    return true;
}

bool NavRouteBuffer::AddTile(NavTileCoord tile, std::uint64_t revision)
{
    for (std::size_t i = 0; i < TileCount_; ++i)
        if (Tiles_[i].Tile == tile)
            return true;
    if (TileCount_ >= Tiles_.size())
        return false;
    Tiles_[TileCount_++] = NavRouteTileDependency{ tile, revision };
    return true;
}

bool NavRouteBuffer::AddLink(NavLinkId link, std::uint32_t revision)
{
    for (std::size_t i = 0; i < LinkCount_; ++i)
        if (Links_[i].Link == link)
            return true;
    if (LinkCount_ >= Links_.size())
        return false;
    Links_[LinkCount_++] = NavRouteLinkDependency{ link, revision };
    return true;
}

NavReachableBuffer::NavReachableBuffer(std::size_t capacity)
    : Regions_(capacity)
{
}

bool NavReachableBuffer::Push(const NavReachableRegion& region)
{
    if (Count_ >= Regions_.size())
        return false;
    Regions_[Count_++] = region;
    return true;
}
