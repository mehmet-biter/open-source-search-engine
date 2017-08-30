#ifndef URL_BLOCK_CHECK_H_
#define URL_BLOCK_CHECK_H_

class Url;

bool isUrlBlocked(const Url &url, int *p_errno = nullptr);
bool isUrlUnwanted(const Url &url, const char **reason = nullptr);

#endif //URL_BLOCK_CHECK_H_
