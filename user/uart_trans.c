#include "user_interface.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"

#include "board.h"
#include "driver/uart.h"
#include "net.h"

uint8 uart_buf[128]={0};
extern UartDevice    UartDev;

LOCAL struct UartBuffer* pTxBuffer = NULL;
LOCAL struct UartBuffer* pRxBuffer = NULL;

#define uart_recvTaskPrio        0
#define uart_recvTaskQueueLen    10
os_event_t    uart_recvTaskQueue[uart_recvTaskQueueLen];

/******************************************************************************
 * FunctionName : Uart_Buf_Init
 * Description  : tx buffer enqueue: fill a first linked buffer 
 * Parameters   : char *pdata - data point  to be enqueue
 * Returns      : NONE
*******************************************************************************/
struct UartBuffer* ICACHE_FLASH_ATTR
Uart_Buf_Init(uint32 buf_size)
{
    uint32 heap_size = system_get_free_heap_size();
    if(heap_size <=buf_size){
        os_printf("no buf for uart\n\r");
        return NULL;
    }else{
        os_printf("test heap size: %d\n\r",heap_size);
        struct UartBuffer* pBuff = (struct UartBuffer* )os_malloc(sizeof(struct UartBuffer));
        pBuff->UartBuffSize = buf_size;
        pBuff->pUartBuff = (uint8*)os_malloc(pBuff->UartBuffSize);
        pBuff->pInPos = pBuff->pUartBuff;
        pBuff->pOutPos = pBuff->pUartBuff;
        pBuff->Space = pBuff->UartBuffSize;
        pBuff->BuffState = OK;
        pBuff->nextBuff = NULL;
        pBuff->TcpControl = RUN;
        return pBuff;
    }
}


//copy uart buffer
LOCAL void Uart_Buf_Cpy(struct UartBuffer* pCur, char* pdata , uint16 data_len)
{
    if(data_len == 0) return ;
    
    uint16 tail_len = pCur->pUartBuff + pCur->UartBuffSize - pCur->pInPos ;
    if(tail_len >= data_len){  //do not need to loop back  the queue
        os_memcpy(pCur->pInPos , pdata , data_len );
        pCur->pInPos += ( data_len );
        pCur->pInPos = (pCur->pUartBuff +  (pCur->pInPos - pCur->pUartBuff) % pCur->UartBuffSize );
        pCur->Space -=data_len;
    }else{
        os_memcpy(pCur->pInPos, pdata, tail_len);
        pCur->pInPos += ( tail_len );
        pCur->pInPos = (pCur->pUartBuff +  (pCur->pInPos - pCur->pUartBuff) % pCur->UartBuffSize );
        pCur->Space -=tail_len;
        os_memcpy(pCur->pInPos, pdata+tail_len , data_len-tail_len);
        pCur->pInPos += ( data_len-tail_len );
        pCur->pInPos = (pCur->pUartBuff +  (pCur->pInPos - pCur->pUartBuff) % pCur->UartBuffSize );
        pCur->Space -=( data_len-tail_len);
    }
    
}

/******************************************************************************
 * FunctionName : uart_buf_free
 * Description  : deinit of the tx buffer
 * Parameters   : struct UartBuffer* pTxBuff - tx buffer struct pointer
 * Returns      : NONE
*******************************************************************************/
void ICACHE_FLASH_ATTR
uart_buf_free(struct UartBuffer* pBuff)
{
    os_free(pBuff->pUartBuff);
    os_free(pBuff);
}


//rx buffer dequeue
uint16 ICACHE_FLASH_ATTR
rx_buff_deq(char* pdata, uint16 data_len )
{
    uint16 buf_len =  (pRxBuffer->UartBuffSize- pRxBuffer->Space);
    uint16 tail_len = pRxBuffer->pUartBuff + pRxBuffer->UartBuffSize - pRxBuffer->pOutPos ;
    uint16 len_tmp = 0;
    len_tmp = ((data_len > buf_len)?buf_len:data_len);
    if(pRxBuffer->pOutPos <= pRxBuffer->pInPos){
        os_memcpy(pdata, pRxBuffer->pOutPos,len_tmp);
        pRxBuffer->pOutPos+= len_tmp;
        pRxBuffer->Space += len_tmp;
    }else{
        if(len_tmp>tail_len){
            os_memcpy(pdata, pRxBuffer->pOutPos, tail_len);
            pRxBuffer->pOutPos += tail_len;
            pRxBuffer->pOutPos = (pRxBuffer->pUartBuff +  (pRxBuffer->pOutPos- pRxBuffer->pUartBuff) % pRxBuffer->UartBuffSize );
            pRxBuffer->Space += tail_len;
            
            os_memcpy(pdata+tail_len , pRxBuffer->pOutPos, len_tmp-tail_len);
            pRxBuffer->pOutPos+= ( len_tmp-tail_len );
            pRxBuffer->pOutPos= (pRxBuffer->pUartBuff +  (pRxBuffer->pOutPos- pRxBuffer->pUartBuff) % pRxBuffer->UartBuffSize );
            pRxBuffer->Space +=( len_tmp-tail_len);                
        }else{
            //os_printf("case 3 in rx deq\n\r");
            os_memcpy(pdata, pRxBuffer->pOutPos, len_tmp);
            pRxBuffer->pOutPos += len_tmp;
            pRxBuffer->pOutPos = (pRxBuffer->pUartBuff +  (pRxBuffer->pOutPos- pRxBuffer->pUartBuff) % pRxBuffer->UartBuffSize );
            pRxBuffer->Space += len_tmp;
        }
    }
    if(pRxBuffer->Space >= UART_FIFO_LEN){
        uart_rx_intr_enable(UART0);
    }
    return len_tmp; 
}


//move data from uart fifo to rx buffer
void Uart_rx_buff_enq()
{
    uint8 fifo_len,buf_idx;
    uint8 fifo_data;
    #if 1
    fifo_len = (READ_PERI_REG(UART_STATUS(UART0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT;
    if(fifo_len >= pRxBuffer->Space){
        os_printf("buf full!!!\n\r");            
    }else{
        buf_idx=0;
        while(buf_idx < fifo_len){
            buf_idx++;
            fifo_data = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
            *(pRxBuffer->pInPos++) = fifo_data;
            if(pRxBuffer->pInPos == (pRxBuffer->pUartBuff + pRxBuffer->UartBuffSize)){
                pRxBuffer->pInPos = pRxBuffer->pUartBuff;
            }            
        }
        pRxBuffer->Space -= fifo_len ;
        if(pRxBuffer->Space >= UART_FIFO_LEN){
            //os_printf("after rx enq buf enough\n\r");
            uart_rx_intr_enable(UART0);
        }
    }
    #endif
}


//fill the uart tx buffer
void ICACHE_FLASH_ATTR
tx_buff_enq(char* pdata, uint16 data_len )
{
    CLEAR_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);

    if(pTxBuffer == NULL){
        os_printf("\n\rnull, create buffer struct\n\r");
        pTxBuffer = Uart_Buf_Init(UART_TX_BUFFER_SIZE);
        if(pTxBuffer!= NULL){
            Uart_Buf_Cpy(pTxBuffer ,  pdata,  data_len );
        }else{
            os_printf("uart tx MALLOC no buf \n\r");
        }
    }else{
        if(data_len <= pTxBuffer->Space){
        Uart_Buf_Cpy(pTxBuffer ,  pdata,  data_len);
        }else{
            os_printf("UART TX BUF FULL!!!!\n\r");
        }
    }
    #if 0
    if(pTxBuffer->Space <= URAT_TX_LOWER_SIZE){
	    set_tcp_block();        
    }
    #endif
    SET_PERI_REG_MASK(UART_CONF1(UART0), (UART_TX_EMPTY_THRESH_VAL & UART_TXFIFO_EMPTY_THRHD)<<UART_TXFIFO_EMPTY_THRHD_S);
    SET_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
}



//--------------------------------
LOCAL void tx_fifo_insert(struct UartBuffer* pTxBuff, uint8 data_len,  uint8 uart_no)
{
    uint8 i;
    for(i = 0; i<data_len;i++){
        WRITE_PERI_REG(UART_FIFO(uart_no) , *(pTxBuff->pOutPos++));
        if(pTxBuff->pOutPos == (pTxBuff->pUartBuff + pTxBuff->UartBuffSize)){
            pTxBuff->pOutPos = pTxBuff->pUartBuff;
        }
    }
    pTxBuff->pOutPos = (pTxBuff->pUartBuff +  (pTxBuff->pOutPos - pTxBuff->pUartBuff) % pTxBuff->UartBuffSize );
    pTxBuff->Space += data_len;
}


/******************************************************************************
 * FunctionName : tx_start_uart_buffer
 * Description  : get data from the tx buffer and fill the uart tx fifo, co-work with the uart fifo empty interrupt
 * Parameters   : uint8 uart_no - uart port num
 * Returns      : NONE
*******************************************************************************/
void tx_start_uart_buffer(uint8 uart_no)
{
    uint8 tx_fifo_len = (READ_PERI_REG(UART_STATUS(uart_no))>>UART_TXFIFO_CNT_S)&UART_TXFIFO_CNT;
    uint8 fifo_remain = UART_FIFO_LEN - tx_fifo_len ;
    uint8 len_tmp;
    uint16 tail_ptx_len,head_ptx_len,data_len;
    //struct UartBuffer* pTxBuff = *get_buff_prt();
    
    if(pTxBuffer){      
        data_len = (pTxBuffer->UartBuffSize - pTxBuffer->Space);
        if(data_len > fifo_remain){
            len_tmp = fifo_remain;
            tx_fifo_insert( pTxBuffer,len_tmp,uart_no);
            SET_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
        }else{
            len_tmp = data_len;
            tx_fifo_insert( pTxBuffer,len_tmp,uart_no);
        }
    }else{
        os_printf("pTxBuff null \n\r");
    }
}

LOCAL void
uart0_rx_intr_handler(void *para)
{
    /* uart0 and uart1 intr combine togther, when interrupt occur, see reg 0x3ff20020, bit2, bit0 represents
    * uart1 and uart0 respectively
    */
    uint8 RcvChar;
    uint8 uart_no = UART0;//UartDev.buff_uart_no;
    uint8 fifo_len = 0;
    uint8 buf_idx = 0;
    uint8 temp,cnt;
    //RcvMsgBuff *pRxBuff = (RcvMsgBuff *)para;
    
    	/*ATTENTION:*/
	/*IN NON-OS VERSION SDK, DO NOT USE "ICACHE_FLASH_ATTR" FUNCTIONS IN THE WHOLE HANDLER PROCESS*/
	/*ALL THE FUNCTIONS CALLED IN INTERRUPT HANDLER MUST BE DECLARED IN RAM */
	/*IF NOT , POST AN EVENT AND PROCESS IN SYSTEM TASK */
    if(UART_FRM_ERR_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_FRM_ERR_INT_ST)){
        os_printf("FRM_ERR\r\n");
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_FRM_ERR_INT_CLR);
    }else if(UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_FULL_INT_ST)){
        // os_printf("f");
        uart_rx_intr_disable(UART0);
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
        system_os_post(uart_recvTaskPrio, 0, 0);
    }else if(UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_TOUT_INT_ST)){
        // os_printf("t");
        uart_rx_intr_disable(UART0);
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
        system_os_post(uart_recvTaskPrio, 0, 0);
    }else if(UART_TXFIFO_EMPTY_INT_ST == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_TXFIFO_EMPTY_INT_ST)){
        // os_printf("e");
	/* to output uart data from uart buffer directly in empty interrupt handler*/
	/*instead of processing in system event, in order not to wait for current task/function to quit */
	/*ATTENTION:*/
	/*IN NON-OS VERSION SDK, DO NOT USE "ICACHE_FLASH_ATTR" FUNCTIONS IN THE WHOLE HANDLER PROCESS*/
	/*ALL THE FUNCTIONS CALLED IN INTERRUPT HANDLER MUST BE DECLARED IN RAM */
	CLEAR_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
		tx_start_uart_buffer(UART0);
        //system_os_post(uart_recvTaskPrio, 1, 0);
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_TXFIFO_EMPTY_INT_CLR);
        
    }else if(UART_RXFIFO_OVF_INT_ST  == (READ_PERI_REG(UART_INT_ST(uart_no)) & UART_RXFIFO_OVF_INT_ST)){
        WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_RXFIFO_OVF_INT_CLR);
        os_printf("RX OVF!!\r\n");
    }

}

LOCAL void ICACHE_FLASH_ATTR
uart_config(uint8 uart_no)
{
    if (uart_no == UART1){
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
    }else{
        /* rcv_buff size if 0x100 */
        ETS_UART_INTR_ATTACH(uart0_rx_intr_handler,  &(UartDev.rcv_buff));
        PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
    }
    uart_div_modify(uart_no, UART_CLK_FREQ / (UartDev.baut_rate));//SET BAUDRATE
    
    WRITE_PERI_REG(UART_CONF0(uart_no), ((UartDev.exist_parity & UART_PARITY_EN_M)  <<  UART_PARITY_EN_S) //SET BIT AND PARITY MODE
                                                                        | ((UartDev.parity & UART_PARITY_M)  <<UART_PARITY_S )
                                                                        | ((UartDev.stop_bits & UART_STOP_BIT_NUM) << UART_STOP_BIT_NUM_S)
                                                                        | ((UartDev.data_bits & UART_BIT_NUM) << UART_BIT_NUM_S));
    
    //clear rx and tx fifo,not ready
    SET_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);    //RESET FIFO
    CLEAR_PERI_REG_MASK(UART_CONF0(uart_no), UART_RXFIFO_RST | UART_TXFIFO_RST);
    
    if (uart_no == UART0){
        //set rx fifo trigger
        WRITE_PERI_REG(UART_CONF1(uart_no),
        ((100 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S) |

        (0x02 & UART_RX_TOUT_THRHD) << UART_RX_TOUT_THRHD_S |
        UART_RX_TOUT_EN|
        ((0x10 & UART_TXFIFO_EMPTY_THRHD)<<UART_TXFIFO_EMPTY_THRHD_S));//wjl 

        SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_TOUT_INT_ENA |UART_FRM_ERR_INT_ENA);
    }else{
        WRITE_PERI_REG(UART_CONF1(uart_no),((UartDev.rcv_buff.TrigLvl & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S));//TrigLvl default val == 1
    }
    //clear all interrupt
    WRITE_PERI_REG(UART_INT_CLR(uart_no), 0xffff);
    //enable rx_interrupt
    SET_PERI_REG_MASK(UART_INT_ENA(uart_no), UART_RXFIFO_FULL_INT_ENA|UART_RXFIFO_OVF_INT_ENA);
}

LOCAL void ICACHE_FLASH_ATTR ///////
uart_recvTask(os_event_t *events)
{
    if(events->sig == 0){
        Uart_rx_buff_enq();
    }else if(events->sig == 1){
	 //already move uart buffer output to uart empty interrupt
        //tx_start_uart_buffer(UART0);
    }
}

void ICACHE_FLASH_ATTR uart0_init(UartBautRate baud)
{
    system_os_task(uart_recvTask, uart_recvTaskPrio, uart_recvTaskQueue, uart_recvTaskQueueLen);  
    
    UartDev.baut_rate = baud;
    uart_config(UART0);
    ETS_UART_INTR_ENABLE();
    
    pTxBuffer = Uart_Buf_Init(UART_TX_BUFFER_SIZE);
    pRxBuffer = Uart_Buf_Init(UART_RX_BUFFER_SIZE);
}

void ICACHE_FLASH_ATTR uart1_init(UartBautRate baud)
{
    UartDev.baut_rate = baud;
    uart_config(UART1);
    ETS_UART_INTR_ENABLE();
}
/*************************************************************/


void ICACHE_FLASH_ATTR uart_trans_send(uint8_t ch)
{
	uart_tx_one_char(UART0, ch);
}

void ICACHE_FLASH_ATTR uart_trans_update(void)
{
    uint8_t len = 0;
    len = rx_buff_deq(uart_buf, 128);
	if(len > 0) {
		os_printf("uart recv(%d):", len);
		for(uint16_t i=0; i<len; i++) {
			os_printf("%02x ", uart_buf[i]);
		}
		os_printf("\n");

		net_send(uart_buf, len);
	}
}

void ICACHE_FLASH_ATTR uart_trans_init(void)
{
	uart0_init(UART_TRANS_BAUD);
	uart1_init(UART_PRINTF_BAUD);
	UART_SetPrintPort(1);
}
