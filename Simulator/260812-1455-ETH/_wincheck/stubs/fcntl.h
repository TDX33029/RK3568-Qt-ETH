/* Windows 编译验证用的 fcntl 补充头 —— 补 O_NONBLOCK/F_GETFL/F_SETFL/fcntl,
 * 其余定义沿用 MinGW 原生 fcntl.h。仅本机语法检查, 不部署。 */
#ifndef STUB_FCNTL_H
#define STUB_FCNTL_H

#include_next <fcntl.h>

#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x800
#endif

#ifdef __cplusplus
extern "C" int fcntl(int fd, int cmd, ...);
#endif

#endif /* STUB_FCNTL_H */
