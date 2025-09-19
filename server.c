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

// data manipulation utilities
char* concat(char* dest, char* src, size_t n) { return memcpy(dest, src, n) + n; }
int endsWith(char* str, char* end) {
	size_t strLen = strlen(str);
	size_t endLen = strlen(end);
	if (endLen > strLen) return 0;
	else return strcmp(str + strLen - endLen, end) == 0;
}

// utility for sending responses without fuss
int sendResponse(int client, char* version, char* status, char* contentType, char* connection, size_t contentLen, char* content) {
	// find lengths of everything to include in response
	size_t versionLen = strlen(version);
	size_t statusLen = strlen(status);
	size_t contentTypeLen = strlen(contentType);
	size_t connectionLen = strlen(connection);
	char contentLenStr[32] = {0};
	size_t contentLenStrLen = snprintf(contentLenStr, sizeof(contentLenStr), "%lu", contentLen); // lol
	
	// calculate memory to allocate and allocate it
	size_t toSendLen = versionLen + 1 + statusLen + 16 + contentTypeLen + 18 + contentLenStrLen + 14 + connectionLen + 4 + contentLen;
	char* toSend = malloc(toSendLen);

	// concat all parts of response
	char* tmp = toSend;
	tmp = concat(tmp, version, versionLen);
	tmp = concat(tmp, " ", 1);
	tmp = concat(tmp, status, statusLen);
	tmp = concat(tmp, "\r\nContent-Type: ", 16);
	tmp = concat(tmp, contentType, contentTypeLen);
	tmp = concat(tmp, "\r\nContent-Length: ", 18);
	tmp = concat(tmp, contentLenStr, contentLenStrLen);
	tmp = concat(tmp, "\r\nConnection: ", 14);
	tmp = concat(tmp, connection, connectionLen);
	tmp = concat(tmp, "\r\n\r\n", 4);
	tmp = concat(tmp, content, contentLen);
	
	// send it
	send(client, toSend, toSendLen, 0);

	// not forgetting to free
	free(toSend);
}

// connection thread
void* connection(void* args) {
	// client passed into thread
	int client = (intptr_t) args;

	// set up timeout
	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	// keep things lively if requested
	int keepAlive = 1;
	while (keepAlive) {
		// recieve request
		char buffer[8192] = {0};
		recv(client, buffer, sizeof(buffer) - 1, 0);

		// temporary pointer for string processing
		char* tmp = buffer;
		
		// determine whether to keep alive
		keepAlive = endsWith(tmp, "Connection: keep-alive\r\n\r\n");

		// assign appropriate Connection header
		char* conn;
		if (keepAlive) conn = "keep-alive";
		else conn = "close";

		// parse method, path, version
		char* method = strsep(&tmp, " ");
		char* path = strsep(&tmp, " ");
		char* version = strsep(&tmp, "\r\n");

		// check if something's whacky about request before attempting file access
		if (method == NULL || path == NULL || version == NULL || *method == '\0' || *path == '\0' || *version == '\0') {
			sendResponse(client, "HTTP/1.1", "400 Bad Request", "text/html", conn, 15, "400 Bad Request");
		}
		else if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
			sendResponse(client, "HTTP/1.1", "505 HTTP Version Not Supported", "text/html", conn, 30, "505 HTTP Version Not Supported");
		}
		else if (strcmp(method, "GET") != 0) {
			sendResponse(client, version, "405 Method Not Allowed", "text/html", conn, 22, "405 Method Not Allowed");
		}
		else {
			// filepath is www + path + maybe index.html + null terminator
			size_t pathlen = strlen(path);
			char* filepath = malloc(3 + pathlen + 10 + 1);
			char* filetmp = concat(concat(filepath, "www", 3), path, pathlen);
			// check if path ends in /, if so append index.html (with nul terminator)
			if (path[pathlen - 1] == '/') concat(filetmp, "index.html", 11);
			// otherwise just append nul terminator
			else concat(filetmp, "", 1);

			// if it isn't in whitelist, 404
			if (stringListSearch(pathWhitelist, filepath) == NULL) {
				sendResponse(client, version, "404 Not Found", "text/html", conn, 13, "404 Not Found");
			}
			else {
				// detect content-type
				char* contentType;
				if (endsWith(filepath, ".html")) contentType = "text/html";
				else if (endsWith(filepath, ".txt")) contentType = "text/plain";
				else if (endsWith(filepath, ".png")) contentType = "image/png";
				else if (endsWith(filepath, ".gif")) contentType = "image/gif";
				else if (endsWith(filepath, ".jpg")) contentType = "image/jpg";
				else if (endsWith(filepath, ".ico")) contentType = "image/x-icon";
				else if (endsWith(filepath, ".css")) contentType = "text/css";
				else if (endsWith(filepath, ".js")) contentType = "application/javascript";
				else contentType = "application/octet-stream";

				// open file and find size to allocate for contents
				FILE* fileptr = fopen(filepath, "rb");
				fseek(fileptr, 0, SEEK_END);
				long filesize = ftell(fileptr);
				rewind(fileptr);

				// allocate space for contents, read and send
				char* content = malloc(filesize);
				size_t contentLen = fread(content, 1, filesize, fileptr);
				sendResponse(client, version, "200 OK", contentType, conn, contentLen, content);

				// close file and free contents
				fclose(fileptr);
				free(content);
			}

			// finished with this
			free(filepath);
		}
	}

	// clean up and exit
	shutdown(client, SHUT_RDWR);
	close(client);
	pthread_exit(0);
}

// util for exiting with message
void quit(char* msg, int code) {
	puts(msg);
	exit(code);
}

int main(int argc, char* argv[]) {
	// usage info if run without port
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

	// accept connections and spawn new threads to handle them
	while (1) {
		int client = accept(listener, NULL, NULL);
		if (client < 0) puts("Error accepting connection");
		pthread_t t;
		pthread_create(&t, NULL, connection, (void*) (intptr_t) client);
		pthread_detach(t);
	}

	// stop listening
	close(listener);
	return 0;
}