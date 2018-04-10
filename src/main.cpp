/*
 * main.c
 *
 *  Created on: 14.08.2013
 *      Author: alexs
 *
 * Modified by Arjan Scherpenisse, july 2016
 */

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <signal.h>

//#include "util.h"
//#include "uart_enum.h"
//#include "uart_comm.h"
#include "./erlcmd.h"

#include <poll.h>
#include <fcntl.h> // File control

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fstream>

#include "./RF24.h"
#include "telemtry.h"

#include <ei.h>

//#define err(code, msg) (fprintf(pFileLog, msg "\n"),exit(code));
//#define dbg(msg) (fprintf(pFileLog, msg "\n"));

//
// Hardware configuration
// Configure the appropriate pins for your connections

/****************** Raspberry Pi ***********************/

// Radio CE Pin, CSN Pin, SPI Speed
// See http://www.airspayce.com/mikem/bcm2835/group__constants.html#ga63c029bd6500167152db4e57736d0939 and the related enumerations for pin information.

// Setup for GPIO 22 CE and CE0 CSN with SPI Speed @ 4Mhz
//RF24 radio(RPI_V2_GPIO_P1_22, BCM2835_SPI_CS0, BCM2835_SPI_SPEED_4MHZ);

// NEW: Setup for RPi B+
//RF24 radio(RPI_BPLUS_GPIO_J8_15,RPI_BPLUS_GPIO_J8_24, BCM2835_SPI_SPEED_8MHZ);

// Setup for GPIO 15 CE and CE0 CSN with SPI Speed @ 8Mhz
RF24 radio(RPI_V2_GPIO_P1_15, RPI_V2_GPIO_P1_24, BCM2835_SPI_SPEED_8MHZ);

/*** RPi Alternate ***/
//Note: Specify SPI BUS 0 or 1 instead of CS pin number.
// See http://tmrh20.github.io/RF24/RPi.html for more information on usage

//RPi Alternate, with MRAA
//RF24 radio(15,0);

//RPi Alternate, with SPIDEV - Note: Edit RF24/arch/BBB/spi.cpp and  set 'this->device = "/dev/spidev0.0";;' or as listed in /dev
//RF24 radio(22,0);

// Radio pipe addresses for the 2 nodes to communicate.
//const uint64_t pipes[2] = { 0xF0F0F0F0E1LL, 0xF0F0F0F0D2LL };
// Radio pipe addresses for the 2 nodes to communicate.
const uint8_t pipes[][6] = {"1Node","2Node"};

const int min_payload_size = 4;
const int max_payload_size = 32;
const int payload_size_increments_by = 1;
int next_payload_size = min_payload_size;

char receive_payload[max_payload_size+1]; // +1 to allow room for a terminating NULL char

bool radioInit(uint8_t reading_pipe, uint8_t writing_pipe);

// Elixir call handlers

/*
 * Radio port handling definitions and prototypes
 */

// Global RF24 references
static struct RF24_config current_config;

// Utilities
static const char response_id = 'r';
static const char notification_id = 'n';

/**
 * @brief Send :ok back to Elixir
 */
static void  send_config_response(struct RF24_config *config)
{
    char resp[256];
    int option_count = 6;

    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
	
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "config");
    ei_encode_map_header(resp, &resp_index, 6); 
//    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "data_rate");
    long x = (long)(config->data_rate);
    ei_encode_long(resp, &resp_index, x);
    ei_encode_atom(resp, &resp_index, "CRC_length");
    x = (long)(config->CRC_length);
    ei_encode_long(resp, &resp_index, x);
    ei_encode_atom(resp, &resp_index, "PA_level");
    x = (long)(config->PA_level);
    ei_encode_long(resp, &resp_index, x);
    ei_encode_atom(resp, &resp_index, "reading_pipe");
    x = (long)(config->reading_pipe);
    ei_encode_long(resp, &resp_index, x);
    ei_encode_atom(resp, &resp_index, "writing_pipe");
    x = (long)(config->writing_pipe);
    ei_encode_long(resp, &resp_index, x);
    ei_encode_atom(resp, &resp_index, "RFchannel");
    x = (long)(config->RFchannel);
    ei_encode_long(resp, &resp_index, x);
	
    erlcmd_send(resp, resp_index);
}


static void send_ok_response()
{
    const char uid[]="command complete";
    int len = 17;
    char resp[256];

    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;

    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "ok");
    ei_encode_string_len(resp, &resp_index, uid, len);
    erlcmd_send(resp, resp_index);
}

/**
 * @brief Send a response of the form {:Error, reason}
 *
 * @param reason a reason (sent back as an atom)
 */
static void send_Error_response(const char *reason)
{
    char resp[256];
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;

    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "exit_status");
    ei_encode_atom(resp, &resp_index, reason);
    erlcmd_send(resp, resp_index);
}

static int parse_option_list(const char *req, int *req_index, RF24_config *config)
{
    int term_type;
    int option_count;
    long int val;

    char buffer [50];
    int buffer_len;

    if (ei_get_type(req, req_index, &term_type, &option_count) < 0 ||
            (term_type != ERL_LIST_EXT && term_type != ERL_NIL_EXT)) {
        logData("Error: expecting option list");
        return -1;
    }

    if (term_type == ERL_NIL_EXT)
        option_count = 0;
    else
        ei_decode_list_header(req, req_index, &option_count);

    // Go through all of the options
    for (int i = 0; i < option_count; i++) {
        int term_size;
        if (ei_decode_tuple_header(req, req_index, &term_size) < 0 ||
                term_size != 2) {
            logData("Error: expecting kv tuple for options");
            return -1;
        }

        char key[64];
        if (ei_decode_atom(req, req_index, key) < 0) {
            logData("Error: expecting atoms for option keys");
            return -1;
        }

        if (strcmp(key, "active") == 0) {
            int  val;
            if (ei_decode_boolean(req, req_index, &val) < 0) {
                logData("Error: active should be a bool");
                return -1;
            }
            config->active = (val != 0);
        } else if (strcmp(key, "data_rate") == 0) {
            if ((ei_decode_long(req, req_index, &val) < 0) && (val >= 0) && (val < 3)){
                logData("Error: data_rate should be an integer (0 to 2)");
                return -1;
            }
	    rf24_datarate_e valEnum = static_cast<rf24_datarate_e>(val);
            config->data_rate = valEnum;
        } else if ((strcmp(key, "CRC_length") == 0) && (val >= 0) && (val < 3)){
            if (ei_decode_long(req, req_index, &val) < 0) {
                logData("Error: CRC length should be an integer (0 to 2)");
                return -1;
            }
	    rf24_crclength_e valEnum = static_cast<rf24_crclength_e>(val);
            config->CRC_length = valEnum;
                sprintf(buffer,"new CRC_Length: %u",config->CRC_length);
                logData(buffer);
        } else if (strcmp(key, "PA_level") == 0) {
           if ((ei_decode_long(req, req_index, &val) < 0) && (val >= 0) && (val < 5)){
                logData("Error: TX Power should be an integer (0 to 4))");
                return -1;
            }
            config->PA_level = static_cast<uint8_t> (val);
        } else if (strcmp(key, "reading_pipe") == 0) {
           if ((ei_decode_long(req, req_index, &val) < 0) && (val >= 0) && (val < 6)){
                logData("Error: reading pipe number should be an integer (0 to 5))");
                return -1;
            }
            config->reading_pipe = static_cast<uint8_t> (val);
        } else if (strcmp(key, "writing_pipe") == 0) {
           if ((ei_decode_long(req, req_index, &val) < 0) && (val >= 0) && (val < 6)){
                logData("Error: writing pipe number should be an integer (0 to 5))");
                return -1;
            }
            config->writing_pipe = static_cast<uint8_t> (val);
        } else if (strcmp(key, "RFchannel") == 0) {
           if ((ei_decode_long(req, req_index, &val) < 0) && (val >= 0) && (val < 128)){
                logData("Error: RF channel should be an integer (0 to 127))");
                return -1;
            }
            config->RFchannel = static_cast<uint8_t> (val);
            logData("\nINFO: Options Parsed\n");           
        } else {
            // unknown term
            ei_skip_term(req, req_index);
        }
    }
    return 0;
}

int RF24_configure(const struct RF24_config *config)
{
    char buffer [50];
    int buffer_len;

        rf24_datarate_e data_rate = static_cast<rf24_datarate_e>(config -> data_rate);
	if (current_config.data_rate != data_rate){
           radio.setDataRate(data_rate);
	   delay(5);
	   if (radio.getDataRate() != data_rate){
	  	logData("\nError: Data Rate did not update\n");
	  	return -1;
	   }
        } else {
	  logData("\nINFO: same data_rate\n");
        }
	
	rf24_crclength_e CRC_length = config -> CRC_length; 
        if(current_config.CRC_length != CRC_length){
	   radio.setCRCLength(CRC_length);
           delay(5);
	   if (radio.getCRCLength() != CRC_length){
                sprintf(buffer,"old CRC_length: %u, new CRC_Length: ,%u",radio.getCRCLength(),CRC_length);
                logData(buffer);
		logData("\nError: CRC length did not update\n");
		return -1;
	    }
          } else {
	     logData("\nINFO: same CRC\n");
	  }
	 
	uint8_t PA_level = config->PA_level ;
        if(current_config.PA_level != PA_level){
	   radio.setPALevel (PA_level);
           delay(5);
	   if (radio.getPALevel() != PA_level){
	      logData("\nError: TX Power did not update\n");
              return -1;
	    }
        } else {
	   logData("\nINFO: same PA\n");
	}
   
	uint8_t RFchannel = config->RFchannel ;
        current_config.RFchannel=radio.getChannel();
        if(current_config.RFchannel != RFchannel){
	   radio.setChannel (RFchannel);
           delay(5);
	   if (radio.getChannel() != RFchannel){
	      logData("\nError: RF channel did not update\n");
              return -1;
	    }
        } else {
	   logData("\nINFO: same RF chanel\n");
	}

    radio.printDetails();

    return 0;
}

static const char *last_Error = "ok";

const char *radio_last_Error()
{
    return last_Error;
}

static void record_last_Error(int err)
{
    char buffer [50];
    int buffer_len;

    // Convert the last Error to an appropriate
    // Erlang atom.
    switch(err) {
    case 0:
        last_Error = "ok";
        break;
    case ENOENT:
        last_Error = "enoent";
        break;
    case EBADF:
        last_Error = "ebadf";
        break;
    case EPERM:
        last_Error = "eperm";
        break;
    case EACCES:
        last_Error = "eacces";
        break;
    case EAGAIN:
        last_Error = "eagain";
        break;
    case ECANCELED:
        last_Error = "ecanceled";
        break;
    case EIO:
        last_Error = "eio";
        break;
    case EINTR:
        last_Error = "eintr";
        break;
    case ENOTTY:
        last_Error = "enotty";
        break;
    case EINVAL:
    default:
        buffer_len = sprintf(buffer,"Got unexpected Error: %d (%s)", err, strerror(err));
        logData(buffer);
        last_Error = "einval";
        break;
    }
}

/*static void record_errno()
{
    record_last_Error(errno);
}
*/
/*
 * Handle {name, kv_list}
 *
 *    name is the serial port name
 *    kv_list a list of configuration values (speed, parity, etc.)
 */
static void handle_open(const char *req, int *req_index)
{

    int term_type;
    int term_size;

    int buffer_len;
    char buffer[100];

    if (ei_decode_tuple_header(req, req_index, &term_size) < 0 ||
            term_size != 2){
	buffer_len = sprintf(buffer,"Error: open requires a 2-tuple");
        logData(buffer);
        send_Error_response(buffer);
        //errx(EXIT_FAILURE, ":open requires a 2-tuple");
	}

    char name[64];
    long binary_len;

    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(name) ||
            ei_decode_binary(req, req_index, name, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist
        logData("Error: enoent");
        send_Error_response("enoent");
        return;
      }
    name[term_size] = '\0';

    // If the Radio was already open
    if (radio.failureDetected==0){
        radio.failureDetected = 0;           // Reset the detection value
    }

   struct RF24_config config;

    if (parse_option_list(req, req_index, &config) < 0) {
        logData("Error: einval");
        send_Error_response("einval");
        return;
    }

   //Start Radio
   if (radioInit(config.reading_pipe, config.writing_pipe)) {  

       current_config.data_rate = radio.getDataRate();
       delay(5);
       current_config.CRC_length = radio.getCRCLength();
       delay(5);
       current_config.PA_level = radio.getPALevel();
       delay(5);
       current_config.reading_pipe=0;
       delay(5);
       current_config.writing_pipe=1;
       delay(5);
       current_config.RFchannel=radio.getChannel();
       delay(5);

        if (RF24_configure(&config) >= 0) {
            current_config = config;
         } else {
             logData(radio_last_Error());
             send_Error_response(radio_last_Error());
             return;
          }

        if (!radio.failureDetected ) {
           //send_ok_response();
           send_config_response(&current_config);
           logData("\nINFO: radio started\n");
        } else {
            logData("\Error: radio failed to start\n");
           send_Error_response(radio_last_Error());
        }
   } else {
         logData("\nError: radio failed to start\n");
         send_Error_response(radio_last_Error());
   }
}

/*
 * Handle {name, kv_list}
 *
 *    name is the serial port name
 *    kv_list a list of configuration values (speed, CRC Length, etc.)
 */

static void handle_configure(const char *req, int *req_index)
{
   struct RF24_config config = current_config;


    if (parse_option_list(req, req_index, &config) < 0) {
        logData("Error: einval");
        send_Error_response("einval");
        return;
    }

    if (RF24_configure(&config) >= 0) {
        current_config = config;
        send_config_response(&config);
    } else {
         logData(radio_last_Error());
        send_Error_response(radio_last_Error());
    }
}

static void handle_close(const char *req, int *req_index)
{
    (void) req;
    (void) req_index;

     //send_data("RF24.B",6);
    // logData("RF24.B");

      if (!radio.failureDetected ) {
         while (radio.available())
         {
             // Fetch the payload, and see if this was the last one.
	     uint8_t payload_len= radio.getDynamicPayloadSize();
             char response[max_payload_size+1]; // +1 to allow room for a terminating NULL char
	     radio.read(response, payload_len);
              // send data to elixir
             //send_data(response, payload_len);
         }

        // First, stop listening
        radio.stopListening();
        // Close reading Pipe
        radio.closeReadingPipe(1);

         //FLUSH_TX 1110 0001 0 Flush TX FIFO, used in TX mode, 
         radio.flush_tx();

         send_ok_response(); 

      } else {
        
        send_Error_response(radio_last_Error());
     }

     radio.powerDown();

     logData("\nINFO: Radio closed\n");

     exit(0);

}

/*static void handle_flush(const char *req, int *req_index)
{
    char dirstr[MAXATOMLEN];

    if (ei_decode_atom(req, req_index, dirstr) < 0) {
        send_Error_response("einval");
        return;
    }

     if (!radio.available()) {
        send_ok_response();
        return;
    }
    
    // First, stop listening
    radio.stopListening();

     while (radio.available())
      {
         // Fetch the payload, and see if this was the last one.
	 uint8_t payload_len= radio.getDynamicPayloadSize();
         char response[max_payload_size+1]; // +1 to allow room for a terminating NULL char
	 radio.read(response, payload_len);
        }

      //FLUSH_TX 1110 0001 0 Flush TX FIFO, used in TX mode, 
      radio.flush_tx();

      // Now, resume listening so we catch the next packets.
      radio.startListening();

      send_ok_response();
}
*/
static void handle_read(const char *payload, int *payload_size)
{
     char buffer[50];
     int buffer_len;

    radio.startListening(); 

    logData("Hi 2");

    for (int i=0;i<1000;i++){

       // if there is data ready
       if ( radio.available() )
      {

           logData("Hi 3");

	   // Dump the payloads until we've gotten everything
	   unsigned long got_time;

	   // Fetch the payload, and see if this was the last one.
	   while(radio.available()){
		radio.read( &got_time, sizeof(unsigned long) );
	   }
	   radio.stopListening();
		
	   radio.write( &got_time, sizeof(unsigned long) );

	   // Now, resume listening so we catch the next packets.
	   radio.startListening();

	   // Spew it
           buffer_len = sprintf(buffer,"%lu",got_time);
           send_data(buffer, buffer_len); 

	    buffer_len = sprintf(buffer,"\nIGot payload size=1 value=%lu\n",got_time);
            logData(buffer);
		
            delay(925); //Delay after payload responded to, minimize RPi CPU time
		
	}
   }
}

static void handle_write(const char *payload, int *payload_size)
{
     char buffer [50];
     int buffer_len;

    if (!radio.available() ) {
        send_Error_response("enodev");
        return;
    }

    // First, stop listening so we can talk.
    radio.stopListening();

    // Take the time, and send it.  This will block until complete
    buffer_len = sprintf(buffer, "INFO: Now sending length %i...",next_payload_size);
    logData(buffer);
    radio.write( payload, (uint8_t) *payload_size );

    // Now, continue listening
    radio.startListening();

    // Wait here until we get a response, or timeout
    unsigned long started_waiting_at = millis();
    bool timeout = false;
    while ( ! radio.available() && ! timeout )
      if (millis() - started_waiting_at > 500 )
        timeout = true;

    // Describe the results
    if ( timeout )
    {
       logData("Error: Failed, response timed out.");
       send_Error_response("etimedout");
       return;
    }
    else
    {
      // Grab the response, compare, and send to debugging spew
      uint8_t response_size = radio.getDynamicPayloadSize();
      char response[max_payload_size+1]; // +1 to allow room for a terminating NULL char
      radio.read( response, response_size );

      // Put a zero at the end for easy printing
      response[response_size] = 0;

      // Spew it
      buffer_len = sprintf(buffer,"Got response size=%i value=%s",response_size,response);
      logData(buffer);

      // send it to elixir
      send_data(receive_payload, (size_t) response_size);      
    
    }
}


struct request_handler {
    const char *name;
    void (*handler)(const char *req, int *req_index);
};

/* Elixir request handler table
 * Ordered roughly based on most frequent calls to least.
 */
static struct request_handler request_handlers[] = {
{ "open", handle_open},
{ "close", handle_close },
//{ "flush", handle_flush },
{ "write", handle_write },
{ "read", handle_read },
{ "configure", handle_configure},
{ NULL, NULL }
};

/**
 * @brief Decode and forward requests from Elixir tof the appropriate handlers
 * @param req the undecoded request
 * @param cookie
 */
static void handle_elixir_request(const char *req, void *cookie)
{
    (void) cookie;
    int buffer_len;
    char buffer[100];

    // Commands are of the form {Command, Arguments}:
    // { atom(), term() }
    int req_index = sizeof(uint16_t);
    ei_decode_version(req, &req_index, NULL) ;
  /*if (ei_decode_version(req, &req_index, NULL) < 0)
        errx(EXIT_FAILURE, "Message version issue?"); */

    int arity;
    //ei_decode_tuple_header(req, &req_index, &arity);
    if (ei_decode_tuple_header(req, &req_index, &arity) < 0 || arity != 2){
        //errx(EXIT_FAILURE, "expecting {cmd, args} tuple");
        buffer_len = sprintf(buffer,"Error: Expecting {cmd, args} tuple but recieved: %s",req);
        logData(buffer);
        send_Error_response(buffer);
     }

    char cmd[MAXATOMLEN];
    //ei_decode_atom(req, &req_index, cmd);
    if (ei_decode_atom(req, &req_index, cmd) < 0){
        //errx(EXIT_FAILURE, "expecting command atom");
        buffer_len = sprintf(buffer, "Error: Expecting command atom: %s", cmd);
        logData(buffer);
        send_Error_response(buffer);
    }

    for (struct request_handler *rh = request_handlers; rh->name != NULL; rh++) {
        if (strcmp(cmd, rh->name) == 0) {
            rh->handler(req, &req_index);
             return;
        }
    }
    buffer_len = sprintf(buffer, "Error: Unknown command: %s", cmd);
    logData(buffer);
    send_Error_response(buffer);
    //errx(EXIT_FAILURE, "unknown command: %s", cmd);
}

int main_loop(void)
 {
        // Print preamble:
        logData("\nINFO: start listening to elixir\n");

       struct erlcmd* handler =   new erlcmd;
       erlcmd_init(handler, handle_elixir_request, NULL);
       
       logData("\nINFO: start listening to elixir\n");

        bool running = true;

        while (running)
	{

         struct pollfd fdset[3];

          fdset[0].fd = STDIN_FILENO;
          fdset[0].events = POLLIN;
          fdset[0].revents = 0;

        int timeout = -1; // Wait forever unless told by otherwise
        int count = 1;

        int rc = poll(fdset, count + 1, timeout);
        if (rc < 0) {
            // Retry if EINTR
            if (errno == EINTR)
                continue;
            logData("\nError: poll failure\n");
            //err(EXIT_FAILURE, "poll");
        }

      if (fdset[0].revents & (POLLIN | POLLHUP)) {
            logData("\nINFO: recieved elixir command\n");
            if (erlcmd_process(handler))
                break;
        }
    }
    return 0;
} 

int main(int argc, char *argv[])
{

        main_loop();

}// end Main

bool radioInit(uint8_t reading_pipe, uint8_t writing_pipe)  {
     // Setup and configure radio
     if (radio.begin()){

         //radio.enableDynamicPayloads();

         radio.setRetries(15,15);

         radio.printDetails();

         radio.openWritingPipe(pipes[writing_pipe]);
         radio.openReadingPipe(1,pipes[reading_pipe]);

         radio.startListening();

         return 1;

      } else {

          return 0;
      }
}
