#include "contiki.h"
#include "contiki-net.h"
#include "sys/ctimer.h"

#include <string.h>

#include "uart2-midi.h"

typedef char data_buffer_t 

/*
 * To be able to handle more than one connection
 * at a time, each parallell connection needs its
 * own protosocket, so TCP_thread would need to
 * be an array of type psock.
*/
static struct psock TCP_thread;
static struct pt    URX_thread;

/* The datastructure needed for this program */
struct signal {
  	struct timer	time;
  	unsigned short	size;
  	unsigned short	flag;
};

enum { // Flag values:
	SKIP = 0xff,
	SEND = 1,
	SENT = 0,
};

// Buffer Length (has to be defined)
#define BL 32
// Queue Length (set to zero to disable)
#define QL (BL/2)

/* This function is only needed for
 * PSOCK_GENERATOR_SEND() to work */
static unsigned short
URX_proc(void *x)
{
  return ((struct signal *)x)->size;
}

/* Process declarations */
PROCESS(Talker, "MIDI Talker");
AUTOSTART_PROCESSES(&Talker);
/* RX Interrupt handlers */
U2_RXI_POLL_PROCESS(&Talker);
/* TX Interrupt is disabled */
U2_TXI_CALL(U2_RX_ONLY());

/*------------------------------------------*/

volatile struct signal RX;
/* Protosockets require it to be a pointer */
static struct signal *urx = (struct signal *) &RX;

static data_buffer_t tcpbuf[BL];

/*------------------------------------------*/
/*= This thread handles the data buffering =*/
static
PT_THREAD(URX_fill(struct pt *p))
{

  PT_BEGIN(p);

  if (urx->flag != SKIP) {
    PT_WAIT_UNTIL(p, (urx->flag == SENT));
    /* It should be okay to proceed now */
  }

  if (urx->size < QL) {
    /* Bytes not sent, increment the size */
    urx->size += urx->size;
  } else {
    /* The bytes have to be skipped */
    urx->size = 0;
  }

  while(*UART2_URXCON > 0) {
    /* Copy the buffer bytes and set the size */
    bcopy(UART2_UDATA, &uip_appdata[urx->size++], 1);
  }

  if(uip_conn != NULL) {

    /* Set the flag and poll the control thread */

    urx->flag = SEND; tcpip_poll_tcp(uip_conn);

  } /* Skipping if there is no connection */

  PT_END(p);
}

/*------------------------------------------*/
/*= This thread handles the data transfers =*/
static
PT_THREAD(TCP_send(struct psock *p))
{
  PSOCK_BEGIN(p);

  while(1) {

    /* Wait for the data in the buffer */
    PSOCK_WAIT_UNTIL(p, (urx->flag == SEND));

    /* Push the buffer of the given size */
    PSOCK_GENERATOR_SEND(p, URX_proc, urx);

    /* It should had been sent */
    urx->size = SENT;
    urx->flag = SENT;

  }
  
  PSOCK_CLOSE(p);

  PSOCK_END(p);
}


/*------------------------------------------*/
/*=// This process controls both threads //=*/
PROCESS_THREAD(Talker, ev, data)
{

  /* When this process is polled,
   * there is data ready to collect
  */
  PROCESS_POLLHANDLER(URX_fill(&URX_thread));

  PROCESS_BEGIN();

  /* Set-up UART2 as a MIDI port:
   * Baud Rate: 31250;
   * Parity: Off;
   * Stop Bits: 1;
   * Start Bits: 1;
  */
  midi_uart_init();

  /* Initialise the flag */
  urx->flag = SKIP;

  /* Set-up the timer (unused) */
  timer_set(&urx->time, CLOCK_SECOND * 120);

  /* Create the protothered
   * for the interrupt handler
  */
  PT_INIT(&URX_thread);

  /* Open the TCP port 2020 */
  tcp_listen(UIP_HTONS(2020));

  while(1) {

    PROCESS_WAIT_EVENT();
    
    if(ev == tcpip_event) {
     
      if(uip_connected()) {
        
	/* Once connection has been requested,
	 * create the TCP thread and, if it is
	 * failing, re-initialise the flag */
        PSOCK_INIT(&TCP_thread, tcpbuf, sizeof(tcpbuf));
     
        while(!(uip_aborted() || uip_closed() || uip_timedout())) {
     
	  PROCESS_WAIT_EVENT();

	  /* There should be a connection set-up now. */
	  if (ev == tcpip_event) TCP_send(&TCP_thread);
	  
        }
      }
    } else { urx->flag = SKIP; timer_reset(&urx->time); }

  PROCESS_END();
}
/*------------------------------------------*/
