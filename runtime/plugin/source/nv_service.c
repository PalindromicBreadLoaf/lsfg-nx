// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later
// Portions adapted from libnx.
// Copyright 2017-2018 libnx Authors
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
// OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
// CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#define NX_SERVICE_ASSUME_NON_DOMAIN

#include <switch.h>

#include <string.h>

extern Result lsfg_plugin_borrow_nvdrv_session(Handle *out);

static Service g_nv_service;
static Mutex g_nv_guard_mutex;
static u32 g_nv_references;

static void nv_cleanup(void) {
    serviceClose(&g_nv_service);
    g_nv_service = (Service){0};
}

Result nvInitialize(void) {
    mutexLock(&g_nv_guard_mutex);
    if (g_nv_references != 0) {
        ++g_nv_references;
        mutexUnlock(&g_nv_guard_mutex);
        return 0;
    }

    Handle session = INVALID_HANDLE;
    Result result = lsfg_plugin_borrow_nvdrv_session(&session);
    if (R_SUCCEEDED(result)) {
        g_nv_service = (Service){
            .session = session,
            .own_handle = 0,
            .object_id = 0,
            .pointer_buffer_size = 0,
        };
        cmifQueryPointerBufferSize(session, &g_nv_service.pointer_buffer_size);
        g_nv_references = 1;
    }
    mutexUnlock(&g_nv_guard_mutex);
    return result;
}

void nvExit(void) {
    mutexLock(&g_nv_guard_mutex);
    if (g_nv_references != 0 && --g_nv_references == 0) {
        nv_cleanup();
    }
    mutexUnlock(&g_nv_guard_mutex);
}

Service *nvGetServiceSession(void) { return &g_nv_service; }

Result nvOpen(u32 *fd, const char *devicepath) {
    struct {
        u32 fd;
        u32 error;
    } out;

    const SfDispatchParams dispatch = {
        .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias},
        .buffers = {{devicepath, strlen(devicepath)}},
    };
    Result result = serviceDispatchImpl(&g_nv_service, 0, NULL, 0, &out,
                                        sizeof(out), dispatch);
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)out.error);
    }
    if (R_SUCCEEDED(result) && fd != NULL) {
        *fd = out.fd;
    }
    return result;
}

Result nvIoctl(u32 fd, u32 request, void *argp) {
    const size_t buffer_size = _NV_IOC_SIZE(request);
    const u32 direction = _NV_IOC_DIR(request);
    const void *send_buffer = NULL;
    void *receive_buffer = NULL;
    size_t send_size = 0;
    size_t receive_size = 0;

    if ((direction & _NV_IOC_WRITE) != 0) {
        send_buffer = argp;
        send_size = buffer_size;
    }
    if ((direction & _NV_IOC_READ) != 0) {
        receive_buffer = argp;
        receive_size = buffer_size;
    }

    const struct {
        u32 fd;
        u32 request;
    } in = {fd, request};

    u32 error = 0;
    const SfDispatchParams dispatch = {
        .buffer_attrs =
            {
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
            },
        .buffers =
            {
                {send_buffer, send_size},
                {receive_buffer, receive_size},
            },
    };
    Result result = serviceDispatchImpl(&g_nv_service, 1, &in, sizeof(in),
                                        &error, sizeof(error), dispatch);
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)error);
    }
    return result;
}

Result nvIoctl2(u32 fd, u32 request, void *argp, const void *in_buffer,
                size_t in_buffer_size) {
    if (hosversionBefore(3, 0, 0)) {
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    }

    const size_t buffer_size = _NV_IOC_SIZE(request);
    const u32 direction = _NV_IOC_DIR(request);
    const void *send_buffer = NULL;
    void *receive_buffer = NULL;
    size_t send_size = 0;
    size_t receive_size = 0;

    if ((direction & _NV_IOC_WRITE) != 0) {
        send_buffer = argp;
        send_size = buffer_size;
    }
    if ((direction & _NV_IOC_READ) != 0) {
        receive_buffer = argp;
        receive_size = buffer_size;
    }

    const struct {
        u32 fd;
        u32 request;
    } in = {fd, request};

    u32 error = 0;
    const SfDispatchParams dispatch = {
        .buffer_attrs =
            {
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
            },
        .buffers =
            {
                {send_buffer, send_size},
                {in_buffer, in_buffer_size},
                {receive_buffer, receive_size},
            },
    };
    Result result = serviceDispatchImpl(&g_nv_service, 11, &in, sizeof(in),
                                        &error, sizeof(error), dispatch);
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)error);
    }
    return result;
}

Result nvIoctl3(u32 fd, u32 request, void *argp, void *out_buffer,
                size_t out_buffer_size) {
    if (hosversionBefore(3, 0, 0)) {
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    }

    const size_t buffer_size = _NV_IOC_SIZE(request);
    const u32 direction = _NV_IOC_DIR(request);
    const void *send_buffer = NULL;
    void *receive_buffer = NULL;
    size_t send_size = 0;
    size_t receive_size = 0;

    if ((direction & _NV_IOC_WRITE) != 0) {
        send_buffer = argp;
        send_size = buffer_size;
    }
    if ((direction & _NV_IOC_READ) != 0) {
        receive_buffer = argp;
        receive_size = buffer_size;
    }

    const struct {
        u32 fd;
        u32 request;
    } in = {fd, request};

    u32 error = 0;
    const SfDispatchParams dispatch = {
        .buffer_attrs =
            {
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
                SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out,
            },
        .buffers =
            {
                {send_buffer, send_size},
                {receive_buffer, receive_size},
                {out_buffer, out_buffer_size},
            },
    };
    Result result = serviceDispatchImpl(&g_nv_service, 12, &in, sizeof(in),
                                        &error, sizeof(error), dispatch);
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)error);
    }
    return result;
}

Result nvClose(u32 fd) {
    u32 error = 0;
    Result result =
        serviceDispatchImpl(&g_nv_service, 2, &fd, sizeof(fd), &error,
                            sizeof(error), (SfDispatchParams){0});
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)error);
    }
    return result;
}

Result nvQueryEvent(u32 fd, u32 event_id, Event *event_out) {
    const struct {
        u32 fd;
        u32 event_id;
    } in = {fd, event_id};

    u32 error = 0;
    Handle event = INVALID_HANDLE;
    const SfDispatchParams dispatch = {
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy},
        .out_handles = &event,
    };
    Result result = serviceDispatchImpl(&g_nv_service, 4, &in, sizeof(in),
                                        &error, sizeof(error), dispatch);
    if (R_SUCCEEDED(result)) {
        result = nvConvertError((int)error);
    }
    if (R_SUCCEEDED(result)) {
        eventLoadRemote(event_out, event, true);
    }
    return result;
}

Result nvConvertError(int result) {
    if (result == 0) {
        return 0;
    }

    int description;
    switch (result) {
    case 1:
        description = LibnxNvidiaError_NotImplemented;
        break;
    case 2:
        description = LibnxNvidiaError_NotSupported;
        break;
    case 3:
        description = LibnxNvidiaError_NotInitialized;
        break;
    case 4:
        description = LibnxNvidiaError_BadParameter;
        break;
    case 5:
        description = LibnxNvidiaError_Timeout;
        break;
    case 6:
        description = LibnxNvidiaError_InsufficientMemory;
        break;
    case 7:
        description = LibnxNvidiaError_ReadOnlyAttribute;
        break;
    case 8:
        description = LibnxNvidiaError_InvalidState;
        break;
    case 9:
        description = LibnxNvidiaError_InvalidAddress;
        break;
    case 10:
        description = LibnxNvidiaError_InvalidSize;
        break;
    case 11:
        description = LibnxNvidiaError_BadValue;
        break;
    case 13:
        description = LibnxNvidiaError_AlreadyAllocated;
        break;
    case 14:
        description = LibnxNvidiaError_Busy;
        break;
    case 15:
        description = LibnxNvidiaError_ResourceError;
        break;
    case 16:
        description = LibnxNvidiaError_CountMismatch;
        break;
    case 0x1000:
        description = LibnxNvidiaError_SharedMemoryTooSmall;
        break;
    case 0x30003:
        description = LibnxNvidiaError_FileOperationFailed;
        break;
    case 0x3000f:
        description = LibnxNvidiaError_IoctlFailed;
        break;
    default:
        description = LibnxNvidiaError_Unknown;
        break;
    }
    return MAKERESULT(Module_LibnxNvidia, description);
}
