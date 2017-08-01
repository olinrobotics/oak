#ifndef OAK_SOFT_SWITCH_H
#define OAK_SOFT_SWITCH_H

 #include "ros.h"
 #include "std_msgs/Byte.h"

class OAKSoftSwitch{
public:
  explicit OAKSoftSwitch(ros::NodeHandle *nh, const char* name, const int pin);

private:
  ros::Subscriber<std_msgs::Bool, OAKSoftSwitch> *signalIn;
  std_msgs::Bool servo_signal;
  const int pin;
  void softCB(const std_msgs::Bool &sig);
};

#endif //OAK_SOFT_SWITCH_H
