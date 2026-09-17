/*! \file rpc_dispatch.h
 *
 * \brief Opcode dispatch loop: reads AT_MSG_CALL frames off serial_link.c, validates body
 * length against at_protocol.h's opcode table, calls the matching at_sensor_config.c handler,
 * and replies with AT_MSG_RESPONSE.
 */

#ifndef RPC_DISPATCH_H_
#define RPC_DISPATCH_H_

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief xTaskCreate entry point. Never returns. */
void at_rpc_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* RPC_DISPATCH_H_ */
