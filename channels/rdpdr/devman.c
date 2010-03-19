/* -*- c-basic-offset: 8 -*-
   FreeRDP: A Remote Desktop Protocol client.
   Redirected Device Manager

   Copyright (C) Marc-Andre Moreau <marcandre.moreau@gmail.com> 2010

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "devman.h"
#include "types_ui.h"
#include "constants_rdpdr.h"

#define LOG_LEVEL 1
#define LLOG(_level, _args) \
  do { if (_level < LOG_LEVEL) { printf _args ; } } while (0)
#define LLOGLN(_level, _args) \
  do { if (_level < LOG_LEVEL) { printf _args ; printf("\n"); } } while (0)

DEVMAN*
devman_new()
{
	DEVMAN* devman;
	PDEVMAN_ENTRY_POINTS pDevmanEntryPoints;

	devman = (PDEVMAN)malloc(sizeof(DEVMAN));
	pDevmanEntryPoints = (PDEVMAN_ENTRY_POINTS)malloc(sizeof(DEVMAN_ENTRY_POINTS));

	devman->idev = NULL;
	devman->head = NULL;
	devman->tail = NULL;
	devman->count = 0;

	pDevmanEntryPoints->pDevmanRegisterService = devman_register_service;
	pDevmanEntryPoints->pDevmanUnregisterService = devman_unregister_service;
	pDevmanEntryPoints->pDevmanRegisterDevice = devman_register_device;
	pDevmanEntryPoints->pDevmanUnregisterDevice = devman_unregister_device;
	devman->pDevmanEntryPoints = (void*)pDevmanEntryPoints;

	return devman;
}

int
devman_free(DEVMAN* devman)
{
	DEVICE* pdev;

	/* unregister all services, which will in turn unregister all devices */

	devman_rewind(devman);

	while (devman_has_next(devman) != 0)
	{
		pdev = devman_get_next(devman);
		devman_unregister_service(devman, pdev->service);
		devman_rewind(devman);
	}

	/* free devman */
	free(devman);

	return 1;
}

SERVICE*
devman_register_service(DEVMAN* devman)
{
	SERVICE* srv = (SERVICE*)malloc(sizeof(SERVICE));

	srv->create = NULL;
	srv->close = NULL;
	srv->read = NULL;
	srv->write = NULL;
	srv->control = NULL;
	srv->type = 0;

	return srv;
}

int
devman_unregister_service(DEVMAN* devman, SERVICE* srv)
{
	DEVICE* pdev;

	/* unregister all devices depending on the service */

	devman_rewind(devman);

	while (devman_has_next(devman) != 0)
	{
		pdev = devman_get_next(devman);

		if (pdev->service == srv)
		{
			devman_unregister_device(devman, pdev);
			devman_rewind(devman);
		}
	}

	/* unregister service */
	free(srv);

	return 1;
}

DEVICE*
devman_register_device(DEVMAN* devman, SERVICE* srv, char* name)
{
	DEVICE* pdev;

	pdev = (DEVICE*)malloc(sizeof(DEVICE));
	pdev->prev = NULL;
	pdev->next = NULL;
	pdev->service = srv;

	pdev->name = malloc(strlen(name));
	strcpy(pdev->name, name);

	if (devman->head == devman->tail)
	{
		/* linked list is empty */
		devman->head = pdev;
		devman->tail = pdev;
	}
	else
	{
		/* append device to the end of the linked list */
		devman->tail->next = (void*)pdev;
		pdev->prev = (void*)devman->tail;
		devman->tail = pdev;
	}

	devman->count++;
	return pdev;
}

int
devman_unregister_device(DEVMAN* devman, DEVICE* dev)
{
	DEVICE* pdev;

	devman_rewind(devman);

	while (devman_has_next(devman) != 0)
	{
		pdev = devman_get_next(devman);
		
		if (pdev == dev) /* device exists */
		{
			/* set previous device to point to next device */
			
			if (dev->prev != NULL)
			{
				/* unregistered device is not the head */
				pdev = (DEVICE*)dev->prev;
				pdev->next = dev->next;
			}
			else
			{
				/* unregistered device is the head, update head */
				devman->head = (DEVICE*)dev->next;
			}
			
			/* set next device to point to previous device */

			if (dev->next != NULL)
			{
				/* unregistered device is not the tail */
				pdev = (DEVICE*)dev->next;
				pdev->prev = dev->prev;
			}
			else
			{
				/* unregistered device is the tail, update tail */
				devman->tail = (DEVICE*)dev->prev;
			}

			devman->count--;

			free(dev); /* free memory for unregistered device */
			return 1; /* unregistration successful */
		}
	}

	/* if we reach this point, the device wasn't found */
	return 0;
}

void
devman_rewind(DEVMAN* devman)
{
	devman->idev = devman->head;
}

int
devman_has_next(DEVMAN* devman)
{
	if (devman->idev == NULL)
		return 0;
	else if (devman->idev == devman->head)
		return 1;
	else if (devman->idev->next != NULL)
		return 1;
	else
		return 0;
}

DEVICE*
devman_get_next(DEVMAN* devman)
{
	DEVICE* pdev;

	pdev = devman->idev;
	devman->idev = (DEVICE*)devman->idev->next;

	return pdev;
}

DEVICE*
devman_get_device_by_id(DEVMAN* devman, uint32 id)
{
	DEVICE* pdev;

	devman_rewind(devman);

	while (devman_has_next(devman) != 0)
	{
		pdev = devman_get_next(devman);

		/* device with given ID was found */
		if (pdev->id == id)
			return pdev;
	}

	/* no device with the given ID was found */
	return NULL;
}

int
devman_load_device_service(DEVMAN* devman, char* filename)
{
	void* dl;
	PDEVICE_SERVICE_ENTRY pDeviceServiceEntry = NULL;

	dl = dlopen(filename, RTLD_LOCAL | RTLD_LAZY);

	pDeviceServiceEntry = (PDEVICE_SERVICE_ENTRY)dlsym(dl, "DeviceServiceEntry");

	if(pDeviceServiceEntry != NULL)
	{
		pDeviceServiceEntry(devman, devman->pDevmanEntryPoints);
		LLOGLN(0, ("loaded device service: %s", filename));
	}

	return 0;
}