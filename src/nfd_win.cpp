/*
  Native File Dialog

  http://www.frogtoss.com/labs
 */

#define MODULE_API_EXPORTS

#ifdef __MINGW32__
// Explicitly setting NTDDI version, this is necessary for the MinGW compiler
#define NTDDI_VERSION NTDDI_VISTA
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

/* only locally define UNICODE in this compilation unit */
#ifndef UNICODE
#define UNICODE
#endif

#if _MSC_VER
// see
// https://developercommunity.visualstudio.com/content/problem/185399/error-c2760-in-combaseapih-with-windows-sdk-81-and.html
struct IUnknown;  // Workaround for "combaseapi.h(229): error C2187: syntax error: 'identifier' was
// unexpected here" when using /permissive-
#endif

#include <wchar.h>
#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include <shobjidl.h>
#include "nfd_common.h"

#include <ShellScalingApi.h>

#include "common.h"
#pragma comment(lib, "Shcore.lib")


#define COM_INITFLAGS ::COINIT_APARTMENTTHREADED | ::COINIT_DISABLE_OLE1DDE

#define SET_DPI_AWARENESS() \
do { \
HMODULE hUser32 = LoadLibraryW(L"user32.dll"); \
if (hUser32) \
{ \
typedef BOOL(WINAPI* SetProcessDpiAwarenessContextPtr)(DPI_AWARENESS_CONTEXT); \
void* procAddr = (void*)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"); \
SetProcessDpiAwarenessContextPtr setProcessDpiAwarenessContext = \
(SetProcessDpiAwarenessContextPtr)(procAddr); \
if (setProcessDpiAwarenessContext) \
{ \
setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); \
} \
FreeLibrary(hUser32); \
} \
} while(0)



static BOOL COMIsInitialized(HRESULT coResult)
{
    if (coResult == RPC_E_CHANGED_MODE)
    {
        // If COM was previously initialized with different init flags,
        // NFD still needs to operate. Eat this warning.
        return TRUE;
    }

    return SUCCEEDED(coResult);
}

static HRESULT COMInit(void)
{
    return ::CoInitializeEx(nullptr, COM_INITFLAGS);
}

static void COMUninit(HRESULT coResult)
{
    // do not uninitialize if RPC_E_CHANGED_MODE occurred -- this
    // case does not refcount COM.
    if (SUCCEEDED(coResult))
        ::CoUninitialize();
}

// allocs the space in outPath -- call free()
static void CopyWCharToNFDChar( const wchar_t *inStr, nfdchar_t **outStr )
{
    int inStrCharacterCount = static_cast<int>(wcslen(inStr));
    int bytesNeeded = WideCharToMultiByte( CP_UTF8, 0,
                                           inStr, inStrCharacterCount,
                                           nullptr, 0, nullptr, nullptr);
    assert( bytesNeeded );
    bytesNeeded += 1;

    *outStr = (nfdchar_t*)NFDi_Malloc( bytesNeeded );
    if ( !*outStr )
        return;

    int bytesWritten = WideCharToMultiByte( CP_UTF8, 0,
                                            inStr, -1,
                                            *outStr, bytesNeeded,
                                            nullptr, nullptr);
    assert( bytesWritten ); _NFD_UNUSED( bytesWritten );
}

/* includes NULL terminator byte in return */
static size_t GetUTF8ByteCountForWChar( const wchar_t *str )
{
    size_t bytesNeeded = WideCharToMultiByte( CP_UTF8, 0,
                                              str, -1,
                                              nullptr, 0, nullptr, nullptr);
    assert( bytesNeeded );
    return bytesNeeded+1;
}

// write to outPtr -- no free() necessary.
static int CopyWCharToExistingNFDCharBuffer( const wchar_t *inStr, nfdchar_t *outPtr )
{
    int bytesNeeded = static_cast<int>(GetUTF8ByteCountForWChar( inStr ));

    /* invocation copies null term */
    int bytesWritten = WideCharToMultiByte( CP_UTF8, 0,
                                            inStr, -1,
                                            outPtr, bytesNeeded,
                                            nullptr, nullptr );
    assert( bytesWritten );

    return bytesWritten;

}


// allocs the space in outStr -- call free()
static void CopyNFDCharToWChar( const nfdchar_t *inStr, wchar_t **outStr )
{
    int inStrByteCount = static_cast<int>(strlen(inStr));
    int charsNeeded = MultiByteToWideChar(CP_UTF8, 0,
                                          inStr, inStrByteCount,
                                          nullptr, 0 );
    assert( charsNeeded );
    assert( !*outStr );
    charsNeeded += 1; // terminator

    *outStr = (wchar_t*)NFDi_Malloc( charsNeeded * sizeof(wchar_t) );
    if ( !*outStr )
        return;

    int ret = MultiByteToWideChar(CP_UTF8, 0,
                                  inStr, inStrByteCount,
                                  *outStr, charsNeeded);
    (*outStr)[charsNeeded-1] = '\0';

#ifdef _DEBUG
    int inStrCharacterCount = static_cast<int>(NFDi_UTF8_Strlen(inStr));
    assert( ret == inStrCharacterCount );
#else
    _NFD_UNUSED(ret);
#endif
}

static nfdresult_t AddFiltersToDialog(::IFileDialog *fileOpenDialog, const char *filterList)
{
    const wchar_t WILDCARD[] = L"*.*";
    NFDFilterList filters;

    // Použijeme společnou funkci pro parsování filtrů
    if (!ParseFilterList(filterList, &filters)) {
        return NFD_ERROR;
    }

    // Pokud nejsou žádné filtry, končíme
    if (filters.count == 0) {
        return NFD_OKAY;
    }

    // Alokujeme pole pro COMDLG_FILTERSPEC (včetně wildcard filtru)
    COMDLG_FILTERSPEC *specList = (COMDLG_FILTERSPEC*)NFDi_Malloc(
        sizeof(COMDLG_FILTERSPEC) * (filters.count + 1));

    if (!specList) {
        NFDi_Free(filters.filters);
        NFDi_SetError("Memory allocation failed.");
        return NFD_ERROR;
    }

    memset(specList, 0, sizeof(COMDLG_FILTERSPEC) * (filters.count + 1));

    // Převedeme filtry do Windows formátu
    for (size_t i = 0; i < filters.count; ++i) {
        CopyNFDCharToWChar(filters.filters[i].name,
                          (wchar_t**)&specList[i].pszName);
        CopyNFDCharToWChar(filters.filters[i].pattern,
                          (wchar_t**)&specList[i].pszSpec);

        // Kontrola, zda se konverze povedla
        if (!specList[i].pszName || !specList[i].pszSpec) {
            // Cleanup při chybě
            for (size_t j = 0; j < i; ++j) {
                NFDi_Free((void*)specList[j].pszName);
                NFDi_Free((void*)specList[j].pszSpec);
            }
            NFDi_Free(specList);
            NFDi_Free(filters.filters);
            NFDi_SetError("Error converting filter to wide string.");
            return NFD_ERROR;
        }
    }

    // Přidáme wildcard filtr
    specList[filters.count].pszSpec = WILDCARD;
    specList[filters.count].pszName = WILDCARD;

    // Nastavíme filtry v dialogu
    fileOpenDialog->SetFileTypes(filters.count + 1, specList);

    // Cleanup
    for (size_t i = 0; i < filters.count; ++i) {
        NFDi_Free((void*)specList[i].pszName);
        NFDi_Free((void*)specList[i].pszSpec);
    }
    NFDi_Free(specList);
    NFDi_Free(filters.filters);

    return NFD_OKAY;
}

static nfdresult_t AllocPathSet( IShellItemArray *shellItems, nfdpathset_t *pathSet )
{
    const char ERRORMSG[] = "Error allocating pathset.";

    assert(shellItems);
    assert(pathSet);

    // How many items in shellItems?
    DWORD numShellItems;
    HRESULT result = shellItems->GetCount(&numShellItems);
    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError(ERRORMSG);
        return NFD_ERROR;
    }

    pathSet->count = static_cast<size_t>(numShellItems);
    assert( pathSet->count > 0 );

    pathSet->indices = (size_t*)NFDi_Malloc( sizeof(size_t)*pathSet->count );
    if ( !pathSet->indices )
    {
        return NFD_ERROR;
    }

    /* count the total bytes needed for buf */
    size_t bufSize = 0;
    for ( DWORD i = 0; i < numShellItems; ++i )
    {
        ::IShellItem *shellItem;
        result = shellItems->GetItemAt(i, &shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError(ERRORMSG);
            return NFD_ERROR;
        }

        // Confirm SFGAO_FILESYSTEM is true for this shellitem, or ignore it.
        SFGAOF attribs;
        result = shellItem->GetAttributes( SFGAO_FILESYSTEM, &attribs );
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError(ERRORMSG);
            return NFD_ERROR;
        }
        if ( !(attribs & SFGAO_FILESYSTEM) )
            continue;

        LPWSTR name;
        shellItem->GetDisplayName(SIGDN_FILESYSPATH, &name);

        // Calculate length of name with UTF-8 encoding
        bufSize += GetUTF8ByteCountForWChar( name );

        CoTaskMemFree(name);
    }

    assert(bufSize);

    pathSet->buf = (nfdchar_t*)NFDi_Malloc( sizeof(nfdchar_t) * bufSize );
    memset( pathSet->buf, 0, sizeof(nfdchar_t) * bufSize );

    /* fill buf */
    nfdchar_t *p_buf = pathSet->buf;
    for (DWORD i = 0; i < numShellItems; ++i )
    {
        ::IShellItem *shellItem;
        result = shellItems->GetItemAt(i, &shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError(ERRORMSG);
            return NFD_ERROR;
        }

        // Confirm SFGAO_FILESYSTEM is true for this shellitem, or ignore it.
        SFGAOF attribs;
        result = shellItem->GetAttributes( SFGAO_FILESYSTEM, &attribs );
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError(ERRORMSG);
            return NFD_ERROR;
        }
        if ( !(attribs & SFGAO_FILESYSTEM) )
            continue;

        LPWSTR name;
        shellItem->GetDisplayName(SIGDN_FILESYSPATH, &name);

        int bytesWritten = CopyWCharToExistingNFDCharBuffer(name, p_buf);
        CoTaskMemFree(name);

        ptrdiff_t index = p_buf - pathSet->buf;
        assert( index >= 0 );
        pathSet->indices[i] = static_cast<size_t>(index);

        p_buf += bytesWritten;
    }

    return NFD_OKAY;
}


static nfdresult_t SetDefaultPath( IFileDialog *dialog, const char *defaultPath )
{
    if ( !defaultPath || strlen(defaultPath) == 0 )
        return NFD_OKAY;

    wchar_t *defaultPathW = {nullptr};
    CopyNFDCharToWChar( defaultPath, &defaultPathW );

    IShellItem *folder;
    HRESULT result = SHCreateItemFromParsingName( defaultPathW, nullptr, IID_PPV_ARGS(&folder) );

    // Valid non results.
    if ( result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_INVALID_DRIVE) )
    {
        NFDi_Free( defaultPathW );
        return NFD_OKAY;
    }

    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("Error creating ShellItem");
        NFDi_Free( defaultPathW );
        return NFD_ERROR;
    }

    // Could also call SetDefaultFolder(), but this guarantees defaultPath -- more consistency across API.
    dialog->SetFolder( folder );

    NFDi_Free( defaultPathW );
    folder->Release();

    return NFD_OKAY;
}

static wchar_t* g_buttonText = nullptr;

LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        CWPSTRUCT* msg = (CWPSTRUCT*)lParam;
        if (msg->message == WM_INITDIALOG)
        {
            HWND cancelBtn = GetDlgItem(msg->hwnd, IDCANCEL);
            if (cancelBtn && g_buttonText)
            {
                SetWindowTextW(cancelBtn, g_buttonText);
                NFDi_Free(g_buttonText);
                g_buttonText = nullptr;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/* public */

NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_OpenDialogEx(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    const nfdchar_t *dialogTitle,
    const nfdchar_t *fileNameLabel,
    const nfdchar_t *selectButtonLabel,
    const nfdchar_t *cancelButtonLabel,
    void* parentWindow,
    nfdchar_t **outPath )
{
    SET_DPI_AWARENESS();

    nfdresult_t nfdResult = NFD_ERROR;
    HHOOK hook = nullptr;
    HRESULT coResult = COMInit();
    HWND hwnd = parentWindow ? static_cast<HWND>(parentWindow) : nullptr;

    if (!COMIsInitialized(coResult))
    {
        NFDi_SetError("Could not initialize COM.");
        return nfdResult;
    }

    // Create dialog
    ::IFileOpenDialog *fileOpenDialog(nullptr);
    HRESULT result = ::CoCreateInstance(::CLSID_FileOpenDialog, nullptr,
                                        CLSCTX_ALL, ::IID_IFileOpenDialog,
                                        reinterpret_cast<void**>(&fileOpenDialog) );

    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("Could not create dialog.");
        goto end;
    }

    // Build the filter list
    if ( !AddFiltersToDialog( fileOpenDialog, filterList ) )
    {
        goto end;
    }

    // Set the default path
    if ( !SetDefaultPath( fileOpenDialog, defaultPath ) )
    {
        goto end;
    }

    // Set the title
    if ( dialogTitle && dialogTitle[0] != '\0' )
    {
        wchar_t *titleW = nullptr;
        CopyNFDCharToWChar( dialogTitle, &titleW );
        if ( titleW )
        {
            fileOpenDialog->SetTitle(titleW);
            NFDi_Free( titleW );
        }
    }

    // Labels customization:
    if ( fileNameLabel && fileNameLabel[0] != '\0' ) {

        wchar_t *titleW = nullptr;
        CopyNFDCharToWChar( fileNameLabel, &titleW );
        if ( titleW )
        {
            fileOpenDialog->SetFileNameLabel(titleW);
            NFDi_Free( titleW );
        }
    }

    if ( selectButtonLabel && selectButtonLabel[0] != '\0' ) {

        wchar_t *titleW = nullptr;
        CopyNFDCharToWChar( selectButtonLabel, &titleW );
        if ( titleW )
        {
            fileOpenDialog->SetOkButtonLabel(titleW);
            NFDi_Free( titleW );
        }
    }

    if ( cancelButtonLabel && cancelButtonLabel[0] != '\0' ) {

        wchar_t *titleW = nullptr;
        CopyNFDCharToWChar( cancelButtonLabel, &titleW );
        if ( titleW )
        {
            g_buttonText = titleW;
            hook = SetWindowsHookEx(WH_CALLWNDPROC, CBTProc, nullptr, GetCurrentThreadId());
        }
    }

    // Show
    result = fileOpenDialog->Show(hwnd);

    if ( SUCCEEDED(result) )
    {
        // Get the file name
        ::IShellItem *shellItem(nullptr);
        result = fileOpenDialog->GetResult(&shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get shell item from dialog.");
            goto end;
        }
        wchar_t *filePath(nullptr);
        result = shellItem->GetDisplayName(::SIGDN_FILESYSPATH, &filePath);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get file path for selected.");
            shellItem->Release();
            goto end;
        }

        CopyWCharToNFDChar( filePath, outPath );
        CoTaskMemFree(filePath);
        if ( !*outPath )
        {
            /* error is malloc-based, error message would be redundant */
            shellItem->Release();
            goto end;
        }

        nfdResult = NFD_OKAY;
        shellItem->Release();
    }
    else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) )
    {
        nfdResult = NFD_CANCEL;
    }
    else
    {
        NFDi_SetError("File dialog box show failed.");
        nfdResult = NFD_ERROR;
    }

end:
    if (hook)
    {
        UnhookWindowsHookEx(hook);
    }

    if (fileOpenDialog)
        fileOpenDialog->Release();

    COMUninit(coResult);

    return nfdResult;
}

NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_OpenDialog(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath )
{
    SET_DPI_AWARENESS();

    nfdresult_t nfdResult = NFD_ERROR;


    HRESULT coResult = COMInit();
    if (!COMIsInitialized(coResult))
    {
        NFDi_SetError("Could not initialize COM.");
        return nfdResult;
    }

    // Create dialog
    ::IFileOpenDialog *fileOpenDialog(nullptr);
    HRESULT result = ::CoCreateInstance(::CLSID_FileOpenDialog, nullptr,
                                        CLSCTX_ALL, ::IID_IFileOpenDialog,
                                        reinterpret_cast<void**>(&fileOpenDialog) );

    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("Could not create dialog.");
        goto end;
    }

    // Build the filter list
    if ( !AddFiltersToDialog( fileOpenDialog, filterList ) )
    {
        goto end;
    }

    // Set the default path
    if ( !SetDefaultPath( fileOpenDialog, defaultPath ) )
    {
        goto end;
    }

    // Show the dialog.
    result = fileOpenDialog->Show(nullptr);
    if ( SUCCEEDED(result) )
    {
        // Get the file name
        ::IShellItem *shellItem(nullptr);
        result = fileOpenDialog->GetResult(&shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get shell item from dialog.");
            goto end;
        }
        wchar_t *filePath(nullptr);
        result = shellItem->GetDisplayName(::SIGDN_FILESYSPATH, &filePath);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get file path for selected.");
            shellItem->Release();
            goto end;
        }

        CopyWCharToNFDChar( filePath, outPath );
        CoTaskMemFree(filePath);
        if ( !*outPath )
        {
            /* error is malloc-based, error message would be redundant */
            shellItem->Release();
            goto end;
        }

        nfdResult = NFD_OKAY;
        shellItem->Release();
    }
    else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) )
    {
        nfdResult = NFD_CANCEL;
    }
    else
    {
        NFDi_SetError("File dialog box show failed.");
        nfdResult = NFD_ERROR;
    }

end:
    if (fileOpenDialog)
        fileOpenDialog->Release();

    COMUninit(coResult);

    return nfdResult;
}

NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_OpenDialogMultiple(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdpathset_t *outPaths )
{
    SET_DPI_AWARENESS();

    nfdresult_t nfdResult = NFD_ERROR;


    HRESULT coResult = COMInit();
    if (!COMIsInitialized(coResult))
    {
        NFDi_SetError("Could not initialize COM.");
        return nfdResult;
    }

    // Create dialog
    ::IFileOpenDialog *fileOpenDialog(nullptr);
    HRESULT result = ::CoCreateInstance(::CLSID_FileOpenDialog, nullptr,
                                        CLSCTX_ALL, ::IID_IFileOpenDialog,
                                        reinterpret_cast<void**>(&fileOpenDialog) );

    if ( !SUCCEEDED(result) )
    {
        fileOpenDialog = nullptr;
        NFDi_SetError("Could not create dialog.");
        goto end;
    }

    // Build the filter list
    if ( !AddFiltersToDialog( fileOpenDialog, filterList ) )
    {
        goto end;
    }

    // Set the default path
    if ( !SetDefaultPath( fileOpenDialog, defaultPath ) )
    {
        goto end;
    }

    // Set a flag for multiple options
    DWORD dwFlags;
    result = fileOpenDialog->GetOptions(&dwFlags);
    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("Could not get options.");
        goto end;
    }
    result = fileOpenDialog->SetOptions(dwFlags | FOS_ALLOWMULTISELECT);
    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("Could not set options.");
        goto end;
    }

    // Show the dialog.
    result = fileOpenDialog->Show(nullptr);
    if ( SUCCEEDED(result) )
    {
        IShellItemArray *shellItems;
        result = fileOpenDialog->GetResults( &shellItems );
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get shell items.");
            goto end;
        }

        if ( AllocPathSet( shellItems, outPaths ) == NFD_ERROR )
        {
            shellItems->Release();
            goto end;
        }

        shellItems->Release();
        nfdResult = NFD_OKAY;
    }
    else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) )
    {
        nfdResult = NFD_CANCEL;
    }
    else
    {
        NFDi_SetError("File dialog box show failed.");
        nfdResult = NFD_ERROR;
    }

end:
    if ( fileOpenDialog )
        fileOpenDialog->Release();

    COMUninit(coResult);

    return nfdResult;
}

NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_SaveDialog(
    const nfdchar_t *filterList,
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath )
{
    SET_DPI_AWARENESS();

    nfdresult_t nfdResult = NFD_ERROR;

    HRESULT coResult = COMInit();
    if (!COMIsInitialized(coResult))
    {
        NFDi_SetError("Could not initialize COM.");
        return nfdResult;
    }

    // Create dialog
    ::IFileSaveDialog *fileSaveDialog(nullptr);
    HRESULT result = ::CoCreateInstance(::CLSID_FileSaveDialog, nullptr,
                                        CLSCTX_ALL, ::IID_IFileSaveDialog,
                                        reinterpret_cast<void**>(&fileSaveDialog) );

    if ( !SUCCEEDED(result) )
    {
        fileSaveDialog = nullptr;
        NFDi_SetError("Could not create dialog.");
        goto end;
    }

    // Build the filter list
    if ( !AddFiltersToDialog( fileSaveDialog, filterList ) )
    {
        goto end;
    }

    // Set the default path
    if ( !SetDefaultPath( fileSaveDialog, defaultPath ) )
    {
        goto end;
    }

    // Show the dialog.
    result = fileSaveDialog->Show(nullptr);
    if ( SUCCEEDED(result) )
    {
        // Get the file name
        ::IShellItem *shellItem;
        result = fileSaveDialog->GetResult(&shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get shell item from dialog.");
            goto end;
        }
        wchar_t *filePath(nullptr);
        result = shellItem->GetDisplayName(::SIGDN_FILESYSPATH, &filePath);
        if ( !SUCCEEDED(result) )
        {
            shellItem->Release();
            NFDi_SetError("Could not get file path for selected.");
            goto end;
        }

        CopyWCharToNFDChar( filePath, outPath );
        CoTaskMemFree(filePath);
        if ( !*outPath )
        {
            /* error is malloc-based, error message would be redundant */
            shellItem->Release();
            goto end;
        }

        nfdResult = NFD_OKAY;
        shellItem->Release();
    }
    else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) )
    {
        nfdResult = NFD_CANCEL;
    }
    else
    {
        NFDi_SetError("File dialog box show failed.");
        nfdResult = NFD_ERROR;
    }

end:
    if ( fileSaveDialog )
        fileSaveDialog->Release();

    COMUninit(coResult);

    return nfdResult;
}

NATIVE_FILE_DIALOG_MODULE_API nfdresult_t NFD_PickFolder(
    const nfdchar_t *defaultPath,
    nfdchar_t **outPath)
{
    SET_DPI_AWARENESS();

    nfdresult_t nfdResult = NFD_ERROR;
    DWORD dwOptions = 0;

    HRESULT coResult = COMInit();
    if (!COMIsInitialized(coResult))
    {
        NFDi_SetError("CoInitializeEx failed.");
        return nfdResult;
    }

    // Create dialog
    ::IFileOpenDialog *fileDialog(nullptr);
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                      nullptr,
                                      CLSCTX_ALL,
                                      IID_PPV_ARGS(&fileDialog));
    if ( !SUCCEEDED(result) )
    {
        NFDi_SetError("CoCreateInstance for CLSID_FileOpenDialog failed.");
        goto end;
    }

    // Set the default path
    if (SetDefaultPath(fileDialog, defaultPath) != NFD_OKAY)
    {
        NFDi_SetError("SetDefaultPath failed.");
        goto end;
    }

    // Get the dialogs options
    if (!SUCCEEDED(fileDialog->GetOptions(&dwOptions)))
    {
        NFDi_SetError("GetOptions for IFileDialog failed.");
        goto end;
    }

    // Add in FOS_PICKFOLDERS which hides files and only allows selection of folders
    if (!SUCCEEDED(fileDialog->SetOptions(dwOptions | FOS_PICKFOLDERS)))
    {
        NFDi_SetError("SetOptions for IFileDialog failed.");
        goto end;
    }

    // Show the dialog to the user
    result = fileDialog->Show(nullptr);
    if ( SUCCEEDED(result) )
    {
        // Get the folder name
        ::IShellItem *shellItem(nullptr);

        result = fileDialog->GetResult(&shellItem);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("Could not get file path for selected.");
            shellItem->Release();
            goto end;
        }

        wchar_t *path = nullptr;
        result = shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path);
        if ( !SUCCEEDED(result) )
        {
            NFDi_SetError("GetDisplayName for IShellItem failed.");            
            shellItem->Release();
            goto end;
        }

        CopyWCharToNFDChar(path, outPath);
        CoTaskMemFree(path);
        if ( !*outPath )
        {
            shellItem->Release();
            goto end;
        }

        nfdResult = NFD_OKAY;
        shellItem->Release();
    }
    else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED) )
    {
        nfdResult = NFD_CANCEL;
    }
    else
    {
        NFDi_SetError("Show for IFileDialog failed.");
        nfdResult = NFD_ERROR;
    }

 end:

    if (fileDialog)
        fileDialog->Release();

    COMUninit(coResult);

    return nfdResult;
}
