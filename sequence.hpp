#pragma once

#include <cstdint>
#include <limits>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <map>
#include <mutex>
#include <string_view>

class SubSequence
{
public:
	using counter_type = uint64_t;

	SubSequence(uint start = 0, uint step = 0): start_(start), step_(step), counter_(start) {}

	void update() noexcept
	{ 
		// Counter overflow check
		counter_type threshold = std::numeric_limits<counter_type>::max() - counter_;

		if(threshold < step_){
			counter_ = start_;
		}
		else{
			counter_ += step_;
		}
	}

	counter_type get_counter() const { return counter_; }

private:
	uint start_ = 0;
	uint step_ = 0;
	counter_type counter_ = 0;
};


class Sequence
{
public:

	void add_subsequence(uint sub_idx, uint start, uint step)
	{
		if( !start || !step ){
			// Ignore zero values
			return;
		}

		subsequnces_[sub_idx] = std::move(SubSequence(start, step));
	}

	std::string to_str() const
	{
		std::string res;

		for(const auto &elem : subsequnces_){
			res += std::to_string( elem.second.get_counter() ) + " ";
		}

		// Remove last space " " if present
		if( !res.empty() ){
			res.pop_back();
		}

		return res;
	}

	void update() noexcept
	{
		for(auto &elem : subsequnces_){
			elem.second.update();
		}
	}

private:
	// Index <-> subsequence  
	std::map<uint, SubSequence> subsequnces_;
};


class SequenceStorage
{
public:
	using key_type = uint64_t;

	void add(key_type key, uint sub_idx, uint start, uint step);

	// Returns copy of stored object that can be changed independently
	Sequence get(key_type key) const;

	void remove(key_type key);

private:
	mutable std::mutex seq_table_mutex_;
	// Client id <-> corresponding sequence settings
	std::unordered_map<key_type, Sequence> seq_table_;
};