/*
 * Audio management UI code
 *
 * Copyright 2004 Chris Morgan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#define WIN32_LEAN_AND_MEAN
#define NONAMELESSSTRUCT
#define NONAMELESSUNION

#include "config.h"
#include "wine/port.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define COBJMACROS
#include <windows.h>
#include <wine/debug.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <mmddk.h>

#include "ole2.h"
#include "initguid.h"
#include "devpkey.h"
#include "mmdeviceapi.h"
#include "audioclient.h"
#include "audiopolicy.h"

#include "winecfg.h"
#include "resource.h"

WINE_DEFAULT_DEBUG_CHANNEL(winecfg);

struct DeviceInfo {
    WCHAR *id;
    PROPVARIANT name;
};

static WCHAR g_drv_keyW[256] = {'S','o','f','t','w','a','r','e','\\',
    'W','i','n','e','\\','D','r','i','v','e','r','s','\\',0};

static const WCHAR reg_out_nameW[] = {'D','e','f','a','u','l','t','O','u','t','p','u','t',0};
static const WCHAR reg_in_nameW[] = {'D','e','f','a','u','l','t','I','n','p','u','t',0};
static const WCHAR reg_vout_nameW[] = {'D','e','f','a','u','l','t','V','o','i','c','e','O','u','t','p','u','t',0};
static const WCHAR reg_vin_nameW[] = {'D','e','f','a','u','l','t','V','o','i','c','e','I','n','p','u','t',0};

static UINT num_render_devs, num_capture_devs;
static struct DeviceInfo *render_devs, *capture_devs;

static BOOL load_device(IMMDevice *dev, struct DeviceInfo *info)
{
    IPropertyStore *ps;
    HRESULT hr;

    hr = IMMDevice_GetId(dev, &info->id);
    if(FAILED(hr)){
        info->id = NULL;
        return FALSE;
    }

    hr = IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps);
    if(FAILED(hr)){
        CoTaskMemFree(info->id);
        info->id = NULL;
        return FALSE;
    }

    PropVariantInit(&info->name);

    hr = IPropertyStore_GetValue(ps,
            (PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &info->name);
    IPropertyStore_Release(ps);
    if(FAILED(hr)){
        CoTaskMemFree(info->id);
        info->id = NULL;
        return FALSE;
    }

    return TRUE;
}

static BOOL load_devices(IMMDeviceEnumerator *devenum, EDataFlow dataflow,
        UINT *ndevs, struct DeviceInfo **out)
{
    IMMDeviceCollection *coll;
    UINT i;
    HRESULT hr;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(devenum, dataflow,
            DEVICE_STATE_ACTIVE, &coll);
    if(FAILED(hr))
        return FALSE;

    hr = IMMDeviceCollection_GetCount(coll, ndevs);
    if(FAILED(hr)){
        IMMDeviceCollection_Release(coll);
        return FALSE;
    }

    if(*ndevs > 0){
        *out = HeapAlloc(GetProcessHeap(), 0,
                sizeof(struct DeviceInfo) * (*ndevs));
        if(!*out){
            IMMDeviceCollection_Release(coll);
            return FALSE;
        }

        for(i = 0; i < *ndevs; ++i){
            IMMDevice *dev;

            hr = IMMDeviceCollection_Item(coll, i, &dev);
            if(FAILED(hr)){
                (*out)[i].id = NULL;
                continue;
            }

            load_device(dev, &(*out)[i]);

            IMMDevice_Release(dev);
        }
    }else
        *out = NULL;

    IMMDeviceCollection_Release(coll);

    return TRUE;
}

static BOOL get_driver_name(IMMDeviceEnumerator *devenum, PROPVARIANT *pv)
{
    IMMDevice *device;
    IPropertyStore *ps;
    HRESULT hr;

    static const WCHAR wine_info_deviceW[] = {'W','i','n','e',' ',
        'i','n','f','o',' ','d','e','v','i','c','e',0};

    hr = IMMDeviceEnumerator_GetDevice(devenum, wine_info_deviceW, &device);
    if(FAILED(hr))
        return FALSE;

    hr = IMMDevice_OpenPropertyStore(device, STGM_READ, &ps);
    if(FAILED(hr)){
        IMMDevice_Release(device);
        return FALSE;
    }

    hr = IPropertyStore_GetValue(ps,
            (const PROPERTYKEY *)&DEVPKEY_Device_Driver, pv);
    IPropertyStore_Release(ps);
    IMMDevice_Release(device);
    if(FAILED(hr))
        return FALSE;

    return TRUE;
}

static void initAudioDlg (HWND hDlg)
{
    WCHAR display_str[256], format_str[256], sysdefault_str[256], disabled_str[64];
    IMMDeviceEnumerator *devenum;
    BOOL have_driver = FALSE;
    HRESULT hr;

    WINE_TRACE("\n");

    LoadStringW(GetModuleHandleW(NULL), IDS_AUDIO_DRIVER,
            format_str, sizeof(format_str) / sizeof(*format_str));
    LoadStringW(GetModuleHandleW(NULL), IDS_AUDIO_DRIVER_NONE,
            disabled_str, sizeof(disabled_str) / sizeof(*disabled_str));
    LoadStringW(GetModuleHandleW(NULL), IDS_AUDIO_SYSDEFAULT,
            sysdefault_str, sizeof(sysdefault_str) / sizeof(*sysdefault_str));

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL,
            CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, (void**)&devenum);
    if(SUCCEEDED(hr)){
        PROPVARIANT pv;

        load_devices(devenum, eRender, &num_render_devs, &render_devs);
        load_devices(devenum, eCapture, &num_capture_devs, &capture_devs);

        PropVariantInit(&pv);
        if(get_driver_name(devenum, &pv) && pv.u.pwszVal[0] != '\0'){
            have_driver = TRUE;
            wnsprintfW(display_str, sizeof(display_str) / sizeof(*display_str),
                    format_str, pv.u.pwszVal);
            lstrcatW(g_drv_keyW, pv.u.pwszVal);
        }
        PropVariantClear(&pv);

        IMMDeviceEnumerator_Release(devenum);
    }

    SendDlgItemMessageW(hDlg, IDC_AUDIOOUT_DEVICE, CB_ADDSTRING,
            0, (LPARAM)sysdefault_str);
    SendDlgItemMessageW(hDlg, IDC_AUDIOOUT_DEVICE, CB_SETCURSEL, 0, 0);
    SendDlgItemMessageW(hDlg, IDC_VOICEOUT_DEVICE, CB_ADDSTRING,
            0, (LPARAM)sysdefault_str);
    SendDlgItemMessageW(hDlg, IDC_VOICEOUT_DEVICE, CB_SETCURSEL, 0, 0);

    SendDlgItemMessageW(hDlg, IDC_AUDIOIN_DEVICE, CB_ADDSTRING,
            0, (LPARAM)sysdefault_str);
    SendDlgItemMessageW(hDlg, IDC_AUDIOIN_DEVICE, CB_SETCURSEL, 0, 0);
    SendDlgItemMessageW(hDlg, IDC_VOICEIN_DEVICE, CB_ADDSTRING,
            0, (LPARAM)sysdefault_str);
    SendDlgItemMessageW(hDlg, IDC_VOICEIN_DEVICE, CB_SETCURSEL, 0, 0);

    if(have_driver){
        WCHAR *reg_out_dev, *reg_vout_dev, *reg_in_dev, *reg_vin_dev;
        UINT i;

        reg_out_dev = get_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, reg_out_nameW, NULL);
        reg_vout_dev = get_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, reg_vout_nameW, NULL);
        reg_in_dev = get_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, reg_in_nameW, NULL);
        reg_vin_dev = get_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, reg_vin_nameW, NULL);

        for(i = 0; i < num_render_devs; ++i){
            if(!render_devs[i].id)
                continue;

            SendDlgItemMessageW(hDlg, IDC_AUDIOOUT_DEVICE, CB_ADDSTRING,
                    0, (LPARAM)render_devs[i].name.u.pwszVal);
            SendDlgItemMessageW(hDlg, IDC_AUDIOOUT_DEVICE, CB_SETITEMDATA,
                    i + 1, (LPARAM)&render_devs[i]);
            if(reg_out_dev && !lstrcmpW(render_devs[i].id, reg_out_dev))
                SendDlgItemMessageW(hDlg, IDC_AUDIOOUT_DEVICE, CB_SETCURSEL, i + 1, 0);

            SendDlgItemMessageW(hDlg, IDC_VOICEOUT_DEVICE, CB_ADDSTRING,
                    0, (LPARAM)render_devs[i].name.u.pwszVal);
            SendDlgItemMessageW(hDlg, IDC_VOICEOUT_DEVICE, CB_SETITEMDATA,
                    i + 1, (LPARAM)&render_devs[i]);
            if(reg_vout_dev && !lstrcmpW(render_devs[i].id, reg_vout_dev))
                SendDlgItemMessageW(hDlg, IDC_VOICEOUT_DEVICE, CB_SETCURSEL, i + 1, 0);
        }

        for(i = 0; i < num_capture_devs; ++i){
            if(!capture_devs[i].id)
                continue;

            SendDlgItemMessageW(hDlg, IDC_AUDIOIN_DEVICE, CB_ADDSTRING,
                    0, (LPARAM)capture_devs[i].name.u.pwszVal);
            SendDlgItemMessageW(hDlg, IDC_AUDIOIN_DEVICE, CB_SETITEMDATA,
                    i + 1, (LPARAM)&capture_devs[i]);
            if(reg_in_dev && !lstrcmpW(capture_devs[i].id, reg_in_dev))
                SendDlgItemMessageW(hDlg, IDC_AUDIOIN_DEVICE, CB_SETCURSEL, i + 1, 0);

            SendDlgItemMessageW(hDlg, IDC_VOICEIN_DEVICE, CB_ADDSTRING,
                    0, (LPARAM)capture_devs[i].name.u.pwszVal);
            SendDlgItemMessageW(hDlg, IDC_VOICEIN_DEVICE, CB_SETITEMDATA,
                    i + 1, (LPARAM)&capture_devs[i]);
            if(reg_vin_dev && !lstrcmpW(capture_devs[i].id, reg_vin_dev))
                SendDlgItemMessageW(hDlg, IDC_VOICEIN_DEVICE, CB_SETCURSEL, i + 1, 0);
        }

        HeapFree(GetProcessHeap(), 0, reg_out_dev);
        HeapFree(GetProcessHeap(), 0, reg_vout_dev);
        HeapFree(GetProcessHeap(), 0, reg_in_dev);
        HeapFree(GetProcessHeap(), 0, reg_vin_dev);
    }else
        wnsprintfW(display_str, sizeof(display_str) / sizeof(*display_str),
                format_str, disabled_str);

    SetDlgItemTextW(hDlg, IDC_AUDIO_DRIVER, display_str);
}

static void set_reg_device(HWND hDlg, int dlgitem, const WCHAR *key_name)
{
    UINT idx;
    struct DeviceInfo *info;

    idx = SendDlgItemMessageW(hDlg, dlgitem, CB_GETCURSEL, 0, 0);

    info = (struct DeviceInfo *)SendDlgItemMessageW(hDlg, dlgitem,
            CB_GETITEMDATA, idx, 0);

    if(!info || info == (void*)CB_ERR)
        set_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, key_name, NULL);
    else
        set_reg_keyW(HKEY_CURRENT_USER, g_drv_keyW, key_name, info->id);
}

static void test_sound(void)
{
    if(!PlaySoundW(MAKEINTRESOURCEW(IDW_TESTSOUND), NULL, SND_RESOURCE | SND_ASYNC)){
        WCHAR error_str[256], title_str[256];

        LoadStringW(GetModuleHandleW(NULL), IDS_AUDIO_TEST_FAILED,
                error_str, sizeof(error_str) / sizeof(*error_str));
        LoadStringW(GetModuleHandleW(NULL), IDS_AUDIO_TEST_FAILED_TITLE,
                title_str, sizeof(title_str) / sizeof(*title_str));

        MessageBoxW(NULL, error_str, title_str, MB_OK | MB_ICONERROR);
    }
}

INT_PTR CALLBACK
AudioDlgProc (HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_AUDIO_TEST:
              test_sound();
              break;
          case IDC_AUDIOOUT_DEVICE:
              if(HIWORD(wParam) == CBN_SELCHANGE){
                  set_reg_device(hDlg, IDC_AUDIOOUT_DEVICE, reg_out_nameW);
                  SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
              }
              break;
          case IDC_VOICEOUT_DEVICE:
              if(HIWORD(wParam) == CBN_SELCHANGE){
                  set_reg_device(hDlg, IDC_VOICEOUT_DEVICE, reg_vout_nameW);
                  SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
              }
              break;
          case IDC_AUDIOIN_DEVICE:
              if(HIWORD(wParam) == CBN_SELCHANGE){
                  set_reg_device(hDlg, IDC_AUDIOIN_DEVICE, reg_in_nameW);
                  SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
              }
              break;
          case IDC_VOICEIN_DEVICE:
              if(HIWORD(wParam) == CBN_SELCHANGE){
                  set_reg_device(hDlg, IDC_VOICEIN_DEVICE, reg_vin_nameW);
                  SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
              }
              break;
        }
        break;

      case WM_SHOWWINDOW:
        set_window_title(hDlg);
        break;

      case WM_NOTIFY:
        switch(((LPNMHDR)lParam)->code) {
            case PSN_KILLACTIVE:
              SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, FALSE);
              break;
            case PSN_APPLY:
              apply();
              SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
              break;
            case PSN_SETACTIVE:
              break;
        }
        break;
      case WM_INITDIALOG:
        initAudioDlg(hDlg);
        break;
  }

  return FALSE;
}