#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

void quit(char* msg, int code) {
	puts(msg);
	exit(code);
}

void* connection(void* args) {
	int client = (int) args;

	// read request
	char buffer[8192] = {0};
	if (recv(client, buffer, sizeof(buffer) - 1, 0) > 0) {
		char* tmp = buffer;
		char* method = strsep(&tmp, " ");
		char* path = strsep(&tmp, " ");
		char* version = strsep(&tmp, "\r");

		puts(method);
		puts(path);
		puts(version);
	}

	// send
	char s[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 18\r\n\r\nBazingus bazongus\n";
	send(client, s, sizeof(s), 0);

	// clean up and exit
	shutdown(client, SHUT_RDWR);
	close(client);
	pthread_exit(0);
}

int main(int argc, char* argv[]) {
	if (argc != 2) quit("Usage: ./server <port>", 0);

	// create, bind, and open listener
	int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener < 0) quit("Error opening socket", 1);
	struct sockaddr_in listener_addr;
	listener_addr.sin_family = AF_INET;
	listener_addr.sin_addr.s_addr = INADDR_ANY;
	listener_addr.sin_port = htons(atoi(argv[1]));
	if (bind(listener, (struct sockaddr*)&listener_addr, sizeof(listener_addr)) < 0) quit("Error binding port", 1);
	if (listen(listener, 100) < 0) quit("Error listening", 1);

	while (1) {
		// accept connections
		int client = accept(listener, NULL, NULL);
		if (client < 0) puts("Error accepting connection");

		// spawn a new thread to handle each connection
		pthread_t t;
		pthread_create(&t, NULL, connection, (void*) client);
		pthread_detach(t);
	}

	close(listener);
	return 0;
}