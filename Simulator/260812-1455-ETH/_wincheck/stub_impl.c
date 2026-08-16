/* Windows 编译验证用的 POSIX 桩实现 —— 仅用于本机链接检查, 不部署、不运行。
 * 通过 GetProcAddress 动态转发到 ws2_32, 避免链接顺序问题。 */
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>

typedef unsigned int socklen_t;   /* 与桩头 sys/socket.h 一致 */
struct sockaddr;
typedef long long ssize_t;

typedef int (__stdcall *t_socket)(int, int, int);
typedef int (__stdcall *t_bind)(int, const struct sockaddr *, int);
typedef int (__stdcall *t_setsockopt)(int, int, int, const char *, int);
typedef int (__stdcall *t_recvfrom)(int, char *, int, int, struct sockaddr *, int *);
typedef int (__stdcall *t_sendto)(int, const char *, int, int, const struct sockaddr *, int);
typedef uint16_t (__stdcall *t_htons)(uint16_t);
typedef uint32_t (__stdcall *t_inet_addr)(const char *);
typedef int (__stdcall *t_closesocket)(int);

static void *wload(const char *name) {
    static HMODULE m;
    if (!m) {
        m = LoadLibraryA("ws2_32.dll");
        if (m) {
            /* winsock 使用前必须 WSAStartup (动态取地址调用, 避免链接依赖) */
            typedef int (__stdcall *t_wsa)(unsigned short, void *);
            t_wsa wsa = (t_wsa)GetProcAddress(m, "WSAStartup");
            if (wsa) {
                char wsadata[408];
                wsa(0x0202, wsadata);
            }
        }
    }
    return m ? (void *)GetProcAddress(m, name) : NULL;
}

int socket(int d, int t, int p)      { return ((t_socket)wload("socket"))(d, t, p); }
int bind(int fd, const struct sockaddr *a, int l)
                                     { return ((t_bind)wload("bind"))(fd, a, l); }
int setsockopt(int fd, int l, int n, const void *v, int len)
                                     { return ((t_setsockopt)wload("setsockopt"))(fd, l, n, (const char *)v, len); }
ssize_t recvfrom(int fd, void *b, size_t len, int fl, struct sockaddr *s, socklen_t *sl)
                                     { return ((t_recvfrom)wload("recvfrom"))(fd, (char *)b, (int)len, fl, s, (int *)sl); }
ssize_t sendto(int fd, const void *b, size_t len, int fl, const struct sockaddr *d, socklen_t dl)
                                     { return ((t_sendto)wload("sendto"))(fd, (const char *)b, (int)len, fl, d, (int)dl); }
uint16_t htons(uint16_t v)           { return ((t_htons)wload("htons"))(v); }
uint16_t ntohs(uint16_t v)           { return ((t_htons)wload("ntohs"))(v); }
uint32_t inet_addr(const char *cp)   { return ((t_inet_addr)wload("inet_addr"))(cp); }

int close(int fd)                    { return ((t_closesocket)wload("closesocket"))(fd); }

int fcntl(int fd, int cmd, ...)      { (void)fd; (void)cmd; return 0; }
