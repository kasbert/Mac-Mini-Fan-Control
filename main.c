
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#include <mach/mach_port.h>
#include <mach/mach_interface.h>
#include <mach/mach_init.h>

#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/IOMessage.h>


#include <CoreFoundation/CoreFoundation.h>

#include <signal.h>

#include <unistd.h>
#include <string.h>
#include <IOKit/IOKitLib.h>

#include "smc.h"

io_connect_t conn;
int reinit, lastSpeed,lastTemp;

struct TempClass {
  int speed;
  int tempUp;   
  int tempDown; // cooling down. move to lower class
};

struct TempClass classes[] = {
  {1800,30,30},
  {2000,65,55},
  {3000,70,65},
  {4000,75,70},
  {5000,80,70},
  {5500,90,80}
};

#define maxClass ((sizeof(classes)/sizeof(struct TempClass)) - 1)

int currentClass = maxClass;

int mapTempUp(int temp) {
  if (temp > 90) return 5500; 
  if (temp > 85) return 5000; 
  if (temp > 80) return 4000; 
  if (temp > 75) return 3000; 
  if (temp > 70) return 2000; 
  if (temp > 30) return 1800; 
  return 5500; 
}

void resetSpeed() {
  setSpeed(5500);
}

void signalHandler(int sig) {
  signal(SIGINT, SIG_DFL);
  signal(SIGHUP, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGTERM, SIG_DFL);
  resetSpeed();
  raise(sig);
}

int setSpeed(int speed) {
    SMCVal_t      val;
    kern_return_t result;
    int i, value;
    char * key = "F0Mx";

    result = SMCReadKey(key, &val);
    if (result != kIOReturnSuccess) {
      printf("Error: SMCReadKey() = %08x\n", result);
      return result;
    }
    printVal(val);
    strcpy(val.key, key);
    value = speed * 4;
    for (i = 0; i < val.dataSize; i++) {
      val.bytes[i] = (value>>((val.dataSize-i-1)*8)) & 0xff;
      //printf ("%d, %d %d\n", value, i , val.bytes[i]);
    }
    result = SMCWriteKey(val);
    if (result != kIOReturnSuccess)
      printf("Error: SMCWriteKey() = %08x\n", result);

    result = SMCReadKey(key, &val);
    if (result != kIOReturnSuccess) {
      printf("Error: SMCReadKey() = %08x\n", result);
      return result;
    }
    printVal(val);
    return result;
}


kern_return_t SMCGetTemp(void)
{
    kern_return_t result;
    SMCVal_t      val;
    UInt32Char_t  key;
    int           totalFans, i;

    result = SMCReadKey("TC0D", &val);
    if (result != kIOReturnSuccess)
        return kIOReturnError;

    int temp =  val.bytes[0];
    int speed = 5500;

    while (currentClass < maxClass && temp > classes[currentClass + 1].tempUp) {
      ++currentClass;
    }

    while (currentClass > 0 && temp < classes[currentClass].tempDown) {
      --currentClass;
    }
    speed = classes[currentClass].speed;
    if (speed < 1800 || speed > 5500) speed = 5500;

    //printf("Temp: %d (%d) Speed %d (%d) Class %d\n", temp, lastTemp, speed, lastSpeed, currentClass);

    //speed = mapTempUp(temp);
    //printf("Temp: %d (%d) Speed %d (%d)\n", temp, lastTemp, speed, lastSpeed);

    if (speed != lastSpeed || reinit) {
      printf("Set Speed: %d (%d) Temp: %d (%d) Class: %d\n", speed, lastSpeed, temp, lastTemp, currentClass);
      setSpeed(speed);
      fflush(stdout);
    }
    lastSpeed = speed;
    lastTemp = temp;

    return kIOReturnSuccess;
}

static void
_perform(void *info __unused)
{
  if (reinit) {
    printf("Reinit\n");
    SMCClose(conn);
    SMCOpen(&conn);
  }

  SMCGetTemp();
  //printf("hello\n");
  reinit = 0;
}

static void
_timer(CFRunLoopTimerRef timer __unused, void *info)
{
  CFRunLoopSourceSignal(info);
}

int
timer_main()
{
  CFRunLoopSourceRef source;
  CFRunLoopSourceContext source_context;
  CFRunLoopTimerRef timer;
  CFRunLoopTimerContext timer_context;

  bzero(&source_context, sizeof(source_context));
  source_context.perform = _perform;
  source = CFRunLoopSourceCreate(NULL, 0, &source_context);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);

  bzero(&timer_context, sizeof(timer_context));
  timer_context.info = source;
  timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 10, 0, 0, _timer, &timer_context);
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopCommonModes);

  //  CFRunLoopRun();

  return 0;
}


io_connect_t  root_port; // a reference to the Root Power Domain IOService

void
MySleepCallBack( void * refCon, io_service_t service, natural_t messageType, void * messageArgument )
{
  //printf( "messageType %08lx, arg %08lx\n",
  //	  (long unsigned int)messageType,
  //	  (long unsigned int)messageArgument );

  switch ( messageType )
    {

    case kIOMessageCanSystemSleep:
      //printf("hello can seleep\n");
      /* Idle sleep is about to kick in. This message will not be sent for forced sleep.
                Applications have a chance to prevent sleep by calling IOCancelPowerChange.
                Most applications should not prevent idle sleep.

                Power Management waits up to 30 seconds for you to either allow or deny idle 
                sleep. If you don't acknowledge this power change by calling either 
                IOAllowPowerChange or IOCancelPowerChange, the system will wait 30 
                seconds then go to sleep.
      */

      //Uncomment to cancel idle sleep
      //IOCancelPowerChange( root_port, (long)messageArgument );
      // we will allow idle sleep
      IOAllowPowerChange( root_port, (long)messageArgument );
      break;

    case kIOMessageSystemWillSleep:
      //printf("hello will sleep\n");
      /* The system WILL go to sleep. If you do not call IOAllowPowerChange or
                IOCancelPowerChange to acknowledge this message, sleep will be
                delayed by 30 seconds.

                NOTE: If you call IOCancelPowerChange to deny sleep it returns
                kIOReturnSuccess, however the system WILL still go to sleep. 
      */

      IOAllowPowerChange( root_port, (long)messageArgument );
      break;

    case kIOMessageSystemWillPowerOn:
      //System has started the wake up process...
      //printf("hello will wake\n");
      break;

    case kIOMessageSystemHasPoweredOn:
      //System has finished waking up...
      //printf("hello wake finhished\n");
      reinit = 1;
      break;

    default:
      break;

    }
}


int main( int argc, char **argv )
{
  // notification port allocated by IORegisterForSystemPower
  IONotificationPortRef  notifyPortRef; 

  // notifier object, used to deregister later
  io_object_t            notifierObject; 
  // this parameter is passed to the callback
  void*                  refCon; 

  atexit(resetSpeed);
  signal(SIGINT, signalHandler);
  signal(SIGHUP, signalHandler);
  signal(SIGQUIT, signalHandler);
  signal(SIGTERM, signalHandler);

    SMCOpen(&conn);

    timer_main();

  // register to receive system sleep notifications

  root_port = IORegisterForSystemPower( refCon, &notifyPortRef, MySleepCallBack, &notifierObject );
  if ( root_port == 0 )
    {
      printf("IORegisterForSystemPower failed\n");
      return 1;
    }

  // add the notification port to the application runloop
  CFRunLoopAddSource( CFRunLoopGetCurrent(),
		      IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopCommonModes ); 

  /* Start the run loop to receive sleep notifications. Don't call CFRunLoopRun if this code 
        is running on the main thread of a Cocoa or Carbon application. Cocoa and Carbon 
        manage the main thread's run loop for you as part of their event handling 
        mechanisms. 
  */ 
  CFRunLoopRun();

  //Not reached, CFRunLoopRun doesn't return in this case.
  return (0);
}
