#pragma once

#include <world/transform/core/TransformServiceTags.h>
#include <world/transform/hierarchy/TransformHierarchyService.h>

class TransformHierarchy2DService
	: public TransformHierarchyService<TransformServiceTags::Transform2DTag>
{
};

class TransformHierarchy3DService
	: public TransformHierarchyService<TransformServiceTags::Transform3DTag>
{
};
