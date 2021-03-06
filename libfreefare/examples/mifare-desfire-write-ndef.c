/*-
 * Copyright (C) 2010, Audrey Diacre.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 * $Id$
 */

#include "config.h"

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <freefare.h>

/*
 * This example was written based on information provided by the
 * following documents:
 *
 * Mifare DESFire as Type 4 Tag
 * NFC Forum Type 4 Tag Extensions for Mifare DESFire
 * Rev. 1.1 - 21 August 2007
 *
 */


uint8_t key_data_app[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// TODO: allow NDEF payload to be provided e.g. via an external file
const uint8_t ndef_default_msg[33] = {
    0xd1, 0x02, 0x1c, 0x53, 0x70, 0x91, 0x01, 0x09,
    0x54, 0x02, 0x65, 0x6e, 0x4c, 0x69, 0x62, 0x6e,
    0x66, 0x63, 0x51, 0x01, 0x0b, 0x55, 0x03, 0x6c,
    0x69, 0x62, 0x6e, 0x66, 0x63, 0x2e, 0x6f, 0x72,
    0x67
};

uint8_t *cc_data;
uint8_t *ndef_msg;
size_t  ndef_msg_len;

struct {
    bool interactive;
} write_options = {
    .interactive = true
};

void
usage(char *progname)
{
    fprintf (stderr, "This application writes a NDEF payload into a Mifare DESFire formatted as NFC Forum Type 4 Tag.\n");
    fprintf (stderr, "usage: %s [-y] -i FILE [-k 11223344AABBCCDD]\n", progname);
    fprintf (stderr, "\nOptions:\n");
    fprintf (stderr, "  -y     Do not ask for confirmation\n");
    fprintf (stderr, "  -i     Use FILE as NDEF message to write on card (\"-\" = stdin)\n");
    fprintf (stderr, "  -k     Provide another NDEF Tag Application key than the default one\n");
}

int
main(int argc, char *argv[])
{
    int ch;
    int error = EXIT_SUCCESS;
    nfc_device *device = NULL;
    MifareTag *tags = NULL;

    char *ndef_input = NULL;
    while ((ch = getopt (argc, argv, "hyi:k:")) != -1) {
        switch (ch) {
        case 'h':
            usage(argv[0]);
            exit (EXIT_SUCCESS);
            break;
        case 'y':
            write_options.interactive = false;
            break;
        case 'i':
            ndef_input = optarg;
            break;
        case 'k':
            if (strlen(optarg) != 16) {
                usage(argv[0]);
                exit (EXIT_FAILURE);
            }
            uint64_t n = strtoull(optarg, NULL, 16);
            int i;
            for (i=7; i>=0; i--) {
                key_data_app[i] = (uint8_t) n;
                n >>= 8;
            }
            break;
        default:
            usage(argv[0]);
            exit (EXIT_FAILURE);
        }
    }
    // Remaining args, if any, are in argv[optind .. (argc-1)]

    if (ndef_input == NULL) {
        ndef_msg_len = sizeof(ndef_default_msg) + 2;
        if (!(ndef_msg = malloc (ndef_msg_len))) {
            err (EXIT_FAILURE, "malloc");
        }
        ndef_msg[0] = (uint8_t) ((ndef_msg_len - 2) >> 8);
        ndef_msg[1] = (uint8_t) (ndef_msg_len - 2);
        memcpy(ndef_msg + 2, ndef_default_msg, ndef_msg_len - 2);
    } else {
	FILE* ndef_stream = NULL;
	if ((strlen (ndef_input) == 1) && (ndef_input[0] == '-')) {
            // FIXME stdin as input have to be readed and buffered in ndef_msg
	    ndef_stream = stdin;
            fprintf (stderr, "stdin as NDEF is not implemented");
            exit (EXIT_FAILURE);
	} else {
	    ndef_stream = fopen(ndef_input, "rb");
	    if (!ndef_stream) {
		fprintf (stderr, "Could not open file %s.\n", ndef_input);
		exit (EXIT_FAILURE);
	    }
	    fseek(ndef_stream, 0L, SEEK_END);
	    ndef_msg_len = ftell(ndef_stream) + 2;
            fseek(ndef_stream, 0L, SEEK_SET);

	    if (!(ndef_msg = malloc (ndef_msg_len))) {
		err (EXIT_FAILURE, "malloc");
	    }
	    ndef_msg[0] = (uint8_t) ((ndef_msg_len - 2) >> 8);
	    ndef_msg[1] = (uint8_t) (ndef_msg_len - 2);
	    if (fread (ndef_msg + 2, 1, ndef_msg_len - 2, ndef_stream) != ndef_msg_len - 2) {
		fprintf (stderr, "Could not read NDEF from file: %s\n", ndef_input);
		fclose (ndef_stream);
		exit (EXIT_FAILURE);
	    }
	    fclose (ndef_stream);
	}
    }
    printf ("NDEF file is %zu bytes long.\n", ndef_msg_len);

    nfc_connstring devices[8];
    size_t device_count;
    
    nfc_init(NULL);

    device_count = nfc_list_devices (NULL, devices, 8);
    if (device_count <= 0)
	errx (EXIT_FAILURE, "No NFC device found.");

    for (size_t d = 0; d < device_count; d++) {
        device = nfc_open (NULL, devices[d]);
        
        if (!device) {
            warnx ("nfc_open() failed.");
            error = EXIT_FAILURE;
            continue;
        }

	tags = freefare_get_tags (device);
	if (!tags) {
	    nfc_close (device);
	    errx (EXIT_FAILURE, "Error listing tags.");
	}

	for (int i = 0; (!error) && tags[i]; i++) {
	    if (DESFIRE != freefare_get_tag_type (tags[i]))
		continue;

	    char *tag_uid = freefare_get_tag_uid (tags[i]);
	    char buffer[BUFSIZ];

	    printf ("Found %s with UID %s. ", freefare_get_tag_friendly_name (tags[i]), tag_uid);
	    bool write_ndef = true;
	    if (write_options.interactive) {
		printf ("Write NDEF [yN] ");
		fgets (buffer, BUFSIZ, stdin);
		write_ndef = ((buffer[0] == 'y') || (buffer[0] == 'Y'));
	    } else {
		printf ("\n");
	    }

	    if (write_ndef) {
		int res;

		res = mifare_desfire_connect (tags[i]);
		if (res < 0) {
		    warnx ("Can't connect to Mifare DESFire target.");
		    error = EXIT_FAILURE;
		    break;
		}

		MifareDESFireKey key_app;
		key_app  = mifare_desfire_des_key_new_with_version (key_data_app);

		// Mifare DESFire SelectApplication (Select application)
		MifareDESFireAID aid = mifare_desfire_aid_new(0xEEEE10);
		res = mifare_desfire_select_application(tags[i], aid);
		if (res < 0)
		    errx (EXIT_FAILURE, "Application selection failed. Try mifare-desfire-create-ndef before running %s.", argv[0]);
		free (aid);

		// Authentication with NDEF Tag Application master key (Authentication with key 0)
		res = mifare_desfire_authenticate (tags[i], 0, key_app);
		if (res < 0)
		    errx (EXIT_FAILURE, "Authentication with NDEF Tag Application master key failed");

		// Read Capability Container file E103
		uint8_t lendata[2];
		res = mifare_desfire_read_data (tags[i], 0x03, 0, 2, lendata);
		if (res < 0)
		    errx (EXIT_FAILURE, "Read CC len failed");
		uint16_t cclen = (((uint16_t) lendata[0]) << 8) + ((uint16_t) lendata[1]);
		if (cclen < 15)
		    errx (EXIT_FAILURE, "CC too short IMHO");
		if (!(cc_data = malloc(cclen)))
		    errx (EXIT_FAILURE, "malloc");
		res = mifare_desfire_read_data (tags[i], 0x03, 0, cclen, cc_data);
		if (res < 0)
		    errx (EXIT_FAILURE, "Read CC data failed");
		// Search NDEF File Control TLV
		uint8_t off = 7;
		while (((off+7) < cclen) && (cc_data[off] != 0x04)) {
		    // Skip TLV
		    off += cc_data[off+1] + 2;
		}
		if (off+7 >= cclen)
		    errx (EXIT_FAILURE, "CC does not contain expected NDEF File Control TLV");
		if (cc_data[off+2] != 0xE1)
		    errx (EXIT_FAILURE, "Unknown NDEF File reference in CC");
		uint8_t file_no = cc_data[off+3];
		uint16_t ndefmaxlen = (((uint16_t) cc_data[off+4]) << 8) + ((uint16_t) cc_data[off+5]);
		fprintf (stdout, "Max NDEF size: %i bytes\n", ndefmaxlen);
		if (ndef_msg_len > ndefmaxlen)
		    errx (EXIT_FAILURE, "Supplied NDEF larger than max NDEF size");

		//Mifare DESFire WriteData to write the content of the NDEF File with NLEN equal to NDEF Message length and NDEF Message
		res = mifare_desfire_write_data(tags[i], file_no, 0, ndef_msg_len, (uint8_t *) ndef_msg);
		if (res < 0)
		    errx (EXIT_FAILURE, " Write data failed");

		free(cc_data);
		mifare_desfire_key_free (key_app);

		mifare_desfire_disconnect (tags[i]);
	    }
	    free (tag_uid);
	}
	free (ndef_msg);
	freefare_free_tags (tags);
	nfc_close (device);
    }
    nfc_exit(NULL);
    exit (error);
}
