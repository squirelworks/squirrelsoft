/*
 * sqfelica: FeliCa command frames and e-money history parsing (C11, no OS calls).
 *
 * This is original code, written from the public frame layouts in the FeliCa
 * Card User's Manual. The history decoding follows SquirrelSuica's own Python
 * and Kotlin parsers (felica_parse.py), and a shared fixture keeps them in step.
 *
 * Every command frame starts with its own length byte, as Android's
 * NfcF.transceive and nfcpy's clf.exchange expect. System codes are big-endian.
 * Service and area codes, block numbers and key versions are little-endian.
 *
 * Builders write into a caller buffer and return the frame length, or -1 when
 * the buffer is too small or an argument is out of range. Parsers return 0 on
 * success and -1 on a malformed or unexpected response.
 */
#ifndef SQFELICA_H
#define SQFELICA_H

#include <stddef.h>
#include <stdint.h>

#define SQF_IDM_LEN 8
#define SQF_BLOCK_LEN 16
#define SQF_MAX_READ_BLOCKS 15

enum {
    SQF_CMD_POLLING = 0x00,
    SQF_CMD_REQUEST_SERVICE = 0x02,
    SQF_CMD_REQUEST_RESPONSE = 0x04,
    SQF_CMD_READ = 0x06,
    SQF_CMD_SEARCH_SERVICE = 0x0A,
    SQF_CMD_REQUEST_SYSTEM_CODE = 0x0C
};

/* Polling request codes. */
enum { SQF_RC_NONE = 0x00, SQF_RC_SYSTEM_CODE = 0x01, SQF_RC_COMM_PERFORMANCE = 0x02 };

/* ---- command builders ---- */

int sqf_polling(uint8_t *out, size_t cap, uint16_t system_code, uint8_t request_code,
                uint8_t time_slots);
int sqf_request_system_code(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN]);
int sqf_request_response(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN]);
int sqf_search_service_code(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                            uint16_t index);
/* 1..32 node codes. */
int sqf_request_service(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                        const uint16_t *codes, size_t n);

/* Block-list element (service-list order 0, access mode 0): 0x80,b for b <= 255,
 * else 0x00,lo,hi. Returns 2 or 3, or -1 if cap is too small. */
int sqf_block_list_element(uint8_t *out, size_t cap, uint16_t block);

/* Read Without Encryption of 1..15 blocks from one service. */
int sqf_read_without_encryption(uint8_t *out, size_t cap, const uint8_t idm[SQF_IDM_LEN],
                                uint16_t service_code, const uint16_t *blocks, size_t n);

/* ---- response parsers ---- */

/* Accepts frames with or without the leading length byte. On success *data and
 * *data_len point at the bytes after the IDm, inside resp. */
int sqf_parse_response(const uint8_t *resp, size_t len, uint8_t expected_code,
                       uint8_t idm_out[SQF_IDM_LEN], const uint8_t **data, size_t *data_len);

typedef struct {
    uint8_t idm[SQF_IDM_LEN];
    uint8_t pmm[8];
    int has_request_data;
    uint16_t request_data;
} SqfPolling;
int sqf_parse_polling(const uint8_t *resp, size_t len, SqfPolling *out);

/* Writes up to cap codes; *n gets the count the card reported. */
int sqf_parse_system_codes(const uint8_t *resp, size_t len, uint16_t *codes, size_t cap,
                           size_t *n);

typedef enum { SQF_SEARCH_END, SQF_SEARCH_SERVICE, SQF_SEARCH_AREA } SqfSearchKind;
typedef struct {
    SqfSearchKind kind;
    uint16_t code;
    uint16_t end_code; /* areas only */
} SqfSearch;
int sqf_parse_search_service(const uint8_t *resp, size_t len, SqfSearch *out);

/* Key versions (0xFFFF = node missing). */
int sqf_parse_key_versions(const uint8_t *resp, size_t len, uint16_t *versions, size_t cap,
                           size_t *n);

typedef struct {
    uint8_t sf1, sf2;
    size_t n_blocks;
    uint8_t blocks[SQF_MAX_READ_BLOCKS][SQF_BLOCK_LEN];
} SqfRead;
/* A status-flag refusal (sf1/sf2 != 0) is a successful parse with n_blocks 0. */
int sqf_parse_read(const uint8_t *resp, size_t len, SqfRead *out);

/* ---- service attributes and PMm ---- */

int sqf_is_area(uint16_t code);
int sqf_is_readable_without_key(uint16_t code);
const char *sqf_service_kind(uint16_t code); /* "random-ro", "cyclic-rw", ... */

/* Max response time in ms for PMm parameter byte EEBBBAAA and n units:
 * T * ((B+1) + n*(A+1)) * 4^E, T = 256*16/13.56 MHz. */
double sqf_response_time_ms(uint8_t param, unsigned n);

/* ---- history ---- */

typedef enum {
    SQF_KIND_TRAIN,
    SQF_KIND_BUS,
    SQF_KIND_SALE,
    SQF_KIND_CHARGE,
    SQF_KIND_OTHER
} SqfKind;
const char *sqf_kind_name(SqfKind k); /* "train", "bus", ... */

typedef struct {
    SqfKind kind;
    int year, month, day;        /* year 0 = unknown date */
    int hour, minute;            /* -1 when the record has no time */
    int32_t fare_yen;            /* filled by sqf_parse_*_history from neighbours */
    int32_t balance_yen;
    int32_t amount_yen;          /* nanaco/Edy/WAON amount field, 0 for Suica rail */
    uint8_t terminal, process;   /* Suica: terminal/process; nanaco/Edy/WAON: type in both */
    uint8_t settlement;          /* Suica byte 2 (payment/settlement type); 0 otherwise */
    int has_enter, has_exit;     /* Suica rail line/station codes (never for bus) */
    uint8_t enter_line, enter_station, exit_line, exit_station;
    uint8_t region;              /* Suica byte 15 (area bits for the station table) */
    uint16_t bus_operator;       /* Suica bus: bytes 6-7 BE; 0 when not bus */
    uint16_t bus_stop;           /* Suica bus: bytes 8-9 BE; 0 when not bus */
} SqfHistory;

/* Suica family (Suica, PASMO, ICOCA, ...): one 16-byte block of service 0x090F.
 * Returns 0 and fills *out, or -1 for an empty or short block. */
int sqf_parse_suica_block(const uint8_t *block, size_t len, SqfHistory *out);
int sqf_parse_nanaco_block(const uint8_t *block, size_t len, SqfHistory *out);
int sqf_parse_edy_block(const uint8_t *block, size_t len, SqfHistory *out);
/* WAON: one 32-byte record (two 16-byte FeliCa blocks of service 0x680B). */
int sqf_parse_waon_block(const uint8_t *record32, size_t len, SqfHistory *out);

/* Parse n contiguous 16-byte blocks (n * SQF_BLOCK_LEN bytes, newest first)
 * into out (cap entries); empty blocks are skipped.
 * For Suica, fares are derived from the balance change to the next older entry.
 * For WAON, blocks are paired (0+1, 2+3, …) into 32-byte records.
 * Returns the number of entries written. */
size_t sqf_parse_suica_history(const uint8_t *blocks, size_t n,
                               SqfHistory *out, size_t cap);
size_t sqf_parse_nanaco_history(const uint8_t *blocks, size_t n,
                                SqfHistory *out, size_t cap);
size_t sqf_parse_edy_history(const uint8_t *blocks, size_t n,
                             SqfHistory *out, size_t cap);
size_t sqf_parse_waon_history(const uint8_t *blocks, size_t n,
                              SqfHistory *out, size_t cap);

/* Station-table area (0..3) for a rail code, from the region byte. */
int sqf_station_area(uint8_t region, uint8_t line, int for_exit);

/* ---- Station table (squirrelsuica/data/StationCode.csv) ---- */

typedef struct {
    uint8_t area, line, station;
    const char *company, *line_name, *station_name; /* UTF-8, inside the CSV buffer */
    const char *note; /* optional; "" when the CSV has no note column or an empty field */
} SqfStation;

/* Index a StationCode.csv held in a caller-owned writable buffer of len + 1
 * bytes (csv[len] is scratch). Field separators and line ends are overwritten
 * with NULs, so the buffer must outlive rows. Columns are found by the header
 * names area, line, station, company, line_name, station_name (any order; a
 * UTF-8 BOM is skipped). An optional note column is accepted when present.
 * Codes are hex 00..FF; rows with a bad code are skipped, and for a repeated
 * code the first row wins, as in felica_parse.py. Fields are trimmed; quoting
 * is not supported (the shipped table has none). rows ends up sorted by code.
 * Returns the number of rows, or -1 if the header lacks a required column or
 * the table has more than cap rows. */
long sqf_station_index(char *csv, size_t len, SqfStation *rows, size_t cap);

/* Exact lookup in an indexed table; NULL if absent. */
const SqfStation *sqf_station_lookup(const SqfStation *rows, size_t n, uint8_t area, uint8_t line,
                                     uint8_t station);

/* Station name for a history entry's rail code, like felica_parse.resolve_rail:
 * the area comes from the region byte (sqf_station_area), falling back to area
 * 0. NULL for code 00/00, an unknown code or an empty name. */
const char *sqf_station_resolve(const SqfStation *rows, size_t n, uint8_t region, uint8_t line,
                                uint8_t station, int for_exit);

/* Balances. Suica: block 0x090F bytes 10-11 LE, block 0x008B bytes 11-12 LE,
 * choosing the plausible one as felica_parse.suica_balance does. Pass NULL for
 * a block that was not read. Returns -1 if neither is available. */
int32_t sqf_suica_balance(const uint8_t *block_090f, size_t len_090f,
                          const uint8_t *block_008b, size_t len_008b);
int32_t sqf_nanaco_balance(const uint8_t *block, size_t len);
int32_t sqf_edy_balance(const uint8_t *block, size_t len);
int32_t sqf_waon_balance(const uint8_t *block, size_t len);

/* Japanese UTF-8 label for a Suica process code, or NULL if none is known. */
const char *sqf_suica_process_label(uint8_t process);

/* Transit-IC display name from attributes 0x008B issuer byte (byte 8) and an
 * optional history settlement byte (pass 0xFF when unknown). Matches
 * felica_parse.suica_family_label: ICOCA, IruCa, PASMO, モバイルSuica, PiTaPa,
 * Suica, or "Suica / PASMO". */
const char *sqf_suica_family_label(const uint8_t *block_008b, size_t len, int settlement);

#endif
