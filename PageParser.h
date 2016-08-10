#ifndef GB_PAGEPARSER_H_
#define GB_PAGEPARSER_H_

// global flag
extern bool g_inPageInject ;


class TcpSocket;
class HttpRequest;

bool sendPageAnalyze ( TcpSocket *s , HttpRequest *r ) ;

#endif // GB_PAGEPARSER_H
