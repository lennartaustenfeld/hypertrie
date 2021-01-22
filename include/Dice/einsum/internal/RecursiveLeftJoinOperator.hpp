#ifndef HYPERTRIE_RECURSIVELEFTJOINOPERATOR_HPP
#define HYPERTRIE_RECURSIVELEFTJOINOPERATOR_HPP

#include "Dice/einsum/internal/Operator.hpp"

namespace einsum::internal {

	template<typename value_type, HypertrieTrait tr_t>
	class RecursiveLeftJoinOperator : public Operator<value_type, tr_t> {
#include "Dice/einsum/internal/OperatorMemberTypealiases.hpp"
		using RecursiveLeftJoinOperator_t = RecursiveLeftJoinOperator<value_type, tr>;

		bool ended_;

        std::shared_ptr<Operator_t> sub_operator; // the operator of the optional part
		std::shared_ptr<Operator_t> node_operator; // the operator of the non-optional part
		std::shared_ptr<Subscript> opt_subscript; // the subscript of the optional part
        std::shared_ptr<Subscript> wwd_subscript; // the subscript of the optional part for wwd cases
        std::shared_ptr<Subscript> next_subscript; // the subscript that will be passed to the sub_operator
        std::shared_ptr<Subscript> node_subscript; // the subscript of the non-optional part
		std::vector<const_Hypertrie<tr>> next_operands{}; // the operands of the optional part before slicing
 		std::vector<OperandPos> non_opt_ops_poss; // the positions of non-optional operands
        std::map<Label, LabelPossInOperands> label_poss_in_operands{};
		std::unique_ptr<Entry_t> sub_entry;

	public:
        RecursiveLeftJoinOperator(const std::shared_ptr<Subscript> &subscript, const std::shared_ptr<Context<key_part_type>> &context)
			: Operator_t(Subscript::Type::LeftJoin, subscript, context, this),
			  non_opt_ops_poss(subscript->getNonOptionalOperands()){
			ended_ = true;
			// prepare node and next subscripts
			extractSubscripts(subscript);
			// reserve next_operands size
			next_operands.resize(subscript->getRawSubscript().operands.size() - non_opt_ops_poss.size());
			// construct node operator
            node_operator = Operator_t::construct(node_subscript, this->context);
		}

        static bool ended(const void *self_raw) {
            auto &self = *static_cast<const RecursiveLeftJoinOperator_t *>(self_raw);
            return self.ended_ or self.context->hasTimedOut();
        }

        static void load(void *self_raw, std::vector<const_Hypertrie<tr>> operands, Entry_t &entry) {
            static_cast<RecursiveLeftJoinOperator *>(self_raw)->load_impl(std::move(operands), entry);
        }

        static void clear(void *self_raw) {
            return static_cast<RecursiveLeftJoinOperator_t *>(self_raw)->clear_impl();
        }

        static void next(void *self_raw) {
            RecursiveLeftJoinOperator_t &self = *static_cast<RecursiveLeftJoinOperator_t *>(self_raw);
            if constexpr (bool_value_type) {
                if (self.subscript->all_result_done) {
                    assert(self.sub_operator);
                    self.ended_ = true;
                    return;
                }
            }
			if(!self.sub_operator->ended())
			    self.sub_operator->next();
            self.find_next_valid();
            self.updateNodeEntry();
        }

	private:
        inline void clear_impl(){
            if(this->node_operator)
                this->node_operator->clear();
			if(this->sub_operator)
				this->sub_operator->clear();
        }

		inline void find_next_valid() {
            if(sub_operator->ended()) {
                if(!node_operator->ended()) {
					node_operator->next();
					if(!node_operator->ended()) {
						sub_entry->clear(default_key_part);
						sub_operator->load(std::move(slice_operands()), *sub_entry);
					}
                    else {
                        ended_ = true;
                        return;
                    }
				}
                else {
                    ended_ = true;
                    return;
                }
            }
		}

        inline void load_impl(std::vector<const_Hypertrie<tr>> operands, Entry_t &entry) {
			if constexpr (_debugeinsum_) fmt::print("RecursiveLeftJoin {}\n", this->subscript);
			this->entry = &entry;
            // split non-optional and optional operands
            std::vector<const_Hypertrie<tr>> node_operands{};
			std::uint8_t pos = 0;
            for(const auto &[op_pos, operand] : iter::enumerate(operands)) {
				if(std::find(non_opt_ops_poss.begin(), non_opt_ops_poss.end(), op_pos) != non_opt_ops_poss.end())
                    node_operands.emplace_back(std::move(operand));
				else {
					next_operands[pos] = operand;
					pos++;
				}
            }
			// load node_operator
			node_operator->load(std::move(node_operands), *this->entry);
			if(node_operator->ended() and wwd_subscript)
				next_subscript = wwd_subscript;
			else
				next_subscript = opt_subscript;
            sub_operator = Operator_t::construct(next_subscript, this->context);
            sub_entry = std::make_unique<Entry_t>(next_subscript->resultLabelCount(), Operator_t::default_key_part);
			// TODO: move them inside the if clauses
			if(!node_operator->ended()) {
				ended_ = false;
				sub_operator->load(std::move(slice_operands()), *sub_entry);
				updateNodeEntry();
			}
			else if(wwd_subscript) {
                ended_ = false;
                sub_operator->load(next_operands, *sub_entry);
                updateNodeEntry();
			}
		}

		// use the mapped labels to slice the next operands
		std::vector<const_Hypertrie<tr>> slice_operands() {
            auto sliced_operands = next_operands;
            key_part_type* mapped_value;
			// iterate over the result labels
			for(auto& node_label : node_subscript->getOperandsLabelSet()) {
                if(!label_poss_in_operands.contains(node_label))
                    continue;
				// do not slice the operand on the wwd label if the node_operator has ended
				if(node_operator->ended() and this->subscript->getWWDLabels().find(node_label) != this->subscript->getWWDLabels().end())
					continue;
                mapped_value = &(this->context->mapping[node_label]);
                const auto &poss_in_ops = label_poss_in_operands[node_label];
                for(const auto &[pos, next_operand] : iter::enumerate(next_operands)) {
                    if(poss_in_ops[pos].empty())
                        continue;
                    else {
                        // prepare the slice key for the operand
                        hypertrie::SliceKey<key_part_type> skey(next_operand.depth(), std::nullopt);
                        for (const auto &pos_in_op : poss_in_ops[pos])
                            skey[pos_in_op] = *mapped_value;
                        // slice the operand
						if(std::holds_alternative<const_Hypertrie<tr>>(next_operand[skey])) {
							auto sliced_operand = std::get<0>(next_operand[skey]);
							sliced_operands[pos] = std::move(sliced_operand);
						}
						// wwd case: if the optional operand is of depth 1, we do not care about its contents
						else {
							sliced_operands.erase(sliced_operands.begin()+pos);
						}
                    }
                }
			}
			return sliced_operands;
        }

		// it prepares the subscripts of the node_operator and sub_operator -- called by constructor
		void extractSubscripts(const std::shared_ptr<Subscript>& subscript) {
            const auto& operands_labels = subscript->getRawSubscript().operands;
            // prepare the subscript of the node operator
            std::vector<std::vector<Label>> node_operands_labels{};
            tsl::hopscotch_set<Label> non_opt_labels{};
			// the labels of the node_subscript are found in non_optional_positions of the current subscript
            for(auto& non_opt_pos : subscript->getNonOptionalOperands()) {
                node_operands_labels.emplace_back(operands_labels[non_opt_pos]);
				non_opt_labels.insert(node_operands_labels.rbegin()->begin(), node_operands_labels.rbegin()->end());
            }
			node_subscript = std::make_shared<Subscript>(node_operands_labels, subscript->getRawSubscript().result);
            // prepare the subscripts of the sub_operator
			opt_subscript = subscript->removeOperands(non_opt_ops_poss); // TODO: workaround, unnecessary creation of subscript
			// store the slicing positions
            for(auto &non_opt_label : non_opt_labels)
				if(opt_subscript->getOperandsLabelSet().find(non_opt_label) != opt_subscript->getOperandsLabelSet().end())
					label_poss_in_operands[non_opt_label] = opt_subscript->getLabelPossInOperands(non_opt_label);
			// remove evaluated labels from the optional subscript
			if(subscript->getWWDLabels().empty()) {
				opt_subscript = opt_subscript->removeLabels(non_opt_labels);
			}
			// if there are wwd edges create opt_subscript and wwd_subscript
			else {
                tsl::hopscotch_set<Label> to_remove{};
				for(auto label : non_opt_labels) {
					if(subscript->getWWDLabels().find(label) == subscript->getWWDLabels().end())
                        to_remove.insert(label);
				}
				// wwd_subscript contains the labels of wwd edges
                wwd_subscript = opt_subscript->removeLabels(to_remove);
				// opt_subscript does not contain the labels of wwd edges
                opt_subscript = wwd_subscript->removeLabels(subscript->getWWDLabels());
			}
		}

		// updates the entry of the node operator with the results of the sub_entry
		void updateNodeEntry() {
            for(auto &sub_res_label : next_subscript->getResultLabelSet())
                this->entry->key[node_subscript->getLabelPosInResult(sub_res_label)] =
                        sub_entry->key[next_subscript->getLabelPosInResult(sub_res_label)];
		}

	};
}

#endif//HYPERTRIE_RECURSIVELEFTJOINOPERATOR_HPP
