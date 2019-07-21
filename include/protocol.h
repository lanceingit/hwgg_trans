#pragma once

#define HEAD1       0x46
#define HEAD2       0xB9

#define SEND_MASK   0x68  
#define RECV_MASK   0x6A 

#define BYTE3_MASK  0

#define TAIL        0x16

#define DEV_TYPE    0

#define PROTOCOL_CH_NET     1
#define PROTOCOL_CH_UART    2

#define CMD_CONNECT             0x01

/*
46 b9 6a 00 06 00 f0 00 10 1c 02 01 8e 16       在设备控制界面下拉刷新会触发，这时控制类型（第二个参数）应答0也可以
*/
#define CMD_STATUS              0x02
    #define CONTROL_MANUAL              3
    #define CONTROL_DELAY               4
    #define CONTROL_TIMING              5
    #define CONTROL_LOOP                6


#define CMD_CONTROL             0x03

/*
46 b9 6a 00 0b 00 f0 00 10 1c 04 01 01 03 02 00 01 9c 16      延时开 3:2:0 （开关状态取决于当前设备状态）
46 b9 6a 00 0b 00 f0 00 10 1c 04 00 01 00 00 00 01 96 16      取消 
46 b9 6a 00 0b 00 f0 00 10 1c 04 01 00 01 02 02 01 9b 16      延时关 1:2:3 （延时秒数会在当前基础-1，如果是0秒，则不减）    
46 b9 6a 00 0b 00 f0 00 10 1c 04 00 00 00 00 00 01 95 16      取消
*/
#define CMD_SET_DELAY_TASK      0x04   
    #define DELAY_TASK_SIZE             5

/*
46 b9 6a 00 06 00 f0 00 10 1c 24 01 b0 16       app切换标签栏会触发
*/
#define CMD_GET_DELAY_TASK      0x24

/*
46 b9 6a 00 10 00 f0 00 10 1c 05 
00          id 
01          任务开
01          开
16 29 33    开始时间
0d 00 03    结束时间
80          重复类型
02 9f 16
*/
#define CMD_SET_TIMING_TASK     0x05

/*
46 b9 6a 00 08 00 f0 00 10 1c 15|00|01|01 a4 16     设置开
46 b9 6a 00 08 00 f0 00 10 1c 15|00|00|01 a3 16     设置关
*/
#define CMD_SET_TIMING_SW       0x15

/*
46 b9 6a 00 06 00 f0 00 10 1c 25 01 b1 16
*/
#define CMD_GET_TIMING_TASK     0x25
    #define TIMING_TASK_SIZE            10

/*
46 b9 6a 00 07 00 f0 00 10 1c 35 00 01 c2 16
*/
#define CMD_DEL_TIMING_TASK     0x35

/*
设置状态循环
46 b9 6a 00 16 00 f0 00 10 1c 06 01|15 25 22|16 26 29|01|00 03 02|00 0f 03|00|80|02 fc 16 
46 b9 6a 00 16 00 f0 00 10 1c 06 
01                  任务开关状态
15 25 22            开始时间
15 26 29            结束时间
01                  打开 （固定前面是开，后面是关  !!协议则是可开可关）
00 02 00            打开时长
00 0f 00            关闭时长
00                  关闭
80                  重复类型
02 f5 16       
*/
#define CMD_SET_LOOP_TASK       0x06
    #define LOOP_TASK_SIZE              16

/*
46 b9 6a 00 07 00 f0 00 10 1c 16 00 01 a3 16        循环任务关
46 b9 6a 00 07 00 f0 00 10 1c 16 01 01 a4 16        循环任务关
*/
#define CMD_SET_LOOP_SW         0x16

/*
46 b9 6a 00 06 00 f0 00 10 1c 26 01 b2 16           查询
*/
#define CMD_GET_LOOP_TASK       0x26

/*
app暂无此操作
*/
#define CMD_DEL_LOOP_TASK       0x36

#define CMD_GET_TIME            0x07
#define CMD_GET_VERSION         0x08
#define CMD_RECONNECT           0x0B
#define CMD_TRANS_VERSION       0x0C
#define CMD_REQUEST_UPGRADE     0xA0
#define CMD_UPGRADE_STATUS      0xA1
#define CMD_GET_FIRMWARE_VER    0xA2
#define CMD_MCU_UPGRADE         0xA4
#define CMD_MCU_UPGRADE_READY   0xAB
#define CMD_MCU_UPGRADE_STATUS  0xAD
#define CMD_MCU_UPGRADE_REQUEST 0xB4


enum parse_status
{
    WAIT_HEAD1 = 0,
    WAIT_HEAD2,
    WAIT_DIR,
    WAIT_BYTE3,
    WAIT_LEN,
    WAIT_DATA,
    WAIT_CHECKSUM_H,
    WAIT_CHECKSUM_L,
    WAIT_TAIL,
};

enum protocol_status
{
    STATUS_IDLE = 0,
    STATUS_WAIT_RECV,
};

void protocol_init(void);
void protocol_update(void);
void protocol_set_recv(uint8_t* data, uint16_t len);
bool protocol_msg_parse(uint8_t* data, uint16_t len, uint8_t* msg_id);
bool protocol_is_need_trans(uint8_t msg_id);
int8_t protocol_send(uint8_t ch, uint8_t cmd, bool need_ack, ...);
