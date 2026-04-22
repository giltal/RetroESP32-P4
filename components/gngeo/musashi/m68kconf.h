#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

/* Neo Geo configuration for Musashi 68K core */

#define OPT_OFF             0
#define OPT_ON              1
#define OPT_SPECIFY_HANDLER 2

/* CPU instance pointer and cycle multiplier */
#define m68ki_cpu m68k
#define MUL (7)

/* Pre-decrement write order (not needed for Neo Geo) */
#define M68K_SIMULATE_PD_WRITES     OPT_OFF

/* Interrupt acknowledge — use callback so we can clear IRQ properly */
#define M68K_EMULATE_INT_ACK        OPT_ON

/* No RESET instruction callback needed */
#define M68K_EMULATE_RESET          OPT_OFF
#define M68K_RESET_CALLBACK()

/* No TAS callback needed */
#define M68K_TAS_HAS_CALLBACK       OPT_OFF
#define M68K_TAS_CALLBACK()         0

/* No function code emulation needed */
#define M68K_EMULATE_FC             OPT_OFF
#define M68K_SET_FC_CALLBACK(A)

/* No trace emulation needed (speed) */
#define M68K_EMULATE_TRACE          OPT_OFF

/* No prefetch emulation (speed) */
#define M68K_EMULATE_PREFETCH       OPT_OFF

/* No address error emulation (speed) */
#define M68K_EMULATE_ADDRESS_ERROR  OPT_OFF
#define M68K_CHECK_PC_ADDRESS_ERROR OPT_OFF

/* No 64-bit ops needed */
#define M68K_USE_64_BIT  OPT_OFF

#endif /* M68KCONF__HEADER */
