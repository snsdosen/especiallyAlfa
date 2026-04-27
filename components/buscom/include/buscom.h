#ifndef _BUSCOM_H_
#define _BUSCOM_H_

    #define LOG_TAG_BUSCOM  "BUSCOM"

    #define BUSCOM_RX_PIN       22
    #define BUSCOM_TX_PIN       23
    #define BUSCOM_RX_SNIFF     21

    #define POWER_MONITOR_PIN   19

    //Head unit command headers
    #define HU_HEADER_ACTIVITY      0x09    //Module activity
    #define HU_HEADER_EJECT         0x18    //Eject command
    #define HU_HEADER_PSTATE        0x23    //Play state commands
    #define HU_HEADER_SEEK          0x32    //Seeking commands
    #define HU_HEADER_CD_INSERTED   0x70    //CD inserted status request
    #define HU_READY_REQUEST        0x79    //Are you ready request
    #define HU_POWER_ON             0x60    //Power available, but ignition might still be off
    #define HU_MODULE_STATUS        0x51    //Extended module status

    //Head unit MSG length
    #define HU_ACTIVITY_LEN     1       //Module activity
    #define HU_PSTATE_LEN       3       //Play state commands
    #define HU_SEEK_LEN         2       //Seeking commands
    #define HU_READY_LEN        1       //Ready request

    //Module activity
    #define MODULE_ACTIVE       0x2     //CD module active
    #define MODULE_STANDBY      0x1     //CD module entering standby
    #define MODULE_INACTIVE     0x0     //CD module inactive

    //Play states
    #define PSTATE_PLAY         0x00    //Play
    #define PSTATE_PAUSE        0x05    //Pause
    #define PSTATE_REW          0xE0    //Rewind
    #define PSTATE_FF           0x60    //Fast forward

    //CD drive/media command headers
    #define CD_RESPONSE         0xE0    //Confirmation response

    #define CD_STATUS_RES       0xE1    //Status response
    #define CD_STATUS_RES2      0xE2    //Status response

    #define CD_PSTATE_RES       0xE4    //Play state

    #define CD_INFO_RES         0x36    //Response with cd geometry

    #define CD_STATUS_NONE          0x70    //Media not inserted
    #define CD_STATUS_INSERTING     0x71    //Media being inserted
    #define CD_STATUS_INSERTED      0x72    //Response from inserted media
    #define CD_STATUS_READY         0x73    //Media is ready to play

    #define MEDIA_INSERT_START      0x6C    //Start inserting emulated media
    #define MEDIA_INSERT_ONGOING    0x62    //Insertion in progress

    #define MEDIA_TIME_STATUS       0x15    //Time status of the played media

void InitBuscom(void);

#endif