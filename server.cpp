extern "C"{
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
}

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <chrono>

#include "server.hpp"


void TCPServer::start()
{
	if(running_.load()){
		// Has been started already 
		return;
	}

	errno = 0;

	sock_ = socket(AF_INET, SOCK_STREAM, 0);
	if(sock_ < 0){
		throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
	}

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port_);

	// Enable address reusing
	int enable = 1;
	if(setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0){
		throw std::runtime_error(std::string("setsockopt() failed: ") + strerror(errno));
	}

	// Set non-blocking socket mode
	int flags = fcntl(sock_, F_GETFL, 0);
	if(flags < 0){
		throw std::runtime_error(std::string("fcntl_get() failed: ") + strerror(errno));
	}
	if(fcntl(sock_, F_SETFL, flags | O_NONBLOCK) < 0){
		throw std::runtime_error(std::string("fcntl_set() failed: ") + strerror(errno));
	}

	if(bind(sock_, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr)) < 0){
		throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));
	}

	if(listen(sock_, 10) < 0){
		throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
	}

	// Start acceptance thread - wait for connections
	acceptance_thread_ = std::thread([this](){ this->accept_handler(); });
}

void TCPServer::stop()
{
	if( !running_.load() ){
		return;
	}

	running_.store(false);

	// Stop accepting new clients
	if(acceptance_thread_.joinable()){
		acceptance_thread_.join();
	}

	// Stop active client threads 
	{
		std::lock_guard<std::mutex> lck(client_threads_mutex_);
		for(auto &elem : client_threads_){
			if(elem.second.joinable()){
				elem.second.join();
			}
		}
	}

	if(sock_ >= 0){
		close(sock_);
		sock_ = -1;
	}
}

void TCPServer::accept_handler()
{
	// New client info
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = 0;
	int client_sock = -1;

	// Prepare structures for select() with timeout call
	fd_set fs;
	FD_ZERO(&fs);
	FD_SET(sock_, &fs);
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 100 * 1000;	// 100ms

	running_.store(true);

	for(;;){
		// Wait for socket descriptor data event
		int res = select(sock_ + 1, &fs, nullptr, nullptr, &timeout);
		if(res < 0){
			std::cerr << "select() failed: " << strerror(errno) << std::endl;
			return;
		}
		else if( !FD_ISSET(sock_, &fs) ){
			// Timeout
			// Check if server was stopped
			if( !running_.load() ){
				return;
			}
		}

		// Nonblocking
		client_sock = accept(sock_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_addr_len);

		if(client_sock < 0){
			// Invalid socket
			if((errno == EWOULDBLOCK) || (errno == EAGAIN)){
				// If the socket is marked nonblocking and no pending connections are 
				// present on the queue, accept() fails with the error EAGAIN or EWOULDBLOCK.
				// This may happen if client disconnects just after select() returned.
				continue;
			}
			else{
				std::cerr << "accept() failed: " << strerror(errno) << std::endl;
				return;
			}
		}

		uint16_t client_port = ntohs(client_addr.sin_port);

		// Create new thread for client handling
		std::cout << "new connection accepted ("
			<< "sock: " << client_sock
			<< ", addr: " << ntohl(client_addr.sin_addr.s_addr)
			<< ", port: " << client_port
			<< ")" << std::endl;
	
		uint64_t id = this->client_id(client_sock, client_port);

		std::thread client_thread ([this, client_sock, id](){ 
			this->client_handler(client_sock, id);
		});

		std::lock_guard<std::mutex> lck(client_threads_mutex_);
		client_threads_[id] = std::move(client_thread);
	}
}

uint64_t TCPServer::client_id(int client_sock, uint16_t client_port)
{
	std::string id_str = std::to_string(client_sock) + std::to_string(client_port);
	return std::stoull(id_str);
}

void TCPServer::disconnect(int client_sock, uint64_t client_id)
{
	shutdown(client_sock, SHUT_RDWR);
	close(client_sock);

	// Release memory of client settings and thread object from cliet_threads_
	seq_storage_.remove(client_id);

	// Thread can't join himself, so use additional detached thread 
	// for correct client closing 
	std::thread helper([this, client_id]()
	{
		std::lock_guard<std::mutex> lck(client_threads_mutex_);
		auto it = client_threads_.find(client_id);
		if(it == client_threads_.end()){
			return;
		}

		it->second.join();
		client_threads_.erase(it);
	});

	helper.detach();
} 

void TCPServer::generate_sequence(int client_sock, uint64_t client_id)
{
	Sequence seq = seq_storage_.get(client_id);
	std::string seq_str = seq.to_str();

	if(seq_str.empty()){
		// Sequence has not benn configured - nothing to send
		std::cout << "sequence for client " << client_id << " has not been configured yet" << std::endl;
		return;
	}

	// Period of sequence generation
	int period_ms = 100;

	// Start sequence generation
	for(;;){
		
		seq_str += "\n";

		if(send(client_sock, seq_str.c_str(), seq_str.size(), MSG_NOSIGNAL) < 0){
			std::cerr << "send failed: " << strerror(errno) << std::endl;
			return;
		}

		seq.update();
		seq_str = seq.to_str();

		// Check server status while waiting - server could be stopped
		int cnt = period_ms;
		while(cnt > 0){

			if( !running_.load() ){
				return;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(period_ms / 10));
			cnt -= period_ms / 10;
		}
	}
}

void TCPServer::add_subsequence(uint64_t client_id, const std::string_view &sv)
{
	// Check if command has correct size
	if(sv.size() < 8){
		return;
	}

	// Parse input command
	std::array<std::string, 3> seq_cmd;
	std::string_view::size_type start_pos = 0;

	for(size_t i = 0; i < 2; ++i){

		auto pos = sv.find(' ', start_pos);
		if(pos == sv.npos){
			// Invalid input format
			return;
		}

		seq_cmd[i] = sv.substr(start_pos, pos - start_pos);
		start_pos = pos + 1;
	}

	seq_cmd[2] = sv.substr(start_pos);

	if((seq_cmd[0].size() < 4) || (seq_cmd[0].substr(0, 3) != "seq")){
		// Invalid seq opcode
		std::cout << "Invalid 'seq' opcode (" << seq_cmd[0] << ")" << std::endl;
		return;
	}

	try{
		int idx_val = std::stoi(seq_cmd[0].substr(3));
		int start_val = std::stoi(seq_cmd[1]);
		int step_val = std::stoi(seq_cmd[2]);

		if((idx_val < 0) || (start_val < 0) || (step_val < 0)){
			// Using only non-negative values
			return;
		}

		if(!idx_val || (idx_val > 3)){
			// Invalid sequence number
			return;
		}

		std::cout << "adding subseq " << idx_val << ": " << start_val << ", " << step_val << std::endl;
		seq_storage_.add(client_id, idx_val, start_val, step_val);
	}
	catch(const std::exception &e){
		std::cerr << "Subsequence adding failed: " << e.what() << std::endl;
		return;
	}
}

void TCPServer::process_client_input(int client_sock, uint64_t client_id, const char *cmd, size_t cmd_len)
{
	std::string_view cmd_view{cmd, cmd_len};

	// Remove '\r\n' ending if present
	auto pos = cmd_view.find('\r');
	if(pos != cmd_view.npos){
		cmd_view.remove_suffix(cmd_view.size() - pos);
	}

	if(cmd_view == "export seq"){
		this->generate_sequence(client_sock, client_id);
	}
	else{
		// Trying to add new subsequence
		this->add_subsequence(client_id, cmd_view);
	}
}

void TCPServer::client_handler(int sock, uint64_t id)
{
	try{
		char buf[128] = {0};

		// Set receive timeout
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100 * 1000;	// 100 ms
		if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0){
			std::cerr << strerror(errno) << std::endl;
			return;
		}

		// Thread main loop
		for(;;){		
			memset(buf, 0, sizeof(buf));

			// Receive with timeout
			ssize_t bytes_read = recv(sock, buf, sizeof(buf), 0);

			if(bytes_read < 0){
				// Timeout
				// Check server status
				if( !running_.load() ){
					this->disconnect(sock, id);
					return;
				}

				continue;
			}

			if(bytes_read == 0){
				std::cout << "client " << id << " disconnected" << std::endl;
				this->disconnect(sock, id);
				return;
			}

			this->process_client_input(sock, id, buf, bytes_read);
		}
	}
	catch(const std::exception &e){
		std::cerr << "client thread failed: " << e.what() << std::endl;
		this->disconnect(sock, id);
	}
}


