// No Copyright. Vladislav Aleinik 2020
//=================================================================================
// Protocol Values File
//=================================================================================
// - All magic numbers, constants and message headers useb by NBD are defined here
//=================================================================================
#ifndef NBD_SERVER_CONSTANTS_H_INCLUDED
#define NBD_SERVER_CONSTANTS_H_INCLUDED

#include <stdint.h>

//--------------------------
// Fixed-newstyle Handshake
//--------------------------

// Server magic:
const uint64_t NBD_MAGIC_INIT_PASSWD = 0x4e42444d41474943;
const uint64_t NBD_MAGIC_I_HAVE_OPT  = 0x49484156454F5054;

// Server handshake flags:
const uint16_t NBD_FLAG_FIXED_NEWSTYLE = 1 << 0;
const uint16_t NBD_FLAG_NO_ZEROES      = 1 << 1;

// Client handshake flags:
const uint32_t NBD_FLAG_C_FIXED_NEWSTYLE = 1 << 0;
const uint32_t NBD_FLAG_C_NO_ZEROES      = 1 << 1;

// Transmission flags:
const uint16_t NBD_FLAG_HAS_FLAGS         = 1 <<  0;
const uint16_t NBD_FLAG_READ_ONLY         = 1 <<  1;
const uint16_t NBD_FLAG_SEND_FLUSH        = 1 <<  2;
const uint16_t NBD_FLAG_SEND_FUA          = 1 <<  3;
const uint16_t NBD_FLAG_ROTATIONAL        = 1 <<  4;
const uint16_t NBD_FLAG_SEND_TRIM         = 1 <<  5;
const uint16_t NBD_FLAG_SEND_WRITE_ZEROES = 1 <<  6;
const uint16_t NBD_FLAG_SEND_DF           = 1 <<  7;
const uint16_t NBD_FLAG_CAN_MULTI_CONN    = 1 <<  8;
const uint16_t NBD_FLAG_SEND_RESIZE       = 1 <<  9;
const uint16_t NBD_FLAG_SEND_CACHE        = 1 << 10;
const uint16_t NBD_FLAG_SEND_FAST_ZERO    = 1 << 11;

// Option types:
#define NBD_OPT_EXPORT_NAME       (uint32_t) 1
#define NBD_OPT_ABORT             (uint32_t) 2
#define NBD_OPT_LIST              (uint32_t) 3
#define NBD_OPT_PEEK_EXPORT       (uint32_t) 4
#define NBD_OPT_STARTTLS          (uint32_t) 5
#define NBD_OPT_INFO              (uint32_t) 6
#define NBD_OPT_GO                (uint32_t) 7
#define NBD_OPT_STRUCTURED_REPLY  (uint32_t) 8
#define NBD_OPT_LIST_META_CONTEXT (uint32_t) 9
#define NBD_OPT_SET_META_CONTEXT  (uint32_t) 10

// Option reply magic:
const uint64_t NBD_MAGIC_OPTION_REPLY = 0x0003e889045565a9;

// Option reply types:
const uint32_t NBD_REP_ACK                 = 1;
const uint32_t NBD_REP_SERVER              = 2;
const uint32_t NBD_REP_INFO                = 3;
const uint32_t NBD_REP_ERR_UNSUP           = (1 << 31) + 1;
const uint32_t NBD_REP_ERR_POLICY          = (1 << 31) + 2;
const uint32_t NBD_REP_ERR_INVALID         = (1 << 31) + 3;
const uint32_t NBD_REP_ERR_PLATFORM        = (1 << 31) + 4;
const uint32_t NBD_REP_ERR_TLS_REQD        = (1 << 31) + 5;
const uint32_t NBD_REP_ERR_UNKNOWN         = (1 << 31) + 6;
const uint32_t NBD_REP_ERR_SHUTDOWN        = (1 << 31) + 7;
const uint32_t NBD_REP_ERR_BLOCK_SIZE_REQD = (1 << 31) + 8;
const uint32_t NBD_REP_ERR_TOO_BIG         = (1 << 31) + 9;

// Information query reply types:
#define NBD_INFO_EXPORT       (uint32_t) 0
#define NBD_INFO_NAME         (uint32_t) 1
#define NBD_INFO_DESCRIPTION  (uint32_t) 2
#define NBD_INFO_BLOCK_SIZE   (uint32_t) 3
#define NBD_INFO_META_CONTEXT (uint32_t) 4

//--------------------
// Transmission Phase
//--------------------

// Request magic:
const uint32_t NBD_MAGIC_REQUEST = 0x25609513;

// Request types:
const uint16_t NBD_CMD_READ         = 0;
const uint16_t NBD_CMD_WRITE        = 1;
const uint16_t NBD_CMD_DISC         = 2;
const uint16_t NBD_CMD_FLUSH        = 3;
const uint16_t NBD_CMD_TRIM         = 4;
const uint16_t NBD_CMD_CACHE        = 5;
const uint16_t NBD_CMD_WRITE_ZEROES = 6;
const uint16_t NBD_CMD_BLOCK_STATUS = 7;
const uint16_t NBD_CMD_RESIZE       = 8;

// Command flags:
const uint16_t NBD_CMD_FLAG_FUA       = 1 << 0;
const uint16_t NBD_CMD_FLAG_NO_HOLE   = 1 << 1;
const uint16_t NBD_CMD_FLAG_DF        = 1 << 2;
const uint16_t NBD_CMD_FLAG_REQ_ONE   = 1 << 3;
const uint16_t NBD_CMD_FLAG_FAST_ZERO = 1 << 4;

// Reply magics:
const uint32_t NBD_MAGIC_SIMPLE_REPLY     = 0x67446698;
const uint32_t NBD_MAGIC_STRUCTURED_REPLY = 0x668e33ef;

// Structired reply flags:
const uint16_t NBD_REPLY_FLAG_DONE = 1 << 0;

// Structured reply types:
const uint32_t NBD_REPLY_TYPE_NONE         = 0;
const uint32_t NBD_REPLY_TYPE_OFFSET_DATA  = 1;
const uint32_t NBD_REPLY_TYPE_OFFSET_HOLE  = 2;
const uint32_t NBD_REPLY_TYPE_BLOCK_STATUS = 5;
const uint32_t NBD_REPLY_TYPE_ERROR        = (1 << 15) + 1;
const uint32_t NBD_REPLY_TYPE_ERROR_OFFSET = (1 << 15) + 2;

// Reply error values:
const uint32_t NBD_EPERM     =   1;
const uint32_t NBD_EIO       =   5;
const uint32_t NBD_ENOMEM    =  12;
const uint32_t NBD_EINVAL    =  22;
const uint32_t NBD_ENOSPC    =  28;
const uint32_t NBD_EOVERFLOW =  75;
const uint32_t NBD_ENOTSUP   =  95;
const uint32_t NBD_ESHUTDOWN = 108;

#endif // NBD_SERVER_CONSTANTS_H_INCLUDED