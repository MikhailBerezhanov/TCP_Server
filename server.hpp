#pragma once

#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "sequence.hpp"


class TCPServer
{
public:
	TCPServer(uint16_t port = 8080): port_(port) {}
	~TCPServer(){ this->stop(); }

	void start();
	void stop();
	uint16_t get_port() const { return port_; }

private:
	int sock_ = -1;
	uint16_t port_ = 0;

	// Nonblocking connection waiter
	std::thread acceptance_thread_;

	// Server status
	std::atomic<bool> running_{false};

	// Thread objects storage (client_id <-> thread)
	mutable std::mutex client_threads_mutex_;
	std::unordered_map<uint64_t, std::thread> client_threads_;

	// Clients settings storage (empty by default)
	SequenceStorage seq_storage_;

	void accept_handler();

	uint64_t client_id(int client_sock, uint16_t client_port);

	void client_handler(int client_sock, uint64_t client_id);
	void disconnect(int client_sock, uint64_t client_id);
	void process_client_input(int client_sock, uint64_t client_id, const char *cmd, size_t cmd_len);
	void generate_sequence(int client_sock, uint64_t client_id);
	void add_subsequence(uint64_t client_id, const std::string_view &sv);
};