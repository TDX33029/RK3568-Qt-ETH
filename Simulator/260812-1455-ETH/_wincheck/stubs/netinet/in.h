/* Windows 编译验证用的 POSIX 桩头 —— 仅本机语法检查, 不部署 */
#ifndef STUB_NETINET_IN_H
#define STUB_NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

typedef uint32_t in_addr_t;

struct in_addr { in_addr_t s_addr; };

struct sockaddr_in {
    uint8_t  sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    char     sin_zero[8];
};

#endif /* STUB_NETINET_IN_H */
