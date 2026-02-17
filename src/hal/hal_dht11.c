#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "hal_dht11.h"

#define DEV_NAME "/dev/dht11"

int hal_dht11_read(float *temp, float *hum)
{
    static int first_run = 1;

    int fd = open(DEV_NAME, O_RDWR); // ← 改成 O_RDWR

    unsigned char data[4] = {0};
    int ret = read(fd, &data, sizeof(data));
    close(fd);

    if (ret > 0)
    {                                    // ← 官方判断 if(ret)
        *temp = data[2] + data[3] * 0.1; // Temperature=data[2].data[3]
        *hum = data[0] + data[1] * 0.1;  // Humidity=data[0].data[1]
        return 0;
    }
    return -1;
}