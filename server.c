#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ftw.h>

// linkedlist utilities for path whitelist
struct StringNode {
	char* string;
	struct StringNode* next;
};
void stringListAdd(struct StringNode** head, const char* string) {
	struct StringNode* newStringNode = malloc(sizeof(struct StringNode));
	newStringNode->string = malloc(strlen(string) + 1);
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
struct StringNode* pathWhitelist;
int addPath(const char* path, const struct stat* statptr, int flags) {
	if (flags == FTW_F) stringListAdd(&pathWhitelist, path);
	return 0;
}

char* concat(char* dest, char* src, size_t n) { return memcpy(dest, src, n) + n; }

int sendResponse(int client, char* version, char* status, char* contentType, size_t contentLen, char* content) {
	size_t versionLen = strlen(version);
	size_t statusLen = strlen(status);
	size_t contentTypeLen = strlen(contentType);
	char contentLenStr[32] = {0};
	size_t contentLenStrLen = snprintf(contentLenStr, sizeof(contentLenStr), "%lu", contentLen); // lol
	
	size_t toSendLen = versionLen + 1 + statusLen + 16 + contentTypeLen + 18 + contentLenStrLen + 4 + contentLen;
	char* toSend = malloc(toSendLen);

	char* tmp = toSend;
	tmp = concat(tmp, version, versionLen);
	tmp = concat(tmp, " ", 1);
	tmp = concat(tmp, status, statusLen);
	tmp = concat(tmp, "\r\nContent-Type: ", 16);
	tmp = concat(tmp, contentType, contentTypeLen);
	tmp = concat(tmp, "\r\nContent-Length: ", 18);
	tmp = concat(tmp, contentLenStr, contentLenStrLen);
	tmp = concat(tmp, "\r\n\r\n", 4);
	tmp = concat(tmp, content, contentLen);
	
	send(client, toSend, toSendLen, 0);

	free(toSend);
}

// connection thread
void* connection(void* args) {
	int client = (intptr_t) args;

	char buffer[8192] = {0};
	recv(client, buffer, sizeof(buffer) - 1, 0);
	char* tmp = buffer;
	char* method = strsep(&tmp, " ");
	char* path = strsep(&tmp, " ");
	char* version = strsep(&tmp, "\r\n");

	if (method == NULL || path == NULL || version == NULL || *method == '\0' || *path == '\0' || *version == '\0') {
		sendResponse(client, "HTTP/1.1", "400 Bad Request", "text/html", 15, "400 Bad Request");
	}
	else if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
		sendResponse(client, "HTTP/1.1", "505 HTTP Version Not Supported", "text/html", 30, "505 HTTP Version Not Supported");
	}
	else if (strcmp(method, "GET") != 0) {
		sendResponse(client, version, "405 Method Not Allowed", "text/html", 22, "405 Method Not Allowed");
	}
	else {
		// filepath is www + path + maybe index.html + null terminator
		size_t pathlen = strlen(path);
		char* filepath = malloc(3 + pathlen + 10 + 1);
		char* tmp = concat(concat(filepath, "www", 3), path, pathlen);
		if (path[pathlen - 1] == '/') concat(tmp, "index.html", 11); // check if path ends in /, if so append index.html (with nul terminator)
		else concat(tmp, "", 1); // otherwise just append nul terminator

		if (stringListSearch(pathWhitelist, filepath) == NULL) {
			sendResponse(client, version, "404 Not Found", "text/html", 13, "404 Not Found");
		}
		else {
			// detect content-type

			// open file and find size to allocate
			FILE* fileptr = fopen(filepath, "rb");
			fseek(fileptr, 0, SEEK_END);
			long filesize = ftell(fileptr);
			rewind(fileptr);

			// read and send
			char* content = malloc(filesize);
			size_t contentLen = fread(content, 1, filesize, fileptr);
			sendResponse(client, version, "200 OK", "text/html", contentLen, content);

			fclose(fileptr);
			free(content);
		}

		free(filepath);
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
	struct sockaddr_in listenerAddr;
	listenerAddr.sin_family = AF_INET;
	listenerAddr.sin_addr.s_addr = INADDR_ANY;
	listenerAddr.sin_port = htons(atoi(argv[1]));
	if (bind(listener, (struct sockaddr*)&listenerAddr, sizeof(listenerAddr)) < 0) quit("Error binding port", 1);
	if (listen(listener, 100) < 0) quit("Error listening", 1);

	// accept connections and spawn new thread to handle them
	while (1) {
		int client = accept(listener, NULL, NULL);
		if (client < 0) puts("Error accepting connection");
		pthread_t t;
		pthread_create(&t, NULL, connection, (void*) (intptr_t) client);
		pthread_detach(t);
	}

	close(listener);
	return 0;
}