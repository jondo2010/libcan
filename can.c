//
//	can.c
//
//	Michael Jean <michael.jean@shaw.ca>
//

#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>

#include "can.h"

static void				(*bus_off_callback_ptr) (void);
static mob_config_t 	mob_configs[15];

static const uint8_t baud_rate_settings[6][3] =
{
	{0x02, 0x04, 0x13},    /* 1000Kbps  */
	{0x02, 0x0c, 0x37},    /* 500Kbps  */
	{0x06, 0x0c, 0x37},    /* 250Kbps  */
	{0x08, 0x0c, 0x37},    /* 200Kbps  */
	{0x0e, 0x0c, 0x37},    /* 125Kbps  */
	{0x12, 0x0c, 0x37}     /* 100Kbps  */
};

ISR (CANIT_vect)
{
	uint8_t			old_page;
	uint8_t 		mob_index = 0;

	uint32_t		id = 0;
	packet_type_t	packet_type;

	int				bus_off_int;

	bus_off_int = (CANHPMOB & 0xF0) == 0xF0;

	if (bus_off_int)
	{
		CANGIT = 0x40;	/* 1 */

		if (bus_off_callback_ptr)
			bus_off_callback_ptr ();
	}
	else
	{
		old_page = CANPAGE;
		mob_index = CANHPMOB >> 4;

		CANPAGE = CANHPMOB;

		if (CANSTMOB & 0x20) 		/* receive */
		{
			CANSTMOB = 0x00;

			if (mob_configs[mob_index].rx_callback_ptr)
			{
				id = (mob_configs[mob_index].id_type == standard) ?
					((uint32_t)CANIDT2 >> 5) | ((uint32_t)CANIDT1 << 3) :
					((uint32_t)CANIDT4 >> 3)  | ((uint32_t)CANIDT3 << 5) |
					((uint32_t)CANIDT2 << 13) | ((uint32_t)CANIDT1 << 21);

				packet_type = (CANIDT4 & 0x40) ? remote : payload;

				mob_configs[mob_index].rx_callback_ptr
					(mob_index, id, packet_type);
			}
		}
		else if (CANSTMOB & 0x40) 	/* transmit */
		{
			CANSTMOB = 0x00;

			if (mob_configs[mob_index].tx_callback_ptr)
				mob_configs[mob_index].tx_callback_ptr (mob_index);
		}

		CANPAGE = old_page;
	}
}

//
//	1.	Writing `1' to a bit in the CANGIT register resets the interrupt.
//

void
can_init
(
	const baud_setting_t baud_rate
)
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

	CANGIT = 0x7F;
	CANGIE = 0xB8;	/* 2 */

	CANIE1 = 0x7F;
	CANIE2 = 0xFF;

	CANBT1 = 0x12;
	CANBT2 = 0x0C;
	CANBT3 = 0x37;

	CANBT1 = baud_rate_settings[baud_rate][0];
	CANBT2 = baud_rate_settings[baud_rate][1];
	CANBT3 = baud_rate_settings[baud_rate][3];

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

void
can_set_bus_off_callback (void (*callback_ptr)(void))
{
	bus_off_callback_ptr = callback_ptr;
}

void
can_config_mob (uint8_t mob_index, mob_config_t *config_desc)
{
	uint8_t old_page;

	old_page = CANPAGE;

	mob_configs[mob_index] = *config_desc;

	CANPAGE = (mob_index << 4);
	CANSTMOB = 0x00;
	CANCDMOB = 0x00;

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

	old_page = CANPAGE;
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
	int 		i;

	uint8_t 	old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);

	n = (n <= 8 ? n : 8);	/* 1 */
	for (i = 0; i < n; i++)
		CANMSG = data[i];

	CANCDMOB = n;
	CANPAGE = old_page;

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
	uint8_t		old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);

	dlc = CANCDMOB & 0x0F;
	n = (dlc >= n) ? n : dlc;

	for (i = 0; i < n; i++)
		data[i] = CANMSG;

	CANPAGE = old_page;

	return n;
}

void
can_ready_to_send (uint8_t mob_index)
{
	uint8_t old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);
	CANIDT4 &= ~0x04;	/* 1 */
	CANCDMOB &= ~0xF0;	/* 2 */
	CANCDMOB |= (mob_configs[mob_index].id_type == extended) ? 0x50 : 0x40;
	CANPAGE = old_page;
}

//
//	1.	The RTR bit could have been set by a previous remote request, so we must
//		explicitly clear it.
//
//	2.	This preserves the DLC portion of the control register, which was previously
//		set by a load data operation.
//

void
can_ready_to_receive (uint8_t mob_index)
{
	uint8_t old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0x90 : 0x80;
	CANPAGE = old_page;
}

void
can_remote_request (uint8_t mob_index, uint8_t n)
{
	uint8_t old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);
	CANIDT4 |= 0x04;
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0x50 | n : 0x40 | n;
	CANPAGE = old_page;
}

void
can_reply_valid (uint8_t mob_index)
{
	uint8_t old_page;

	old_page = CANPAGE;

	CANPAGE = (mob_index << 4);
	CANCDMOB = (mob_configs[mob_index].id_type == extended) ? 0xB0 : 0xA0; /* 1 */
	CANPAGE = old_page;
}

//
//	1.	Note the data length portion is ignored. The remote packet will ask us
//		for how many bytes to reply with, and we will provide that many bytes.
//		This seems stupid but that's how the controller works.
//
