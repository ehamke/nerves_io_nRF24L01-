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
#include <sys/wait.h>
#include <signal.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include "./RF24.h"

#include <ei.h>

#define err(code, msg) (fprintf(stderr, msg "\n"),exit(code));
#define dbg(msg) (fprintf(stderr, msg "\n"));

using namespace std;

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

void erlcmd_send(char *response, size_t len);
void send_data(const char *uid, size_t len);
void radioInit(void) ;


int main(int argc, char *argv[])
 {

	char *p;
	char sn_str[23];
        char testString[20];
        int testStringLength;
        unsigned int time;

        // Print preamble:
        printf("nerves_io_RF24\n");

       // test mode; send tag to host every second
       if (!strcmp(argv[1], "test"))
       {
           dbg("RF24 port test mode.");
            for (;;) 
             {
               time = millis();
               testStringLength = sprintf(testString,"RF24");
               send_data("RF24", 4);
               usleep(1000000);
            }
       }

        radioInit();

	if (argc != 2)
       {
            err(1, "Usage: rf24 <spi_speed|test>");
        }        radioInit();

	if (argc != 2)
       {
            err(1, "Usage: rf24 <spi_speed|test>");
        }

        // forever loop
	while (1)
	{
	    // if there is data ready
	    if ( radio.available() )
	    {
 	        // Dump the payloads until we've gotten everything
	        uint8_t len;

	        while (radio.available())
	        {
	           // Fetch the payload, and see if this was the last one.
		   len = radio.getDynamicPayloadSize();
		   radio.read( receive_payload, len );

		    // Put a zero at the end for easy printing
		    receive_payload[len] = 0;

	             // Spew it
		     printf("Got payload size=%i value=%s\n\r",len,receive_payload);
	        }

	        // First, stop listening so we can talk
	         radio.stopListening();

	         // Send the final one back.
	        radio.write( receive_payload, len );
	        printf("Sent response.\n\r");

	        // Now, resume listening so we catch the next packets.
	        radio.startListening();
             }

  /*        Status= find_tag(&CType);
    	   if (status == TAG_NOTAG) {
			usleep(50000);
			continue;
		} else if ((status!=TAG_OK) && (status!=TAG_COLLISION)) {
                  continue;
             }

	    if (select_tag_sn(SN,&SN_len) != TAG_OK) {
                continue;
            }

            p = sn_str;
            for (tmp=0;tmp<SN_len;tmp++) {
	        sprintf(p,"%02X",SN[tmp]);
	         p+=2;
            }
           *p = 0;

           fprintf(stderr,"Type: %04X, Serial: %s\n",CType,&sn_str[1]);
           send_data(sn_str, 2 * SN_len);

	   PcdHalt();  */
	}  

	return 0;

} // end Main

void send_data(const char *uid, size_t len) {
    char resp[1024];
    int resp_index = sizeof(uint16_t); // Space for payload size
    ei_encode_version(resp, &resp_index);

    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "tag");
    ei_encode_binary(resp, &resp_index, uid, len);

    erlcmd_send(resp, resp_index);
}

void radioInit(void)  {
        // Setup and configure rf radio
      radio.begin();
      radio.enableDynamicPayloads();
      radio.setRetries(5,15);
      radio.printDetails();

      radio.openWritingPipe(pipes[1]);
      radio.openReadingPipe(1,pipes[0]);
      radio.startListening();
}

/**
 * @brief Synchronously send a response back to Erlang
 *
 * @param response what to send back
 */
void erlcmd_send(char *response, size_t len)
{
    uint16_t be_len = htons(len - sizeof(uint16_t));
    memcpy(response, &be_len, sizeof(be_len));

    size_t wrote = 0;
    do {
        ssize_t amount_written = write(STDOUT_FILENO, response + wrote, len - wrote);
        if (amount_written < 0) {
            if (errno == EINTR)
                continue;

            //err(EXIT_FAILURE, "write");
            exit(0);
        }

        wrote += amount_written;
    } while (wrote < len);
}
