/*
 * Add/Remove Programs applet
 * Partially based on Wine Uninstaller
 *
 * Copyright 2000 Andreas Mohr
 * Copyright 2004 Hannu Valtonen
 * Copyright 2005 Jonathan Ernst
 * Copyright 2001-2002, 2008 Owen Rudge
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

#define NONAMELESSUNION

#include "config.h"
#include "wine/port.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <wingdi.h>
#include <winreg.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cpl.h>

#include "wine/unicode.h"
#include "wine/list.h"
#include "wine/debug.h"
#include "appwiz.h"
#include "res.h"

WINE_DEFAULT_DEBUG_CHANNEL(appwizcpl);

/* define a maximum length for various buffers we use */
#define MAX_STRING_LEN    1024

typedef struct APPINFO
{
    struct list entry;
    int id;

    LPWSTR title;
    LPWSTR path;
    LPWSTR path_modify;

    LPWSTR icon;
    int iconIdx;

    LPWSTR publisher;
    LPWSTR version;

    HKEY regroot;
    WCHAR regkey[MAX_STRING_LEN];
} APPINFO;

static struct list app_list = LIST_INIT( app_list );
HINSTANCE hInst;

static WCHAR btnRemove[MAX_STRING_LEN];
static WCHAR btnModifyRemove[MAX_STRING_LEN];

static const WCHAR openW[] = {'o','p','e','n',0};

/* names of registry keys */
static const WCHAR BackSlashW[] = { '\\', 0 };
static const WCHAR DisplayNameW[] = {'D','i','s','p','l','a','y','N','a','m','e',0};
static const WCHAR DisplayIconW[] = {'D','i','s','p','l','a','y','I','c','o','n',0};
static const WCHAR DisplayVersionW[] = {'D','i','s','p','l','a','y','V','e','r',
    's','i','o','n',0};
static const WCHAR PublisherW[] = {'P','u','b','l','i','s','h','e','r',0};
static const WCHAR ContactW[] = {'C','o','n','t','a','c','t',0};
static const WCHAR HelpLinkW[] = {'H','e','l','p','L','i','n','k',0};
static const WCHAR HelpTelephoneW[] = {'H','e','l','p','T','e','l','e','p','h',
    'o','n','e',0};
static const WCHAR ModifyPathW[] = {'M','o','d','i','f','y','P','a','t','h',0};
static const WCHAR NoModifyW[] = {'N','o','M','o','d','i','f','y',0};
static const WCHAR ReadmeW[] = {'R','e','a','d','m','e',0};
static const WCHAR URLUpdateInfoW[] = {'U','R','L','U','p','d','a','t','e','I',
    'n','f','o',0};
static const WCHAR CommentsW[] = {'C','o','m','m','e','n','t','s',0};
static const WCHAR UninstallCommandlineW[] = {'U','n','i','n','s','t','a','l','l',
    'S','t','r','i','n','g',0};
static const WCHAR WindowsInstallerW[] = {'W','i','n','d','o','w','s','I','n','s','t','a','l','l','e','r',0};
static const WCHAR SystemComponentW[] = {'S','y','s','t','e','m','C','o','m','p','o','n','e','n','t',0};

static const WCHAR PathUninstallW[] = {
        'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
        'U','n','i','n','s','t','a','l','l',0 };

/******************************************************************************
 * Name       : DllMain
 * Description: Entry point for DLL file
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason,
                    LPVOID lpvReserved)
{
    TRACE("(%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            hInst = hinstDLL;
            break;
    }
    return TRUE;
}

/******************************************************************************
 * Name       : FreeAppInfo
 * Description: Frees memory used by an AppInfo structure, and any children.
 */
static void FreeAppInfo(APPINFO *info)
{
    HeapFree(GetProcessHeap(), 0, info->title);
    HeapFree(GetProcessHeap(), 0, info->path);
    HeapFree(GetProcessHeap(), 0, info->path_modify);
    HeapFree(GetProcessHeap(), 0, info->icon);
    HeapFree(GetProcessHeap(), 0, info->publisher);
    HeapFree(GetProcessHeap(), 0, info->version);
    HeapFree(GetProcessHeap(), 0, info);
}

/******************************************************************************
 * Name       : ReadApplicationsFromRegistry
 * Description: Creates a linked list of uninstallable applications from the
 *              registry.
 * Parameters : root    - Which registry root to read from
 * Returns    : TRUE if successful, FALSE otherwise
 */
static BOOL ReadApplicationsFromRegistry(HKEY root)
{
    HKEY hkeyApp;
    int i, id = 0;
    DWORD sizeOfSubKeyName, displen, uninstlen;
    DWORD dwNoModify, dwType, value, size;
    WCHAR subKeyName[256];
    WCHAR *command;
    APPINFO *info = NULL;
    LPWSTR iconPtr;

    sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);

    for (i = 0; RegEnumKeyExW(root, i, subKeyName, &sizeOfSubKeyName, NULL,
        NULL, NULL, NULL) != ERROR_NO_MORE_ITEMS; ++i)
    {
        RegOpenKeyExW(root, subKeyName, 0, KEY_READ, &hkeyApp);
        size = sizeof(value);
        if (!RegQueryValueExW(hkeyApp, SystemComponentW, NULL, &dwType, (LPBYTE)&value, &size)
            && dwType == REG_DWORD && value == 1)
        {
            RegCloseKey(hkeyApp);
            sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);
            continue;
        }
        displen = 0;
        uninstlen = 0;
        if (!RegQueryValueExW(hkeyApp, DisplayNameW, 0, 0, NULL, &displen))
        {
            size = sizeof(value);
            if (!RegQueryValueExW(hkeyApp, WindowsInstallerW, NULL, &dwType, (LPBYTE)&value, &size)
                && dwType == REG_DWORD && value == 1)
            {
                static const WCHAR fmtW[] = {'m','s','i','e','x','e','c',' ','/','x','%','s',0};
                int len = lstrlenW(fmtW) + lstrlenW(subKeyName);

                if (!(command = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR)))) goto err;
                wsprintfW(command, fmtW, subKeyName);
            }
            else if (!RegQueryValueExW(hkeyApp, UninstallCommandlineW, 0, 0, NULL, &uninstlen))
            {
                if (!(command = HeapAlloc(GetProcessHeap(), 0, uninstlen))) goto err;
                RegQueryValueExW(hkeyApp, UninstallCommandlineW, 0, 0, (LPBYTE)command, &uninstlen);
            }
            else
            {
                RegCloseKey(hkeyApp);
                sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);
                continue;
            }

            info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct APPINFO));
            if (!info) goto err;

            info->title = HeapAlloc(GetProcessHeap(), 0, displen);

            if (!info->title)
                goto err;

            RegQueryValueExW(hkeyApp, DisplayNameW, 0, 0, (LPBYTE)info->title,
                &displen);

            /* now get DisplayIcon */
            displen = 0;
            RegQueryValueExW(hkeyApp, DisplayIconW, 0, 0, NULL, &displen);

            if (displen == 0)
                info->icon = 0;
            else
            {
                info->icon = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!info->icon)
                    goto err;

                RegQueryValueExW(hkeyApp, DisplayIconW, 0, 0, (LPBYTE)info->icon,
                    &displen);

                /* separate the index from the icon name, if supplied */
                iconPtr = strchrW(info->icon, ',');

                if (iconPtr)
                {
                    *iconPtr++ = 0;
                    info->iconIdx = atoiW(iconPtr);
                }
            }

            /* publisher, version */
            if (RegQueryValueExW(hkeyApp, PublisherW, 0, 0, NULL, &displen) ==
                ERROR_SUCCESS)
            {
                info->publisher = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!info->publisher)
                    goto err;

                RegQueryValueExW(hkeyApp, PublisherW, 0, 0, (LPBYTE)info->publisher,
                    &displen);
            }

            if (RegQueryValueExW(hkeyApp, DisplayVersionW, 0, 0, NULL, &displen) ==
                ERROR_SUCCESS)
            {
                info->version = HeapAlloc(GetProcessHeap(), 0, displen);

                if (!info->version)
                    goto err;

                RegQueryValueExW(hkeyApp, DisplayVersionW, 0, 0, (LPBYTE)info->version,
                    &displen);
            }

            /* Check if NoModify is set */
            dwType = REG_DWORD;
            dwNoModify = 0;
            displen = sizeof(DWORD);

            if (RegQueryValueExW(hkeyApp, NoModifyW, NULL, &dwType, (LPBYTE)&dwNoModify, &displen)
                != ERROR_SUCCESS)
            {
                dwNoModify = 0;
            }

            /* Some installers incorrectly create a REG_SZ instead of a REG_DWORD */
            if (dwType == REG_SZ)
                dwNoModify = (*(BYTE *)&dwNoModify == '1');

            /* Fetch the modify path */
            if (!dwNoModify)
            {
                size = sizeof(value);
                if (!RegQueryValueExW(hkeyApp, WindowsInstallerW, NULL, &dwType, (LPBYTE)&value, &size)
                    && dwType == REG_DWORD && value == 1)
                {
                    static const WCHAR fmtW[] = {'m','s','i','e','x','e','c',' ','/','i','%','s',0};
                    int len = lstrlenW(fmtW) + lstrlenW(subKeyName);

                    if (!(info->path_modify = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR)))) goto err;
                    wsprintfW(info->path_modify, fmtW, subKeyName);
                }
                else if (!RegQueryValueExW(hkeyApp, ModifyPathW, 0, 0, NULL, &displen))
                {
                    if (!(info->path_modify = HeapAlloc(GetProcessHeap(), 0, displen))) goto err;
                    RegQueryValueExW(hkeyApp, ModifyPathW, 0, 0, (LPBYTE)info->path_modify, &displen);
                }
            }

            /* registry key */
            info->regroot = root;
            lstrcpyW(info->regkey, subKeyName);
            info->path = command;

            info->id = id++;
            list_add_tail( &app_list, &info->entry );
        }

        RegCloseKey(hkeyApp);
        sizeOfSubKeyName = sizeof(subKeyName) / sizeof(subKeyName[0]);
    }

    return TRUE;
err:
    RegCloseKey(hkeyApp);
    if (info) FreeAppInfo(info);
    HeapFree(GetProcessHeap(), 0, command);
    return FALSE;
}


/******************************************************************************
 * Name       : AddApplicationsToList
 * Description: Populates the list box with applications.
 * Parameters : hWnd    - Handle of the dialog box
 */
static void AddApplicationsToList(HWND hWnd, HIMAGELIST hList)
{
    APPINFO *iter;
    LVITEMW lvItem;
    HICON hIcon;
    int index;

    LIST_FOR_EACH_ENTRY( iter, &app_list, APPINFO, entry )
    {
        if (!iter->title[0]) continue;

        /* get the icon */
        index = 0;

        if (iter->icon)
        {
            if (ExtractIconExW(iter->icon, iter->iconIdx, NULL, &hIcon, 1) == 1)
            {
                index = ImageList_AddIcon(hList, hIcon);
                DestroyIcon(hIcon);
            }
        }

        lvItem.mask = LVIF_IMAGE | LVIF_TEXT | LVIF_PARAM;
        lvItem.iItem = iter->id;
        lvItem.iSubItem = 0;
        lvItem.pszText = iter->title;
        lvItem.iImage = index;
        lvItem.lParam = iter->id;

        index = ListView_InsertItemW(hWnd, &lvItem);

        /* now add the subitems (columns) */
        ListView_SetItemTextW(hWnd, index, 1, iter->publisher);
        ListView_SetItemTextW(hWnd, index, 2, iter->version);
    }
}

/******************************************************************************
 * Name       : RemoveItemsFromList
 * Description: Clears the application list box.
 * Parameters : hWnd    - Handle of the dialog box
 */
static void RemoveItemsFromList(HWND hWnd)
{
    SendDlgItemMessageW(hWnd, IDL_PROGRAMS, LVM_DELETEALLITEMS, 0, 0);
}

/******************************************************************************
 * Name       : EmptyList
 * Description: Frees memory used by the application linked list.
 */
static inline void EmptyList(void)
{
    APPINFO *info, *next;
    LIST_FOR_EACH_ENTRY_SAFE( info, next, &app_list, APPINFO, entry )
    {
        list_remove( &info->entry );
        FreeAppInfo( info );
    }
}

/******************************************************************************
 * Name       : UpdateButtons
 * Description: Enables/disables the Add/Remove button depending on current
 *              selection in list box.
 * Parameters : hWnd    - Handle of the dialog box
 */
static void UpdateButtons(HWND hWnd)
{
    APPINFO *iter;
    LVITEMW lvItem;
    LRESULT selitem = SendDlgItemMessageW(hWnd, IDL_PROGRAMS, LVM_GETNEXTITEM, -1,
       LVNI_FOCUSED | LVNI_SELECTED);
    BOOL enable_modify = FALSE;

    if (selitem != -1)
    {
        lvItem.iItem = selitem;
        lvItem.mask = LVIF_PARAM;

        if (SendDlgItemMessageW(hWnd, IDL_PROGRAMS, LVM_GETITEMW, 0, (LPARAM) &lvItem))
        {
            LIST_FOR_EACH_ENTRY( iter, &app_list, APPINFO, entry )
            {
                if (iter->id == lvItem.lParam)
                {
                    /* Decide whether to display Modify/Remove as one button or two */
                    enable_modify = (iter->path_modify != NULL);

                    /* Update title as appropriate */
                    if (iter->path_modify == NULL)
                        SetWindowTextW(GetDlgItem(hWnd, IDC_ADDREMOVE), btnModifyRemove);
                    else
                        SetWindowTextW(GetDlgItem(hWnd, IDC_ADDREMOVE), btnRemove);

                    break;
                }
            }
        }
    }

    /* Enable/disable other buttons if necessary */
    EnableWindow(GetDlgItem(hWnd, IDC_ADDREMOVE), (selitem != -1));
    EnableWindow(GetDlgItem(hWnd, IDC_SUPPORT_INFO), (selitem != -1));
    EnableWindow(GetDlgItem(hWnd, IDC_MODIFY), enable_modify);
}

/******************************************************************************
 * Name       : InstallProgram
 * Description: Search for potential Installer and execute it.
 * Parameters : hWnd    - Handle of the dialog box
 */
static void InstallProgram(HWND hWnd)
{
    static const WCHAR filters[] = {'%','s','%','c','*','i','n','s','t','a','l','*','.','e','x','e',';','*','s','e','t','u','p','*','.','e','x','e',';','*','.','m','s','i','%','c','%','s','%','c','*','.','e','x','e','%','c','%','s','%','c','*','.','*','%','c',0}
;
    OPENFILENAMEW ofn;
    WCHAR titleW[MAX_STRING_LEN];
    WCHAR filter_installs[MAX_STRING_LEN];
    WCHAR filter_programs[MAX_STRING_LEN];
    WCHAR filter_all[MAX_STRING_LEN];
    WCHAR FilterBufferW[MAX_PATH];
    WCHAR FileNameBufferW[MAX_PATH];

    LoadStringW(hInst, IDS_CPL_TITLE, titleW, sizeof(titleW)/sizeof(WCHAR));
    LoadStringW(hInst, IDS_FILTER_INSTALLS, filter_installs, sizeof(filter_installs)/sizeof(WCHAR));
    LoadStringW(hInst, IDS_FILTER_PROGRAMS, filter_programs, sizeof(filter_programs)/sizeof(WCHAR));
    LoadStringW(hInst, IDS_FILTER_ALL, filter_all, sizeof(filter_all)/sizeof(WCHAR));

    snprintfW( FilterBufferW, MAX_PATH, filters, filter_installs, 0, 0,
               filter_programs, 0, 0, filter_all, 0, 0 );
    memset(&ofn, 0, sizeof(OPENFILENAMEW));
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hWnd;
    ofn.hInstance = hInst;
    ofn.lpstrFilter = FilterBufferW;
    ofn.nFilterIndex = 0;
    ofn.lpstrFile = FileNameBufferW;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrTitle = titleW;
    ofn.Flags = OFN_HIDEREADONLY | OFN_ENABLESIZING;
    FileNameBufferW[0] = 0;

    if (GetOpenFileNameW(&ofn))
    {
        SHELLEXECUTEINFOW sei;
        memset(&sei, 0, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.lpVerb = openW;
        sei.nShow = SW_SHOWDEFAULT;
        sei.fMask = SEE_MASK_NO_CONSOLE;
        sei.lpFile = ofn.lpstrFile;

        ShellExecuteExW(&sei);
    }
}

/******************************************************************************
 * Name       : UninstallProgram
 * Description: Executes the specified program's installer.
 * Parameters : id      - the internal ID of the installer to remove
 * Parameters : button  - ID of button pressed (Modify or Remove)
 */
static void UninstallProgram(int id, DWORD button)
{
    APPINFO *iter;
    STARTUPINFOW si;
    PROCESS_INFORMATION info;
    WCHAR errormsg[MAX_STRING_LEN];
    WCHAR sUninstallFailed[MAX_STRING_LEN];
    HKEY hkey;
    BOOL res;

    LoadStringW(hInst, IDS_UNINSTALL_FAILED, sUninstallFailed,
        sizeof(sUninstallFailed) / sizeof(sUninstallFailed[0]));

    LIST_FOR_EACH_ENTRY( iter, &app_list, APPINFO, entry )
    {
        if (iter->id == id)
        {
            TRACE("Uninstalling %s (%s)\n", wine_dbgstr_w(iter->title),
                wine_dbgstr_w(iter->path));

            memset(&si, 0, sizeof(STARTUPINFOW));
            si.cb = sizeof(STARTUPINFOW);
            si.wShowWindow = SW_NORMAL;

            res = CreateProcessW(NULL, (button == IDC_MODIFY) ? iter->path_modify : iter->path,
                NULL, NULL, FALSE, 0, NULL, NULL, &si, &info);

            if (res)
            {
                CloseHandle(info.hThread);

                /* wait for the process to exit */
                WaitForSingleObject(info.hProcess, INFINITE);
                CloseHandle(info.hProcess);
            }
            else
            {
                wsprintfW(errormsg, sUninstallFailed, iter->path);

                if (MessageBoxW(0, errormsg, iter->title, MB_YESNO |
                    MB_ICONQUESTION) == IDYES)
                {
                    /* delete the application's uninstall entry */
                    RegOpenKeyExW(iter->regroot, PathUninstallW, 0, KEY_READ, &hkey);
                    RegDeleteKeyW(hkey, iter->regkey);
                    RegCloseKey(hkey);
                }
            }

            break;
        }
    }
}

/**********************************************************************************
 * Name       : SetInfoDialogText
 * Description: Sets the text of a label in a window, based upon a registry entry
 *              or string passed to the function.
 * Parameters : hKey         - registry entry to read from, NULL if not reading
 *                             from registry
 *              lpKeyName    - key to read from, or string to check if hKey is NULL
 *              lpAltMessage - alternative message if entry not found
 *              hWnd         - handle of dialog box
 *              iDlgItem     - ID of label in dialog box
 */
static void SetInfoDialogText(HKEY hKey, LPCWSTR lpKeyName, LPCWSTR lpAltMessage,
  HWND hWnd, int iDlgItem)
{
    WCHAR buf[MAX_STRING_LEN];
    DWORD buflen;
    HWND hWndDlgItem;

    hWndDlgItem = GetDlgItem(hWnd, iDlgItem);

    /* if hKey is null, lpKeyName contains the string we want to check */
    if (hKey == NULL)
    {
        if ((lpKeyName) && (lstrlenW(lpKeyName) > 0))
            SetWindowTextW(hWndDlgItem, lpKeyName);
        else
            SetWindowTextW(hWndDlgItem, lpAltMessage);
    }
    else
    {
        buflen = MAX_STRING_LEN;

        if ((RegQueryValueExW(hKey, lpKeyName, 0, 0, (LPBYTE) buf, &buflen) ==
           ERROR_SUCCESS) && (lstrlenW(buf) > 0))
            SetWindowTextW(hWndDlgItem, buf);
        else
            SetWindowTextW(hWndDlgItem, lpAltMessage);
    }
}

/******************************************************************************
 * Name       : SupportInfoDlgProc
 * Description: Callback procedure for support info dialog
 * Parameters : hWnd    - hWnd of the window
 *              msg     - reason for calling function
 *              wParam  - additional parameter
 *              lParam  - additional parameter
 * Returns    : Depends on the message
 */
static INT_PTR CALLBACK SupportInfoDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    APPINFO *iter;
    HKEY hkey;
    WCHAR oldtitle[MAX_STRING_LEN];
    WCHAR buf[MAX_STRING_LEN];
    WCHAR key[MAX_STRING_LEN];
    WCHAR notfound[MAX_STRING_LEN];

    switch(msg)
    {
        case WM_INITDIALOG:
            LIST_FOR_EACH_ENTRY( iter, &app_list, APPINFO, entry )
            {
                if (iter->id == (int) lParam)
                {
                    lstrcpyW(key, PathUninstallW);
                    lstrcatW(key, BackSlashW);
                    lstrcatW(key, iter->regkey);

                    /* check the application's registry entries */
                    RegOpenKeyExW(iter->regroot, key, 0, KEY_READ, &hkey);

                    /* Load our "not specified" string */
                    LoadStringW(hInst, IDS_NOT_SPECIFIED, notfound,
                        sizeof(notfound) / sizeof(notfound[0]));

                    /* Update the data for items already read into the structure */
                    SetInfoDialogText(NULL, iter->publisher, notfound, hWnd,
                        IDC_INFO_PUBLISHER);
                    SetInfoDialogText(NULL, iter->version, notfound, hWnd,
                        IDC_INFO_VERSION);

                    /* And now update the data for those items in the registry */
                    SetInfoDialogText(hkey, ContactW, notfound, hWnd,
                        IDC_INFO_CONTACT);
                    SetInfoDialogText(hkey, HelpLinkW, notfound, hWnd,
                        IDC_INFO_SUPPORT);
                    SetInfoDialogText(hkey, HelpTelephoneW, notfound, hWnd,
                        IDC_INFO_PHONE);
                    SetInfoDialogText(hkey, ReadmeW, notfound, hWnd,
                        IDC_INFO_README);
                    SetInfoDialogText(hkey, URLUpdateInfoW, notfound, hWnd,
                        IDC_INFO_UPDATES);
                    SetInfoDialogText(hkey, CommentsW, notfound, hWnd,
                        IDC_INFO_COMMENTS);

                    /* Update the main label with the app name */
                    if (GetWindowTextW(GetDlgItem(hWnd, IDC_INFO_LABEL), oldtitle,
                        MAX_STRING_LEN) != 0)
                    {
                        wsprintfW(buf, oldtitle, iter->title);
                        SetWindowTextW(GetDlgItem(hWnd, IDC_INFO_LABEL), buf);
                    }

                    RegCloseKey(hkey);

                    break;
                }
            }

            return TRUE;

        case WM_DESTROY:
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    EndDialog(hWnd, TRUE);
                    break;

            }

            return TRUE;
    }

    return FALSE;
}

/******************************************************************************
 * Name       : SupportInfo
 * Description: Displays the Support Information dialog
 * Parameters : hWnd    - Handle of the main dialog
 *              id      - ID of the application to display information for
 */
static void SupportInfo(HWND hWnd, int id)
{
    DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_INFO), hWnd, SupportInfoDlgProc, id);
}

/* Definition of column headers for AddListViewColumns function */
typedef struct AppWizColumn {
   int width;
   int fmt;
   int title;
} AppWizColumn;

static const AppWizColumn columns[] = {
    {200, LVCFMT_LEFT, IDS_COLUMN_NAME},
    {150, LVCFMT_LEFT, IDS_COLUMN_PUBLISHER},
    {100, LVCFMT_LEFT, IDS_COLUMN_VERSION},
};

/******************************************************************************
 * Name       : AddListViewColumns
 * Description: Adds column headers to the list view control.
 * Parameters : hWnd    - Handle of the list view control.
 * Returns    : TRUE if completed successfully, FALSE otherwise.
 */
static BOOL AddListViewColumns(HWND hWnd)
{
    WCHAR buf[MAX_STRING_LEN];
    LVCOLUMNW lvc;
    UINT i;

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;

    /* Add the columns */
    for (i = 0; i < sizeof(columns) / sizeof(columns[0]); i++)
    {
        lvc.iSubItem = i;
        lvc.pszText = buf;

        /* set width and format */
        lvc.cx = columns[i].width;
        lvc.fmt = columns[i].fmt;

        LoadStringW(hInst, columns[i].title, buf, sizeof(buf) / sizeof(buf[0]));

        if (ListView_InsertColumnW(hWnd, i, &lvc) == -1)
            return FALSE;
    }

    return TRUE;
}

/******************************************************************************
 * Name       : AddListViewImageList
 * Description: Creates an ImageList for the list view control.
 * Parameters : hWnd    - Handle of the list view control.
 * Returns    : Handle of the image list.
 */
static HIMAGELIST AddListViewImageList(HWND hWnd)
{
    HIMAGELIST hSmall;
    HICON hDefaultIcon;

    hSmall = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                              ILC_COLOR32 | ILC_MASK, 1, 1);

    /* Add default icon to image list */
    hDefaultIcon = LoadIconW(hInst, MAKEINTRESOURCEW(ICO_MAIN));
    ImageList_AddIcon(hSmall, hDefaultIcon);
    DestroyIcon(hDefaultIcon);

    SendMessageW(hWnd, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)hSmall);

    return hSmall;
}

/******************************************************************************
 * Name       : ResetApplicationList
 * Description: Empties the app list, if need be, and recreates it.
 * Parameters : bFirstRun  - TRUE if this is the first time this is run, FALSE otherwise
 *              hWnd       - handle of the dialog box
 *              hImageList - handle of the image list
 * Returns    : New handle of the image list.
 */
static HIMAGELIST ResetApplicationList(BOOL bFirstRun, HWND hWnd, HIMAGELIST hImageList)
{
    static const BOOL is_64bit = sizeof(void *) > sizeof(int);
    HWND hWndListView;
    HKEY hkey;

    hWndListView = GetDlgItem(hWnd, IDL_PROGRAMS);

    /* if first run, create the image list and add the listview columns */
    if (bFirstRun)
    {
        if (!AddListViewColumns(hWndListView))
            return NULL;
    }
    else /* we need to remove the existing things first */
    {
        RemoveItemsFromList(hWnd);
        ImageList_Destroy(hImageList);

        /* reset the list, since it's probably changed if the uninstallation was
           successful */
        EmptyList();
    }

    /* now create the image list and add the applications to the listview */
    hImageList = AddListViewImageList(hWndListView);

    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, PathUninstallW, 0, KEY_READ, &hkey))
    {
        ReadApplicationsFromRegistry(hkey);
        RegCloseKey(hkey);
    }
    if (is_64bit &&
        !RegOpenKeyExW(HKEY_LOCAL_MACHINE, PathUninstallW, 0, KEY_READ|KEY_WOW64_32KEY, &hkey))
    {
        ReadApplicationsFromRegistry(hkey);
        RegCloseKey(hkey);
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, PathUninstallW, 0, KEY_READ, &hkey))
    {
        ReadApplicationsFromRegistry(hkey);
        RegCloseKey(hkey);
    }

    AddApplicationsToList(hWndListView, hImageList);
    UpdateButtons(hWnd);

    return(hImageList);
}

/******************************************************************************
 * Name       : MainDlgProc
 * Description: Callback procedure for main tab
 * Parameters : hWnd    - hWnd of the window
 *              msg     - reason for calling function
 *              wParam  - additional parameter
 *              lParam  - additional parameter
 * Returns    : Depends on the message
 */
static INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int selitem;
    static HIMAGELIST hImageList;
    LPNMHDR nmh;
    LVITEMW lvItem;

    switch(msg)
    {
        case WM_INITDIALOG:
            hImageList = ResetApplicationList(TRUE, hWnd, hImageList);

            if (!hImageList)
                return FALSE;

            return TRUE;

        case WM_DESTROY:
            RemoveItemsFromList(hWnd);
            ImageList_Destroy(hImageList);

            EmptyList();

            return 0;

        case WM_NOTIFY:
            nmh = (LPNMHDR) lParam;

            switch (nmh->idFrom)
            {
                case IDL_PROGRAMS:
                    switch (nmh->code)
                    {
                        case LVN_ITEMCHANGED:
                            UpdateButtons(hWnd);
                            break;
                    }
                    break;
            }

            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_INSTALL:
                    InstallProgram(hWnd);
                    break;

                case IDC_ADDREMOVE:
                case IDC_MODIFY:
                    selitem = SendDlgItemMessageW(hWnd, IDL_PROGRAMS,
                        LVM_GETNEXTITEM, -1, LVNI_FOCUSED|LVNI_SELECTED);

                    if (selitem != -1)
                    {
                        lvItem.iItem = selitem;
                        lvItem.mask = LVIF_PARAM;

                        if (SendDlgItemMessageW(hWnd, IDL_PROGRAMS, LVM_GETITEMW,
                          0, (LPARAM) &lvItem))
                            UninstallProgram(lvItem.lParam, LOWORD(wParam));
                    }

                    hImageList = ResetApplicationList(FALSE, hWnd, hImageList);

                    break;

                case IDC_SUPPORT_INFO:
                    selitem = SendDlgItemMessageW(hWnd, IDL_PROGRAMS,
                        LVM_GETNEXTITEM, -1, LVNI_FOCUSED | LVNI_SELECTED);

                    if (selitem != -1)
                    {
                        lvItem.iItem = selitem;
                        lvItem.mask = LVIF_PARAM;

                        if (SendDlgItemMessageW(hWnd, IDL_PROGRAMS, LVM_GETITEMW,
                          0, (LPARAM) &lvItem))
                            SupportInfo(hWnd, lvItem.lParam);
                    }

                    break;
            }

            return TRUE;
    }

    return FALSE;
}

static int CALLBACK propsheet_callback( HWND hwnd, UINT msg, LPARAM lparam )
{
    switch (msg)
    {
    case PSCB_INITIALIZED:
        SendMessageW( hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW( hInst, MAKEINTRESOURCEW(ICO_MAIN) ));
        break;
    }
    return 0;
}

/******************************************************************************
 * Name       : StartApplet
 * Description: Main routine for applet
 * Parameters : hWnd    - hWnd of the Control Panel
 */
static void StartApplet(HWND hWnd)
{
    PROPSHEETPAGEW psp;
    PROPSHEETHEADERW psh;
    WCHAR tab_title[MAX_STRING_LEN], app_title[MAX_STRING_LEN];

    /* Load the strings we will use */
    LoadStringW(hInst, IDS_TAB1_TITLE, tab_title, sizeof(tab_title) / sizeof(tab_title[0]));
    LoadStringW(hInst, IDS_CPL_TITLE, app_title, sizeof(app_title) / sizeof(app_title[0]));
    LoadStringW(hInst, IDS_REMOVE, btnRemove, sizeof(btnRemove) / sizeof(btnRemove[0]));
    LoadStringW(hInst, IDS_MODIFY_REMOVE, btnModifyRemove, sizeof(btnModifyRemove) / sizeof(btnModifyRemove[0]));

    /* Fill out the PROPSHEETPAGE */
    psp.dwSize = sizeof (PROPSHEETPAGEW);
    psp.dwFlags = PSP_USETITLE;
    psp.hInstance = hInst;
    psp.u.pszTemplate = MAKEINTRESOURCEW (IDD_MAIN);
    psp.u2.pszIcon = NULL;
    psp.pfnDlgProc = MainDlgProc;
    psp.pszTitle = tab_title;
    psp.lParam = 0;

    /* Fill out the PROPSHEETHEADER */
    psh.dwSize = sizeof (PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_USECALLBACK;
    psh.hwndParent = hWnd;
    psh.hInstance = hInst;
    psh.u.pszIcon = MAKEINTRESOURCEW(ICO_MAIN);
    psh.pszCaption = app_title;
    psh.nPages = 1;
    psh.u3.ppsp = &psp;
    psh.pfnCallback = propsheet_callback;
    psh.u2.nStartPage = 0;

    /* Display the property sheet */
    PropertySheetW (&psh);
}

static LONG start_params(const WCHAR *params)
{
    static const WCHAR install_geckoW[] = {'i','n','s','t','a','l','l','_','g','e','c','k','o',0};
    static const WCHAR install_monoW[] = {'i','n','s','t','a','l','l','_','m','o','n','o',0};

    if(!params)
        return FALSE;

    if(!strcmpW(params, install_geckoW)) {
        install_addon(ADDON_GECKO);
        return TRUE;
    }

    if(!strcmpW(params, install_monoW)) {
        install_addon(ADDON_MONO);
        return TRUE;
    }

    WARN("unknown param %s\n", debugstr_w(params));
    return FALSE;
}

/******************************************************************************
 * Name       : CPlApplet
 * Description: Entry point for Control Panel applets
 * Parameters : hwndCPL - hWnd of the Control Panel
 *              message - reason for calling function
 *              lParam1 - additional parameter
 *              lParam2 - additional parameter
 * Returns    : Depends on the message
 */
LONG CALLBACK CPlApplet(HWND hwndCPL, UINT message, LPARAM lParam1, LPARAM lParam2)
{
    INITCOMMONCONTROLSEX iccEx;

    switch (message)
    {
        case CPL_INIT:
            iccEx.dwSize = sizeof(iccEx);
            iccEx.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_LINK_CLASS;

            InitCommonControlsEx(&iccEx);

            return TRUE;

        case CPL_GETCOUNT:
            return 1;

        case CPL_STARTWPARMSW:
            return start_params((const WCHAR *)lParam2);

        case CPL_INQUIRE:
        {
            CPLINFO *appletInfo = (CPLINFO *) lParam2;

            appletInfo->idIcon = ICO_MAIN;
            appletInfo->idName = IDS_CPL_TITLE;
            appletInfo->idInfo = IDS_CPL_DESC;
            appletInfo->lData = 0;

            break;
        }

        case CPL_DBLCLK:
            StartApplet(hwndCPL);
            break;
    }

    return FALSE;
}
