#ifndef MSG55_H_
#define MSG55_H_

class Host;

bool registerMsg56Handler();

bool initializeWatchdog();
void finalizeWatchdog();

int getEffectiveWatchdogInterval(const Host *host); // in millisecs

#endif
