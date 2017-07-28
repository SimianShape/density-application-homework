#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "xmodem.h"
#include "xmodem_receiver.h"

static bool (*callback_is_inbound_empty)();
static bool (*callback_is_outbound_full)();
static bool (*callback_read_data)(const uint32_t requested_size, uint8_t *buffer, uint32_t *returned_size);
static bool (*callback_write_data)(const uint32_t requested_size, uint8_t *buffer, bool *write_status);

static xmodem_receive_state_t receive_state;

static const uint32_t  READ_BLOCK_TIMEOUT      = 60000; // 60 seconds
static const uint32_t  C_ACK_TIMEOUT           = 3000; // 3 seconds
static const uint32_t  SEND_C_MAX_RETRIES      = 5;
static uint8_t         control_character       = 0;
static uint32_t        returned_size           = 0;
static uint8_t         inbound                 = 0;
static uint8_t         *payload_buffer         = 0;
static uint32_t        payload_buffer_position = 0;
static uint32_t        payload_size            = 0;
static uint8_t         current_packet_id       = 0;
static xmodem_packet_t current_packet;


xmodem_receive_state_t xmodem_receive_state()
{
   return receive_state;
}


bool xmodem_receive_init()
{
  
   bool result          = false; 
   receive_state        = XMODEM_RECEIVE_UNKNOWN;

   if (0 != callback_is_inbound_empty &&
       0 != callback_is_outbound_full  &&
       0 != callback_read_data &&
       0 != callback_write_data)
   {
      receive_state   = XMODEM_RECEIVE_INITIAL;
      result = true;
   }

   return result;
}

bool xmodem_receive_cleanup()
{
   callback_is_inbound_empty = 0;
   callback_is_outbound_full = 0;
   callback_read_data        = 0;
   callback_write_data       = 0;
   receive_state             = XMODEM_RECEIVE_UNKNOWN;
   payload_buffer_position   = 0;
   payload_buffer            = 0;
   inbound                   = 0;
   returned_size             = 0;
   control_character         = 0;

   return true;
}


bool xmodem_receive_process(const uint32_t current_time)
{
   static uint32_t stopwatch = 0;
   static uint32_t ack_retry_count = 0;

   switch(receive_state)
   {

      case XMODEM_RECEIVE_INITIAL:
      {
         receive_state = XMODEM_RECEIVE_SEND_C;
         ack_retry_count = 0;
         break;
      }

      case XMODEM_RECEIVE_SEND_C:
      {
         receive_state = XMODEM_RECEIVE_WAIT_FOR_ACK;
         stopwatch = current_time;
         break;
      }

      case XMODEM_RECEIVE_WAIT_FOR_ACK:
      {
         // Stay in this state and check for a valid XmodemCRC start byte
         // until we time out or receive a invalid start byte
         if (current_time > (stopwatch + C_ACK_TIMEOUT))
         {
            receive_state = XMODEM_RECEIVE_TIMEOUT_ACK;
         }
         else
         {
            uint8_t   inbound       = 0;
            uint32_t  returned_size = 0;

            if (!callback_is_inbound_empty())
            {
               callback_read_data(1, &inbound, &returned_size);

               if (returned_size > 0)
               {
                  // SOH, EOT, CAN, and ETB are the only valid XmodemCRC start bytes
                  if (SOH == inbound)
                  {
                     receive_state = XMODEM_RECEIVE_ACK_SUCCESS;
                  }
                  else if (EOT == inbound)
                  {
                     receive_state = XMODEM_RECEIVE_TRANSFER_COMPLETE;
                  }
                  else if (CAN == inbound)
                  {
                     receive_state = XMODEM_RECEIVE_SEND_C;
                  }
                  else if (ETB == inbound)
                  {
                     receive_state = XMODEM_RECEIVE_ABORT_TRANSFER;
                  }
                  else
                  {
                     // Don't really like this solution, but the state machine
                     // implies this is where we would go. Mostly just don't
                     // what this error to slip through
                     receive_state = XMODEM_RECEIVE_ABORT_TRANSFER;
                  }
               }
            }
         }
         break;
      }

      case XMODEM_RECEIVE_TIMEOUT_ACK:
      { 
         ++ack_retry_count;

         // Retry send C unless max retries then abort the the transfer
         if(ack_retry_count < SEND_C_MAX_RETRIES)
         {
            receive_state = XMODEM_RECEIVE_SEND_C;
         }
         else
         {
            receive_state = XMODEM_RECEIVE_ABORT_TRANSFER;
         }
         break;
      }

      case XMODEM_RECEIVE_ABORT_TRANSFER:
      {
         //TODO: implement final state
         break;
      }

      case XMODEM_RECEIVE_UNKNOWN:
      {
          receive_state = XMODEM_RECEIVE_ABORT_TRANSFER;
          break;
      }

      case XMODEM_RECEIVE_READ_BLOCK:
      {

          if (current_time > (stopwatch + READ_BLOCK_TIMEOUT))
          {
             receive_state = XMODEM_RECEIVE_READ_BLOCK_TIMEOUT;
          }
          else
          {
             uint8_t   inbound       = 0;
             uint32_t  returned_size = 0;
 
             if (!callback_is_inbound_empty())
             {
                callback_read_data(1, &inbound, &returned_size);

                if (returned_size > 0)
                {
                   if (ACK == inbound)
                   {
                       receive_state = XMODEM_RECEIVE_ACK_SUCCESS;
                   }
                   else if (EOT == inbound)
                   {
                       receive_state = XMODEM_RECEIVE_TRANSFER_COMPLETE;
                   }
                } 
             } 

          }
          break;
      }

      case XMODEM_RECEIVE_READ_BLOCK_TIMEOUT:
      {
          receive_state = XMODEM_RECEIVE_ABORT_TRANSFER;
          stopwatch = current_time;
          break;
      }

      case XMODEM_RECEIVE_READ_BLOCK_SUCCESS:
      {
          break;
      }

      case XMODEM_RECEIVE_BLOCK_INVALID:
      {
          break;
      }

      case XMODEM_RECEIVE_BLOCK_VALID:
      {
          receive_state = XMODEM_RECEIVE_BLOCK_ACK;
          break;
      }

      case XMODEM_RECEIVE_BLOCK_ACK:
      {
          //TODO: send ACK
          stopwatch = current_time;  // start the stopwatch to watch for a TRANSFER_ACK TIMEOUT
          receive_state = XMODEM_RECEIVE_WAIT_FOR_ACK;
          break;
      }

      default:
      {
          receive_state = XMODEM_RECEIVE_UNKNOWN; 
      }



   };

   return true;
    
}



void xmodem_receive_set_callback_write(bool (*callback)(const uint32_t requested_size, uint8_t *buffer, bool *write_status))
{
   callback_write_data = callback;
}

void xmodem_receive_set_callback_read(bool (*callback)(const uint32_t requested_size, uint8_t *buffer, uint32_t *returned_size))
{
   callback_read_data = callback;
}

void xmodem_receive_set_callback_is_outbound_full(bool (*callback)())
{
   callback_is_outbound_full = callback;
}

void xmodem_receive_set_callback_is_inbound_empty(bool (*callback)())
{
   callback_is_inbound_empty = callback;
}








