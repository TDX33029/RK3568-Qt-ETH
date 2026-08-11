/* Windows 编译验证用的 POSIX 桩头 —— 仅本机语法检查, 不部署 */
#ifndef STUB_ARPA_INET_H
#define STUB_ARPA_INET_H

#include <stdint.h>
#include <sys/socket.h>

#define INADDR_NONE 0xFFFFFFFFu
#define INADDR_ANY  0x00000000u
#define INET_ADDRSTRLEN 16

#ifdef __cplusplus
extern "C" {
#endif
uint32_t    inet_addr(const char *cp);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
uint16_t    htons(uint16_t v);
uint16_t    ntohs(uint16_t v);
#ifdef __cplusplus
}
#endif

#endif /* STUB_ARPA_INET_H */
