#ifndef INSTANCE_INFO_EXCHANGE_H_
#define INSTANCE_INFO_EXCHANGE_H_

namespace InstanceInfoExchange {

bool initialize();
void finalize();

//Tell the exchange that this info is still alive and kicking.
//  'forHowLong' is relative time in milliseconds
void weAreAlive(int forHowLong);

} //namespace

#endif
