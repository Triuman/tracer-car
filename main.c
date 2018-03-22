#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "config.h"
#include "dyad.h"
#include <glib.h>


/* Tracer Methods and Variables*/
static void *tracer_servo_control_thread(void *data);
static GThread *servo_control_thread;
void stopTheCar();
void stopSteering();
void stopThrottling();

/* Dyad Socket Stuff */
static void tracer_socket_onData(dyad_Event *e);
static void tracer_socket_onClose(dyad_Event *e);
static void tracer_socket_onConnect(dyad_Event *e);

static char *tracer_server_address;
static int tracer_server_port;
static char *tracer_car_id;
static char *servoblaster_file_name;
static char *steering_pin;
static char *throttle_pin;

static volatile gint running = 0;

/* Position Stuff */

void float2Bytes(byte* bytes_temp[4],float float_variable){ 
  union {
    float a;
    unsigned char bytes[4];
  } thing;
  thing.a = float_variable;
  memcpy(bytes_temp, thing.bytes, 4);
}

static void tracer_on_distance_change(char* node_name, float distance)
{


}


/* Position Stuff End*/


//Tracer Socket functions for car connection

static void tracer_socket_onData(dyad_Event *e)
{
   char *message = g_malloc0(e->size);
   memcpy(message, e->data, e->size);
   printf("Got a message from LS: %s\n", message);

   //Process the command
   char command = message[0];
   switch (command)
   {
   case '0':
      //Process Control Command
      char directionstr[2] = "", speedstr[2] = "";

      //direction
      directionstr[0] = message[1];
      directionstr[1] = message[2];
      targetRawSteeringValue = atoi(directionstr);
      printf("STEERING: %d\n", targetRawSteeringValue);

      //speed
      speedstr[0] = message[3];
      speedstr[1] = message[4];
      targetRawThrottleValue = atoi(speedstr);
      printf("THROTTLE: %d\n", targetRawThrottleValue);

      break;
   case '1':
      //exit (shut down)
      printf("Local Server wanted me to EXIT");
      g_atomic_int_set(&running, 0);
      break;
   
   }
}

static void tracer_socket_onClose(dyad_Event *e)
{
   printf("Disconnected from Local Server!!!\n");
   //TODO: Try to reconnect!
}

static void tracer_socket_onConnect(dyad_Event *e)
{
   //First thing, send your ID
   dyad_writef(e->remote, tracer_car_id);
   printf("Connected to Local Server!\n");
}

FILE *servoBlasterFile; //ServoBlaster
FILE *IP;               //file description

void error(char *msg)
{
   perror(msg);
   exit(1);
}
double servoStepPerSec = 30;
double steeringMax = 12;
double throttleMax = 9;
double throttleMin = 9;
double steeringStepValue = 1.8;
double throttleStepValue = 0.1;
double idleSteeringValue = 49;
double idleThrottleValue = 50;
static double currentSteeringValue = 50;
static double currentThrottleValue = 50;
static double targetSteeringValue = 50;
static double targetThrottleValue = 50;
static double targetRawSteeringValue = 50; //These need to be normalized with max value
static double targetRawThrottleValue = 50; //These need to be normalized with max value

static gboolean steeringIsStopped = TRUE;

void *tracer_servo_control_thread(void *data)
{

   currentSteeringValue = idleSteeringValue;
   currentThrottleValue = idleThrottleValue;
   targetSteeringValue = idleSteeringValue;
   targetThrottleValue = idleThrottleValue;

   const int stepSleepInterval = G_USEC_PER_SEC / servoStepPerSec;
   gboolean steeringChanged = 1;
   gboolean throttleChanged = 1;
   while (g_atomic_int_get(&running))
   {
      steeringChanged = 1;
      throttleChanged = 1;

      double throttleStepPercentage = (-abs(currentThrottleValue - idleThrottleValue) + throttleMax) / (throttleMax + throttleMax / 10);

      //normalize the coming percentage according to its max value
      targetThrottleValue = (targetRawThrottleValue - 50) / 50 * throttleMax + idleThrottleValue;

      if (targetThrottleValue > currentThrottleValue)
      {
         currentThrottleValue += currentThrottleValue < idleThrottleValue && targetThrottleValue > idleThrottleValue ? throttleStepValue * 5 : targetThrottleValue == idleThrottleValue ? throttleStepValue * 3 : throttleStepValue * throttleStepPercentage;
         //currentThrottleValue += currentThrottleValue < idleThrottleValue ? throttleStepValue * 4 : throttleStepValue;
         if (targetThrottleValue < currentThrottleValue)
            currentThrottleValue = targetThrottleValue;
         if (targetThrottleValue > idleThrottleValue && currentThrottleValue > idleThrottleValue && currentThrottleValue < idleThrottleValue + throttleMin)
            currentThrottleValue = idleThrottleValue + throttleMin;
      }
      else if (targetThrottleValue < currentThrottleValue)
      {
         currentThrottleValue -= currentThrottleValue > idleThrottleValue && targetThrottleValue < idleThrottleValue ? throttleStepValue * 5 : targetThrottleValue == idleThrottleValue ? throttleStepValue * 3 : throttleStepValue * throttleStepPercentage;
         //currentThrottleValue -= currentThrottleValue > idleThrottleValue ? throttleStepValue * 4 : throttleStepValue;
         if (targetThrottleValue > currentThrottleValue)
            currentThrottleValue = targetThrottleValue;
         if (targetThrottleValue < idleThrottleValue && currentThrottleValue < idleThrottleValue && currentThrottleValue > idleThrottleValue - throttleMin)
            currentThrottleValue = idleThrottleValue - throttleMin;
      }
      else
      {
         throttleChanged = 0;
      }

      double steeringStepPercentage = (-abs(currentThrottleValue - idleThrottleValue) + throttleMax + throttleMax) / (throttleMax + throttleMax);

      //normalize the coming percentage according to its max value
      targetSteeringValue = (targetRawSteeringValue - 50) / 50 * steeringMax + idleSteeringValue;

      /*printf("targetRawSteeringValue value: %f", targetRawSteeringValue);
      printf("target steering value: %f", targetSteeringValue);
      printf("steeringMax value: %f", steeringMax);
      printf("idleSteeringValue value: %f", idleSteeringValue);*/

      if (targetSteeringValue > currentSteeringValue)
      {
         currentSteeringValue += currentSteeringValue < idleSteeringValue && targetSteeringValue > idleSteeringValue ? steeringStepValue * 3 * steeringStepPercentage : steeringStepValue * steeringStepPercentage;
         //currentSteeringValue += steeringStepValue * steeringStepPercentage;
         if (targetSteeringValue < currentSteeringValue)
            currentSteeringValue = targetSteeringValue;
      }
      else if (targetSteeringValue < currentSteeringValue)
      {
         currentSteeringValue -= currentSteeringValue > idleSteeringValue && targetSteeringValue < idleSteeringValue ? steeringStepValue * 3 * steeringStepPercentage : steeringStepValue * steeringStepPercentage;
         //currentSteeringValue -= steeringStepValue * steeringStepPercentage;
         if (targetSteeringValue > currentSteeringValue)
            currentSteeringValue = targetSteeringValue;
      }
      else
      {
         if (!steeringIsStopped)
         {
            steeringIsStopped = TRUE;
            stopSteering();
         }
         steeringChanged = 0;
      }

      if (throttleChanged)
      {
         char throtlecommand[100];
         sprintf(throtlecommand, "%s=%f%%\n", throttle_pin, currentThrottleValue);
         sendCommandToPWM(throtlecommand);
      }

      g_usleep(1000);

      if (steeringChanged)
      {
         steeringIsStopped = FALSE;
         char steeringCommand[100];
         sprintf(steeringCommand, "%s=%f%%\n", steering_pin, currentSteeringValue);
         sendCommandToPWM(steeringCommand);
      }

      g_usleep(stepSleepInterval);
   }
}

void stopTheCar()
{
   stopSteering();
   stopThrottling();
}

void stopSteering()
{
   sendCommandToPWM("%s=0\n", steering_pin);
}

void stopThrottling()
{
   sendCommandToPWM("%s=0\n", throttle_pin);
}

void sendCommandToPWM(char *command)
{
   fprintf(servoBlasterFile, command);
   fflush(servoBlasterFile);
   //usleep(1000); //5 ms
}

int main()
{

   char *config_file = "./tracer.cfg";
   if ((config = janus_config_parse(config_file)) == NULL)
   {
      g_print("Error reading/parsing the configuration file, going on with the defaults and the command line arguments\n");
      exit(1);
   }

   janus_config_item *serverIpItem = janus_config_get_item_drilldown(config, "server", "server_ip");
   if (serverIpItem && serverIpItem->value)
   {
      tracer_server_address = serverIpItem->value;
   }
   janus_config_item *serverPortItem = janus_config_get_item_drilldown(config, "server", "server_port");
   if (serverPortItem && serverPortItem->value)
   {
      tracer_server_port = atoi(serverPortItem->value);
   }

   janus_config_item *tracerCarIdItem = janus_config_get_item_drilldown(config, "car", "id");
   if (tracerCarIdItem && tracerCarIdItem->value)
   {
      tracer_car_id = tracerCarIdItem->value;
   }

   janus_config_item *servoblasterFileItem = janus_config_get_item_drilldown(config, "gpio", "servoblaster_file");
   if (servoblasterFileItem && servoblasterFileItem->value)
   {
      servoblaster_file_name = servoblasterFileItem->value;
   }

   janus_config_item *steeringPinItem = janus_config_get_item_drilldown(config, "gpio", "steering_pin");
   if (steeringPinItem && steeringPinItem->value)
   {
      steering_pin = steeringPinItem->value;
   }

   janus_config_item *throttlePinItem = janus_config_get_item_drilldown(config, "gpio", "throttle_pin");
   if (throttlePinItem && throttlePinItem->value)
   {
      throttle_pin = throttlePinItem->value;
   }

   janus_config_item *servoStepItem = janus_config_get_item_drilldown(config, "gpio", "servo_step_per_second");
   if (servoStepItem && servoStepItem->value)
   {
      servoStepPerSec = atoi(servoStepItem->value);
   }

   janus_config_item *steeringMaxItem = janus_config_get_item_drilldown(config, "gpio", "max_steering");
   if (steeringMaxItem && steeringMaxItem->value)
   {
      steeringMax = atoi(steeringMaxItem->value);
   }

   janus_config_item *throttleMaxItem = janus_config_get_item_drilldown(config, "gpio", "max_throttle");
   if (throttleMaxItem && throttleMaxItem->value)
   {
      throttleMax = atoi(throttleMaxItem->value);
   }

   janus_config_item *throttleMinItem = janus_config_get_item_drilldown(config, "gpio", "min_throttle");
   if (throttleMinItem && throttleMinItem->value)
   {
      throttleMin = atoi(throttleMinItem->value);
   }

   janus_config_item *steeringStepItem = janus_config_get_item_drilldown(config, "gpio", "steering_step_value");
   if (steeringStepItem && steeringStepItem->value)
   {
      steeringStepValue = atoi(steeringStepItem->value);
   }

   janus_config_item *throttleStepItem = janus_config_get_item_drilldown(config, "gpio", "throttle_step_value");
   if (steeringStepItem && throttleStepItem->value)
   {
      throttleStepValue = atoi(throttleStepItem->value);
   }

   janus_config_item *idleSteeringItem = janus_config_get_item_drilldown(config, "gpio", "idle_steering_value");
   if (idleSteeringItem && idleSteeringItem->value)
   {
      idleSteeringValue = atoi(idleSteeringItem->value);
   }

   janus_config_item *idleThrottleItem = janus_config_get_item_drilldown(config, "gpio", "idle_throttle_value");
   if (idleThrottleItem && idleThrottleItem->value)
   {
      idleThrottleValue = atoi(idleThrottleItem->value);
   }

   g_atomic_int_set(&running, 1);

   /* Tracer GPIO Library Setup */
   servoBlasterFile = fopen(servoblaster_file_name, "w");
   sleep(5);

   if (servoBlasterFile == NULL)
   {
      error("Error opening servoblaster ");
      exit(0);
   }

   /* Start the servo_control_thread */
   servo_control_thread = g_thread_try_new("Servo control thread", &tracer_servo_control_thread, NULL, &error);
   if (!servo_control_thread)
   {
      g_atomic_int_set(&running, 0);
      //TODO: Let server know
      printf("Servo control thread coulnd't be started.\n");
      return -1;
   }

   dyad_init();

   dyad_Stream *s = dyad_newStream();
   dyad_addListener(s, DYAD_EVENT_CONNECT, tracer_socket_onConnect, NULL);
   dyad_addListener(s, DYAD_EVENT_DATA, tracer_socket_onData, NULL);
   dyad_addListener(s, DYAD_EVENT_CLOSE, tracer_socket_onClose, NULL);
   dyad_connect(s, tracer_server_address, tracer_server_port);

   while (g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping))
   {
      dyad_update();
   }

   printf("Socket Client thread ended\n");
   dyad_shutdown();

   /* Tracer Close ServoBlaster file */
   fclose(servoBlasterFile);

   return 0;
}