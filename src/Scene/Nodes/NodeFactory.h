#pragma once

#include <memory>
#include <string>
#include "Scene/Node.h"

class NodeFactory {
public:
    static std::shared_ptr<Node> CreateNode(NodeType type, const std::string& name = "", uint32_t parentId = 0);
};

