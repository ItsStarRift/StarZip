#include "archive_tree.h"

#include <algorithm>
#include <map>
#include <utility>

#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "7zip/PropID.h"

namespace {

void AppendUtf16(std::u16string &out, const wchar_t *text) {
    for (; *text; ++text) {
        uint32_t cp = static_cast<uint32_t>(*text);
        if (cp > 0x10FFFF) cp = 0xFFFD;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
}

char16_t Fold(char16_t c) {
    return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 32) : c;
}

int CompareRange(const char16_t *a, uint32_t aLen, const char16_t *b, uint32_t bLen) {
    const uint32_t common = aLen < bLen ? aLen : bLen;
    for (uint32_t i = 0; i < common; ++i) {
        const char16_t x = Fold(a[i]);
        const char16_t y = Fold(b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    if (aLen != bLen) return aLen < bLen ? -1 : 1;
    for (uint32_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

}

HRESULT ArchiveTree::build(IInArchive *archive, const CancelTokenPtr &token) {
    nodes_.clear();
    pool_.clear();
    childStart_.clear();
    childIds_.clear();
    if (!archive) return E_FAIL;

    UInt32 total = 0;
    HRESULT res = archive->GetNumberOfItems(&total);
    if (res != S_OK) return res;

    nodes_.reserve(static_cast<size_t>(total) + 1);
    TreeNode root{};
    root.itemIndex = -1;
    root.isDir = true;
    nodes_.push_back(root);

    {
        std::map<std::pair<uint32_t, std::u16string>, uint32_t> dirs;
        std::u16string path;
        std::vector<std::pair<size_t, size_t>> parts;

        auto addNode = [&](uint32_t parent, const std::u16string &name, bool isDir,
                           uint64_t size, int32_t itemIndex) -> int64_t {
            if (nodes_.size() >= 0x7FFFFFF0u || pool_.size() + name.size() >= 0xFFFFFFF0u) return -1;
            TreeNode node{};
            node.size = size;
            node.parent = parent;
            node.nameOffset = static_cast<uint32_t>(pool_.size());
            node.nameLength = static_cast<uint32_t>(name.size());
            node.itemIndex = itemIndex;
            node.isDir = isDir;
            pool_.append(name);
            nodes_.push_back(node);
            return static_cast<int64_t>(nodes_.size() - 1);
        };

        for (UInt32 i = 0; i < total; ++i) {
            if (token && token->flag.load(std::memory_order_relaxed)) return E_ABORT;

            bool isDir = false;
            uint64_t size = 0;
            path.clear();
            {
                NWindows::NCOM::CPropVariant prop;
                res = archive->GetProperty(i, kpidPath, &prop);
                if (res != S_OK) return res;
                if (prop.vt == VT_BSTR && prop.bstrVal) AppendUtf16(path, prop.bstrVal);
            }
            {
                NWindows::NCOM::CPropVariant prop;
                res = archive->GetProperty(i, kpidIsDir, &prop);
                if (res != S_OK) return res;
                if (prop.vt == VT_BOOL) isDir = VARIANT_BOOLToBool(prop.boolVal);
            }
            {
                NWindows::NCOM::CPropVariant prop;
                res = archive->GetProperty(i, kpidSize, &prop);
                if (res != S_OK) return res;
                UInt64 value = 0;
                if (ConvertPropVariantToUInt64(prop, value)) size = value;
            }

            parts.clear();
            size_t start = 0;
            for (size_t k = 0; k <= path.size(); ++k) {
                if (k == path.size() || path[k] == u'/') {
                    if (k > start) parts.emplace_back(start, k - start);
                    start = k + 1;
                }
            }
            if (parts.empty()) {
                path = u"[Content]";
                parts.emplace_back(0, path.size());
            }

            uint32_t current = 0;
            for (size_t k = 0; k < parts.size(); ++k) {
                const bool last = (k + 1 == parts.size());
                const std::u16string name = path.substr(parts[k].first, parts[k].second);
                if (last && !isDir) {
                    if (addNode(current, name, false, size, static_cast<int32_t>(i)) < 0)
                        return E_OUTOFMEMORY;
                    break;
                }
                auto key = std::make_pair(current, name);
                auto it = dirs.find(key);
                uint32_t id;
                if (it == dirs.end()) {
                    int64_t created = addNode(current, name, true, 0,
                                              last ? static_cast<int32_t>(i) : -1);
                    if (created < 0) return E_OUTOFMEMORY;
                    id = static_cast<uint32_t>(created);
                    dirs.emplace(std::move(key), id);
                } else {
                    id = it->second;
                    if (last && nodes_[id].itemIndex < 0) nodes_[id].itemIndex = static_cast<int32_t>(i);
                }
                current = id;
            }
        }
    }

    const size_t count = nodes_.size();
    childStart_.assign(count + 1, 0);
    for (size_t id = 1; id < count; ++id) childStart_[nodes_[id].parent + 1]++;
    for (size_t k = 0; k < count; ++k) childStart_[k + 1] += childStart_[k];
    childIds_.resize(count - 1);
    std::vector<uint32_t> fill(childStart_.begin(), childStart_.end() - 1);
    for (size_t id = 1; id < count; ++id)
        childIds_[fill[nodes_[id].parent]++] = static_cast<uint32_t>(id);

    for (size_t k = 0; k < count; ++k) {
        auto first = childIds_.begin() + childStart_[k];
        auto last = childIds_.begin() + childStart_[k + 1];
        if (last - first < 2) continue;
        std::sort(first, last, [this](uint32_t x, uint32_t y) {
            const TreeNode &a = nodes_[x];
            const TreeNode &b = nodes_[y];
            if (a.isDir != b.isDir) return a.isDir;
            int c = CompareRange(pool_.data() + a.nameOffset, a.nameLength,
                                 pool_.data() + b.nameOffset, b.nameLength);
            if (c != 0) return c < 0;
            return x < y;
        });
    }
    return S_OK;
}
