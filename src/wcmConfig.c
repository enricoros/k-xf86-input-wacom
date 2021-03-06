/*
 * Copyright 1995-2002 by Frederic Lepied, France. <Lepied@XFree86.org>
 * Copyright 2002-2009 by Ping Cheng, Wacom. <pingc@wacom.com>
 *                                                                            
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "xf86Wacom.h"
#include "wcmFilter.h"
#include <sys/stat.h>
#include <fcntl.h>

extern Bool wcmIsWacomDevice (char* fname);
extern Bool wcmIsAValidType(const char* type, unsigned long* keys);
extern int wcmIsDuplicate(char* device, LocalDevicePtr local);
extern int wcmNeedAutoHotplug(LocalDevicePtr local,
	const char **type, unsigned long* keys);
extern int wcmAutoProbeDevice(LocalDevicePtr local);
extern int wcmParseOptions(LocalDevicePtr local, unsigned long* keys);
extern void wcmHotplugOthers(LocalDevicePtr local, unsigned long* keys);
extern int wcmDeviceTypeKeys(LocalDevicePtr local, unsigned long* keys);

static int xf86WcmAllocate(LocalDevicePtr local, char* name, int flag);

/*****************************************************************************
 * xf86WcmAllocate --
 ****************************************************************************/

static int xf86WcmAllocate(LocalDevicePtr local, char* type_name, int flag)
{
	WacomDevicePtr   priv   = NULL;
	WacomCommonPtr   common = NULL;
	WacomToolPtr     tool   = NULL;
	WacomToolAreaPtr area   = NULL;
	int i;

	priv = xcalloc(1, sizeof(WacomDeviceRec));
	if (!priv)
		goto error;

	common = xcalloc(1, sizeof(WacomCommonRec));
	if (!common)
		goto error;

	tool = xcalloc(1, sizeof(WacomTool));
	if(!tool)
		goto error;

	area = xcalloc(1, sizeof(WacomToolArea));
	if (!area)
		goto error;

	local->type_name = type_name;
	local->flags = 0;
	local->device_control = gWacomModule.DevProc;
	local->read_input = gWacomModule.DevReadInput;
	local->control_proc = gWacomModule.DevChangeControl;
	local->close_proc = gWacomModule.DevClose;
	local->switch_mode = gWacomModule.DevSwitchMode;
	local->conversion_proc = gWacomModule.DevConvert;
	local->reverse_conversion_proc = gWacomModule.DevReverseConvert;
	local->fd = -1;
	local->atom = 0;
	local->dev = NULL;
	local->private = priv;
	local->private_flags = 0;
	local->old_x = -1;
	local->old_y = -1;

	priv->next = NULL;
	priv->local = local;
	priv->flags = flag;          /* various flags (device type, absolute, first touch...) */
	priv->common = common;       /* common info pointer */
	priv->hardProx = 1;	     /* previous hardware proximity */
	priv->old_device_id = IsStylus(priv) ? STYLUS_DEVICE_ID :
		(IsEraser(priv) ? ERASER_DEVICE_ID : 
		(IsCursor(priv) ? CURSOR_DEVICE_ID : 
		(IsTouch(priv) ? TOUCH_DEVICE_ID :
		PAD_DEVICE_ID)));

	priv->screen_no = -1;        /* associated screen */
	priv->nPressCtrl [0] = 0;    /* pressure curve x0 */
	priv->nPressCtrl [1] = 0;    /* pressure curve y0 */
	priv->nPressCtrl [2] = 100;  /* pressure curve x1 */
	priv->nPressCtrl [3] = 100;  /* pressure curve y1 */

	/* Default button and expresskey values */
	for (i=0; i<WCM_MAX_BUTTONS; i++)
		priv->button[i] = i + 1;

	priv->nbuttons = WCM_MAX_BUTTONS;		/* Default number of buttons */
	priv->relup = 5;			/* Default relative wheel up event */
	priv->reldn = 4;			/* Default relative wheel down event */

	priv->wheelup = IsPad (priv) ? 4 : 0;	/* Default absolute wheel up event */
	priv->wheeldn = IsPad (priv) ? 5 : 0;	/* Default absolute wheel down event */
	priv->striplup = 4;			/* Default left strip up event */
	priv->stripldn = 5;			/* Default left strip down event */
	priv->striprup = 4;			/* Default right strip up event */
	priv->striprdn = 5;			/* Default right strip down event */
	priv->naxes = 6;			/* Default number of axes */
	priv->numScreen = screenInfo.numScreens; /* configured screens count */
	priv->currentScreen = -1;                /* current screen in display */
	priv->twinview = TV_NONE;		/* not using twinview gfx */
	priv->wcmMMonitor = 1;			/* enabled (=1) to support multi-monitor desktop. */
						/* disabled (=0) when user doesn't want to move the */
						/* cursor from one screen to another screen */

	/* JEJ - throttle sampling code */
	priv->throttleLimit = -1;

	common->wcmDevice = "";                  /* device file name */
	common->wcmFlags = RAW_FILTERING_FLAG;   /* various flags */
	common->wcmDevices = priv;
	common->wcmProtocolLevel = 4;      /* protocol level */
	common->wcmISDV4Speed = 38400;  /* serial ISDV4 link speed */

	common->wcmDevCls = &gWacomUSBDevice; /* device-specific functions */
	common->wcmTPCButton = 
		common->wcmTPCButtonDefault; /* set Tablet PC button on/off */
	common->wcmCapacity = -1;          /* Capacity is disabled */
	common->wcmCapacityDefault = -1;    /* default to -1 when capacity isn't supported */
					   /* 3 when capacity is supported */
	common->wcmRotate = ROTATE_NONE;   /* default tablet rotation to off */
	common->wcmMaxX = 0;               /* max digitizer logical X value */
	common->wcmMaxY = 0;               /* max digitizer logical Y value */
	common->wcmMaxTouchX = 1024;       /* max touch X value */
	common->wcmMaxTouchY = 1024;       /* max touch Y value */
	common->wcmMaxStripX = 4096;       /* Max fingerstrip X */
	common->wcmMaxStripY = 4096;       /* Max fingerstrip Y */
	common->wcmMaxtiltX = 128;	   /* Max tilt in X directory */
	common->wcmMaxtiltY = 128;	   /* Max tilt in Y directory */
	common->wcmCursorProxoutDistDefault = PROXOUT_INTUOS_DISTANCE; 
			/* default to Intuos */
	common->wcmSuppress = DEFAULT_SUPPRESS;    
			/* transmit position if increment is superior */
	common->wcmRawSample = DEFAULT_SAMPLES;    
			/* number of raw data to be used to for filtering */

	/* tool */
	priv->tool = tool;
	common->wcmTool = tool;
	tool->next = NULL;          /* next tool in list */
	tool->typeid = DEVICE_ID(flag); /* tool type (stylus/touch/eraser/cursor/pad) */
	tool->arealist = area;      /* list of defined areas */

	/* tool area */
	priv->toolarea = area;
	area->next = NULL;    /* next area in list */
	area->device = local; /* associated WacomDevice */

	return 1;

error:
	xfree(area);
	xfree(tool);
	xfree(common);
	xfree(priv);
	return 0;
}

static int xf86WcmAllocateByType(LocalDevicePtr local, const char *type)
{
	int rc = 0;

	if (!type)
	{
		xf86Msg(X_ERROR, "%s: No type or invalid type specified.\n"
				"Must be one of stylus, touch, cursor, eraser, or pad\n",
				local->name);
		return rc;
	}

	if (xf86NameCmp(type, "stylus") == 0)
		rc = xf86WcmAllocate(local, XI_STYLUS, ABSOLUTE_FLAG|STYLUS_ID);
	else if (xf86NameCmp(type, "touch") == 0)
		rc = xf86WcmAllocate(local, XI_TOUCH, ABSOLUTE_FLAG|TOUCH_ID);
	else if (xf86NameCmp(type, "cursor") == 0)
		rc = xf86WcmAllocate(local, XI_CURSOR, CURSOR_ID);
	else if (xf86NameCmp(type, "eraser") == 0)
		rc = xf86WcmAllocate(local, XI_ERASER, ABSOLUTE_FLAG|ERASER_ID);
	else if (xf86NameCmp(type, "pad") == 0)
		rc = xf86WcmAllocate(local, XI_PAD, PAD_ID);

	return rc;
}

/* 
 * Be sure to set vmin appropriately for your device's protocol. You want to
 * read a full packet before returning
 *
 * JEJ - Actually, anything other than 1 is probably a bad idea since packet
 * errors can occur.  When that happens, bytes are read individually until it
 * starts making sense again.
 */

static const char *default_options[] =
{
	"StopBits",    "1",
	"DataBits",    "8",
	"Parity",      "None",
	"Vmin",        "1",
	"Vtime",       "10",
	"FlowControl", "Xoff",
	NULL
};

/* xf86WcmUninit - called when the device is no longer needed. */

static void xf86WcmUninit(InputDriverPtr drv, LocalDevicePtr local, int flags)
{
	WacomDevicePtr priv = (WacomDevicePtr) local->private;
	WacomDevicePtr dev;
	WacomDevicePtr *prev;

	DBG(1, priv, "\n");

	if (priv->isParent)
	{
		/* HAL removal sees the parent device removed first. */
		WacomDevicePtr next;
		dev = priv->common->wcmDevices;

		xf86Msg(X_INFO, "%s: removing automatically added devices.\n",
			local->name);

		while(dev)
		{
			next = dev->next;
			if (!dev->isParent)
			{
				xf86Msg(X_INFO, "%s: removing dependent device '%s'\n",
					local->name, dev->local->name);
				DeleteInputDeviceRequest(dev->local->dev);
			}
			dev = next;
		}
	}

	prev = &priv->common->wcmDevices;
	dev = *prev;
	while(dev)
	{
		if (dev == priv)
		{
			*prev = dev->next;
			break;
		}
		prev = &dev->next;
		dev = dev->next;
	}

	/* free pressure curve */
	xfree(priv->pPressCurve);

	xfree(priv);
	local->private = NULL;


	xf86DeleteInput(local, 0);    
}

/* xf86WcmMatchDevice - locate matching device and merge common structure */

static Bool xf86WcmMatchDevice(LocalDevicePtr pMatch, LocalDevicePtr pLocal)
{
	WacomDevicePtr privMatch = (WacomDevicePtr)pMatch->private;
	WacomDevicePtr priv = (WacomDevicePtr)pLocal->private;
	WacomCommonPtr common = priv->common;
	char * type;

	if ((pLocal != pMatch) &&
		strstr(pMatch->drv->driverName, "wacom") &&
		!strcmp(privMatch->common->wcmDevice, common->wcmDevice))
	{
		DBG(2, priv, "port share between"
			" %s and %s\n", pLocal->name, pMatch->name);
		type = xf86FindOptionValue(pMatch->options, "Type");
		if ( type && (strstr(type, "eraser")) )
			privMatch->common->wcmEraserID=pMatch->name;
		else
		{
			type = xf86FindOptionValue(pLocal->options, "Type");
			if ( type && (strstr(type, "eraser")) )
			{
				privMatch->common->wcmEraserID=pLocal->name;
			}
		}
		xfree(common);
		common = priv->common = privMatch->common;
		priv->next = common->wcmDevices;
		common->wcmDevices = priv;
		return 1;
	}
	return 0;
}

/* xf86WcmInit - called for each input devices with the driver set to
 * "wacom" */
static LocalDevicePtr xf86WcmInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
	LocalDevicePtr local = NULL;
	WacomDevicePtr priv = NULL;
	WacomCommonPtr common = NULL;
	const char*	type;
	char*		device;
	static int	numberWacom = 0;
	int		need_hotplug = 0;
	unsigned long   keys[NBITS(KEY_MAX)];

	gWacomModule.wcmDrv = drv;

	if (!(local = xf86AllocateInput(drv, 0)))
		goto SetupProc_fail;

	local->conf_idev = dev;
	local->name = dev->identifier;

	/* Force default port options to exist because the init
	 * phase is based on those values.
	 */
	xf86CollectInputOptions(local, default_options, NULL);

	/* initialize supported keys */
	wcmDeviceTypeKeys(local, keys);

	device = xf86SetStrOption(local->options, "Device", NULL);
	type = xf86FindOptionValue(local->options, "Type");
	need_hotplug = wcmNeedAutoHotplug(local, &type, keys);

	/* leave the undefined for auto-dev (if enabled) to deal with */
	if(device)
	{
		/* check if the type is valid for those don't need hotplug */
		if(!need_hotplug && !wcmIsAValidType(type, keys))
			goto SetupProc_fail;

		/* check if the device has been added */
		if (wcmIsDuplicate(device, local))
			goto SetupProc_fail;
	}

	if (!xf86WcmAllocateByType(local, type))
		goto SetupProc_fail;

	priv = (WacomDevicePtr) local->private;
	priv->name = local->name;
	common = priv->common;

	common->wcmDevice = device;

	/* Auto-probe the device if required, otherwise just noop. */
	if (numberWacom)
		if (!wcmAutoProbeDevice(local))
			goto SetupProc_fail;

	/* Lookup to see if there is another wacom device sharing the same port */
	if (common->wcmDevice)
	{
		LocalDevicePtr localDevices = xf86FirstLocalDevice();
		for (; localDevices != NULL; localDevices = localDevices->next)
		{
			if (xf86WcmMatchDevice(localDevices,local))
			{
				common = priv->common;
				break;
			}
		}
	}

	/* Process the common options. */
	xf86ProcessCommonOptions(local, local->options);
	if (!wcmParseOptions(local, keys))
		goto SetupProc_fail;

	/* mark the device configured */
	local->flags |= XI86_POINTER_CAPABLE | XI86_CONFIGURED;

	/* keep a local count so we know if "auto-dev" is necessary or not */
	numberWacom++;

	if (need_hotplug)
	{
		priv->isParent = 1;
		wcmHotplugOthers(local, keys);
	}

	/* return the LocalDevice */
	return (local);

SetupProc_fail:
	xfree(common);
	xfree(priv);
	if (local)
	{
		local->private = NULL;
		xf86DeleteInput(local, 0);
	}

	return NULL;
}

InputDriverRec WACOM =
{
	1,             /* driver version */
	"wacom",       /* driver name */
	NULL,          /* identify */
	xf86WcmInit,   /* pre-init */
	xf86WcmUninit, /* un-init */
	NULL,          /* module */
	0              /* ref count */
};


/* xf86WcmUnplug - Uninitialize the device */

static void xf86WcmUnplug(pointer p)
{
}

/* xf86WcmPlug - called by the module loader */

static pointer xf86WcmPlug(pointer module, pointer options, int* errmaj,
		int* errmin)
{
	xf86AddInputDriver(&WACOM, module, 0);
	return module;
}

static XF86ModuleVersionInfo xf86WcmVersionRec =
{
	"wacom",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}  /* signature, to be patched into the file by a tool */
};

_X_EXPORT XF86ModuleData wacomModuleData =
{
	&xf86WcmVersionRec,
	xf86WcmPlug,
	xf86WcmUnplug
};

/* vim: set noexpandtab shiftwidth=8: */
