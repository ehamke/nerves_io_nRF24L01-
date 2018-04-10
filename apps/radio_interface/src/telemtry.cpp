#include "telemtry.h"
#include <unistd.h>

void logData(const char *uid){
        FILE *pFileLog;
        char log_file[64];
       
        sprintf(log_file, "nerves_io_RF24_%d.log", (int) getpid());

        pFileLog = fopen (log_file,"a");     // use "a" for append, "w" to overwrite, previous content will be deleted

       fprintf(pFileLog,"%s\n",uid);

       fclose (pFileLog);
}

void send_data(const char *uid, size_t len) {
    char resp[1024];
    int resp_index = sizeof(uint16_t); // Space for payload size
    ei_encode_version(resp, &resp_index);

    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "data");
    ei_encode_binary(resp, &resp_index, uid, len);

    erlcmd_send(resp, resp_index);
}
