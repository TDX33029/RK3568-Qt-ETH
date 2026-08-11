/* Windows 编译验证用的 POSIX 桩头 —— 仅本机语法检查, 不部署 */
#ifndef STUB_SYS_SOCKET_H
#define STUB_SYS_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t socklen_t;

struct sockaddr { uint8_t sa_family; char sa_data[14]; };

#define AF_INET     2
#define PF_INET     2
#define SOCK_DGRAM  2
#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SO_RCVBUF   8

#define MSG_TRUNC   0x20

#ifdef __cplusplus
extern "C" {
#endif
int       socket(int domain, int type, int protocol);
int       setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
int       bind(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t   recvfrom(int fd, void *buf, size_t len, int flags,
                   struct sockaddr *src, socklen_t *srclen);
int       close(int fd);
#ifdef __cplusplus
}
#endif

#endif /* STUB_SYS_SOCKET_H */
