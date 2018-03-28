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
const uint64_t pipes[2] = { 0xF0F0F0F0E1LL, 0xF0F0F0F0D2LL };

const int min_payload_size = 4;
const int max_payload_size = 32;
const int payload_size_increments_by = 1;
int next_payload_size = min_payload_size;

char receive_payload[max_payload_size+1]; // +1 to allow room for a terminating NULL char

void radioInit(void) ;

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
static void send_ok_response()
{
    const char uid[] = "command complete";
    int len = 16;

    char resp[256];
    //send_data("RF24.9",6);
    logData("RF24.9");

    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "ok");
    ei_encode_binary(resp, &resp_index, uid, len);
    erlcmd_send(resp, resp_index);
}

/**
 * @brief Send a response of the form {:error, reason}
 *
 * @param reason a reason (sent back as an atom)
 */
static void send_error_response(const char *reason)
{
    char resp[256];
    int resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = response_id;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "error");
    ei_encode_atom(resp, &resp_index, reason);
    erlcmd_send(resp, resp_index);
}

static int parse_option_list(const char *req, int *req_index, RF24_config *config)
{
    int term_type;
    int option_count;
     long int val;

    if (ei_get_type(req, req_index, &term_type, &option_count) < 0 ||
            (term_type != ERL_LIST_EXT && term_type != ERL_NIL_EXT)) {
        logData("expecting option list");
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
            logData("expecting kv tuple for options");
            return -1;
        }

        char key[64];
        if (ei_decode_atom(req, req_index, key) < 0) {
            logData("expecting atoms for option keys");
            return -1;
        }

        if (strcmp(key, "active") == 0) {
            int  val;
            if (ei_decode_boolean(req, req_index, &val) < 0) {
                logData("active should be a bool");
                return -1;
            }
            config->active = (val != 0);
        } else if (strcmp(key, "speed") == 0) {
            if ((ei_decode_long(req, req_index, &val) < 0) && (val > 0) && (val < 3)){
                logData("speed should be an integer (0 to 2)");
                return -1;
            }
	    rf24_datarate_e valEnum = static_cast<rf24_datarate_e>(val);
            config->speed = valEnum;
        } else if ((strcmp(key, "CRC_length") == 0) && (val > 0) && (val < 3)){
            if (ei_decode_long(req, req_index, &val) < 0) {
                logData("CRC length should be an integer (0 to 2)");
                return -1;
            }
	    rf24_crclength_e valEnum = static_cast<rf24_crclength_e>(val);
            config->CRC_length = valEnum;
        } else if (strcmp(key, "PA_level") == 0) {
           if ((ei_decode_long(req, req_index, &val) < 0) && (val > 0) && (val < 5)){
                logData("TX Power should be an integer (0 to 4))");
                return -1;
            }
	    //rf24_pa_dbm_e valEnum = static_cast<rf24_pa_dbm_e>(val);
            config->PA_level = static_cast<uint8_t> (val);
            logData("EEH");
            
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
     int len;

        rf24_datarate_e speed = static_cast<rf24_datarate_e>(config -> speed);
	if (current_config.speed != speed){
           radio.setDataRate(speed);
	   delay(5);
	   if (radio.getDataRate() != speed){
	  	logData("Data Rate did not update");
	  	return -1;
	   }
        } else {
	  logData("same Speed");
        }
	
	rf24_crclength_e CRC_length = static_cast<rf24_crclength_e>(config -> CRC_length); 
        if(current_config.CRC_length != CRC_length){
	   radio.setCRCLength(CRC_length);
	   delay(5);
	  //check = radio.getCRCLength();
	  if (radio.getCRCLength() != CRC_length){
		logData("CRC length did not update");
		return -1;
	    }
          } else {
	     logData("same CRC");
	  }
	 
	uint8_t PA_level = config->PA_level ;
	     len = sprintf(buffer,"PA_level = %i", PA_level);
             logData(buffer);
        if(current_config.PA_level != PA_level){
	   radio.setPALevel (PA_level);
           delay(5);
	   //check = radio.getPALevel();
	   if (radio.getPALevel() != PA_level){
	      logData("TX Power did not update");
              return -1;
	    }
        } else {
	   logData("same PA");
	}
   
    radio.printDetails();

    return 0;
}

static const char *last_error = "ok";

const char *radio_last_error()
{
    return last_error;
}

static void record_last_error(int err)
{
    char buffer [50];
     int len;

    // Convert the last error to an appropriate
    // Erlang atom.
    switch(err) {
    case 0:
        last_error = "ok";
        break;
    case ENOENT:
        last_error = "enoent";
        break;
    case EBADF:
        last_error = "ebadf";
        break;
    case EPERM:
        last_error = "eperm";
        break;
    case EACCES:
        last_error = "eacces";
        break;
    case EAGAIN:
        last_error = "eagain";
        break;
    case ECANCELED:
        last_error = "ecanceled";
        break;
    case EIO:
        last_error = "eio";
        break;
    case EINTR:
        last_error = "eintr";
        break;
    case ENOTTY:
        last_error = "enotty";
        break;
    case EINVAL:
    default:
        len = sprintf(buffer,"Got unexpected error: %d (%s)", err, strerror(err));
        logData(buffer);
        last_error = "einval";
        break;
    }
}

static void record_errno()
{
    record_last_error(errno);
}

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

     ei_decode_tuple_header(req, req_index, &term_size);
    /*if (ei_decode_tuple_header(req, req_index, &term_size) < 0 ||
            term_size != 2)
        errx(EXIT_FAILURE, ":open requires a 2-tuple");*/

    char name[64];
    long binary_len;
    ei_get_type(req, req_index, &term_type, &term_size);
    ei_decode_binary(req, req_index, name, &binary_len) ;
/*    if (ei_get_type(req, req_index, &term_type, &term_size) < 0 ||
            term_type != ERL_BINARY_EXT ||
            term_size >= (int) sizeof(name) ||
            ei_decode_binary(req, req_index, name, &binary_len) < 0) {
        // The name is almost certainly too long, so report that it
        // doesn't exist

        send_error_response("enoent");
        return;
    }*/

    name[term_size] = '\0';

    //send_data("RF24.N", 6);
    logData("RF24.N");

    // If the Radio was already open
    if (radio.failureDetected==0){
        radio.failureDetected = 0;           // Reset the detection value
    }

    radioInit();  //Start Radio

   current_config.speed = radio.getDataRate();
   current_config.CRC_length = radio.getCRCLength();
   current_config.PA_level = radio.getPALevel();

    struct RF24_config config = current_config;

    if (parse_option_list(req, req_index, &config) < 0) {
        send_error_response("einval");
        return;
    }

    if (RF24_configure(&config) >= 0) {
        current_config = config;
        logData("Config worked");
    } else {
        send_error_response(radio_last_error());
    }

   if (!radio.failureDetected ) {
        send_ok_response();
        //send_data("RF24.G", 6);
        logData("RF24.G");
    } else {
        //send_data("RF24.K", 6);
        logData("RF24.K");
        send_error_response(radio_last_error());
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
        send_error_response("einval");
        return;
    }

    if (RF24_configure(&config) >= 0) {
        current_config = config;
        send_ok_response();
    } else {
        send_error_response(radio_last_error());
    }
}

static void handle_close(const char *req, int *req_index)
{
    (void) req;
    (void) req_index;

     //send_data("RF24.B",6);
     logData("RF24.B");

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
        radio.closeReadingPipe(pipes[0]);

         //FLUSH_TX 1110 0001 0 Flush TX FIFO, used in TX mode, 
         radio.flush_tx();

         send_ok_response(); 

      } else {
        send_error_response(radio_last_error());
     }

     radio.powerDown();

     exit(0);

}

static void handle_flush(const char *req, int *req_index)
{
    char dirstr[MAXATOMLEN];

    if (ei_decode_atom(req, req_index, dirstr) < 0) {
        send_error_response("einval");
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

static void handle_read(const char *payload, int *payload_size)
{
     char buffer [50];
     int len;

    logData("Hi 1");

    char receive_payload[100];

   radio.startListening(); 

   logData("Hi 2");

   while(1){

       // if there is data ready
      if(radio.available() )
      {
        // Dump the payloads until we've gotten everything
        uint8_t len;
          logData("Hi 3");
        while (radio.available())
        {
 
           // Fetch the payload, and see if this was the last one.
	   len = radio.getDynamicPayloadSize();
	   radio.read( receive_payload, len );

            // send it to elixir
            send_data(receive_payload, len);    

	    // Put a zero at the end for easy printing
	    receive_payload[len] = 0;

             // Spew it
	     len = sprintf(buffer,"Got payload size=%i value=%s",len,receive_payload);
             logData(buffer);
  
        }

        // First, stop listening so we can talk
         radio.stopListening();

         // Send the final one back.
        radio.write( receive_payload, len );
        logData("Sent response");

        // Now, resume listening so we catch the next packets.
       radio.startListening(); 

      send_ok_response();

       break;
   }
}
}

static void handle_write(const char *payload, int *payload_size)
{
     char buffer [50];
     int len;

    if (!radio.available() ) {
        send_error_response("enodev");
        return;
    }

    // First, stop listening so we can talk.
    radio.stopListening();

    // Take the time, and send it.  This will block until complete
    len = sprintf(buffer, "Now sending length %i...",next_payload_size);
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
       logData("Failed, response timed out.");
       send_error_response("etimedout");
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
      len = sprintf(buffer,"Got response size=%i value=%s",response_size,response);
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
{ "flush", handle_flush },
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

    // Commands are of the form {Command, Arguments}:
    // { atom(), term() }
    int req_index = sizeof(uint16_t);
    ei_decode_version(req, &req_index, NULL) ;
  /*  if (ei_decode_version(req, &req_index, NULL) < 0)
        errx(EXIT_FAILURE, "Message version issue?"); */

    int arity;
    ei_decode_tuple_header(req, &req_index, &arity);
   /* if (ei_decode_tuple_header(req, &req_index, &arity) < 0 ||
            arity != 2)
        errx(EXIT_FAILURE, "expecting {cmd, args} tuple");*/

    char cmd[MAXATOMLEN];
    ei_decode_atom(req, &req_index, cmd);
   /* if (ei_decode_atom(req, &req_index, cmd) < 0)
        errx(EXIT_FAILURE, "expecting command atom");*/

    for (struct request_handler *rh = request_handlers; rh->name != NULL; rh++) {
        if (strcmp(cmd, rh->name) == 0) {
            rh->handler(req, &req_index);
             return;
        }
    }
    //errx(EXIT_FAILURE, "unknown command: %s", cmd);
}

int main_loop(void)
 {
        //send_data("RF24.1", 6);
        logData("RF24.1");

        // Print preamble:
        logData("nerves_io_RF2");

       struct erlcmd* handler =   new erlcmd;
       erlcmd_init(handler, handle_elixir_request, NULL);

      //send_data("RF24.4", 6);
      logData("RF24.4");

        bool running = true;

        while (running)
	{

         struct pollfd fdset[3];

          fdset[0].fd = STDIN_FILENO;
          fdset[0].events = POLLIN;
          fdset[0].revents = 0;

        int timeout = -1; // Wait forever unless told by otherwise
        //int count = uart_add_poll_events(uart, &fdset[1], &timeout);
        int count = 1;

        int rc = poll(fdset, count + 1, timeout);
        if (rc < 0) {
            // Retry if EINTR
            if (errno == EINTR)
                continue;

            //err(EXIT_FAILURE, "poll");
        }

      if (fdset[0].revents & (POLLIN | POLLHUP)) {
            //send_data("RF24.A", 6);
            logData("RF24.A");
            if (erlcmd_process(handler))
                break;
        }

      
        /*// Call uart_process if it added any events
       // if (count)
        //    uart_process(uart, &fdset[1]); 
        } */    
    }

    // Exit due to Erlang trying to end the process.
    //
   /* if (uart_is_open(uart))
        uart_flush_all(uart);
         } */     
  
	return 0;

} // end Main
int main(int argc, char *argv[])
{

 //   if (argc == 1)
        main_loop();
/*    else if (argc == 2 && strcmp(argv[1], "test") == 0)
   {
        //test();
       // test mode; send tag to host every second
       if (!strcmp(argv[1], "test"))
       {
           //dbg("RF24 port test mode.");
            for (;;) 
             {
               erlcmd_process(handler);
               usleep(1000000);
            }
       }
	if (argc != 2)
       {
            //err(1, "Usage: rf24 <spi_speed|test>");
        }
    }*/
//    else
        //errx(EXIT_FAILURE, "%s [enumerate]", argv[0]);
//    return 0;
}

void radioInit(void)  {
     // Setup and configure rf radio
     radio.begin();

     if(radio.failureDetected)
         logData("Failed");
 
      radio.enableDynamicPayloads();

      radio.setRetries(5,15);

      radio.printDetails();

      radio.openWritingPipe(pipes[0]);
      radio.openReadingPipe(1,pipes[1]);

      radio.startListening();
}
