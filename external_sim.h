/*
 * This work is licensed under the terms of the GNU GPL
 * version 2. Seethe COPYING file in the top-level directory.
 *
 * A module for pacing the rate of advance of the computer
 * clock in reference to an external simulation clock. The
 * basic approach used here is adapted from QBox from Green
 * Socs. The simulator uses shared memory to exchange timing
 * data. The external simulator starts the exchange by forking a
 * QEMU process and creating semaphores and shared memory with
 * a well known name. The external simulator use these to get
 * time advance requests from QEMU and supply time advance
 * grants in return. The QEMU side of the protocol is implemented
 * here. The simulator side of the protocol is available in
 * the qemu_sync.cpp and qemu_sync.h files that are included in
 * the adevs simulation package.
 *
 * Authors:
 *   James Nutaro <nutaro@gmail.com>
 *
 */
#ifndef EXTERNAL_SIM_H
#define EXTERNAL_SIM_H

void external_sim_sync(void);
bool external_sim_enabled(void);
void setup_external_sim(void);

#endif
