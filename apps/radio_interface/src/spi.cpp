#include "spi.h"
#include <pthread.h>
#include "telemtry.h"

static pthread_mutex_t spiMutex = PTHREAD_MUTEX_INITIALIZER;

SPI::SPI() {

}


void SPI::begin( int busNo ) {
	if (!bcm2835_init()){
		return;
	}
	
	bcm2835_spi_begin();
        logData("init");
}

void SPI::beginTransaction(SPISettings settings){
//  	logData("begin ");
	pthread_mutex_lock (&spiMutex);
	setBitOrder(settings.border);
//	logData("bitOrder");
	setDataMode(settings.dmode);
	setClockDivider(settings.clck);
}

void SPI::endTransaction() {
	pthread_mutex_unlock (&spiMutex);
}

void SPI::setBitOrder(uint8_t bit_order) {
	bcm2835_spi_setBitOrder(bit_order);
}

void SPI::setDataMode(uint8_t data_mode) {
//      logData("set");
      bcm2835_spi_setDataMode(data_mode);
//      logData("set end");
}

void SPI::setClockDivider(uint16_t spi_speed) {
	bcm2835_spi_setClockDivider(spi_speed);
}

void SPI::chipSelect(int csn_pin){
//        logData("chipSelect_1");
	bcm2835_spi_chipSelect(csn_pin);
//        logData("chipSelect_2");
	delayMicroseconds(5);
//        logData("chipSelect_3");
}

SPI::~SPI() {

}
