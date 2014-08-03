/*
  File edited by Maxime Marches and Thomas Brunner for the requirements of
  our MSc Project at the University of Kent, Canterbury, UK

  License: distributed under GPL version 3 (http://www.gnu.org/licenses/gpl.html)

  Requirements:
  libnfc (>= 1.7.1)

*/

extern "C" {

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>

#include <nfc/nfc.h>

int    pn53x_transceive(struct nfc_device *pnd, const uint8_t *pbtTx, const size_t szTx, uint8_t *pbtRx, const size_t szRxLen, int timeout);
}

#include <iostream>

#include "cchack.hh"
#include "application.hh"

static const byte_t SELECT_APP[] = {0x40,0x01,0x00,0xA4,0x04,0x00,0x07,0xA0,0x00,0x00,0x00,0x42,0x10,0x10,0x00};
static const byte_t READ_RECORD_VISA[] = {0x40, 0x01, 0x00, 0xB2, 0x02, 0x0C, 0x00, 0x00};
static const byte_t READ_RECORD_MC[] = {0x40, 0x01, 0x00, 0xB2, 0x01, 0x14, 0x00, 0x00};
static byte_t READ_PAYLOG_VISA[] = {0x40, 0x01, 0x00, 0xB2, 0x01, 0x8C, 0x00, 0x00};
static byte_t READ_PAYLOG_MC[] = {0x40, 0x01, 0x00, 0xB2, 0x01, 0x5C, 0x00, 0x00};
static byte_t READ_PAYLOG_LEN = 8;

static void sig_handler(int signum) {
  if (signum == SIGINT) {

    /* if (pnd) */
    /*   nfc_disconnect(pnd); */

    exit(EXIT_SUCCESS);
  }
}

void show(const size_t len, const byte_t *recv)
{

  printf("HEXA <<  ");
  for (size_t i = 0; i < len; i++) {
    printf("%02X", (unsigned int) recv[i]);
  }
  printf("\nASCII <<  ");
  for (size_t i = 0; i < len; i++) {
    printf("%c ", (char) (isprint(recv[i]) ? recv[i] : '.'));
  }
  printf("\n");
}

static void	init() {

  nfc_context *context;

  nfc_init(&context);
  if (context == NULL) {
    printf("Unable to init libnfc (malloc)");
    exit(EXIT_FAILURE);
  }

  pnd = nfc_open(context, NULL);

  if (pnd == NULL) {
    printf("%s", "Unable to open NFC device.");
    nfc_exit(context);
    exit(EXIT_FAILURE);
  }
}

static int	start_and_select_app() {
  byte_t abtRx[MAX_FRAME_LEN];
  int szRx;

  if ((szRx = pn53x_transceive(pnd,
			       Command::START_14443A, sizeof(Command::START_14443A),
			       abtRx, sizeof(abtRx),
			       0)) < 0) {
    nfc_perror(pnd, "START_14443A");
    return 1;
  }
  puts("Answer from START_14443A");
  show(szRx, abtRx);

  AppList list = ApplicationHelper::getAll();

  std::cout << list.size() << " apps found" << std::endl;

  for (Application a : list) {
    show(7, a.aid);
  }

  return 0;
}

static void	look_for_cardholder(const byte_t* res, const size_t size) {

  static byte_t buff[MAX_FRAME_LEN];

  for (size_t i = 0; i < size - 2; i++) {
    if (res[0] == 0x5f && res[1] == 0x20) {
      memcpy(buff, res+3, res[2]);
      buff[res[2]] = 0;
      printf("Cardholder name: %s\n", buff);
      break;
    }
    res++;
  }  
}

static void	look_for_pan_and_expire_date(const byte_t* res, const size_t size, const byte_t flag_start) {

  static byte_t buff[MAX_FRAME_LEN];

  for (size_t i = 0; i < size-1; i++) {
    if (res[0] == flag_start && res[1] == 0x57) {
      memcpy(buff, res+3, 13);
      buff[11] = 0;
      printf("PAN:");

      for (size_t j = 0; j < 8; j++) {
	if (j % 2 == 0)
	  printf(" ");
	if (j >= 2 && j<= 5)
	  printf("**");
	else
	  printf("%02x", buff[j]);
      }
      printf("\n");

      unsigned int expiry = (buff[10] | (buff[9] << 8) | (buff[8] << 16)) >> 4;
      printf("Expiration date: %02x/20%02x\n\n", (expiry & 0xff), ((expiry >> 8) & 0xff));
      break;
    }
    res++;
  }
}

static int	read_paylog(byte_t read_paylog[MAX_FRAME_LEN]) {

  static byte_t abtRx[MAX_FRAME_LEN];
  char amount[10], msg[100];
  int szRx;

  for (byte_t i = 1; i <= 20; i++) {
    read_paylog[4] = i;
    if ((szRx = pn53x_transceive(pnd,
				 read_paylog, READ_PAYLOG_LEN,
				 abtRx, sizeof(abtRx),
				 0)) < 0) {
      nfc_perror(pnd, "READ_RECORD");
      return 1;
    }
    if (szRx == 18) { // Non-empty transaction
      //show(szRx, abtRx);

      /* Look for date */
      sprintf(msg, "%02x/%02x/20%02x", abtRx[14], abtRx[13], abtRx[12]);

      /* Look for transaction type */
      if (abtRx[15] == 0)
	sprintf(msg, "%s %s", msg, "Payment");
      else if (abtRx[15] == 1)
	sprintf(msg, "%s %s", msg, "Withdrawal");

      /* Look for amount*/
      sprintf(amount, "%02x%02x%02x", abtRx[3], abtRx[4], abtRx[5]);
      sprintf(msg, "%s\t%d,%02x€", msg, atoi(amount), abtRx[6]);

      printf("%s\n", msg);
    }
  }

  return 0;
}

static int	read_paylogs() {
    // Read payloads for Visa and MC
    if (read_paylog(READ_PAYLOG_VISA))
      return 1;
    if (read_paylog(READ_PAYLOG_MC))
      return 1;
    return 0;
}

int	main(__attribute__((unused)) int argc,
	     __attribute__((unused)) char **argv) {

  byte_t abtRx[MAX_FRAME_LEN];
  int szRx;


  init();

  signal(SIGINT, sig_handler);
  while (1) {

    if (start_and_select_app())
      goto endloop;

    return 0;

    if ((szRx = pn53x_transceive(pnd,
				 READ_RECORD_VISA, sizeof(READ_RECORD_VISA),
				 abtRx, sizeof(abtRx),
				 0)) < 0) {
      nfc_perror(pnd, "READ_RECORD");
      goto endloop;
    }
   show(szRx, abtRx);

    /* Look for cardholder name */
    look_for_cardholder(abtRx, szRx);
    /* Look for PAN & Expiry date */
    look_for_pan_and_expire_date(abtRx, szRx, 0x4d);


    if ((szRx = pn53x_transceive(pnd,
				 READ_RECORD_MC, sizeof(READ_RECORD_MC),
				 abtRx, sizeof(abtRx),
				 0)) < 0) {
      nfc_perror(pnd, "READ_RECORD");
      goto endloop;
    }
    //show(szRx, abtRx);

    /* Look for cardholder name */
    look_for_cardholder(abtRx, szRx);
    /* Look for PAN & Expiry date */
    look_for_pan_and_expire_date(abtRx, szRx, 0x9c);

    // Read payloads for Visa and MC
    if (read_paylogs()) {
    } // goto endloop;

  endloop:
    printf("-------------------------\n");
  }

  return 0;
}

