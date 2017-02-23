#include <iostream>
#include <mutex>
#include <cstdio>

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>         
#include <arpa/inet.h>      
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <netinet/in.h>

// http://blog.abhijeetr.com/2010/04/very-simple-http-server-writen-in-c.html
// http://stackoverflow.com/questions/14045064/how-to-accept-socket-with-timeout

using namespace std;

FILE * fout_stream;                 // log file
const char * fname_out = "log.txt"; // name of log file
std::mutex log_mutex;

// Synchronized writing to log file
void printlog(const char * format, ...)
{
    std::lock_guard < std::mutex > lc(log_mutex);
    va_list ap;
    va_start(ap, format); 
    vfprintf ( fout_stream, format, ap );
    fflush(fout_stream);
    va_end(ap);    
}

// web server functions
/*
    HTTP 1.0 protocol, GET command only, 
    404 and 200 answers, MIME-type text/html only 

*/

#define IF_ERROR(message) printlog(message);res=-1;break;

#define BAD_REQUEST_HEADER      "HTTP/1.0 400 Bad Request\n"
#define NOT_FOUND_HEADER        "HTTP/1.0 404 Not Found\n"
#define OK_HEADER               "HTTP/1.0 200 OK\n\n"
#define GET_COMMAND             "GET"
#define PROTOCOL_SUPPORTED      "HTTP/1.0"
#define DEFAULT_TARGET          "/index.html"

#define BUFFER_SIZE             4096

// Process the client's request
void process_request(int client_socket_fd, const char * root_folder)
{
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    pid_t pid = getpid();
    
    // log clients information
    getpeername(client_socket_fd , (struct sockaddr*)&clientaddr , (socklen_t*)&addrlen);
    printlog("Process request start, socket fd is %d, ip %s, port %d, pid = %d\n", 
        client_socket_fd, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), pid);
    
    // Read request
    char request[BUFFER_SIZE], *rline[3], response_data[BUFFER_SIZE], filepath[FILENAME_MAX];
    int rcv, fd, bytes_read;
    int res = 0;

    memset( (void*)request, (int)'\0', BUFFER_SIZE );

    do
    {
        rcv = recv(client_socket_fd, request, BUFFER_SIZE, 0);
        if ( rcv < 0)
        {
            // Receive error
            IF_ERROR("recv() error\n");
        }
        else if ( 0 == rcv )   
            {
                // receive socket closed
                IF_ERROR("Client disconnected upexpectedly.\n");
            }
    
        // Parse request, prepare and write responce
        printlog("Request : %s\npid = %d\n", request, pid);
        rline[0] = strtok (request, " \t\n");
        // Check the command
        if ( strcmp(rline[0], GET_COMMAND) == 0 )
        {
            rline[1] = strtok (NULL, " \t");
            rline[2] = strtok (NULL, " \t\n");
            
            // Check the protocol version
            if (strcmp( rline[2],PROTOCOL_SUPPORTED) == 0 )
            {
                // Get the filename
                if ( strncmp(rline[1], "/\0", 2) == 0 )
                    rline[1] = (char *)DEFAULT_TARGET;

                strcpy(filepath, root_folder);
                strcpy(&filepath[strlen(root_folder)], rline[1]);
                printlog("Request for file: %s, pid = %d\n", filepath, pid);
                
                // Try to open and send the file specified
                if ( ( fd = open(filepath, O_RDONLY)) != -1 )
                {
                    send(client_socket_fd, OK_HEADER, strlen(OK_HEADER), 0);
                    while ( (bytes_read = read(fd, response_data, BUFFER_SIZE)) > 0 )
                        write (client_socket_fd, response_data, bytes_read);
                    
                    printlog("File is sent : %s , pid = %d\n", filepath, pid);
                }
                else
                {
                    printlog("File not found : %s , pid = %d\n", filepath, pid);
                    write(client_socket_fd, NOT_FOUND_HEADER, strlen(NOT_FOUND_HEADER));
                }                
            }
            else
            {
                printlog("Unsupported protocol: %s , pid = %d\n", rline[2], pid);
                write(client_socket_fd, BAD_REQUEST_HEADER, strlen(BAD_REQUEST_HEADER));
            }    
        }
        else
        {
            printlog("Unsupported command: %s , pid = %d\n", rline[0], pid);
            write(client_socket_fd, BAD_REQUEST_HEADER, strlen(BAD_REQUEST_HEADER));
        }
    } while (false);
  
    // Close client's socket
    shutdown(client_socket_fd, SHUT_RDWR);
    close(client_socket_fd);
    printlog("Process request end, pid = %d\n", pid);
}

// Start the master socket listening loop
void start_web_server(const char * ip, int port, const char * root_folder)
{
    printlog("Start web server, pid = %d\n", getpid());
        
    int master_socket, addrlen, res;
    struct sockaddr_in address;
    int opt = 1;
    
    res = 0;
    
    do 
    {
        // Create a master socket
        if( (master_socket = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP)) == 0) 
        {
            IF_ERROR("Failed to create master socket\n");
        }
        
        // Set master socket to allow multiple connections
        if( setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
        {
            IF_ERROR("Failed to call setsockopt\n");
        }

        // Type of socket created
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(ip);
        address.sin_port = htons( port );
        
        // Bind the socket 
        if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) 
        {
            IF_ERROR("Bind failed\n");
        }
        
        printlog("Listen on port %d\n", port);
        
        // Background queue up to 128 requests
        if (listen(master_socket, 128) < 0)
        {
            IF_ERROR("listen failed\n");
        }
        
        // Ready to accept the incoming connection
        addrlen = sizeof(address);
        printlog("Waiting for connections ...\n");
        
    } while (false);
       
    // NO RESTRICTIONS to number of client - try to run each request in separate process
    if ( 0 == res )
    {
        while(true)
        {
            struct sockaddr_in clientaddr;
            socklen_t addrlen;
            
            int client_fd = accept (master_socket, (struct sockaddr *) &clientaddr, &addrlen);
            if ( client_fd < 0 )
            {
                IF_ERROR("Accept failed\n");
            }
            // log the new connection info
            printlog("New request, socket fd is %d, ip %s, port %d\n", 
                client_fd, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

			pid_t pf = fork();
            if ( pf == 0 )
            {
				// child process - process the request
                process_request(client_fd, root_folder);
                exit(0);
            }
			else
			{
				// parent process
				close(client_fd);
			}
        }
    }
    
    printlog("Server loop is closed\n");
    shutdown(master_socket, SHUT_RDWR);
    close(master_socket);
}

// Application entry point
int main(int argc, char * argv[])
{
    // Arguments are mandatory !
    if (argc < 4)
    {
        printf("Usage:\n application -h <ip> -p <port> -d <directory>\n");
        exit(EXIT_FAILURE);    
    }
    
    // Open the log file
    fout_stream = fopen (fname_out,"w");
    printlog("Start application, pid = %d\n", getpid());

     // Read parameters of the server
    int     opt;
    char    ip[FILENAME_MAX];
    int     port;
    char    directory[FILENAME_MAX];
 
    while ((opt = getopt(argc,argv, "h:p:d:")) != -1) 
    {
        switch (opt) {
            case 'h':
                strcpy(ip,optarg);
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                strcpy(directory,optarg);
                break;
            default: /* '?' */
                printlog("Wrong arguments\n");
                fclose(fout_stream);
                exit(EXIT_FAILURE);
        }
    }

    printlog("Server parameters: host = %s, port = %d, directory = %s\n", 
                                                        ip, port, directory);

    // Try to daemonize the process
    int dmn = daemon(1,0);
    if ( dmn < 0)
    {
        // error happened on 'daemon' call
        printlog("daemon failed, errno = %d\n", errno);
        fclose(fout_stream);
        exit(errno);
    }
    else if ( 0 == dmn )
    {
        // Success, continue the child process 
        printlog("Moved to background, pid = %d\n", getpid());
        
        // Call the web server function
        start_web_server((const char *)ip, port, (const char *)directory);

        printlog("End application, pid = %d\n", getpid());
    
        fclose(fout_stream);
   
        return 0;
    }
}
