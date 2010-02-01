//
//	can.h
//
//	Driver for the internal CAN controller featured on the AT90CAN128.
//
//	Michael Jean <michael.jean@shaw.ca>
//

//
//	This particular configuration utilizes the following bit timings, assuming
//	a 16 MHz system clock:
//
//	Parameter				Value
//	======================	============
//
//	Time Quantum Length		0.625 us
//	Bit Time				16 TQ
//	Propagation Segment 	7 TQ
//	Phase Segment #1		4 TQ
//	Phase Segment #2		4 TQ
//	Synchro Jump Width		1 TQ
//
//	Effective Baud Rate		100 Kbps
//

#ifndef _CAN_H
#define _CAN_H

typedef enum baud_setting_t
{
	baud_1000,
	baud_500,
	baud_250,
	baud_200,
	baud_125,
	baud_100
}
baud_setting_t;

typedef enum mob_mode_t
{
	disabled,	/* inactive */
	receive,	/* listen for packets matching mask+id */
	transmit,	/* packets will be broadcast */
	reply		/* automatically reply to a remote request matching mask+id */
}
mob_mode_t;

typedef enum id_type_t
{
	standard,	/* 2.0A 11-bit identifier */
	extended	/* 2.0B 29-bit identifier */
}
id_type_t;

typedef enum packet_type_t
{
	payload,	/* regular data packet */
	remote		/* remote transmission request packet */
}
packet_type_t;

typedef enum can_err_t
{
	bit_err,	/* bit monitored different from sent outside arbitration block */
	stuff_err,	/* five more bits with the same polarity */
	crc_err,	/* data does not pass crc check */
	form_err,	/* violation of crc, ack, or eof fields */
	ack_err		/* no dominant bit in ack field, e.g., nobody's home */
}
can_err_t;

typedef struct mob_config_t
{
	uint32_t		id;		/* 1 */
	uint32_t		mask;	/* 1 */

	mob_mode_t		mode;
	id_type_t		id_type;

	void			(*tx_callback_ptr)	(uint8_t mob_index);
	void			(*rx_callback_ptr)	(uint8_t mob_index, uint32_t id, packet_type_t type);
	void			(*err_callback_ptr)	(uint8_t mob_index, can_err_t err_type);
}
mob_config_t;

//
//	1. 	When in `receive' mode, both `id' and `mask' are used to filter out
//		incoming packets. If bit `n' of the mask is a 1, then bit `n' of the
//		incoming packet id must match bit `n' of `id'. If bit `n' of the mask
//		is a 0, then bit `n' of the incoming packet automatically matches.
//
//		e.g., 	ID 		0x71 = 0111 0001
//				Mask 	0xf0 = 1111 0000
//
//				All packets 0x7X = 0111 XXXX will match,
//				where X is "don't care."
//

//
//	Initialize the CAN controller to a known state. The possible baud
//	rates are described by the baud_setting_t enum above.
//

void
can_init
(
	const baud_setting_t baud_rate
);

//
//	Set a callback function for the bus off failure event. If this ever
//	gets called, there is something really wrong with the system and you
//	should consider entering a fail state.
//

void
can_set_bus_off_callback
(
	void (*callback_ptr)(void)		/* callback pointer */
);

//
//	Configure the message object indexed at `mob_index'. Use parameters from
//	the configuration descriptor `config_desc'.
//

void
can_config_mob
(
	uint8_t 		mob_index,		/* message object to configure */
	mob_config_t 	*config_desc	/* configuraiton descriptor */
);

//
//	Load at most `n' data bytes in the `data' array into the data
//	buffer of a particular message object indexed at `mob_index'.
//	Return the total number of bytes written.
//
//	N.B. 	One CAN packet can hold at most 8 bytes. At most eight
//			bytes will be loaded.
//

uint8_t
can_load_data
(
	uint8_t mob_index,		/* message object to load data into */
	uint8_t *data, 			/* pointer to data */
	uint8_t n				/* number of bytes to load */
);

//
//	Read at most `n' data bytes from a particular message object indexed
//	at `mob_index' into the buffer pointed to by `data'. Return the total
//	number of bytes read.
//

uint8_t
can_read_data
(
	uint8_t mob_index,		/* message object to read data from */
	uint8_t *data, 			/* pointer to data */
	uint8_t n				/* number of bytes to load */
);

//
//	Flag a particular message object indexed at `mob_index' as ready
//	to send data. The data will be sent as soon as the bus is free and
//	all lower priority packets are sent.
//

void
can_ready_to_send
(
	uint8_t mob_index		/* message object */
);

//
//	Make a remote request with a particular message object indexed at
//	`mob_index'. Request `n' bytes of data from the remote target. The
//	request will be sent as soon as the bus is free and all lower priority
//	packets are sent.

void
can_remote_request
(
	uint8_t mob_index, 		/* message object */
	uint8_t n				/* bytes to request */
);

//
//	Flag a particular message object indexed at `mob_index' as having
//	valid reply data. This is for automatic-reply mode message objects.
//	When a remote request matching the mask+id combo for this message
//	object arrives, the data will be automatically sent.
//
//	N.B.	The local CAN controller uses the DLC field of the remote
//			request to determine how many bytes to reply with.
//

void
can_reply_valid
(
	uint8_t mob_index		/* message object */
);

#endif
