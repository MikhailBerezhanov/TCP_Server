/*============================================================================== 
Descr: 		Multithreaded TCP\IP server, that generates client-specified 
			64-bit sequences. Uses string-based messages protocol:

			* seq1 xxxx yyyy (set start value = xxxx, step = yyyy for 1st subsequence);
			* seq2 xxxx yyyy (set start value = xxxx, step = yyyy for 2nd subsequence);
			* seq3 xxxx yyyy (set start value = xxxx, step = yyyy for 3rd subsequence);
			* export seq - periodically send generated sequence back to client.

Author: 	berezhanov.m@gmail.com
Date:		22.08.2022
Version: 	1.0
==============================================================================*/

#include <iostream>
#include <csignal>
#include <cstring>
#include <atomic>
#include <limits>

extern "C"{
#include "unistd.h"
}

#include "server.hpp"

static std::atomic<int> stop_main;

static void signal_handler(std::initializer_list<int> signals)
{
	stop_main.store(0);

	auto signal_handler = [](int sig_num){ stop_main.store(sig_num); };

	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;        
	sigemptyset(&act.sa_mask);     

	for(const auto &sig : signals){
		sigaddset(&act.sa_mask, sig);
		sigaction(sig, &act, nullptr);
	}                                        
}


int main(int argc, char* argv[])
{
	signal_handler({SIGINT, SIGQUIT, SIGTERM});

	uint16_t port = 8080;

	// Configure server port or use default
	if(argc > 1){
		int custom_port = std::atoi(argv[1]);
		if((custom_port > 0) && (custom_port <= std::numeric_limits<uint16_t>::max())){
			port = custom_port;
		}
	}

	try{
		TCPServer serv{port};
		serv.start();

		std::cout << "server starts (port: " << serv.get_port() << ")" << std::endl;

		for(;;){
			sleep(10);	// signals interrupt sleep

			if(stop_main.load()){
				serv.stop();
				break;
			}
		}
	}
	catch(const std::exception &e){
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}