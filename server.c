#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFF_SZ 4096

// figure out what mime type to send based on file extension
void get_mime(char *file, char *mime) {
    char *dot = strrchr(file, '.');
    
    if (!dot) {
        strcpy(mime, "text/html");
        return;
    }
    
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        strcpy(mime, "text/html");
    else if (strcmp(dot, ".css") == 0)
        strcpy(mime, "text/css");
    else if (strcmp(dot, ".js") == 0)
        strcpy(mime, "application/javascript");
    else if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        strcpy(mime, "image/jpeg");
    else if (strcmp(dot, ".png") == 0)
        strcpy(mime, "image/png");
    else if (strcmp(dot, ".gif") == 0)
        strcpy(mime, "image/gif");
    else
        strcpy(mime, "application/octet-stream"); // fallback for unknown types
}

// convert URL path to actual file path
void build_filepath(char *route, char *filepath) {
    // strip query params if any
    char *q = strchr(route, '?');
    if (q) *q = '\0';
    
    // default to index if hitting root
    if (strcmp(route, "/") == 0) {
        strcpy(filepath, "htdocs/index.html");
        return;
    }
    
    sprintf(filepath, "htdocs%s", route);
    
    // add .html if no extension
    if (!strchr(route, '.')) {
        strcat(filepath, ".html");
    }
}

int main() {
    int srv_sock, cli_sock;
    struct sockaddr_in srv_addr;
    char buffer[BUFF_SZ];
    
    // create socket
    srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) {
        perror("socket failed");
        exit(1);
    }
    
    // reuse addr so we don't get "address already in use" on restart
    int reuse = 1;
    setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(PORT);
    
    if (bind(srv_sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind failed");
        close(srv_sock);
        exit(1);
    }
    
    if (listen(srv_sock, MAX_CLIENTS) < 0) {
        perror("listen failed");
        close(srv_sock);
        exit(1);
    }
    
    printf("Server running on http://localhost:%d\n", PORT);
    printf("Serving files from ./htdocs/\n\n");
    
    // main loop
    while(1) {
        cli_sock = accept(srv_sock, NULL, NULL);
        if (cli_sock < 0) {
            perror("accept failed");
            continue; // keep trying
        }
        
        memset(buffer, 0, BUFF_SZ);
        int bytes = read(cli_sock, buffer, BUFF_SZ - 1);
        if (bytes <= 0) {
            close(cli_sock);
            continue;
        }
        
        // parse request line
        char method[16], path[256], version[16];
        sscanf(buffer, "%s %s %s", method, path, version);
        
        printf("[%s] %s ", method, path);
        
        char filepath[512];
        build_filepath(path, filepath);
        
        // try to open the file
        FILE *fp = fopen(filepath, "rb");
        if (!fp) {
            // send 404
            char *resp = "HTTP/1.1 404 Not Found\r\n"
                        "Content-Type: text/html\r\n\r\n"
                        "<html><body><h1>404 Not Found</h1></body></html>";
            send(cli_sock, resp, strlen(resp), 0);
            printf("-> 404\n");
            close(cli_sock);
            continue;
        }
        
        // get file size
        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        // figure out content type
        char mime[64];
        get_mime(filepath, mime);
        
        // build response header
        time_t now = time(NULL);
        struct tm *gmt = gmtime(&now);
        char date[128];
        strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", gmt);
        
        char header[512];
        sprintf(header, 
                "HTTP/1.1 200 OK\r\n"
                "Date: %s\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %ld\r\n"
                "Connection: close\r\n"
                "\r\n",
                date, mime, fsize);
        
        // send header
        send(cli_sock, header, strlen(header), 0);
        
        // send file data
        char *file_data = malloc(fsize);
        if (file_data) {
            fread(file_data, 1, fsize, fp);
            send(cli_sock, file_data, fsize, 0);
            free(file_data);
        }
        
        fclose(fp);
        close(cli_sock);
        
        printf("-> 200 %s (%ld bytes)\n", mime, fsize);
    }
    
    close(srv_sock);
    return 0;
}
