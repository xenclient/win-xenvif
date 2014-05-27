/* Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, 
 * with or without modification, are permitted provided 
 * that the following conditions are met:
 * 
 * *   Redistributions of source code must retain the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer.
 * *   Redistributions in binary form must reproduce the above 
 *     copyright notice, this list of conditions and the 
 *     following disclaimer in the documentation and/or other 
 *     materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#include <ntddk.h>
#include <util.h>
#include <version.h>

#include "registry.h"
#include "fdo.h"
#include "pdo.h"
#include "netio.h"
#include "receiver.h"
#include "driver.h"
#include "dbg_print.h"
#include "assert.h"

extern PULONG       InitSafeBootMode;

typedef struct _XENVIF_DRIVER {
    PDRIVER_OBJECT      DriverObject;
    HANDLE              ServiceKey;
    HANDLE              ParametersKey;
    HANDLE              AddressesKey;
    HANDLE              AliasesKey;
} XENVIF_DRIVER, *PXENVIF_DRIVER;

static XENVIF_DRIVER    Driver;

static FORCEINLINE VOID
__DriverSetDriverObject(
    IN  PDRIVER_OBJECT  DriverObject
    )
{
    Driver.DriverObject = DriverObject;
}

static FORCEINLINE PDRIVER_OBJECT
__DriverGetDriverObject(
    VOID
    )
{
    return Driver.DriverObject;
}

PDRIVER_OBJECT
DriverGetDriverObject(
    VOID
    )
{
    return __DriverGetDriverObject();
}

static FORCEINLINE VOID
__DriverSetServiceKey(
    IN  HANDLE  Key
    )
{
    Driver.ServiceKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetServiceKey(
    VOID
    )
{
    return Driver.ServiceKey;
}

HANDLE
DriverGetServiceKey(
    VOID
    )
{
    return __DriverGetServiceKey();
}

static FORCEINLINE VOID
__DriverSetParametersKey(
    IN  HANDLE  Key
    )
{
    Driver.ParametersKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetParametersKey(
    VOID
    )
{
    return Driver.ParametersKey;
}

HANDLE
DriverGetParametersKey(
    VOID
    )
{
    return __DriverGetParametersKey();
}

static FORCEINLINE VOID
__DriverSetAddressesKey(
    IN  HANDLE  Key
    )
{
    Driver.AddressesKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetAddressesKey(
    VOID
    )
{
    return Driver.AddressesKey;
}

HANDLE
DriverGetAddressesKey(
    VOID
    )
{
    return __DriverGetAddressesKey();
}

static FORCEINLINE VOID
__DriverSetAliasesKey(
    IN  HANDLE  Key
    )
{
    Driver.AliasesKey = Key;
}

static FORCEINLINE HANDLE
__DriverGetAliasesKey(
    VOID
    )
{
    return Driver.AliasesKey;
}

HANDLE
DriverGetAliasesKey(
    VOID
    )
{
    return __DriverGetAliasesKey();
}

DRIVER_UNLOAD       DriverUnload;

VOID
DriverUnload(
    IN  PDRIVER_OBJECT  DriverObject
    )
{
    HANDLE              ServiceKey;
    HANDLE              AliasesKey;
    HANDLE              AddressesKey;
    HANDLE              ParametersKey;

    ASSERT3P(DriverObject, ==, __DriverGetDriverObject());

    Trace("====>\n");

    if (*InitSafeBootMode > 0)
        goto done;

    AliasesKey = __DriverGetAliasesKey();
    __DriverSetAliasesKey(NULL);

    RegistryCloseKey(AliasesKey);

    AddressesKey = __DriverGetParametersKey();
    __DriverSetAddressesKey(NULL);

    RegistryCloseKey(AddressesKey);

    ParametersKey = __DriverGetParametersKey();
    if (ParametersKey != NULL) {
        __DriverSetParametersKey(NULL);

        RegistryCloseKey(ParametersKey);
    }

    ServiceKey = __DriverGetServiceKey();
    __DriverSetServiceKey(NULL);

    RegistryCloseKey(ServiceKey);

    RegistryTeardown();

done:
    __DriverSetDriverObject(NULL);

    ASSERT(IsZeroMemory(&Driver, sizeof (XENVIF_DRIVER)));

    Trace("<====\n");
}

DRIVER_ADD_DEVICE   AddDevice;

NTSTATUS
AddDevice(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PDEVICE_OBJECT  DeviceObject
    )
{
    NTSTATUS            status;

    ASSERT3P(DriverObject, ==, __DriverGetDriverObject());

    status = FdoCreate(DeviceObject);
    if (!NT_SUCCESS(status))
        goto fail1;

    // prefast stupidity
    ASSERT(!(DeviceObject->Flags & DO_DEVICE_INITIALIZING));
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;

fail1:
    Error("fail1 (%08x)\n", status);

    return status;
}

DRIVER_DISPATCH Dispatch;

NTSTATUS 
Dispatch(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp
    )
{
    PXENVIF_DX          Dx;
    NTSTATUS            status;

    Dx = (PXENVIF_DX)DeviceObject->DeviceExtension;
    ASSERT3P(Dx->DeviceObject, ==, DeviceObject);

    if (Dx->DevicePnpState == Deleted) {
        status = STATUS_NO_SUCH_DEVICE;

        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        goto done;
    }

    status = STATUS_NOT_SUPPORTED;
    switch (Dx->Type) {
    case PHYSICAL_DEVICE_OBJECT: {
        PXENVIF_PDO Pdo = Dx->Pdo;

        status = PdoDispatch(Pdo, Irp);
        break;
    }
    case FUNCTION_DEVICE_OBJECT: {
        PXENVIF_FDO Fdo = Dx->Fdo;

        status = FdoDispatch(Fdo, Irp);
        break;
    }
    default:
        ASSERT(FALSE);
        break;
    }

done:
    return status;
}

DRIVER_INITIALIZE   DriverEntry;

NTSTATUS
DriverEntry(
    IN  PDRIVER_OBJECT  DriverObject,
    IN  PUNICODE_STRING RegistryPath
    )
{
    HANDLE              ServiceKey;
    HANDLE              ParametersKey;
    HANDLE              AddressesKey;
    HANDLE              AliasesKey;
    ULONG               Index;
    NTSTATUS            status;

    ASSERT3P(__DriverGetDriverObject(), ==, NULL);

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    __DbgPrintEnable();

    Trace("====>\n");

    __DriverSetDriverObject(DriverObject);

    if (*InitSafeBootMode > 0)
        goto done;

    Driver.DriverObject->DriverUnload = DriverUnload;

    Info("XENVIF %d.%d.%d (%d) (%02d.%02d.%04d)\n",
         MAJOR_VERSION,
         MINOR_VERSION,
         MICRO_VERSION,
         BUILD_NUMBER,
         DAY,
         MONTH,
         YEAR);

    NetioInitializeMutex();    

    status = RegistryInitialize(RegistryPath);
    if (!NT_SUCCESS(status))
        goto fail1;

    status = RegistryOpenServiceKey(KEY_ALL_ACCESS, &ServiceKey);
    if (!NT_SUCCESS(status))
        goto fail2;

    __DriverSetServiceKey(ServiceKey);

    (VOID) RegistryDeleteValue(ServiceKey, "NeedReboot");

    status = RegistryOpenSubKey(ServiceKey, 
                                "Parameters", 
                                KEY_READ, 
                                &ParametersKey);
    if (NT_SUCCESS(status))
        __DriverSetParametersKey(ParametersKey);

    status = RegistryCreateSubKey(ServiceKey, 
                                  "Addresses", 
                                  REG_OPTION_VOLATILE, 
                                  &AddressesKey);
    if (!NT_SUCCESS(status))
        goto fail3;

    __DriverSetAddressesKey(AddressesKey);

    status = RegistryOpenSubKey(ServiceKey, 
                                "Aliases", 
                                KEY_READ, 
                                &AliasesKey);
    if (!NT_SUCCESS(status))
        goto fail4;

    __DriverSetAliasesKey(AliasesKey);

    DriverObject->DriverExtension->AddDevice = AddDevice;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++) {
#pragma prefast(suppress:28169) // No __drv_dispatchType annotation
#pragma prefast(suppress:28168) // No matching __drv_dispatchType annotation for IRP_MJ_CREATE
        DriverObject->MajorFunction[Index] = Dispatch;
    }

done:
    Trace("<====\n");

    return STATUS_SUCCESS;

fail4:
    Error("fail4\n");

    __DriverSetAddressesKey(NULL);

    RegistryCloseKey(AddressesKey);

fail3:
    Error("fail3\n");

    __DriverSetParametersKey(NULL);

    __DriverSetServiceKey(NULL);

    RegistryCloseKey(ServiceKey);

fail2:
    Error("fail2\n");

    RegistryTeardown();

fail1:
    Error("fail1 (%08x)\n", status);

    __DriverSetDriverObject(NULL);

    ASSERT(IsZeroMemory(&Driver, sizeof (XENVIF_DRIVER)));

    return status;
}
