#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ftw.h>

// linkedlist utility for path whitelist
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
struct StringNode* stringListSearch(struct StringNode* searching, const char* string) {
	while (searching != NULL) {
		if (strcmp(searching->string, string) == 0) return searching;
		else searching = searching->next;
	}
	return NULL;
}

// ftw ftw :)
struct StringNode* pathWhitelist;
int addPath(const char* path, const struct stat* statptr, int flags) {
	if (flags == FTW_F) stringListAdd(&pathWhitelist, path);
	return 0;
}

// connection thread
void* connection(void* args) {
	int client = (int) args;

	char buffer[8192] = {0};
	recv(client, buffer, sizeof(buffer) - 1, 0);
	char* tmp = buffer;
	char* method = strsep(&tmp, " ");
	char* path = strsep(&tmp, " ");
	char* version = strsep(&tmp, "\r\n");

	if (method == NULL || path == NULL || version == NULL || *method == '\0' || *path == '\0' || *version == '\0') {
		puts("400 Bad Request");
	}
	else if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
		puts("505 HTTP Version Not Supported");
	}
	else if (strcmp(method, "GET") != 0) {
		puts("405 Method Not Allowed");
	}
	else {
		// filepath is www + path + maybe index.html + null terminator
		size_t pathlen = strlen(path);
		char* filepath = malloc(sizeof(char) * (3 + pathlen + 10 + 1));
		strcpy(filepath, "www");
		strcpy(filepath+3, path);
		if (path[pathlen - 1] == '/') strcpy(filepath+3+pathlen, "index.html"); // check if path ends in /, if so append index.html

		if (stringListSearch(pathWhitelist, filepath) == NULL) {
			puts("404 Not Found");
		}
		else {
			// attempt to access path
			FILE* fptr = fopen(filepath, "rb");

			// no longer need this
			free(filepath);

			// send
			char s[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 18\r\n\r\nBazingus bazongus\n";
			send(client, s, sizeof(s), 0);
		}
	}

	// clean up and exit
	shutdown(client, SHUT_RDWR);
	close(client);
	pthread_exit(0);
}

void quit(char* msg, int code) {
	puts(msg);
	exit(code);
}

int main(int argc, char* argv[]) {
	if (argc != 2) quit("Usage: ./server <port>", 0);

	// add only files in www to whitelist
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

	// accept connections and spawn new thread to handle them
	while (1) {
		int client = accept(listener, NULL, NULL);
		if (client < 0) puts("Error accepting connection");
		pthread_t t;
		pthread_create(&t, NULL, connection, (void*) client);
		pthread_detach(t);
	}

	close(listener);
	return 0;
}