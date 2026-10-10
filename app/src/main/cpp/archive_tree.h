#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Common/MyWindows.h"
#include "7zip/Archive/IArchive.h"
#include "fd_stream.h"

struct TreeNode {
    uint64_t size;
    uint32_t parent;
    uint32_t nameOffset;
    uint32_t nameLength;
    int32_t itemIndex;
    bool isDir;
};

class ArchiveTree {
public:
    HRESULT build(IInArchive *archive, const CancelTokenPtr &token);
    size_t nodeCount() const { return nodes_.size(); }
    const TreeNode &at(uint32_t id) const { return nodes_[id]; }
    uint32_t childCount(uint32_t node) const { return childStart_[node + 1] - childStart_[node]; }
    uint32_t childId(uint32_t node, uint32_t index) const { return childIds_[childStart_[node] + index]; }
    std::u16string name(const TreeNode &n) const { return pool_.substr(n.nameOffset, n.nameLength); }

private:
    std::vector<TreeNode> nodes_;
    std::u16string pool_;
    std::vector<uint32_t> childStart_;
    std::vector<uint32_t> childIds_;
};
