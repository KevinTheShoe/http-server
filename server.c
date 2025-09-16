#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ftw.h>

// linkedlist for requestable paths
struct StringNode {
	char* string;
	struct StringNode* next;
};
void stringListAdd(struct StringNode** head, const char* string) {
	struct StringNode* newStringNode = malloc(sizeof(struct StringNode));
	newStringNode->string = malloc(sizeof(char) * (strlen(string) + 1));
	strcpy(newStringNode->string, string);
	newStringNode->next = *head;
	*head = newStringNode;
}
struct StringNode* stringListSearch(struct StringNode* head, const char* string) {
	struct StringNode* searching = head;
	while (searching != NULL) {
		if (strcmp(searching->string, string) == 0) return searching;
		searching = searching->next;
	}
	return NULL;
}
struct StringNode* pathList;
int addPath(const char* path, const struct stat* statptr, int flags) {
	if (flags == FTW_F) stringListAdd(&pathList, path);
	return 0;
}

void quit(char* msg, int code) {
	puts(msg);
	exit(code);
}

void* connection(void* args) {
	int client = (int) args;

	// read request
	char buffer[8192] = {0};
	recv(client, buffer, sizeof(buffer) - 1, 0);
	char* tmp = buffer;
	char* method = strsep(&tmp, " ");
	char* path = strsep(&tmp, " ");
	char* version = strsep(&tmp, "\r");

	// pick what to send
	if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
		// 505 HTTP Version Not Supported
	}
	else if (strcmp(method, "GET") != 0) {
		// 405 Method Not Allowed
	}
	else {
		// filepath is www + path + maybe index.html + null terminator
		size_t pathlen = strlen(path);
		char* filepath = malloc(sizeof(char) * (3 + pathlen + 10 + 1));
		strcpy(filepath, "www");
		strcpy(filepath+3, path);
		if (path[pathlen - 1] == '/') strcpy(filepath+3+pathlen, "index.html"); // check if path ends in /, if so append index.html

		// check that path is allowed
		if (stringListSearch(pathList, filepath) == NULL) {
			// 400 Not Found
			puts("Not Found");
		}

		// attempt to access path
		FILE* fptr = fopen(filepath, "rb");

		// send
		char s[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 18\r\n\r\nBazingus bazongus\n";
		send(client, s, sizeof(s), 0);
	}

	// clean up and exit
	shutdown(client, SHUT_RDWR);
	close(client);
	pthread_exit(0);
}

int main(int argc, char* argv[]) {
	if (argc != 2) quit("Usage: ./server <port>", 0);

	// find all requestable paths
	ftw("www", addPath, 5);

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