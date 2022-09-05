# Multithreaded nonblocking TCP server implementation (Linux)
## Sequences generator
Generates three 64-bit counters independantly for each connected client. 
Counter's start and step is configured with string-based messages protocol:

	* seq1 xxxx yyyy (set start value = xxxx, step = yyyy for 1st subsequence);
	* seq2 xxxx yyyy (set start value = xxxx, step = yyyy for 2nd subsequence);
	* seq3 xxxx yyyy (set start value = xxxx, step = yyyy for 3rd subsequence);
	* export seq - periodically send generated sequence back to client.