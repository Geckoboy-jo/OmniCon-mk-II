#ifndef SAVE_TREE_HPP
#define SAVE_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class SaveBPlusTree
{
public:
    void upsert(std::uint64_t key, std::vector<unsigned char> value)
    {
        if (!root) {
            root = std::make_shared<Node>(true);
            root->keys.push_back(key);
            root->values.push_back(std::move(value));
            return;
        }

        const SplitResult splitResult = insertRecursive(root, key, std::move(value));
        if (splitResult.split) {
            auto newRoot = std::make_shared<Node>(false);
            newRoot->keys.push_back(splitResult.promotedKey);
            newRoot->children.push_back(root);
            newRoot->children.push_back(splitResult.rightNode);
            root = newRoot;
        }
    }

    bool find(std::uint64_t key, std::vector<unsigned char>& value) const
    {
        const Node* node = root.get();
        while (node) {
            if (node->leaf) {
                const auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
                const std::size_t index = static_cast<std::size_t>(std::distance(node->keys.begin(), it));
                if (it == node->keys.end() || *it != key) {
                    return false;
                }
                value = node->values[index];
                return true;
            }

            const std::size_t index = childIndexForKey(node, key);
            node = node->children[index].get();
        }

        return false;
    }

    bool empty() const
    {
        return !root;
    }

private:
    static constexpr std::size_t kMaxKeys = 6;

    struct Node {
        explicit Node(bool leafNode) : leaf(leafNode) {}

        bool leaf = false;
        std::vector<std::uint64_t> keys;
        std::vector<std::shared_ptr<Node>> children;
        std::vector<std::vector<unsigned char>> values;
        Node* next = nullptr;
    };

    struct SplitResult {
        bool split = false;
        std::uint64_t promotedKey = 0;
        std::shared_ptr<Node> rightNode;
    };

    std::shared_ptr<Node> root;

    static std::size_t childIndexForKey(const Node* node, std::uint64_t key)
    {
        return static_cast<std::size_t>(std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin());
    }

    SplitResult insertRecursive(std::shared_ptr<Node>& node, std::uint64_t key, std::vector<unsigned char> value)
    {
        if (node->leaf) {
            const auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            const std::size_t index = static_cast<std::size_t>(std::distance(node->keys.begin(), it));

            if (it != node->keys.end() && *it == key) {
                node->values[index] = std::move(value);
                return {};
            }

            node->keys.insert(it, key);
            node->values.insert(node->values.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));

            if (node->keys.size() <= kMaxKeys) {
                return {};
            }

            const std::size_t splitIndex = node->keys.size() / 2;
            auto right = std::make_shared<Node>(true);
            right->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(splitIndex), node->keys.end());
            right->values.assign(node->values.begin() + static_cast<std::ptrdiff_t>(splitIndex), node->values.end());
            node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(splitIndex), node->keys.end());
            node->values.erase(node->values.begin() + static_cast<std::ptrdiff_t>(splitIndex), node->values.end());
            right->next = node->next;
            node->next = right.get();

            SplitResult result;
            result.split = true;
            result.promotedKey = right->keys.front();
            result.rightNode = std::move(right);
            return result;
        }

        const std::size_t childIndex = childIndexForKey(node.get(), key);
        SplitResult childSplit = insertRecursive(node->children[childIndex], key, std::move(value));
        if (!childSplit.split) {
            return {};
        }

        node->keys.insert(node->keys.begin() + static_cast<std::ptrdiff_t>(childIndex), childSplit.promotedKey);
        node->children.insert(node->children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1), std::move(childSplit.rightNode));

        if (node->keys.size() <= kMaxKeys) {
            return {};
        }

        const std::size_t midIndex = node->keys.size() / 2;
        const std::uint64_t promotedKey = node->keys[midIndex];
        auto right = std::make_shared<Node>(false);
        right->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(midIndex + 1), node->keys.end());
        right->children.reserve(node->children.size() - midIndex - 1);
        for (std::size_t i = midIndex + 1; i < node->children.size(); ++i) {
            right->children.push_back(std::move(node->children[i]));
        }

        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(midIndex), node->keys.end());
        node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(midIndex + 1), node->children.end());

        SplitResult result;
        result.split = true;
        result.promotedKey = promotedKey;
        result.rightNode = std::move(right);
        return result;
    }
};

#endif
