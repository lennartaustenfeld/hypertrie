#ifndef HYPERTRIE_NODECONTEXT_HPP
#define HYPERTRIE_NODECONTEXT_HPP

#include <compare>

#include "Dice/hypertrie/internal/util/CONSTANTS.hpp"
#include "Dice/hypertrie/internal/node_based/Hypertrie_traits.hpp"
#include "Dice/hypertrie/internal/node_based/NodeStorage.hpp"
#include "TaggedNodeHash.hpp"

#include <Dice/hypertrie/internal/util/CountDownNTuple.hpp>
#include <itertools.hpp>

namespace hypertrie::internal::node_based {

	template<pos_type depth, bool compressed, typename tri_t = Hypertrie_internal_t<>>
	struct SpecificNodeContainer;

	template<pos_type depth, typename tri_t = Hypertrie_internal_t<>>
	struct NodeContainer {
		TaggedNodeHash thash_{};
	protected:
		void *node_ = nullptr;
	public:
		NodeContainer(const TaggedNodeHash &thash, void *node) : thash_(thash), node_(node) {}

		[[nodiscard]] Node<depth, true, tri_t> *compressed_node() {
			return static_cast<Node<depth, true, tri_t> *>(node_);
		}

		[[nodiscard]] Node<depth, false, tri_t> *uncompressed_node() {
			return static_cast<Node<depth, false, tri_t> *>(node_);
		}

		template<bool compressed>
		[[nodiscard]] auto specific() {
			return SpecificNodeContainer<depth, compressed, tri_t>(thash_, node_);
		}

		[[nodiscard]] auto compressed() {
			return specific<true>();
		}

		[[nodiscard]] auto uncompressed() {
			return specific<false>();
		}

		[[nodiscard]] bool empty() const { return thash_ == TaggedNodeHash{}; }
	};

	template<pos_type depth, typename tri_t>
	struct SpecificNodeContainer<depth, true, tri_t> : public NodeContainer<depth, tri_t> {

		SpecificNodeContainer(const TaggedNodeHash &thash, void *node) : NodeContainer<depth, tri_t>(thash, node) {}


		[[nodiscard]] Node<depth, true, tri_t> *node() {
			return static_cast<Node<depth, true, tri_t> *>(this->node_);
		}

		operator NodeContainer<depth, tri_t>() const { return {this->thash_, this->node_}; }
	};

	template<pos_type depth,
			typename tri_t = Hypertrie_internal_t<>>
	using CompressedNodeContainer = SpecificNodeContainer<depth, true, tri_t>;

	template<pos_type depth,
			typename tri_t>
	struct SpecificNodeContainer<depth, false, tri_t> : public NodeContainer<depth, tri_t> {

		SpecificNodeContainer(const TaggedNodeHash &thash, void *node) : NodeContainer<depth, tri_t>(thash, node) {}

		[[nodiscard]] Node<depth, true, tri_t> *node() {
			return static_cast<Node<depth, false, tri_t> *>(this->node_);
		}

		operator NodeContainer<depth, tri_t>() const { return {this->thash_, this->node_}; }
	};

	template<pos_type depth,
			typename tri_t = Hypertrie_internal_t<>>
	using UncompressedNodeContainer = SpecificNodeContainer<depth, false, tri_t>;

	template<pos_type max_depth,
			typename tri_t = Hypertrie_internal_t<>,
			typename  = typename std::enable_if_t<(max_depth >= 1)>>
	class NodeContext {
	public:
		using tri = tri_t;
		/// public definitions
		using key_part_type = typename tri::key_part_type_t;
		using value_type = typename tri::value_type_t;
		template<typename key, typename value>
		using map_type = typename tri::template map_type_t<key, value>;
		template<typename key>
		using set_type = typename tri::template set_type_t<key>;
		template<pos_type depth>
		using RawKey = typename tri::template RawKey<depth>;

		template<pos_type depth>
		using RawSliceKey = typename tri::template RawSliceKey<depth>;

		template<pos_type depth>
		using NodeStorage_t = NodeStorage<depth, tri>;
	private:
		util::CountDownNTuple<NodeStorage_t, max_depth> node_storages_{};
		std::list<TaggedNodeHash> primary_nodes_{};

		template<pos_type depth>
		NodeStorage_t<depth> &getNodeStorage() {
			return std::get<depth - 1>(node_storages_);
		}

		template<pos_type depth, bool compressed>
		auto getNodeStorage() -> std::conditional_t<compressed,
				typename NodeStorage_t<depth>::CompressedNodeMap, typename NodeStorage_t<depth>::UncompressedNodeMap> & {
			if constexpr(compressed)
				return std::get<depth - 1>(node_storages_).compressed_nodes_;
			else
				return std::get<depth - 1>(node_storages_).uncompressed_nodes_;
		}

		template<pos_type depth>
		NodeContainer<depth, tri> getCompressedNode(const TaggedNodeHash &node_hash) {
			assert(node_hash.isCompressed());
			auto compressed_nodes = getNodeStorage<depth>().compressed_nodes_;
			auto found = compressed_nodes.find(node_hash);
			if (found != compressed_nodes.end())
				return {node_hash, &found.value()};
			else
				return {};
		}

		template<pos_type depth>
		NodeContainer<depth, tri> getUncompressedNode(const TaggedNodeHash &node_hash) {
			assert(node_hash.isUncompressed());
			auto uncompressed_nodes = getNodeStorage<depth>().uncompressed_nodes_;
			auto found = uncompressed_nodes.find(node_hash);
			if (found != uncompressed_nodes.end())
				return {node_hash, &found.value()};
			else
				return {};
		}

		template<pos_type depth>
		void deleteCompressedNode(const TaggedNodeHash &node_hash) {
			assert(node_hash.isCompressed());
			auto compressed_nodes = getNodeStorage<depth>().compressed_nodes_;
			compressed_nodes.erase(node_hash);
		}

		template<pos_type depth>
		void deleteCompressedNode(const NodeContainer<depth, tri> &node_container) {
			deleteCompressedNode<depth>(node_container.thash_);
		}

		template<pos_type depth>
		void deleteUncompressedNode(const TaggedNodeHash &node_hash) {
			assert(node_hash.isUncompressed());
			auto uncompressed_nodes = getNodeStorage<depth>().uncompressed_nodes_;
			uncompressed_nodes.erase(node_hash);
		}

		template<pos_type depth>
		void deleteUncompressedNode(const NodeContainer<depth, tri> &node_container) {
			deleteUncompressedNode<depth>(node_container.thash_);
		}

		template<pos_type depth>
		void deleteNode(const TaggedNodeHash &node_hash) {
			if (node_hash.isCompressed()) {
				deleteCompressedNode<depth>(node_hash);
			} else {
				deleteUncompressedNode<depth>(node_hash);
			}
		}

		template<pos_type depth>
		NodeContainer<depth, tri> newCompressedNode(RawKey<depth> key, value_type value, size_t ref_count, TaggedNodeHash hash) {
			const auto [it, success] = getNodeStorage<depth>().compressed_nodes_.insert({hash, Node<depth, true, tri>{key, value, ref_count}});
			assert(success);
			return NodeContainer<depth, tri>{hash, it.value()};
		}

		template<pos_type depth>
		NodeContainer<depth, tri> newUncompressedNode(RawKey<depth> key, value_type value, RawKey<depth> second_key, value_type second_value, size_t ref_count, TaggedNodeHash hash) {
			const auto [it, success] = getNodeStorage<depth>().uncompressed_nodes_.insert({hash, Node<depth, false, tri>{key, value, second_key, second_value, ref_count}});
		}

		/**
		 * creates a new node with the value changed. The old node is NOT deleted and must eventually be deleted afterwards.
		 */
		template<pos_type depth, bool compressed>
		NodeContainer<depth, tri> changeNodeValue(NodeContainer<depth, tri> nc, value_type value, TaggedNodeHash new_hash) {
			auto nc_ = nc.template specific<compressed>();
			if constexpr(compressed) nc_.node()->value_ = value;
			auto &nodes = getNodeStorage<depth, compressed>();
			const auto[it, success] = nodes.insert({new_hash, *nc_.node()});
			assert(success);
			// const auto removed = nodes.erase(nc_->thash_);
			// assert(removed);
			return NodeContainer<depth, tri>{new_hash, it.value()};
		}

		template<pos_type depth>
		NodeContainer<depth, tri> &getNode(const TaggedNodeHash &node_hash) {
			if (node_hash.isCompressed()) {
				getCompressedNode<depth>(node_hash);
			} else {
				getUncompressedNode<depth>(node_hash);
			}
		}

	public:

		template<pos_type depth>
		NodeContainer<depth, tri> newPrimaryNode() {
			TaggedNodeHash base_hash{depth};
			primary_nodes_.push_front(base_hash);
			auto node_storage = getNodeStorage<depth>();
			auto found = node_storage.uncompressed_nodes_.find(base_hash);
			if (found) {
				auto &node = found.value();
				++node.ref_count_;
				return NodeContainer{base_hash, &node};
			} else {
				auto nodec_inserted = node_storage.uncompressed_nodes_.insert(base_hash, Node<depth, false, tri>{});
				auto &node = nodec_inserted.first.value();
				++node.ref_count_;
				return NodeContainer{base_hash, &nodec_inserted.first.value()};
			}
		}

		template<pos_type depth>
		void removeNode(NodeContainer<depth, tri> &nodec) {
			if (nodec.compressed_node) {
				auto *node = nodec.compressed_node();
				if (--node->ref_count_ == 0)
					getNodeStorage<depth>().compressed_nodes_.erase(nodec.thash_);
			} else {
				// TODO: first implement set
				// TODO: decrement counter
				// TODO: remove from NodeStorage if counter is 0
				// TODO: remove recursively if this counter AND their counter is 0
			}


		}

		template<pos_type depth>
		bool deletePrimaryNode(TaggedNodeHash thash) {
			{ // remove the hash from primary nodes list
				auto found = std::find(primary_nodes_.begin(), primary_nodes_.end(), thash);
				if (found == primary_nodes_.end())
					return false;
				primary_nodes_.erase(found);
			}
			removeNode<depth>(thash);

			return true;
		}


		template<pos_type depth>
		bool change(NodeContainer<depth, tri> &nodec, RawKey<depth>) {
			// TODO: implement or remove?
			return false;
		}

		template<pos_type depth>
		inline auto getChildHashOrValue(NodeContainer<depth, tri> &nodec, pos_type pos, key_part_type key_part)
		-> std::optional<std::conditional<(depth > 1), TaggedNodeHash, value_type>> {
			assert (nodec.thash_.isUncompressed());
			assert(pos < depth);
			auto &edges = nodec.compressed_node->edges_[pos];
			if constexpr(depth > 1) {
				auto found = edges.find(key_part);
				if (found != edges.end()) {
					return found.second;
				} else
					return std::nullopt;
			} else { // depth == 1
				auto found = edges.find(key_part);
				if (found != edges.end()) {
					if constexpr(tri::is_bool)
						return true;
					else
						return found.second;
				} else
					return {}; // false, 0, 0.0
			}
		}

		/**
		 * Resolves a keypart by a given position
		 * @tparam depth the depth of the node container
		 * @param nodec the node container, must be a uncompressed node
		 * @param pos the position at which the key_part should be resolved
		 * @param key_part the key part
		 * @return an NodeContainer of depth - 1. It might be empty if there is no child for the given pos and key part. <br/>
		 * If depth is 1, a value_type is return
		 */
		template<pos_type depth>
		inline auto getChild(NodeContainer<depth, tri> &nodec, pos_type pos, key_part_type key_part)
		-> std::conditional<(depth > 1), NodeContainer<depth - 1, tri>, value_type> {
			// TODO: use getChildHashOrValue
			assert (nodec.thash_.isUncompressed());
			assert(pos < depth);
			auto &edges = nodec.compressed_node->edges_[pos];
			if constexpr(depth > 1) {
				auto child_hash_opt = getChildHashOrValue<depth>(nodec, pos, key_part);
				if (child_hash_opt.has_value()) {
					TaggedNodeHash child_hash = child_hash_opt.value();
					if (child_hash.isCompressed()) {
						return getCompressedNode<depth>(child_hash);
					} else {
						return getUncompressedNode<depth>(child_hash);
					}
				} else {
					return {};
				}
			} else { // depth == 1
				auto value_opt = getChildHashOrValue<depth>(nodec, pos, key_part);
				if (value_opt.has_value()) {
					return value_opt.value();
				} else
					return {}; // false, 0, 0.0
			}
		}

		template<pos_type depth>
		inline void deleteChild(NodeContainer<depth, tri> &nodec, pos_type pos, key_part_type key_part) {
			assert (nodec.thash_.isUncompressed());
			assert(pos < depth);
			auto &edges = nodec.compressed_node->edges_[pos];
			edges.erase(key_part);
		}

		template<class K, class V>
		using Map = container::boost_flat_map<K, V>;
		template<class K>
		using Set = container::boost_flat_set<K>;

		/**
		 *
		 * @tparam depth
		 * @param nodec
		 * @param key
		 * @return old value
		 */template<pos_type depth>
		auto set(NodeContainer<depth, tri> &nodec, RawKey<depth> key, value_type value) -> value_type {
			auto[old_value, _] = set_rek<depth, depth>({nodec}, {{nodec, 1}}, key, value);
			return old_value;
		}

		struct set_rek_result {
			value_type old_value_;
			bool done_ = false;
		};


		enum struct InsertOp : unsigned int {
			CHANGE_VALUE = 0,
			INSERT_TWO_KEY_UC_NODE,
			INSERT_C_NODE,
			EXPAND_UC_NODE,
			EXPAND_C_NODE
		};

		template<pos_type depth>
		struct PlannedUpdate {

			InsertOp insert_op{};
			TaggedNodeHash hash_before{};
			RawKey<depth> sub_key{};
			value_type value{};
			RawKey<depth> second_sub_key{};
			value_type second_value{};

		public:
			auto hashAfter(const value_type &old_value) const -> TaggedNodeHash {
				TaggedNodeHash next_hash = (hash_before != TaggedNodeHash{}) ? TaggedNodeHash(depth) : hash_before;
				switch (insert_op) {
					case InsertOp::CHANGE_VALUE:
						assert(next_hash != TaggedNodeHash(depth));
						next_hash.changeValue(old_value, value);
						break;
					case InsertOp::INSERT_TWO_KEY_UC_NODE:
						assert(next_hash == TaggedNodeHash(depth));
						next_hash.addFirstEntry(sub_key, value);
						next_hash.addEntry(second_sub_key, second_value);
						break;
					case InsertOp::INSERT_C_NODE:
						assert(next_hash == TaggedNodeHash(depth));
						next_hash.addFirstEntry(sub_key, value);
						break;
					case InsertOp::EXPAND_UC_NODE:
						assert(next_hash != TaggedNodeHash(depth) and next_hash.isUncompressed());
						next_hash.addEntry(sub_key, value);
						break;
					case InsertOp::EXPAND_C_NODE:
						assert(next_hash != TaggedNodeHash(depth) and next_hash.isCompressed());
						next_hash.addEntry(sub_key, value);
						break;
				}
				return next_hash;
			}

			auto operator<=>(const PlannedUpdate<depth> &other) const = default;

		};


		template<pos_type depth, pos_type total_depth>
		auto set_rek(
				std::vector<std::tuple<NodeContainer<depth, tri>, RawKey<depth>, value_type>> node_cs,
				std::vector<std::tuple<RawKey<depth>, value_type, RawKey<depth>, value_type>> expand_uc,
				bool &only_value_changes,
				const value_type &old_value,
				const value_type new_value
		) -> set_rek_result {
			bool change_only_the_value = false;


			Set<PlannedUpdate<depth - 1>> planned_updates{};

			std::vector<std::tuple<NodeContainer<depth - 1, tri>, RawKey<depth - 1>, value_type>> next_node_cs{};

			std::vector<std::tuple<RawKey<depth - 1>, value_type, RawKey<depth - 1>, value_type>> next_expand_uc{};
			// descending
			if constexpr(depth > 1) { // we only need to look at what to do with subtries for a depth > 1.
				for (auto &[nodec, key, value] : node_cs) {
					for (pos_type pos: iter::range(depth)) {
						RawKey<depth - 1> sub_key = subkey<depth>(key, pos);
						PlannedUpdate<depth - 1> planned_update{};
						planned_update.sub_key = sub_key;
						planned_update.value = value;
						auto child_hash_opt = getChildHashOrValue(nodec, pos, key[pos]);
						if (child_hash_opt.has_value()) {
							TaggedNodeHash child_hash = child_hash_opt.value();
							planned_update.hash_before = child_hash;
							if (child_hash.isCompressed()) {
								NodeContainer<depth - 1, tri> childc = getCompressedNode<depth - 1>(child_hash);
								auto *child = childc.compressed_node();
								if (child->key_ == sub_key) {
									if (child->value_ == value) {
										if (value == new_value) {
											return {value, true}; // nothing left to be done. The value is already set.
										}
									} else { // child->value_ != value
										if (value == new_value) {
											only_value_changes = true;
										}
										planned_update.insert_op = InsertOp::CHANGE_VALUE;
									}
								} else { // child->key_ != sub_key
									planned_update.second_sub_key = child->key_;
									planned_update.second_value = child->value_;
									next_expand_uc.push_back(
											{{{child->key_, child->value_}, {sub_key, value}}});
									planned_update.insert_op = InsertOp::EXPAND_C_NODE;
								}
							} else { // child_hash.isUncompressed()
								next_node_cs.push_back(
										{getUncompressedNode<depth - 1>(child_hash), sub_key, value});
								planned_update.insert_op = InsertOp::EXPAND_UC_NODE;
							}
						} else { // not child_hash_opt.has_value()
							// no child exists for that key_part at that position
							planned_update.insert_op = InsertOp::INSERT_C_NODE;
						}
						planned_updates.insert(std::move(planned_update));
					}
				}
				// creating uncompressed nodes with two keys (expanded compressed nodes)
				for (auto &[key, value, second_key, second_value] : expand_uc) {
					for (pos_type pos: iter::range(depth)) {
						RawKey<depth - 1> sub_key = subkey<depth>(key, pos);
						RawKey<depth - 1> second_sub_key = subkey<depth>(second_key, pos);
						if (key[pos] == second_key[pos]) {
							PlannedUpdate<depth - 1> planned_update{};
							planned_update.sub_key = sub_key;
							planned_update.value = value;
							planned_update.second_sub_key = second_sub_key;
							planned_update.second_value = second_value;
							planned_update.insert_op = InsertOp::INSERT_TWO_KEY_UC_NODE;
							planned_updates.insert(std::move(planned_update));
							expand_uc.insert({sub_key, value, second_sub_key, second_value});
						} else {
							PlannedUpdate<depth - 1> planned_update{};
							planned_update.sub_key = sub_key;
							planned_update.value = value;
							planned_update.insert_op = InsertOp::INSERT_C_NODE;
							planned_updates.insert(std::move(planned_update));

							PlannedUpdate<depth - 1> second_planned_update{};
							second_planned_update.sub_key = second_sub_key;
							second_planned_update.value = second_value;
							second_planned_update.insert_op = InsertOp::INSERT_C_NODE;
							planned_updates.insert(std::move(second_planned_update));
						}
					}
				}
				set_rek_result result = set_rek<depth - 1, total_depth>(next_node_cs, next_expand_uc,
																		only_value_changes, new_value);
				if (result.done_)
					return result;
			}

			// ascending
			std::map<TaggedNodeHash, long> count_changes{};
			for (PlannedUpdate<depth - 1> &planned_update : planned_updates) {
				if(planned_update.hash_before)
					count_changes[planned_update.hash_before]--;
				if(auto hash_after = planned_update.hashAfter(old_value); hash_after)
					count_changes[hash_after]++;
			}

			auto pop_count_change = [&](TaggedNodeHash hash) -> std::tuple<bool, long> {
				if (auto changed = count_changes.find(hash); changed != count_changes.end()) {
					auto diff = changed->second;
					count_changes.erase(changed);
					return { true, diff };
				} else return {false, 0};
			};

			Set<TaggedNodeHash> nodes_to_remove{};

			for (PlannedUpdate<depth - 1> &planned_update : planned_updates) {
				
				const TaggedNodeHash hash_before = planned_update.hash_before;
				const auto[update_node_before, node_before_count_diff] = pop_count_change(hash_before);
				const TaggedNodeHash hash_after = planned_update.hashAfter(old_value);
				const auto[update_node_after, node_after_count_diff] = pop_count_change(hash_after);
				// TODO: work in progress



				switch (planned_update.insert_op) {
					case InsertOp::CHANGE_VALUE:
					{
						if (hash_before.isCompressed()) {
							if (update_node_after) {
								bool reuse_node_before = false;
								CompressedNodeContainer<depth - 1, tri_t> nc_before = getCompressedNode<depth - 1>(
										planned_update.hash_before).compressed();
								assert(not nc_before.empty());
								if (update_node_before) {

									Node<depth - 1, true, tri_t> *node_before = nc_before.node();
									node_before->ref_count_ += node_before_count_diff;
									if (node_before->ref_count_ == 0)
										reuse_node_before = true;
								}

								CompressedNodeContainer<depth - 1, tri_t> nc_after = getCompressedNode<depth - 1>(
										planned_update.hash_after).compressed();
								if (reuse_node_before){  // node before ref_count is zero -> maybe reused
									if ( nc_after.empty() ) { // node_after doesn't exit already
										nc_before.node()->ref_count_ += node_after_count_diff;
										changeNodeValue<true>(nc_before, planned_update.value, planned_update.new_hash);
										nodes_to_remove.insert(hash_before);
									} else {
										// reinsert hash_before, so that node_before con be reused by another change later
										count_changes[hash_before] = 0;
									}
								} else {
									if (nc_after.empty()) { // node_after doesn't exit already
										newCompressedNode<depth - 1>(nc_before.node()->key_, planned_update.value,
																	 node_after_count_diff, hash_after);
									}else {
										nc_after.node()->ref_count_ +=
										}
									}

								}
						} else { // uncompressed

						}
					}
						break;
					case InsertOp::INSERT_TWO_KEY_UC_NODE:
						break;
					case InsertOp::INSERT_C_NODE:
						break;
					case InsertOp::EXPAND_UC_NODE:
						break;
					case InsertOp::EXPAND_C_NODE:
						break;
				}
				
				if (planned_update.hash_before) { // not empty
					NodeContainer<depth - 1, tri> nc_before = getNode<depth - 1>(planned_update.hash_before);
					
					if (planned_update.hash_before.isCompressed()){
						auto *node_before = nc_before.compressed_node();
						node_before->ref_count_ += node_before_count_diff;

						NodeContainer<depth - 1, tri> nc_after = getUncompressedNode<depth - 1>(planned_update.hash_before);
						if(nc_after.empty()) { // hashafter doesn't exists

							nc_after = newUncompressedNode<depth - 1>(node_before->key_, node_before->value_, planned_update.sub_key, planned_update.value, hash_after);
						} else
						if (node_before->ref_count_ == 0)
							removeNode(nc_before);
					} else {
						auto *node = nc_before.uncompressed_node();
						node->ref_count_ += node_before_count_diff;
						if (node->ref_count_ == 0)
							removeNode(nc_before);
					}
				}


				}

			for (auto &node_hash : nodes_to_remove)
				deleteNode(node_hash);

			if constexpr(depth == 1) {

			}

			// TODO: find required changes.
			// TODO: do required changes.
			// TODO: update references in nodes accordingly
			Map<TaggedNodeHash, PlannedUpdate<depth - 1>> required_updates;
			for (auto &planned_update : planned_updates) {

			}


		}

		template<pos_type depth>
		static auto subkey = &tri::subkey;

		/**
		 * Retrieves the value for a key.
		 * @tparam depth the depth of the node container
		 * @param nodec the node container
		 * @param key the key
		 * @return the value associated to the key.
		 */
		template<pos_type depth>
		auto get(NodeContainer<depth, tri> &nodec, RawKey<depth> key) -> value_type {
			if (nodec.thash_.isCompressed()) {
				auto *node = nodec.compressed_node();
				if (node->key_ == key) {
					if constexpr(tri::is_bool) return true;
					else return node->value_;
				} else
					return {};
			} else if constexpr(depth > 1) {
				// TODO: implement minCardPos();
				auto pos = 0;//minCardPos();
				NodeContainer<depth - 1, tri> child = get<depth>(nodec, pos, key[pos]);
				if (not child.empty()) {
					return get<depth - 1>(child, subkey<depth>(key, pos));
				} else {
					return {}; // false, 0, 0.0
				}
			} else {
				return get<1>(nodec, 0, key[0]);
			}
		}

		template<pos_type depth>
		bool slice(NodeContainer<depth, tri> &nodec, RawSliceKey<depth>) {
			return false;
		}

		template<pos_type depth>
		bool size(NodeContainer<depth, tri> &nodec) {

		}


	};
}

#endif //HYPERTRIE_NODECONTEXT_HPP
