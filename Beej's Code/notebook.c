/* network review */

// socket: an endpoint in communication between two computers across a computer network

// file descriptor: an integer associated with an open "file"

// everything in Unix is a file: a real on-the-disk file, a socket connection, a pipe, a terminal ...

// TCP stream sockets: SOCK_STREAM, reliable two-way connected communication streams, data arrives in order and error-free
// UDP datagram sockets: SOCK_DGRAM, unreliable, connectionless, but much faster in terms of transfer speed

// packet encapsulation: Ethernet/Wifi [ IP [ TCP [ HTTP/SMTP/TELNET/FTP/SSH [Data] ] ] ]

// IPv4: 32 bits, 4 octets, 192.0.2.111, loopback address 127.0.0.1
// IPv6: 128 bits, 16 octets, but expressed as 8 16-bit hexadecimal numbers
//       leading zeros or inner zeros can be suppressed, 2001:0db8:c9d2:0012:0000:0000:0000:0051 -> 2001:db8:c9d2:12::51
//       loopback address ::1

// convert IPv4 to IPv6: 192.0.2.33 -> ::ffff:192.0.2.33 (prefix with ::ffff:)

// netmask: 192.0.2.12/30, 2001:db8::/32, 2001:db8:5413:4028::9db9/64

// port numbers: used to identify different services, HTTP 80, FTP 21, SSH 22, TELNET 23, SMTP 25
//               ports < 1024 are RESERVED, which require special OS privileges (not necessarily root) to use
                 emacs -nw /etc/services  // list all port numbers in your OS
                 netstat                  // show all active network connections and open ports
                 netstat -r               // view the routing table
                 route                    // view the routing table

// byte order: Big-Endian (or Network Byte Order) vs Little-Endian (machines with an Intel or Intel-compatible processor)
//             to agree on the network connection, must always convert Host Byte Order to Network Byte Order
//             convert once before data is sent out on the wire, convert once again after data comes in off the wire
//             functions below can convert short (2 bytes) and long (4 bytes), also work for the unsigned variations

               htons()  // host to network short
               htonl()  // host to network long
               ntohs()  // network to host short
               ntohl()  // network to host long

// the firewall translates internal IP to external IP (provided by ISP) using NAT (Network Address Translation)
// reserved networks only to be used on private (disconnected) networks, or on networks behind firewalls:
// 10.x.x.x, 192.168.x.x, 172.y.x.x, where x is between [0,255], y is between [16,31] (RFC 1918)

// INADDR_ANY: all available network interfaces on the machine, a client can connect to it both within and outside LAN
// INADDR_LOOPBACK: "localhost", intended only for connection with peers running on the same host


/* structs */

// a linked list, used to prep the socket address structures for subsequent use
struct addrinfo {
    int              ai_flags;          // AI_PASSIVE, AI_CANONNAME, etc.
    int              ai_family;         // AF_INET, AF_INET6, AF_UNSPEC (IPv4, IPv6 or unspecified)
    int              ai_socktype;       // SOCK_STREAM, SOCK_DGRAM (TCP, UDP)
    int              ai_protocol;       // use 0 for "any"
    size_t           ai_addrlen;        // size of ai_addr in bytes
    struct sockaddr* ai_addr;           // struct sockaddr_in or _in6
    char*            ai_canonname;      // full canonical hostname
    struct addrinfo* ai_next;           // linked list, next node
};

// sockaddr holds socket address information for many types of sockets
struct sockaddr {
    unsigned short sa_family;    // address family, AF_INET (IPv4), AF_INET6 (IPv6) or AF_xxx (others)
    char           sa_data[14];  // 14 bytes of protocol address
};

// an alternative to sockaddr, but larger, large enough to hold both IPv4 and IPv6 structures
// use this if you don't know whether IPv4 or IPv6 address will be filled out to sockaddr
struct sockaddr_storage {
    sa_family_t ss_family;                // address family
    char        __ss_pad1[_SS_PAD1SIZE];  // padding, implementation specific, ignore it
    int64_t     __ss_align;               // padding, implementation specific, ignore it
    char        __ss_pad2[_SS_PAD2SIZE];  // padding, implementation specific, ignore it
};

// a parallel structure used to reference sockaddr elements more easily, to be cast from sockaddr or sockaddr_storage
struct sockaddr_in {  // IPv4 version
    short int          sin_family;  // corresponds to sockaddr.sa_family, AF_INET
    unsigned short int sin_port;    // port number, use htons() to convert to Network Byte Order
    struct in_addr     sin_addr;    // IP address, IPv4 version
    unsigned char      sin_zero[8]; // included to pad sockaddr_in to the same length of sockaddr, use memset() to set zeros
};

struct sockaddr_in6 {  // IPv6 version
    u_int16_t       sin6_family;   // corresponds to sockaddr.sa_family, AF_INET6
    u_int16_t       sin6_port;     // port number, use htons() to convert to Network Byte Order
    u_int32_t       sin6_flowinfo; // IPv6 flow information
    struct in6_addr sin6_addr;     // IPv6 address
    u_int32_t       sin6_scope_id; // scope ID
};

// now we have 4 pointers {sa, ss, sin, sin6}, any pair of them can be casted to each other
// if don't know IPv4 or IPv6, which one to cast? sin or sin6? -> depend on the value of sa_family or ss_family
struct sockaddr*         sa;
struct sockaddr_storage* ss;
struct sockaddr_in*      sin;
struct sockaddr_in6*     sin6;

// if cast from `sockaddr_storage`, cast it to `sockaddr_in` or `sockaddr_in6`? -> use the value of ss_family to decide

struct in_addr {  // IPv4 version
    uint32_t s_addr; // a 32-bit int (4 bytes) IP address
};

struct in6_addr {  // IPv6 version
    unsigned char s6_addr[16]; // IPv6 address
};

struct sockaddr_in sin;  // now `sin.sin_addr.s_addr` references the 4-byte IP address


/* convert IP addresses */

// inet_pton(), converts an IP address to in_addr/in6_addr (pton, print to network)
// inet_pton() returns -1 on error, or 0 if the address is messed up
struct sockaddr_in sin; // IPv4
struct sockaddr_in6 sin6; // IPv6

int pton = inet_pton(AF_INET, "10.12.110.57", &(sin.sin_addr)); // IPv4
int pton = inet_pton(AF_INET6, "2001:db8:63b3:1::3490", &(sin6.sin6_addr)); // IPv6

if (!pton > 0) {
    perror("inet_pton")
} else {
    ...
}

// inet_ntop(), extracts IP from in_addr.sin_addr or in6_addr.sin6_addr (ntop, network to print)
// INET_ADDRSTRLEN = 16, INET6_ADDRSTRLEN = 46, are 2 macros defined in <netinet/in.h>
char ip4[INET_ADDRSTRLEN]; // space to hold the IPv4 string
char ip6[INET6_ADDRSTRLEN]; // space to hold the IPv6 string

inet_ntop(AF_INET, &(sin.sin_addr), ip4, INET_ADDRSTRLEN);  // argument ip4/ip6 is a pointer to the string to hold the IP
inet_ntop(AF_INET6, &(sin6.sin6_addr), ip6, INET6_ADDRSTRLEN);


/* from IPv4 to IPv6 */

AF_INET -> AF_INET6
PF_INET -> PF_INET6

gethostbyname()  // deprecated on most platforms, use getaddrinfo() instead
gethostbyaddr()  // deprecated on most platforms, use getnameinfo() instead

struct sockaddr_in sin;
struct sockaddr_in6 sin6;

sin.sin_addr.s_addr = INADDR_ANY; // use my IPv4 addres
sin6.sin6_addr = in6addr_any; // use my IPv6 addres

struct in_addr ia;
struct in6_addr ia6;

inet_aton()  // deprecated, use inet_pton() instead
inet_addr()  // deprecated, use inet_pton() instead

inet_ntoa()  // deprecated, use inet_ntop() instead


/* Socket API system calls */

// system calls are functions used in the kernel itself, appear as normal C function calls
// system calls allow us to access the network functionality (sockets API) of the OS kernel
// system calls, or the sockets API, differs from platform to platform (BSD, Windows, Linux, Mac, ...)
// system calls can be found on the man pages, section 2

// 1. get a linked-list of addrinfo, used by both server and client, returns a status, 0 if success, otherwise error
//    keep in mind that ports < 1024 are reserved, using them may cause problems if you are not a superuser
//    for common user roles, choose a free port (not being occupied by another program) between 1025 ~ 65535

int getaddrinfo(const char*            node,    // the host name or an IP address ("www.example.com" or IP)
                const char*            service, // the name of a service or a port number ("ftp" or "80")
                const struct addrinfo* hints,
                struct addrinfo**      res      // returns a pointer to a linked-list results
    );

void freeaddrinfo(struct addrinfo* res);  // free the addrinfo resources pointed by res when we are done with them

if ((status = getaddrinfo("www.example.com", NULL, &hints, &res)) != 0) {  // sample code
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    exit(1);
}
...
freeaddrinfo(res);

// usually, getaddrinfo() will give us enough address information for subsequent use
// for further functionality, functions below may also come in handy
int getpeername(int sockfd, struct sockaddr* addr, int* addrlen);  // returns the address of the peer connected to sockfd
int gethostname(char* hostname, size_t size);  // returns the null-terminated hostname (starspower for me)
int sethostname(const char* name, size_t len);
int getnameinfo(const struct sockaddr* addr, socklen_t addrlen,
                char* host, socklen_t hostlen,
                char* serv, socklen_t servlen,
                int flags
    );  // the inverse of function getaddrinfo()


// 2. feed the addrinfo to socket() to make a socket, returns a file descriptor > 0, or -1 on error
//    the global variable `errno` is set to the error's value

int socket(int domain,   // PF_INET or PF_INET6 (IPv4 or IPv6)
           int type,     // SOCK_STREAM or SOCK_DGRAM (TCP stream or UDP datagram)
           int protocol  // 0, auto choose the proper protocol for the given type
    );

int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);  // sample code
if (sockfd == -1) {
    perror("client/server: socket");
    continue;
}


// 3. bind that socket with a port on your local machine (for server only)
//    returns -1 on error and sets errno to the error's value

int bind(int              sockfd,   // the socket file descriptor returned by socket()
         struct sockaddr* my_addr,  // a pointer to a struct sockaddr (which has server's local IP address and port)
         int              addrlen   // the length in bytes of server's local address
    );

if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {  // sample code
    close(listener);
    continue;
}

// this code fixes the "Address already in use" error in call to bind(), which allows a running port to be reused (rebound)
int yes = 1;
if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    perror("setsockopt");
    exit(1);
}


// 4. fire up the connection (for client only)
//    returns -1 on error and sets errno to the error's value

int connect(int              sockfd,     // the socket file descriptor returned by socket()
            struct sockaddr* serv_addr,  // a pointer to a struct sockaddr (destination IP and port, either client/server)
            int              addrlen     // the length in bytes of the destination address
    );

if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {  // sample code
    perror("client: connect");
    close(sockfd);
    continue;
}

// 5. listen on a port to wait for incoming connections (for server only)
//    returns -1 on error and sets errno to the error's value

int listen(int sockfd, int backlog);  // backlog is the max number of connections allowed on the incoming queue
                                      // in most systems, this limit is silently set to about 20

#define BACKLOG 10  // sample code
listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
if (listener < 0) {
    perror("listner socket");
    continue;
}
if (listen(listener, BACKLOG) == -1) {
    perror("listen");
    exit(3);
}


// 6. accept one of the connections on the incoming queue (for server only)
//    returns a brand new socket file descriptor associated with that accepted client connection
//    which will be -1 on error and sets errno to the error's value

int accept(int              sockfd,  // the single listen()ed socket descriptor of the server
           struct sockaddr* addr,    // address of the incoming client connection
           socklen_t*       addrlen  // a pointer to the length of the client address, = &(sizeof(addr))
    );

struct sockaddr_storage cli_addr;  // sample code
int new_sock = accept(listner, (struct sockaddr*)&cli_addr, &(sizeof(cli_addr)));
if (new_sock == -1) {
    perror("accept");
    exit(1);
}


// 7. communication between the accepted server socket (new_sock) and the client socket (in client.c code)
//    send() returns the number of bytes actually sent out, which may be < the length you specified if data is too large
//    therefore, we need to limit the number of bytes being sent in one shot
//    recv() returns the number of bytes actually read into the buffer
//    recv() can return 0, which means the remote side has closed the connection
//    both return -1 on error and set errno to the error's value

int send(int         sockfd,  // the socket to send data to (either client or server socket)
         const void* msg,     // a pointer to the data you want to send
         int         len,     // the length in bytes of that data
         int         flags    // set to 0, or see the man page for more
    );

char* msg = "Hello!";  // sample code
if (send(dest_sock, msg, strlen(msg), 0) == -1) {
    perror("send");
    exit(1);
}

int recv(int   sockfd,  // the socket to read data from (either client or server socket)
         void* buf,     // the buffer to read data into
         int   len,     // maximum length of the buffer
         int   flags    // set to 0, or see the man page for more
    );

char buf[256];  // sample code
int n_bytes = recv(sockfd, buf, sizeof(buf) - 1, 0);  // remember to leave a space for '\0' in the buffer

if (n_bytes <= 0) {
    if (n_bytes == 0) {
        printf("socket connection %d closed\n", sockfd);  // connection closed
    } else {
        perror("recv");
        exit(1);
    }
}

buf[n_bytes] = '\0';  // recv() does not place a null terminator at the end, must add one in order to use printf()
printf("client: received '%s'\n", buf);


// 8. close() and shutdown()

close(sockfd);  // cut off communication in both ways, no more reads and writes
                // close() frees a socket descriptor from the file descriptor table, recommended

shutdown(sockfd, 0);  // no more recv()s
shutdown(sockfd, 1);  // no more send()s
shutdown(sockfd, 2);  // no more send()s and recv()s, = close()
                      // shutdown() does not free the file descriptor, not recommended


/* General system calls */

int open(const char* pathname, int flags);
int open(const char* pathname, int flags, mode_t mode);
ssize_t read(int fd, void* buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
char* fgets(char* restrict s, int n, FILE* restrict stream);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);

pid_t fork(void);
pid_t getpid(void);
pid_t getppid(void);
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);

int execl(const char* path, const char* arg, ...);  // more on "exec" functions family
int execve(const char* pathname, char* const argv[], char* const envp[]);

// monitors a bunch of sockets at once and then handle the ones that have data ready
// returns the number of elements in the array fds[] that have had an event occur
// OS will block on the poll() call until a socket has data ready or timeout is reached
// if there's a large number of connections, poll() is very slow, instead use the "libevent" event library
int poll(struct pollfd fds[],  // an array with info (which sockets and what events to monitor) will be returned
         nfds_t nfds,          // number of elements in the array
         int    timeout        // developer-specified timeout in milliseconds, -1 if infinite
        );

struct pollfd {
    int   fd;       // the socket file descriptor
    short events;   // events to monitor, POLLIN || POLLOUT (data is ready to recv() || can send() without blocking)
    short revents;  // events that occurred, when poll() returns, this will be updated, POLLIN || POLLOUT
};









int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout);

int fcntl(int fd, int cmd, ... /* arg */ );
...

// besides, in Python, system calls are available in the os module

>  import os
>
>  def foo():
>      os.socket()
>      os.connect()
>      os.fork()
>      os.waitpid()
>      ...



















//////
