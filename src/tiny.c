/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh 
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *cgiargs, unsigned methods);
int parse_uri(char *uri, char *filename, char *cgiargs, unsigned methods);
void serve_static(int fd, char *filename, int filesize, unsigned methods);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, unsigned methods);
void handler(int sig);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    if (Signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        unix_error("mask signal pipe error");
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                             //line:netp:tiny:doit
	Close(connfd);                                            //line:netp:tiny:close
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
    unsigned methods = 0;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET") == 0)
    {
        methods = 0;
    }
    else if (strcasecmp(method, "HEAD") == 0)
    {
        methods = 1;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        methods = 2; /* POST */
    }
    else
    {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                                   //line:netp:doit:endrequesterr
    read_requesthdrs(&rio, cgiargs, methods);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs, methods);       //line:netp:doit:staticcheck
    printf("file path: %s\n", filename);
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    //line:netp:doit:endnotfound

    if (is_static) { /* Serve static content */          
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size, methods);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs, methods);            //line:netp:doit:servedynamic
    }
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp , char *cgiargs, unsigned methods) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
    }

    int readSize = 0;
    if (methods == 2){  /* POST */
        readSize = Rio_readnb(rp, buf, rp->rio_cnt);
        // buf[readSize] = '\0';
        printf("%s\n", buf);
        strcpy(cgiargs, buf);
    }
    
    return;
}
/* $end read_requesthdrs */

// 简单的URL解码函数
void url_decode(char *url) {
    char *p = url;
    int i = 0;

    while (*url) {
        if (*url == '%' && url[1] == '2' && url[2] == '0') { // 如果遇到 %20 符号
            *p++ = ' '; // 将 %20 替换为空格
            url += 3;
        } else if (*url == '+') { // 如果遇到 + 符号，表示空格
            *p++ = ' ';
            url++;
        } else { // 其他情况直接复制字符
            *p++ = *url++;
        }
    }
    *p = '\0'; // 添加字符串结束符
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs, unsigned methods)
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
	strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
	strcat(filename, uri);                           //line:netp:parseuri:endconvert1
    // static method ignore ?
    ptr = index(filename, '?');
    if (ptr)
    *ptr = '\0';
	if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
	    strcat(filename, "index.html");               //line:netp:parseuri:appenddefault
    url_decode(filename);
	return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
    if (methods == 0){  /* GET */
	ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
    }
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
	strcat(filename, uri);                           //line:netp:parseuri:endconvert2
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize, unsigned methods)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);    //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); //line:netp:servestatic:beginserve
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Cookie: aside-status=show\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Sec-Fetch-Dest: documentr\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Sec-Fetch-Mode: navigate\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf));    //line:netp:servestatic:endserve

    /* HEAD method doesn't need to send response body to client */
    if (methods == 1)
        return;

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = Malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    Rio_writen(fd, srcp, filesize);            
    Free(srcp);
    // srcfd = Open(filename, O_RDONLY, 0); //line:netp:servestatic:open
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //line:netp:servestatic:mmap
    // Close(srcfd);                       //line:netp:servestatic:close
    // Rio_writen(fd, srcp, filesize);     //line:netp:servestatic:write
    // Munmap(srcp, filesize);             //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
    else if (strstr(filename, ".css"))
    strcpy(filetype, "text/css");
    else if (strstr(filename, ".js")) // 添加对.js文件的支持
    strcpy(filetype, "application/javascript");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs, unsigned methods) 
{
    if (signal(SIGCHLD, handler) == SIG_ERR)
    {
        unix_error("signal error");
    }
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* HEAD method doesn't need to send response body to client */
    if (methods == 1)
        return;
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
    if (Signal(SIGPIPE, SIG_DFL) == SIG_ERR)
	{
    	unix_error("mask signal pipe error");
	}
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

void handler(int sig)
{
  int olderrno = errno;
  
  while (waitpid(-1, NULL, 0) > 0)
  {
    continue;
  }
  if (errno != ECHILD)
  {
    Sio_error("waitpid error");
  }
  errno = olderrno;
}


/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */
