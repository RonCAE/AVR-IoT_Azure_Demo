/**
 * \file
 * \brief ATCA Hardware abstraction layer for Windows using kit protocol over a USB HID device.
 *
 * Copyright (c) 2015 Atmel Corporation. All rights reserved.
 *
 * \atmel_crypto_device_library_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel integrated circuit.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \atmel_crypto_device_library_license_stop
 */

#include "atca_hal.h"
#include "hal_win_kit_hid.h"
#include "kit_protocol.h"
#include "kit_phy.h"
#include <SetupAPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

/** \defgroup hal_ Hardware abstraction layer (hal_)
 *
 * \brief
 * These methods define the hardware abstraction layer for communicating with a CryptoAuth device
 *
   @{ */

// File scope globals
atcahid_t _gHid;

// The HID GUID filter
#define HID_GUID    { 0x4d1e55b2, 0xf16f, 0x11cf, 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 }



/** \brief HAL implementation of Kit USB HID init
 *  \param[in] hal pointer to HAL specific data that is maintained by this HAL
 *  \param[in] cfg pointer to HAL specific configuration data that is used to initialize this HAL
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_init(void* hal, ATCAIfaceCfg* cfg)
{
	ATCA_STATUS status = ATCA_SUCCESS;
	ATCAHAL_t *phal = NULL;
	GUID hid_guid = HID_GUID;
	HDEVINFO dev_info;
	SP_DEVICE_INTERFACE_DATA dev_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA dev_detail_data;
	DWORD required_size = 0;
	BOOL result = FALSE;
	DWORD device_index = 0;
	int i = 0;
	int index = 0;
	TCHAR hid_filter[32];

	// Check the input variables
	if ((hal == NULL) || (cfg == NULL))
		return ATCA_BAD_PARAM;

	// Cast the hal to the ATCAHAL_t structure
	phal = (ATCAHAL_t*)hal;

	// Initialize the _gHid structure
	memset(&_gHid, 0, sizeof(_gHid));
	for (i = 0; i < HID_DEVICES_MAX; i++) {
		_gHid.kits[i].read_handle = INVALID_HANDLE_VALUE;
		_gHid.kits[i].write_handle = INVALID_HANDLE_VALUE;
	}

	_gHid.num_kits_found = 0;

	// Initialize the GUID
//	UuidFromString(cfg->atcahid.guid, &hid_guid);

	// Query the devices
	dev_info = SetupDiGetClassDevs(&hid_guid, NULL, NULL, (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
	if (dev_info == INVALID_HANDLE_VALUE)
		return ATCA_COMM_FAIL;

	// Initialize the dev_data object
	memset(&dev_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
	dev_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	// Construct the filter string
	memset(hid_filter, 0, sizeof(hid_filter));
	_stprintf(hid_filter, _T("vid_%04x&pid_%04x"), cfg->atcahid.vid, cfg->atcahid.pid);

	// Find the Atmel kit USB devices
	while (SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, device_index, &dev_data)) {
		// Get the required buffer size of the detailed data
		SetupDiGetDeviceInterfaceDetail(dev_info, &dev_data, NULL, 0, &required_size, NULL);

		// Allocate the required space for the detailed interface data
		dev_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(required_size);

		// Retrieve the detailed interface data
		dev_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		result = SetupDiGetDeviceInterfaceDetail(dev_info, &dev_data, dev_detail_data,
		                                         required_size, &required_size, NULL);

		// Determine if this is the correct kit USB device

		if (_tcsstr(dev_detail_data->DevicePath, hid_filter) != NULL) {
			// Open the kit USB device for reading and writing
			if (_gHid.kits[index].read_handle != INVALID_HANDLE_VALUE)
				CloseHandle(_gHid.kits[index].read_handle);
			if (_gHid.kits[index].write_handle != INVALID_HANDLE_VALUE)
				CloseHandle(_gHid.kits[index].write_handle);

			_gHid.kits[index].read_handle = CreateFile(dev_detail_data->DevicePath,
			                                           GENERIC_READ,
			                                           (FILE_SHARE_READ | FILE_SHARE_WRITE),
			                                           NULL,
			                                           OPEN_EXISTING,
			                                           0,
			                                           NULL);
			_gHid.kits[index].write_handle = CreateFile(dev_detail_data->DevicePath,
			                                            GENERIC_WRITE,
			                                            (FILE_SHARE_READ | FILE_SHARE_WRITE),
			                                            NULL,
			                                            OPEN_EXISTING,
			                                            0,
			                                            NULL);

			// Increment the opened kit USB device index
			index++;
		}
		// Deallocate the required space for the detailed interface data
		free(dev_detail_data);

		// Break the while loop, if the maximum number of supported
		// kit USB devices have been found
		if (index == HID_DEVICES_MAX)
			break;

		// Increment the device member index
		device_index++;
	}

	// Delete device info now that we're done
	SetupDiDestroyDeviceInfoList(dev_info);

	// Save the results of this discovery of HID
	if (index > 0) {
		_gHid.num_kits_found = index;
		phal->hal_data = &_gHid;
	}
	return status;
}

ATCA_STATUS hal_kit_hid_discover_buses(int i2c_buses[], int max_buses)
{
	// TODO: This should be set to the com port index(s)
	return ATCA_UNIMPLEMENTED;
}

/** \brief discover any CryptoAuth devices on a given logical bus number
 * \param[in] busNum - logical bus number on which to look for CryptoAuth devices
 * \param[out] cfg[] - pointer to head of an array of interface config structures which get filled in by this method
 * \param[out] *found - number of devices found on this bus
 */
ATCA_STATUS hal_kit_hid_discover_devices(int busNum, ATCAIfaceCfg cfg[], int *found)
{
	// TODO: Add kitg protocol calls to discover all devices
	return ATCA_UNIMPLEMENTED;
}

/** \brief HAL implementation of Kit HID post init
 *  \param[in] iface  instance
 *  \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_post_init(ATCAIface iface)
{
	ATCA_STATUS status = ATCA_SUCCESS;
	atcahid_t* pHid = atgetifacehaldat(iface);
	ATCAIfaceCfg *pCfg = atgetifacecfg(iface);
	int i = 0;

	do {
		// Check the pointers
		if (pHid == NULL || pCfg == NULL) {
			status = ATCA_BAD_PARAM;
			BREAK(status, "NULL pointers in hal_kit_hid_post_init");
		}
		// Init all kit USB HID devices
		for (i = 0; i < pHid->num_kits_found; i++) {
			// Perform the kit protocol init
			status = kit_init(iface);
			if (status != ATCA_SUCCESS) BREAK(status, "kit_init() Failed");
		}

	} while (0);
	return status;
}

/** \brief HAL implementation of send over USB HID
 *  \param[in] iface     instance
 *  \param[in] txdata    pointer to bytes to send
 *  \param[in] txlength  number of bytes to send
 *  \return ATCA_STATUS
 */
ATCA_STATUS kit_phy_send(ATCAIface iface, uint8_t* txdata, int txlength)
{
	ATCA_STATUS status = ATCA_SUCCESS;
	ATCAIfaceCfg *cfg = atgetifacecfg(iface);
	int hidid = cfg->atcahid.idx;
	atcahid_t* pHid = (atcahid_t*)atgetifacehaldat(iface);
	uint8_t buffer[HID_PACKET_MAX];
	DWORD bytes_to_send = 0;
	DWORD bytes_left = 0;
	DWORD bytes_sent = 0;
	BOOL result = FALSE;

	if ((txdata == NULL) || (pHid == NULL))
		return ATCA_BAD_PARAM;

	if (pHid->kits[hidid].write_handle == INVALID_HANDLE_VALUE)
		return ATCA_COMM_FAIL;

	bytes_left = txlength;

	while (bytes_left > 0) {
		memset(buffer, 0, (HID_PACKET_MAX));

		if (bytes_left >= cfg->atcahid.packetsize)
			bytes_to_send = cfg->atcahid.packetsize;
		else
			bytes_to_send = bytes_left;

		memcpy(&buffer[1], &txdata[(txlength - bytes_left)], bytes_to_send);

		result = WriteFile(pHid->kits[hidid].write_handle, buffer, (cfg->atcahid.packetsize + 1), &bytes_sent, NULL);
		if (result == FALSE)
			return ATCA_TX_FAIL;

		bytes_left -= bytes_to_send;
	}
	return status;
}

/** \brief HAL implementation of kit protocol send over USB HID
 * \param[in]    iface   instance
 * \param[out]   rxdata  pointer to space to receive the data
 * \param[inout] rxsize  ptr to expected number of receive bytes to request
 * \return ATCA_STATUS
 */
ATCA_STATUS kit_phy_receive(ATCAIface iface, uint8_t* rxdata, int* rxsize)
{
	ATCAIfaceCfg *cfg = atgetifacecfg(iface);
	int hidid = cfg->atcahid.idx;
	atcahid_t* pHid = (atcahid_t*)atgetifacehaldat(iface);
	uint8_t buffer[HID_PACKET_MAX] = { 0 };
	BOOL continue_read = TRUE;
	DWORD bytes_read = 0;
	DWORD total_bytes = 0;
	BOOL result = true;
	char* location = NULL;
	int bytes_remain = 0;
	int bytes_to_cpy = 0;
	int size_adjust = 0;

	// Verify the input variables
	if ((rxdata == NULL) || (rxsize == NULL) || (pHid == NULL))
		return ATCA_BAD_PARAM;

	if (pHid->kits[hidid].read_handle == INVALID_HANDLE_VALUE)
		return ATCA_COMM_FAIL;

	while (continue_read == true) {
		result = ReadFile(pHid->kits[hidid].read_handle, buffer, (cfg->atcahid.packetsize + 1), &bytes_read, NULL);
		if (result == false)
			return ATCA_RX_FAIL;
		// Find the location of the '\n' character in read buffer
		// todo: generalize this read...  it only applies if there is an ascii protocol with an <eom> of \n and if the <eom> exists
		location = strchr((char*)&buffer[1], '\n');
		if (location == NULL) {
			// Copy all of the bytes
			bytes_to_cpy = bytes_read;
			// An extra byte is prepended to HID communication
			size_adjust = 1;
		}else {
			// Copy only the bytes remaining in the read buffer to the <eom>
			bytes_to_cpy = (uint8_t)(location - (char*)buffer);
			size_adjust = 0;
			// The response has been received, stop receiving more data
			continue_read = false;
		}
		// Protect rxdata from overwriting, this will have the result of truncating the returned bytes
		// Remaining space in rxdata
		bytes_remain = (*rxsize - total_bytes);
		// Use the minimum between number of bytes read and remaining space
		bytes_to_cpy = min(bytes_remain, bytes_to_cpy);

		// Copy the received data
		memcpy(&rxdata[total_bytes], &buffer[1], bytes_to_cpy);
		total_bytes += bytes_to_cpy - size_adjust;
	}
	*rxsize = total_bytes;
	return ATCA_SUCCESS;
}

/** \brief Number of USB HID devices found
 *  \param[out] num_found
 *  \return ATCA_STATUS
 */
ATCA_STATUS kit_phy_num_found(int8_t* num_found)
{
	*num_found = _gHid.num_kits_found;
	return ATCA_SUCCESS;
}

/** \brief HAL implementation of kit protocol send over USB HID
 *  \param[in] iface     instance
 *  \param[in] txdata    pointer to bytes to send
 *  \param[in] txlength  number of bytes to send
 *  \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_send(ATCAIface iface, uint8_t* txdata, int txlength)
{
	// Call the kit_send() function that will call phy_send() implemented below
	return kit_send(iface, txdata, txlength);
}

/** \brief HAL implementation of send over USB HID
 * \param[in]    iface   instance
 * \param[in]    rxdata  pointer to space to receive the data
 * \param[inout] rxsize  ptr to expected number of receive bytes to request
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_receive(ATCAIface iface, uint8_t* rxdata, uint16_t* rxsize)
{
	// Call the kit_receive() function that will call phy_receive() implemented below
	return kit_receive(iface, rxdata, rxsize);
}

/** \brief Call the wake for kit protocol
 * \param[in] iface  ATCAIface instance that is the interface object to send the bytes over
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_wake(ATCAIface iface)
{
	// Call the kit_wake() function that will call phy_send() and phy_receive()
	return kit_wake(iface);
}

/** \brief Call the idle for kit protocol
 * \param[in] iface  ATCAIface instance that is the interface object to send the bytes over
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_idle(ATCAIface iface)
{
	// Call the kit_idle() function that will call phy_send() and phy_receive()
	return kit_idle(iface);
}

/** \brief Call the sleep for kit protocol
 * \param[in] iface  ATCAIface instance that is the interface object to send the bytes over
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_sleep(ATCAIface iface)
{
	// Call the kit_sleep() function that will call phy_send() and phy_receive()
	return kit_sleep(iface);
}

/** \brief Close the physical port for HID
 * \param[in] hal_data  The hardware abstraction data specific to this HAL
 * \return ATCA_STATUS
 */
ATCA_STATUS hal_kit_hid_release(void* hal_data)
{
	int i = 0;
	atcahid_t* phaldat = (atcahid_t*)hal_data;

	if ((hal_data == NULL))
		return ATCA_BAD_PARAM;

	// Close all kit USB devices
	for (i = 0; i < phaldat->num_kits_found; i++) {
		if (phaldat->kits[i].read_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(phaldat->kits[i].read_handle);
			phaldat->kits[i].read_handle = INVALID_HANDLE_VALUE;
		}

		if (phaldat->kits[i].write_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(phaldat->kits[i].write_handle);
			phaldat->kits[i].write_handle = INVALID_HANDLE_VALUE;
		}
	}
	return ATCA_SUCCESS;
}

/** @} */
