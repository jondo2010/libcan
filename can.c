//
//	can.c
//
//	Michael Jean <michael.jean@shaw.ca>
//

#include <avr/io.h>
#include <avr/interrupt.h>

#include "can.h"

static void				(*bus_off_callback_ptr) (void);
static mob_config_t 	mob_configs[15];

ISR (CANIT_vect)
{
	uint8_t 		old_page, int_page;
	uint8_t			mob_index;

	uint32_t		id = 0;

	uint8_t			status;

	packet_type_t	packet_type = 0;
	can_err_t		err_type = 0;

	mob_config_t	mob_config;

	old_page = CANPAGE;		/* 1 */
	int_page = CANHPMOB;

	if (int_page == 0xF0)	/* 2 */
	{
		if ((CANGIT & 0x40) && bus_off_callback_ptr)
			bus_off_callback_ptr ();

		CANGIT = 0x40;		/* 3 */
	}
	else
	{
		status = CANSTMOB;	/* 4 */

		mob_index = int_page >> 4;
		mob_config = mob_configs[mob_index];

		if (status & 0x40)
		{
			if (mob_config.tx_callback_ptr)
				 mob_config.tx_callback_ptr (mob_index);

			can_config_mob (mob_index, &mob_config); /* 5 */
		}
		else if (status & 0x20)
		{
			if (mob_config.rx_callback_ptr)
			{
				id = (mob_config.id_type == standard) ?
					((uint32_t)CANIDT2 >> 5) | ((uint32_t)CANIDT1 << 3) :
					((uint32_t)CANIDT4 >> 3)  | ((uint32_t)CANIDT3 << 5) |
					((uint32_t)CANIDT2 << 13) | ((uint32_t)CANIDT1 << 21);

				packet_type = (CANIDT4 & 0x40) ? remote : payload;

				mob_config.rx_callback_ptr (mob_index, id, packet_type);
			}

			can_config_mob (mob_index, &mob_config); /* 5 */
		}
		else if ((status & 0x1F))
		{
			if 		(status & 0x10)		err_type = bit_err;
			else if	(status & 0x08)		err_type = stuff_err;
			else if	(status & 0x04)		err_type = crc_err;
			else if	(status & 0x02)		err_type = form_err;
			else if	(status & 0x01)		err_type = ack_err;

			if (mob_config.err_callback_ptr)
				mob_config.err_callback_ptr (mob_index, err_type);

			CANSTMOB = CANSTMOB & ~0x1F; /* 6 */
		}

		CANPAGE = old_page;
	}
}

//
//	1.	It's possible we are interrupting while using the CANPAGE register,
//		e.g., configuring a message object or reading data. We will set
//		the CANPAGE register back when we finish the ISR.
//
//	2. 	CANPHMOB == 0xF0 when a general (non-message object) interrupt occurs.
//
//	3.	General interrupts are cleared by writing 1 to the particular interrupt
//		bit. Message object interrupts require a read-modify-write to clear the
//		particular interrupt.
//
//	4.	It's possible that the status register might change while we are in
//		the ISR, e.g., if we are handling a bit error and the retry goes
//		through and sets the transmission ok flag. So, we have to be careful
//		with the status register.
//
//	5.	We have to `re-arm' the interrupt by re-configuring the message
//		object. It seems like a waste of time but it's the only reliable
//		way to make the controller work.
//
//	6.	Unlike in (5), we don't `re-arm' the interrupt because that would
//		ruin any chance of automatically retrying. The controller will
//		keep trying until the message goes through or the other devices on
//		the bus tell it to shut up.
//

void
can_init (void)
{
	int i;

	CANGCON = 0x01;

	for (i = 0; i < 15; i++) /* 1 */
	{
		CANPAGE = (i << 4);

		CANSTMOB = 0x00;
		CANCDMOB = 0x00;

		CANIDT1 = 0x00;
		CANIDT2 = 0x00;
		CANIDT3 = 0x00;
		CANIDT4 = 0x00;

		CANIDM1 = 0x00;
		CANIDM2 = 0x00;
		CANIDM3 = 0x00;
		CANIDM4 = 0x00;
	}

	CANGIE = 0xF8;	/* 2 */
	CANIE1 = 0x7F;
	CANIE2 = 0xFF;

	CANBT1 = 0x12;	/* 3 */
	CANBT2 = 0x0C;
	CANBT3 = 0x37;

	CANGCON = 0x02;
}

//
//	1.	For some reason, all of the message object registers have unknown
//		values after reset. They must be manually zeroed before going online,
//		or spurious interrupt states may fire.
//
//	2.	We listen for bus-off, reception ok, transmission ok.  We ignore
//		single-packet errors.
//
//	3.	Check the header file for more information on the bit timings chosen.
//

void
can_set_bus_off_callback (void (*callback_ptr)(void))
{
	bus_off_callback_ptr = callback_ptr;
}

void
can_config_mob (uint8_t mob_index, mob_config_t *config_desc)
{
	mob_configs[mob_index] = *config_desc;

	CANPAGE = (mob_index << 4);

	switch (config_desc->id_type)
	{
		case standard:

			CANIDT1 = (config_desc->id >> 3) & 0xFF;
			CANIDT2 = (config_desc->id << 5) & 0xE0;
			CANIDT3 = 0x00;
			CANIDT4 = 0x00;	/* 1 */

			CANIDM1 = (config_desc->mask >> 3) & 0xFF;
			CANIDM2 = (config_desc->mask << 5) & 0xE0;
			CANIDM3 = 0x00;
			CANIDM4 = 0x00; /* 1 */

			break;

		case extended:

			CANIDT1 = (config_desc->id >> 21) & 0xFF;
			CANIDT2 = (config_desc->id >> 13) & 0xFF;
			CANIDT3 = (config_desc->id >> 5) & 0xFF;
			CANIDT4 = (config_desc->id << 3) & 0xF8;

			CANIDM1 = (config_desc->mask >> 21) & 0xFF;
			CANIDM2 = (config_desc->mask >> 13) & 0xFF;
			CANIDM3 = (config_desc->mask >> 5) & 0xFF;
			CANIDM4 = (config_desc->mask << 3) & 0xF8;

			break;
	}

	CANSTMOB = 0x00;

	switch (config_desc->mode)
	{
		case receive: 	/* 2 */

			CANCDMOB = (config_desc->id_type == extended) ? 0x90 : 0x80;
			break;

		case disabled:
		case transmit: 	/* 3 */
		case reply:		/* 4 */

			CANCDMOB = 0x00;
			break;
	}
}

//
//	1.	The `remote' flag is not set until transmission is requested. The
//		ability to filter received packets based on remote/payload is not
//		implemented.
//
//	2.	As soon as the message object is configured to receive, it is
//		armed and waiting for a packet.
//
//	3.	Unlike (2), transmit mode requires the user to call the
//		`can_ready_to_send' or `can_remote_request' to arm the object.
//
//	4.	Unlike (2) but like (3), reply mode requires the user to call the
//		`can_reply_valid' to arm the object.
//

uint8_t
can_load_data (uint8_t mob_index, uint8_t *data, uint8_t n)
{
	int i;

	CANPAGE = (mob_index << 4);

	n = (n <= 8 ? n : 8);	/* 1 */
	for (i = 0; i < n; i++)
		CANMSG = data[i];

	CANCDMOB = n;
	return n;
}

//
//	1.	CAN packets can hold at most eight bytes.
//

uint8_t
can_read_data (uint8_t mob_index, uint8_t *data, uint8_t n)
{
	int 		i;
	uint8_t 	dlc;

	CANPAGE = (mob_index << 4);

	dlc = CANCDMOB & 0x0F;
	n = (dlc >= n) ? n : dlc;

	for (i = 0; i < n; i++)
		data[i] = CANMSG;

	return n;
}

void
can_ready_to_send (uint8_t mob_index)
{
	uint8_t dlc;

	dlc = CANCDMOB & 0x0F;

	CANPAGE = (mob_index << 4);
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0x50 | dlc : 0x40 | dlc;
}

void
can_remote_request (uint8_t mob_index, uint8_t n)
{
	CANPAGE = (mob_index << 4);
	CANIDT4 |= 0x04;
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0x50 | n : 0x40 | n;
}

void
can_reply_valid (uint8_t mob_index)
{
	CANPAGE = (mob_index << 4);
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0xB0 : 0xA0; /* 1 */
}

//
//	1.	Note the data length portion is ignored. The remote packet will ask us
//		for how many bytes to reply with, and we will provide that many bytes.
//		This seems stupid but that's how the controller works.
//