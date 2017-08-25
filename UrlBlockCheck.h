#ifndef URL_BLOCK_CHECK_H_
#define URL_BLOCK_CHECK_H_

class Url;

bool isUrlBlocked(const Url &url, int *p_errno = nullptr);

#endif //URL_BLOCK_CHECK_H_
