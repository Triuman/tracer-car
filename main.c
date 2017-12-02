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
#include <gst/gst.h> //To be able to use GStreamer1.0 library


static GThread *gstreamer_thread;

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

static char* tracer_server_address;
static int tracer_server_port;
static char* tracer_car_id;
static char* servoblaster_file_name;
static char* steering_pin;
static char* throttle_pin;
static volatile gint running = 0;

//Tracer Socket functions for car connection

static void tracer_socket_onData(dyad_Event *e) {
   char* message = g_malloc0(e->size);
   memcpy(message, e->data, e->size);
   printf("Got a message from LS: %s\n", message);


   //Process Command
   int direction, speed;
   char directionstr[2] = "",speedstr[2] = "";

   //Process the command
   char command = message[0];
   switch (command) {
      case '0':
         //direction
         directionstr[0] = message[1];
         directionstr[1] = message[2];
         direction = atoi(directionstr);
         printf("DIRECTION: %d\n", direction);

         //speed
         speedstr[0] = message[3];
         speedstr[1] = message[4];
         speed = atoi(speedstr);
         printf("SPEED: %d\n", speed);

         //Set target values
         targetRawSteeringValue = direction;
         targetRawThrottleValue = speed;
         break;
      case '1':
         //exit (shut down)
         printf("Local Server wanted to EXIT");
         g_atomic_int_set(&running, 0);
         break;
      case '2':
         //start streaming
         printf("Local Server wanted to START streaming\n");
         start_gstreamer_streaming();
         break;
      case '3':
         //stop streaming
         printf("Local Server wanted to STOP streaming\n");
         stop_gstreamer_streaming();
         break;
   }
}


static void tracer_socket_onClose(dyad_Event *e) {
    printf("Disconnected from Local Server!!!\n");
    //TODO: Try to reconnect!
}

static void tracer_socket_onConnect(dyad_Event *e) {
   //First thing, send your ID
   dyad_writef(e->remote, tracer_car_id);
   printf("Connected to Local Server!\n");
}


FILE *servoBlasterFile; //ServoBlaster
FILE *IP; //file description

void error(char *msg){
   perror (msg);
   exit(1);
}
const double servoStepPerSec = 30;
const double steeringMax = 12;
const double throttleMax = 9;
const double throttleMin = 9;
const double steeringStepValue = 1.8;
const double throttleStepValue = 0.1;
const double idleSteeringValue = 49;
const double idleThrottleValue = 50;
static double currentSteeringValue = 50;
static double currentThrottleValue = 50;
static double targetSteeringValue = 50;
static double targetThrottleValue = 50;
static double targetRawSteeringValue = 50; //These need to be normalized with max value
static double targetRawThrottleValue = 50; //These need to be normalized with max value

static gboolean steeringIsStopped = TRUE;

void *tracer_servo_control_thread(void *data) {


   currentSteeringValue = idleSteeringValue;
   currentThrottleValue = idleThrottleValue;
   targetSteeringValue = idleSteeringValue;
   targetThrottleValue = idleThrottleValue;

   const int stepSleepInterval = G_USEC_PER_SEC / servoStepPerSec;
   gboolean steeringChanged = 1;
   gboolean throttleChanged = 1;
   while (g_atomic_int_get(&running)) {
      steeringChanged = 1;
      throttleChanged = 1;

      double throttleStepPercentage = (-abs(currentThrottleValue - idleThrottleValue) + throttleMax) / (throttleMax + throttleMax / 10);

      //normalize the coming percentage according to its max value
      targetThrottleValue = (targetRawThrottleValue - 50) / 50 * throttleMax + idleThrottleValue;

      if (targetThrottleValue > currentThrottleValue)
      {
         currentThrottleValue += currentThrottleValue < idleThrottleValue && targetThrottleValue > idleThrottleValue ? throttleStepValue * 5 :
                                 targetThrottleValue == idleThrottleValue ? throttleStepValue * 3 : throttleStepValue * throttleStepPercentage;
         //currentThrottleValue += currentThrottleValue < idleThrottleValue ? throttleStepValue * 4 : throttleStepValue;
         if (targetThrottleValue < currentThrottleValue)
               currentThrottleValue = targetThrottleValue;
         if (targetThrottleValue > idleThrottleValue && currentThrottleValue > idleThrottleValue  && currentThrottleValue < idleThrottleValue + throttleMin)
               currentThrottleValue = idleThrottleValue + throttleMin;
      }
      else if (targetThrottleValue < currentThrottleValue)
      {
         currentThrottleValue -= currentThrottleValue > idleThrottleValue && targetThrottleValue < idleThrottleValue ? throttleStepValue * 5 :
                                 targetThrottleValue == idleThrottleValue ? throttleStepValue * 3 : throttleStepValue * throttleStepPercentage;
         //currentThrottleValue -= currentThrottleValue > idleThrottleValue ? throttleStepValue * 4 : throttleStepValue;
         if (targetThrottleValue > currentThrottleValue)
               currentThrottleValue = targetThrottleValue;
         if (targetThrottleValue < idleThrottleValue && currentThrottleValue < idleThrottleValue  && currentThrottleValue > idleThrottleValue - throttleMin)
               currentThrottleValue = idleThrottleValue - throttleMin;
      }
      else {
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
      else {
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



void stopTheCar() {
   stopSteering();
   stopThrottling();
}

void stopSteering() {
   sendCommandToPWM("%s=0\n", steering_pin);
}

void stopThrottling() {
   sendCommandToPWM("%s=0\n", throttle_pin);
}

void sendCommandToPWM(char* command) {
   fprintf(servoBlasterFile, command);
   fflush(servoBlasterFile);
   //usleep(1000); //5 ms
}


//Video elements
GstElement *pipeline;
GstElement *sourceVideo;
GstElement *encoder;
GstElement *h264parse;
GstElement *rtppay;
GstElement *sinkVideo;

//Audio elements
GstElement *sourceAudio;
GstElement *audioconvert;
GstElement *audioresample;
GstElement *opusenc;
GstElement *rtpopuspay;
GstElement *sinkAudio;

void* gstreamer_init_pipeline_x264enc() {
   //gst - launch - 1.0
   //rpicamsrc
   //! video / x - raw, width = 640, height = 480
   //! x264enc speed - preset = ultrafast tune = zerolatency byte - stream = true threads = 1 bitrate = 200
   //! h264parse config - interval = 1
   //! rtph264pay
   //! udpsink host = 127.0.0.1 port = 8004 sync = true

   //alsasrc device = hw:1, 0
   //! audioconvert
   //! audioresample
   //! opusenc bandwidth = 1101 bitrate = 32000
   //! rtpopuspay
   //! udpsink host = 127.0.0.1 port = 8002 sync = true


   /* Initialize GStreamer */
   gst_init(NULL, NULL);

   /* Create the empty pipeline */
   pipeline = gst_pipeline_new("pipeline");

   /* Create elements */
   //Video elements
   sourceVideo = gst_element_factory_make("rpicamsrc", "rpicamsrc");
   encoder = gst_element_factory_make("x264enc", "x264enc");
   h264parse = gst_element_factory_make("h264parse", "h264parse");
   rtppay = gst_element_factory_make("rtph264pay", "rtph264pay");
   sinkVideo = gst_element_factory_make("udpsink", "sinkVideo");

   //Audio elements
   sourceAudio = gst_element_factory_make("alsasrc", "alsasrc");
   audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
   audioresample = gst_element_factory_make("audioresample", "audioresample");
   opusenc = gst_element_factory_make("opusenc", "opusenc");
   rtpopuspay = gst_element_factory_make("rtpopuspay", "rtpopuspay");
   sinkAudio = gst_element_factory_make("udpsink", "sinkAudio");

   if (!pipeline || !sourceVideo || !encoder || !h264parse || !rtppay || !sinkVideo || !sourceAudio || !audioconvert ||
      !audioresample || !opusenc || !rtpopuspay || !sinkAudio) {
      //  JANUS_LOG(LOG_ERR, "GStreamer; Unable to create GStreamer elements!\n");
      return -1;
   }

   /* Build the pipeline. */
   gst_bin_add_many(GST_BIN(pipeline), sourceVideo, encoder, h264parse, rtppay, sinkVideo, sourceAudio, audioconvert,
                  audioresample, opusenc, rtpopuspay, sinkAudio, NULL);


   gboolean link_ok;
   GstCaps *caps;

   caps = gst_caps_new_simple("video/x-raw",
                              "width", G_TYPE_INT, 640,
                              "height", G_TYPE_INT, 480,
                              NULL);

   link_ok = gst_element_link_filtered(sourceVideo, encoder, caps);
   gst_caps_unref(caps);

   if (!link_ok) {
      // JANUS_LOG(LOG_ERR, "GStreamer; Failed to link sourceVideo and encoder!\n");
   }

   if (!gst_element_link(encoder, h264parse)) {
      g_printerr("Elements could not be linked. encoder, h264parse\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(h264parse, rtppay)) {
      g_printerr("Elements could not be linked. h264parse, rtppay\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(rtppay, sinkVideo)) {
      g_printerr("Elements could not be linked. rtppay, sinkVideo\n");
      gst_object_unref(pipeline);
      return -1;
   }


   if (!gst_element_link(sourceAudio, audioconvert)) {
      g_printerr("Elements could not be linked. sourceAudio, audioconvert\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(audioconvert, audioresample)) {
      g_printerr("Elements could not be linked. audioconvert, audioresample\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(audioresample, opusenc)) {
      g_printerr("Elements could not be linked. audioresample, opusenc\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(opusenc, rtpopuspay)) {
      g_printerr("Elements could not be linked. opusenc, rtpopuspay\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(rtpopuspay, sinkAudio)) {
      g_printerr("Elements could not be linked. rtpopuspay, sinkAudio\n");
      gst_object_unref(pipeline);
      return -1;
   }


   /* Set element properties */

   //Video elements
   //g_object_set(sourceVideo, "awb-mode", 6, NULL);
   //g_object_set(sourceVideo, "sensor-mode", 6, NULL);
   g_object_set(encoder, "speed-preset", 1, NULL);
   g_object_set(encoder, "tune", 4, NULL);
   //g_object_set(encoder, "key-int-max", 30, NULL);
   //g_object_set(encoder, "subme", 3, NULL);
   g_object_set(encoder, "byte-stream", 1, NULL);
   g_object_set(encoder, "bitrate", 200, NULL);
   g_object_set(encoder, "threads", 1, NULL);
   g_object_set(rtppay, "config-interval", 1, NULL);
   g_object_set(sinkVideo, "host", "127.0.0.1", NULL);
   g_object_set(sinkVideo, "port", 8004, NULL);
   g_object_set(sinkVideo, "sync", 1, NULL);
   //g_object_set(sinkVideo, "async", 0, NULL);


   //Audio elements
   g_object_set(sourceAudio, "device", "hw:1,0", NULL);
   g_object_set(opusenc, "bandwidth", 1101, NULL);
   g_object_set(opusenc, "bitrate", 48000, NULL);
   g_object_set(sinkAudio, "host", "127.0.0.1", NULL);
   g_object_set(sinkAudio, "port", 8002, NULL);
   g_object_set(sinkAudio, "sync", 1, NULL);

}

void* gstreamer_init_pipeline_omxh264enc() {

   //gst - launch - 1.0
   //rpicamsrc !video / x - raw, width = 640, height = 480
   //! omxh264enc !video / x - h264, width = 640, height = 480, framerate = 30 / 1, profile = baseline
   //! rtph264pay config - interval = 1
   //! udpsink host = 127.0.0.1 port = 8004 sync = false async = false


   //gst - launch - 1.0
   //rpicamsrc
   //! video / x - raw, width = 640, height = 480
   //! x264enc speed - preset = ultrafast tune = zerolatency byte - stream = true threads = 1 bitrate = 200
   //! h264parse config - interval = 1
   //! rtph264pay !udpsink host = 127.0.0.1 port = 8004 sync = true
   //alsasrc device = hw:1, 0
   //! audioconvert
   //! audioresample
   //! opusenc bandwidth = 1101 bitrate = 32000
   //! rtpopuspay
   //! udpsink host = 127.0.0.1 port = 8002 sync = true



   /* Initialize GStreamer */
   gst_init(NULL, NULL);

   /* Create the empty pipeline */
   pipeline = gst_pipeline_new("pipeline");

   /* Create elements */
   sourceVideo = gst_element_factory_make("rpicamsrc", "rpicamsrc");
   encoder = gst_element_factory_make("omxh264enc", "omxh264enc");
   rtppay = gst_element_factory_make("rtph264pay", "rtph264pay");
   sinkVideo = gst_element_factory_make("udpsink", "udpsink");

   if (!pipeline || !sourceVideo || !encoder || !rtppay || !sinkVideo) {
//      JANUS_LOG(LOG_ERR, "GStreamer; Unable to create GStreamer elements!\n");
      return;
   }

   /* Build the pipeline. */
   gst_bin_add_many(GST_BIN(pipeline), sourceVideo, encoder, rtppay, sinkVideo, NULL);
   /*if (!gst_element_link(sourceVideo, encoder)) {
   g_printerr("Elements could not be linked. sourceVideo, encoder\n");
   gst_object_unref(pipeline);
   return -1;
   }*/

   gboolean link_ok;
   GstCaps *caps;

   caps = gst_caps_new_simple("video/x-raw",
                              "width", G_TYPE_INT, 640,
                              "height", G_TYPE_INT, 480,
                              NULL);

   link_ok = gst_element_link_filtered(sourceVideo, encoder, caps);
   gst_caps_unref(caps);

   if (!link_ok) {
//      JANUS_LOG(LOG_ERR, "GStreamer; Failed to link sourceVideo and encoder!\n");
   }



   if (!gst_element_link(encoder, rtppay)) {
      g_printerr("Elements could not be linked. encoder, rtppay\n");
      gst_object_unref(pipeline);
      return -1;
   }
   if (!gst_element_link(rtppay, sinkVideo)) {
      g_printerr("Elements could not be linked. rtppay, sinkVideo\n");
      gst_object_unref(pipeline);
      return -1;
   }


   //rpicamsrc inline-headers=true ! video/x-raw,width=640,height=480 ! x264enc speed-preset=ultrafast byte-stream=true bitrate=50 threads=1 ! rtph264pay ! udpsink host=127.0.0.1 port=8004 sync=false async=false

   /* Set element properties */





   //g_object_set(sourceVideo, "awb-mode", 6, NULL);
   //g_object_set(sourceVideo, "caps", "video/x-raw,width=640,height=480", NULL);
   //g_object_set(sourceVideo, "sensor-mode", 6, NULL);
   g_object_set(encoder, "speed-preset", 1, NULL);
   g_object_set(encoder, "tune", 4, NULL);
   //g_object_set(encoder, "key-int-max", 30, NULL);
   //g_object_set(encoder, "subme", 3, NULL);
   g_object_set(encoder, "byte-stream", 1, NULL);
   g_object_set(encoder, "bitrate", 50, NULL);
   g_object_set(encoder, "threads", 1, NULL);
   g_object_set(rtppay, "config-interval", 1, NULL);
   g_object_set(sinkVideo, "host", "127.0.0.1", NULL);
   g_object_set(sinkVideo, "port", 8004, NULL);
   //g_object_set(sinkVideo, "sync", 0, NULL);
   //g_object_set(sinkVideo, "async", 0, NULL);

}

void *gstreamer_init_pipeline() {

   gstreamer_init_pipeline_x264enc();
   gst_init(NULL, NULL);
   gst_element_set_state(pipeline, GST_STATE_READY);

}

void start_gstreamer_streaming() {
   /* Start playing */

   GstStateChangeReturn  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
   if (ret == GST_STATE_CHANGE_FAILURE) {
      printf("GStreamer; Unable to set the pipeline to the playing state.\n");
      gst_object_unref(pipeline);
      //TODO: Let server know
   }
}

void stop_gstreamer_streaming() {
   GstStateChangeReturn  ret =  gst_element_set_state(pipeline, GST_STATE_PAUSED);
   if (ret == GST_STATE_CHANGE_FAILURE) {
      printf("GStreamer; Unable to set the pipeline to the playing state.\n");
      gst_object_unref(pipeline);
      //TODO: Let server know
   }
}

int main() {

   char *config_file = "./tracer.cfg";
   if((config = janus_config_parse(config_file)) == NULL) {
      g_print("Error reading/parsing the configuration file, going on with the defaults and the command line arguments\n");
      exit(1);
   }

   janus_config_item *serverIpItem = janus_config_get_item_drilldown(config, "server", "server_ip");
   if(serverIpItem && serverIpItem->value) {
      tracer_server_address = serverIpItem->value;
   }
   janus_config_item *serverPortItem = janus_config_get_item_drilldown(config, "server", "server_port");
   if(serverPortItem && serverPortItem->value) {
      tracer_server_port = atoi(serverPortItem->value);
   }

   janus_config_item *tracerCarIdItem = janus_config_get_item_drilldown(config, "car", "id");
   if(tracerCarIdItem && tracerCarIdItem->value) {
      tracer_car_id = tracerCarIdItem->value;
   }


   janus_config_item *servoblasterFileItem = janus_config_get_item_drilldown(config, "gpio", "servoblaster_file");
   if(servoblasterFileItem && servoblasterFileItem->value) {
      servoblaster_file_name = servoblasterFileItem->value;
   }

   janus_config_item *steeringPinItem = janus_config_get_item_drilldown(config, "gpio", "steering_pin");
   if(steeringPinItem && steeringPinItem->value) {
      steering_pin = steeringPinItem->value;
   }

   janus_config_item *throttlePinItem = janus_config_get_item_drilldown(config, "gpio", "throttle_pin");
   if(throttlePinItem && throttlePinItem->value) {
      throttle_pin = throttlePinItem->value;
   }


   g_atomic_int_set(&running, 1);

   gstreamer_init_pipeline();

   /* Tracer GPIO Library Setup */
   servoBlasterFile = fopen(servoblaster_file_name, "w");
   sleep(5);

   if (servoBlasterFile == NULL) {
      error("Error opening servoblaster ");
      exit(0);
   }


   /* Start the servo_control_thread */
   servo_control_thread = g_thread_try_new("Servo control thread", &tracer_servo_control_thread, NULL, &error);
   if (!servo_control_thread) {
      g_atomic_int_set(&running, 0);
      //TODO: Let server know
      return -1;
   }


   dyad_init();

   dyad_Stream *s = dyad_newStream();
   dyad_addListener(s, DYAD_EVENT_CONNECT, tracer_socket_onConnect, NULL);
   dyad_addListener(s, DYAD_EVENT_DATA,    tracer_socket_onData,    NULL);
   dyad_addListener(s, DYAD_EVENT_CLOSE,    tracer_socket_onClose,    NULL);
   dyad_connect(s, tracer_server_address, tracer_server_port);

   while (g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {
      dyad_update();
   }
   
   printf("Socket Client thread ended\n");
   dyad_shutdown();

   //close gstreamer
   gst_element_set_state(pipeline, GST_STATE_NULL);
   gst_object_unref(pipeline);
   /* Tracer Close ServoBlaster file */
   fclose(servoBlasterFile);

   return 0;
}