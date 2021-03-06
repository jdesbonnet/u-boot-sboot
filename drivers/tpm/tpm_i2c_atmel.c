/*
 * ATMEL I2C TPM AT97SC3204T
 *
 * Copyright (C) 2012 V Lab Technologies
 *
 * Authors:
 * Teddy Reed <teddy@prosauce.org>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This device driver implements the TPM interface as defined in
 * the TCG TPM Interface Spec version 1.2.
 *
 * It is based on the AVR code in ATMEL's AT90USB128 TPM Development Board,
 * the Linux Infineon TIS 12C TPM driver from Peter Huewe, the original tpm_tis
 * device driver from Leendert van Dorn and Kyleen Hall, and code provided from
 * ATMEL's Application Group, Crypto Products Division and Max R. May.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <common.h>
#include <i2c.h>
#include <linux/types.h>
#include <tpm.h>
#include <asm-generic/errno.h>
#include <linux/types.h>
#include <linux/unaligned/be_byteshift.h>

#include "tpm_private.h"

/** Found in AVR code and in Max's implementation **/
#ifndef TPM_BUFSIZE
#define TPM_BUFSIZE 1024
#endif

/* Atmel-defined I2C bus ID */
/* Note: this is defined in board configuration */
#ifndef CONFIG_TPM_TIS_I2C_SLAVE_ADDRESS
#define CONFIG_TPM_TIS_I2C_SLAVE_ADDRESS 0x29
#endif

#define TPM_HEADER_SIZE 10
#define TPM_TIMEOUT     5000

struct tpm_i2c_atmel_dev {
  uint addr;
  u8 buf[TPM_BUFSIZE];
  /* chip struct is in tpm.c */
  //struct tpm_chip *chip;
};

static struct tpm_i2c_atmel_dev tpm_dev = {
    /* Note: replace with defined addr from board configuration */
    .addr = CONFIG_TPM_TIS_I2C_SLAVE_ADDRESS
};

static u8 tpm_i2c_read(u8 *buffer, size_t len);

static void tpm_tis_i2c_ready (struct tpm_chip *chip);
static u8 tpm_tis_i2c_status (struct tpm_chip *chip);
static int tpm_tis_i2c_recv (struct tpm_chip *chip, u8 *buf, size_t count);
static int tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count);


static u8 tpm_i2c_read(u8 *buffer, size_t len)
{
  int rc;
//  u32 trapdoor = 0;
//  const u32 trapdoor_limit = 60000; /* not 5min with base 5mil seconds */
  unsigned long start;

  start = get_timer(0);
  do {
    /* Atmel TPM requires RAW reads */
    rc = i2c_read(tpm_dev.addr, 0, 0, buffer, len);
    if (rc == 0x00) /* successful read */
      break;
    udelay(60);
  } while (get_timer(start) < TPM_TIMEOUT); /*trapdoor_limit*/

  /** failed to read **/
  if (rc != 0x00)
    return -EFAULT;

  return rc;
}

static int tpm_tis_i2c_recv (struct tpm_chip *chip, u8 *buf, size_t count)
{
  int rc = 0;
  size_t expected;

  memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
  rc = tpm_i2c_read(tpm_dev.buf, TPM_HEADER_SIZE); /* returns status of read */
  if (rc != 0x00)
    return rc;

  //expected = tpm_dev.buf[4];
  //expected = expected << 8;
  //expected += tpm_dev.buf[5]; /* should never be > TPM_BUFSIZE */
  expected = get_unaligned_be32(tpm_dev.buf + 2);

  /* this should not happen, but just in case. */
  if (expected > TPM_BUFSIZE)
    return -EIO;

  if (expected > TPM_HEADER_SIZE) {
    rc = tpm_i2c_read(tpm_dev.buf, expected);
    if (rc != 0x00)
      return rc;
  }

  memcpy(buf, tpm_dev.buf, expected);

  return expected;
}

static int tpm_tis_i2c_send (struct tpm_chip *chip, u8 *buf, size_t count)
{
  int rc;
  u8 tries, tries_limit = 2;

  if (count > TPM_BUFSIZE)
    return -EINVAL;

  memset(tpm_dev.buf, 0x00, TPM_BUFSIZE);
  /* should add sanitization */
  memcpy(tpm_dev.buf, buf, count);

  for (tries = 0; tries < tries_limit; tries++) {
    /* Atmel TPM uses RAW writes */
    rc = i2c_write(tpm_dev.addr, 0, 0, tpm_dev.buf, count);
    if (rc == 0)
      break; /*win*/
    udelay(60);
  }

  /** should unlock device **/
  if (rc) {
    printf("tpm_i2c_atmel: write error\n");
    return -EIO;
  }

  return count;
}

static u8 tpm_tis_i2c_status (struct tpm_chip *chip)
{
  return 1; /* not a timeout */
}

static void tpm_tis_i2c_ready (struct tpm_chip *chip)
{
  /* nothing */
}


static struct tpm_vendor_specific tpm_tis_i2c = {
  .status = tpm_tis_i2c_status,
  .recv = tpm_tis_i2c_recv,
  .send = tpm_tis_i2c_send,
  .cancel = tpm_tis_i2c_ready,
  /*.req_complete_mask = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
  .req_complete_val = TPM_STS_DATA_AVAIL | TPM_STS_VALID,
  .req_canceled = TPM_STS_COMMAND_READY,*/
};

int tpm_vendor_init (uint32_t dev_addr)
{
  uint old_addr;
  struct tpm_chip *chip;

  old_addr = tpm_dev.addr;
  if (dev_addr != 0) {
    tpm_dev.addr = dev_addr;
  }

  /* could call probe here */

  chip = tpm_register_hardware(&tpm_tis_i2c);

  if (chip < 0) {
    tpm_dev.addr = old_addr;
    return -ENODEV;
  }

  /* Note: not sure why we set the irq to an int
   * This causes tpm_transmit to skip retries, waits, and interrupts
   */
  chip->vendor.irq = 0;

  printf("1.2 TPM (atmel)\n");

  return 0;
}

void tpm_vendor_cleanup(struct tpm_chip *chip)
{
  /* nothing */
}
