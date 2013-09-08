/*
 * *****************************************************************************
 * file name   : robust_io.c
 * author      : Randal E. Bryant and David R. O'Hallaron 
 * description : Hung Ngo copied from Bryant & O'Hallaron's book sample codes
 *
 *   To use the buffered I/O versions, just need to initialize a rio_t variable
 *   and pass it to subsequent calls to rio_readlineb(...). For example
 * 
 *   rio_t rio;
 *   rio_readinitb(&rio, fd);
 *   rio_readlineb(&rio, buf, MAXLINE);
 *
 *   readlineb() and readnb() can be used interleavingly.
 * *****************************************************************************
 */

/*
 * -----------------------------------------------------------------------------
 * headers related to networking functions
 * -----------------------------------------------------------------------------
 */
#include  <sys/socket.h>  /* basic socket definitions               */
#include  <netinet/in.h>  /* sockaddr_in{} and other Internet defns */
#include  <netdb.h>       /* name and address conversion            */
#include  <arpa/inet.h>   /* inet_pton/ntop                         */


#include "common_headers.h"
#include "error_handlers.h"
#include "robust_io.h"


/*
 * ----------------------------------------------------------------------------
 * rio_readinitb - Associate a descriptor with a read buffer and reset buffer
 * ----------------------------------------------------------------------------
 */
void rio_readinitb(rio_t *rp, int fd)
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

/*
 * ----------------------------------------------------------------------------
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 * ----------------------------------------------------------------------------
 */
ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    while (rp->rio_cnt <= 0) {  /* refill if buf is empty */
        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
                           sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) {
            if (errno != EINTR) /* interrupted by sig handler return */
                return -1;
        }
        else if (rp->rio_cnt == 0)  /* EOF */
            return 0;
        else
            rp->rio_bufptr = rp->rio_buf; /* reset buffer ptr */
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;
    if (rp->rio_cnt < n)
        cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}


/*
 * ----------------------------------------------------------------------------
 * rio_readnb - Robustly read n bytes (buffered)
 * ----------------------------------------------------------------------------
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) {
            if (errno == EINTR) /* interrupted by sig handler return */
                nread = 0;      /* call read() again */
            else
                return -1;      /* errno set by read() */
        }
        else if (nread == 0)
            break;              /* EOF */
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft);         /* return >= 0 */
}


/* 
 * ----------------------------------------------------------------------------
 * rio_readlineb - robustly read a text line (buffered)
 * ----------------------------------------------------------------------------
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    int n, rc;
    char c, *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) {
        if ((rc = rio_read(rp, &c, 1)) == 1) {
            *bufp++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            if (n == 1)
                return 0; /* EOF, no data read */
            else
                break;    /* EOF, some data was read */
        } else
            return -1;    /* error */
    }
    *bufp = 0;
    return n;
}

/*
 * ----------------------------------------------------------------------------
 * readn:
 *   read 'n' bytes or upto EOF from descriptor 'fd' into 'vptr'
 *   returns number of bytes read or -1 on error
 *   our program will be blocked waiting for n bytes to be available on fd
 *
 * fd:   the file descriptor (socket) we're reading from
 * vptr: address of memory space to put read data
 * n:    number of bytes to be read
 * ----------------------------------------------------------------------------
 */ 
ssize_t readn(int fd, void* vptr, size_t n)
{
  size_t  nleft;
  ssize_t nread;
  char*   ptr;
  
  ptr = vptr;
  nleft = n;
  while (nleft > 0) {           /* keep reading upto n bytes     */
    if ( (nread = read(fd, ptr, nleft)) < 0) {
      if (errno == EINTR) {     /* got interrupted by a signal ? */
        nread = 0;              /* try calling read() again      */
      } else {
        return(-1);
      }
    } else if (nread == 0) {
      break;                    /* EOF */
    }
    nleft -= nread;
    ptr   += nread;
  }

  return(n - nleft);            /* return >= 0 */
}

/*
 * ----------------------------------------------------------------------------
 * writen:
 *   write 'n' bytes from 'vptr' to descriptor 'fd'
 *   returns number of bytes written or -1 on error
 * ----------------------------------------------------------------------------
 */
ssize_t writen(int fd, const void* vptr, size_t n)
{
  size_t      nleft;
  ssize_t     nwritten;
  const char* ptr;

  ptr = vptr;
  nleft = n;
  while (nleft > 0) {
    if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
      if (errno == EINTR) { /* interrupted by a signal ? */
	nwritten = 0;       /* try call write() again    */
      } else {
	return(-1);
      }
    }
    nleft -= nwritten;
    ptr   += nwritten;
  }
  return(n);
}

/*
 * ----------------------------------------------------------------------------
 * simpler_sigaction:
 *   appropriately calls POSIX's sigaction, except for SIGALARM, we try to 
 *     restart any interrupted system calls after any other signals
 *   'signo' is the signal number
 *   'func' is the signal handler
 *   SIG_ERR is returned if the call to sigaction fails
 * ----------------------------------------------------------------------------
 */
Sigfunc* simpler_sigaction(int signo, Sigfunc *func)
{
  struct sigaction act, oact;

  act.sa_handler = func;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  if (signo == SIGALRM) {
#ifdef  SA_INTERRUPT
    act.sa_flags |= SA_INTERRUPT;  /* SunOS 4.x : only in the case of SIGALARM
                                    * we do not want to restart the sys. call */
#endif
  } else {
#ifdef  SA_RESTART
    act.sa_flags |= SA_RESTART;    /* SVR4, 44BSD : restart interrupted system
                                    * calls */
#endif
  }

  if (sigaction(signo, &act, &oact) < 0) {
    return(SIG_ERR);
  }
  return(oact.sa_handler);
}

/*
 * ----------------------------------------------------------------------------
 * sig_child_handler:
 *   we do not want zombies, so try to wait for all children to finish whenever
 *     a SIGCHLD is received
 * ----------------------------------------------------------------------------
 */
void sig_child_handler(int signo)
{
  pid_t pid;
  int   stat;
  
  while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
    note("Child process %d terminated\n", pid);
  }
}
