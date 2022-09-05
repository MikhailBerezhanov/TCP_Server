#include "sequence.hpp"



void SequenceStorage::add(key_type key, uint sub_idx, uint start, uint step)
{
	std::lock_guard<std::mutex> lck(seq_table_mutex_);
	seq_table_[key].add_subsequence(sub_idx, start, step);
}

Sequence SequenceStorage::get(key_type key) const
{
	Sequence res;

	std::lock_guard<std::mutex> lck(seq_table_mutex_);
	auto it = seq_table_.find(key);
	if(it != seq_table_.end()){
		res = it->second;
	}

	return res;
}

void SequenceStorage::remove(key_type key)
{
	std::lock_guard<std::mutex> lck(seq_table_mutex_);
	seq_table_.erase(key);
}